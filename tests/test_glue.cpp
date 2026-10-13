#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Signals.h"
#include "polisher/GlueCompressor.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <vector>

using namespace polisher;
using Catch::Matchers::WithinAbs;

namespace {

GlueParams glue (double ratio, double thresholdDb, GlueCharacter character = GlueCharacter::bus)
{
    GlueParams p;
    p.ratio = ratio;
    p.thresholdDb = thresholdDb;
    p.character = character;
    p.sidechainHz = 20.0;   // off: the tests' tones are read as they are
    p.autoRelease = false;
    return p;
}

/** Runs a clip through a fresh Glue; params(i) can change the settings at sample i (checked every 64). */
AudioClip render (const AudioClip& in, const std::function<GlueParams (int)>& params, int block = 64)
{
    GlueCompressor g;
    g.setParams (params (0));
    g.prepare (in.sampleRate, in.numChannels());
    AudioClip c = in;
    std::vector<float*> ch (static_cast<size_t> (c.numChannels()));
    for (int s = 0, n = c.numSamples(); s < n; s += block)
    {
        g.setParams (params (s));
        for (int k = 0; k < c.numChannels(); ++k) ch[static_cast<size_t> (k)] = c.channels[static_cast<size_t> (k)].data() + s;
        g.process (ch.data(), c.numChannels(), std::min (block, n - s));
    }
    return c;
}
AudioClip render (const AudioClip& in, const GlueParams& p) { return render (in, [p] (int) { return p; }); }

double peakFrom (const AudioClip& c, double fromSeconds, double toSeconds = 1.0e9)
{
    double peak = 0.0;
    const auto a = static_cast<size_t> (fromSeconds * c.sampleRate);
    const auto b = std::min (c.channels[0].size(), static_cast<size_t> (toSeconds * c.sampleRate));
    for (const auto& ch : c.channels)
        for (size_t i = a; i < b; ++i) peak = std::max (peak, static_cast<double> (std::abs (ch[i])));
    return 20.0 * std::log10 (peak + 1.0e-30);
}

/** Level (dB, a sine's peak) of frequency hz in channel 0 between two times. */
double toneDb (const AudioClip& c, double hz, double from, double to)
{
    double re = 0.0, im = 0.0;
    const int a = static_cast<int> (from * c.sampleRate), b = static_cast<int> (to * c.sampleRate);
    for (int i = a; i < b; ++i)
    {
        const double w = 2.0 * std::numbers::pi * hz * i / c.sampleRate;
        re += c.channels[0][static_cast<size_t> (i)] * std::cos (w);
        im += c.channels[0][static_cast<size_t> (i)] * std::sin (w);
    }
    return 20.0 * std::log10 (2.0 * std::hypot (re, im) / (b - a) + 1.0e-12);
}

double sharpest (const AudioClip& c, int from)
{
    double worst = 0.0;
    for (const auto& x : c.channels)
        for (size_t i = static_cast<size_t> (std::max (2, from)); i < x.size(); ++i)
            worst = std::max (worst, std::abs (static_cast<double> (x[i]) - 2.0 * x[i - 1] + x[i - 2]));
    return worst;
}

} // namespace

TEST_CASE ("Glue at 1:1 and 0 dB, or off, is an exact pass-through", "[glue]")
{
    const auto beat = testsig::trapLike (48000.0, 3.0);
    CHECK (render (beat, GlueParams {}).channels == beat.channels);
    auto off = glue (4.0, -30.0);
    off.enabled = false;
    CHECK (render (beat, off).channels == beat.channels);
    auto dry = glue (4.0, -30.0);
    dry.mix = 0.0;   // Mix 0 %: the compressor runs, nothing of it is heard
    double worst = 0.0;
    const auto out = render (beat, dry);
    for (size_t k = 0; k < 2; ++k)
        for (size_t i = 0; i < out.channels[k].size(); ++i) worst = std::max (worst, std::abs (static_cast<double> (out.channels[k][i]) - beat.channels[k][i]));
    CHECK (worst < 1.0e-6);
}

TEST_CASE ("Glue follows its curve: threshold, ratio and the character's knee", "[glue]")
{
    // A steady -6 dBFS tone, 14 dB over a -20 dB threshold at 4:1: 10.5 dB of reduction.
    const auto tone = testsig::sine (48000.0, 2, 1000.0, -6.0, 2.0);
    const auto out = render (tone, glue (4.0, -20.0));
    CHECK_THAT (peakFrom (out, 1.5), WithinAbs (-16.5, 0.3));

    // The knee: Bus starts at about the threshold, Tube well under it and more gently.
    auto curve = [] (GlueCharacter ch, double level)
    {
        GlueCompressor g;
        g.setParams (glue (4.0, -20.0, ch));
        g.prepare (48000.0, 2);
        return g.gainComputer (level);
    };
    CHECK (curve (GlueCharacter::bus, -26.0) == 0.0);
    CHECK (curve (GlueCharacter::tube, -26.0) < -0.1);
    CHECK (curve (GlueCharacter::opto, -26.0) == 0.0);
    CHECK (curve (GlueCharacter::opto, -23.0) < 0.0);
    for (auto ch : { GlueCharacter::bus, GlueCharacter::opto, GlueCharacter::tube })
        CHECK_THAT (curve (ch, 0.0), WithinAbs (-15.0, 1e-9));   // far over: the full ratio for all three
}

TEST_CASE ("Glue Ignore Lows keeps the 808 from pumping the vocal", "[glue]")
{
    const double sr = 48000.0;
    for (double hz808 : { 50.0, 60.0 })
    {
        auto mix = testsig::sine (sr, 2, hz808, -6.0, 2.0);   // the 808
        const auto vocal = testsig::sine (sr, 2, 1000.0, -30.0, 2.0);
        for (size_t k = 0; k < 2; ++k)
            for (size_t i = 0; i < mix.channels[k].size(); ++i) mix.channels[k][i] += vocal.channels[k][i];
        const double vocalIn = toneDb (mix, 1000.0, 1.0, 2.0);
        auto hears = glue (4.0, -20.0);
        auto ignores = hears;
        ignores.sidechainHz = GlueParams {}.sidechainHz;   // the default, 90 Hz
        const double ducked = vocalIn - toneDb (render (mix, hears), 1000.0, 1.0, 2.0);
        const double kept = vocalIn - toneDb (render (mix, ignores), 1000.0, 1.0, 2.0);
        INFO (hz808 << " Hz 808: vocal turned down " << ducked << " dB hearing the 808, " << kept << " dB ignoring it");
        CHECK (ducked > 8.0);
        CHECK (kept < 1.0);
    }
}

TEST_CASE ("Glue Mix blends in the compressed sound and Gain sets the level after it", "[glue]")
{
    const auto tone = testsig::sine (48000.0, 2, 1000.0, -6.0, 2.0);
    const double full = peakFrom (render (tone, glue (4.0, -20.0)), 1.5);   // -16.5
    auto half = glue (4.0, -20.0);
    half.mix = 0.5;
    const double expected = 20.0 * std::log10 (0.5 * std::pow (10.0, -6.0 / 20.0) + 0.5 * std::pow (10.0, full / 20.0));
    CHECK_THAT (peakFrom (render (tone, half), 1.5), WithinAbs (expected, 0.05));
    auto louder = glue (1.0, -20.0);
    louder.gainDb = 6.0;
    CHECK_THAT (peakFrom (render (tone, louder), 0.5), WithinAbs (0.0, 0.01));
}

TEST_CASE ("Glue Attack and Release set how fast it grabs and lets go", "[glue]")
{
    const double sr = 48000.0;
    auto clip = testsig::silence (sr, 2, 0.2);
    testsig::appendSine (clip, 1000.0, -6.0, 1.0);
    testsig::appendSine (clip, 1000.0, -60.0, 1.5);
    auto p = glue (4.0, -20.0);
    p.attackMs = 10.0;
    p.releaseMs = 300.0;
    GlueCompressor g;
    g.setParams (p);
    g.prepare (sr, 2);
    std::vector<double> gr;   // per millisecond
    const int ms = static_cast<int> (sr / 1000.0);
    for (int s = 0; s + ms <= clip.numSamples(); s += ms)
    {
        float* ch[2] = { clip.channels[0].data() + s, clip.channels[1].data() + s };
        g.process (ch, 2, ms);
        gr.push_back (g.currentReductionDb());
    }
    const double settled = gr[1150];   // ~-10.5
    INFO ("settled " << settled << ", 10 ms in " << gr[210] << ", 50 ms in " << gr[250]);
    CHECK_THAT (settled, WithinAbs (-10.5, 0.3));
    CHECK (gr[210] < 0.4 * settled);   // 10 ms after the tone starts: well on its way...
    CHECK (gr[210] > 0.85 * settled);  // ...but not there yet
    CHECK (gr[250] < 0.95 * settled);
    // 300 ms after the tone stops, about a third of the reduction is left.
    const double left = gr[1200 + 300] / settled;
    INFO ("left after the release time: " << left);
    CHECK (left > 0.25);
    CHECK (left < 0.5);
}

TEST_CASE ("Glue Auto release lets go quickly after a short peak", "[glue]")
{
    const double sr = 48000.0;
    auto clip = testsig::sine (sr, 2, 1000.0, -14.0, 2.0);   // steady, a little over the threshold
    testsig::appendSine (clip, 1000.0, -4.0, 0.03);           // a short peak
    testsig::appendSine (clip, 1000.0, -14.0, 2.0);
    auto recoverMs = [&] (bool autoRelease)
    {
        auto p = glue (4.0, -20.0);
        p.releaseMs = 1000.0;
        p.autoRelease = autoRelease;
        GlueCompressor g;
        g.setParams (p);
        g.prepare (sr, 2);
        AudioClip c = clip;
        const int ms = static_cast<int> (sr / 1000.0);
        double steady = 0.0;
        int back = -1;
        for (int s = 0, k = 0; s + ms <= c.numSamples(); s += ms, ++k)
        {
            float* ch[2] = { c.channels[0].data() + s, c.channels[1].data() + s };
            g.process (ch, 2, ms);
            if (k == 1990) steady = g.currentReductionDb();
            if (k > 2030 && back < 0 && g.currentReductionDb() > steady - 1.0) back = k - 2030;
        }
        return back;
    };
    const int fast = recoverMs (true), slow = recoverMs (false);
    INFO ("back within 1 dB after " << fast << " ms (Auto), " << slow << " ms (fixed 1000 ms)");
    CHECK (fast >= 0);
    CHECK (slow > 3 * fast);
}

TEST_CASE ("Glue Opto never grabs faster than 10 ms", "[glue]")
{
    const double sr = 48000.0;
    auto clip = testsig::silence (sr, 2, 0.1);
    testsig::appendSine (clip, 1000.0, -6.0, 0.2);
    auto grAfter2Ms = [&] (GlueCharacter ch)
    {
        auto p = glue (4.0, -20.0, ch);
        p.attackMs = 0.1;
        GlueCompressor g;
        g.setParams (p);
        g.prepare (sr, 2);
        AudioClip c = clip;
        const int n = static_cast<int> (0.102 * sr);
        float* chs[2] = { c.channels[0].data(), c.channels[1].data() };
        g.process (chs, 2, n);
        return g.currentReductionDb();
    };
    const double bus = grAfter2Ms (GlueCharacter::bus), opto = grAfter2Ms (GlueCharacter::opto);
    INFO ("2 ms in: Bus " << bus << " dB, Opto " << opto << " dB");
    CHECK (bus < -5.0);
    CHECK (opto > 0.5 * bus);
}

TEST_CASE ("Glue switches on and off while playing without a click, then leaves the sound untouched", "[glue][switching]")
{
    // A steady low tone shows any kink: its own bend is tiny, a click is not.
    const double sr = 48000.0;
    const auto tone = testsig::sine (sr, 2, 200.0, -6.0, 4.0);
    const auto on = glue (4.0, -30.0), neutral = GlueParams {};
    // On at 0.5-1, 1.5-2 and 2.5-3 s (about 18 dB of reduction), neutral in between and after.
    const auto isOn = [&] (int i) { const int k = i / static_cast<int> (0.5 * sr); return k % 2 == 1 && k < 6; };
    const auto switching = render (tone, [&] (int i) { return isOn (i) ? on : neutral; });
    const double limit = std::max (sharpest (render (tone, on), 4800), sharpest (tone, 4800));
    INFO ("sharpest bend switching " << sharpest (switching, 4800) << ", steady " << limit);
    CHECK (sharpest (switching, 4800) < 1.1 * limit);
    // The check can fail: the same switches made without the fade (compressed and dry spliced) are caught.
    const auto compressed = render (tone, on);
    AudioClip spliced = tone;
    for (size_t k = 0; k < 2; ++k)
        for (size_t i = 0; i < spliced.channels[k].size(); ++i)
            if (isOn (static_cast<int> (i))) spliced.channels[k][i] = compressed.channels[k][i];
    CHECK (sharpest (spliced, 4800) > 10.0 * limit);
    // Switched off at 3 s: once faded out, untouched.
    const auto from = static_cast<size_t> (3.1 * sr);
    bool same = true;
    for (size_t k = 0; k < 2; ++k)
        for (size_t i = from; i < tone.channels[k].size(); ++i) same = same && switching.channels[k][i] == tone.channels[k][i];
    CHECK (same);
}

TEST_CASE ("Glue follows the same curve at 44.1 and 96 kHz", "[glue]")
{
    for (double sr : { 44100.0, 96000.0 })
    {
        const auto tone = testsig::sine (sr, 2, 1000.0, -6.0, 2.0);
        INFO (sr << " Hz");
        CHECK_THAT (peakFrom (render (tone, glue (4.0, -20.0)), 1.5), WithinAbs (-16.5, 0.3));
    }
}

TEST_CASE ("Glue works on mono and puts the same gain on every channel", "[glue]")
{
    const double sr = 48000.0;
    auto mono = testsig::sine (sr, 1, 1000.0, -6.0, 2.0);
    CHECK_THAT (peakFrom (render (mono, glue (4.0, -20.0)), 1.5), WithinAbs (-16.5, 0.3));
    auto quad = testsig::sine (sr, 4, 1000.0, -6.0, 2.0);
    for (auto& v : quad.channels[3]) v *= 0.5f;
    const auto out = render (quad, glue (4.0, -20.0));
    for (size_t i = 48000; i < out.channels[0].size(); i += 997)
        CHECK_THAT (out.channels[3][i], WithinAbs (0.5 * out.channels[0][i], 1e-6));
    // Silence stays silence, nothing blows up at the extremes.
    auto quiet = testsig::silence (sr, 2, 0.5);
    auto hard = glue (10.0, -40.0);
    hard.attackMs = 0.1;
    hard.releaseMs = 50.0;
    hard.gainDb = 12.0;
    const auto silent = render (quiet, hard);
    for (float v : silent.channels[0]) CHECK (v == 0.0f);
    const auto loud = render (testsig::trapLike (sr, 2.0), hard);
    bool finite = true;
    for (const auto& ch : loud.channels) for (float v : ch) finite = finite && std::isfinite (v);
    CHECK (finite);
}

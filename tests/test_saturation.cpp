#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Signals.h"
#include "polisher/Saturation.h"

#include <cmath>
#include <numbers>

using namespace polisher;
using Catch::Matchers::WithinAbs;

namespace {

AudioClip saturate (AudioClip c, const SaturationParams& p)
{
    Saturation s;
    s.prepare (c.sampleRate, c.numChannels());
    s.setParams (p);
    s.reset();
    std::vector<float*> ch;
    for (auto& v : c.channels) ch.push_back (v.data());
    s.process (ch.data(), c.numChannels(), c.numSamples());
    return c;
}

/** Level (dB) of frequency f in the last second of channel 0, relative to a full-scale sine. */
double levelAt (const AudioClip& c, double f)
{
    const int n = static_cast<int> (c.sampleRate);
    const int start = c.numSamples() - n;
    double re = 0.0, im = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double x = c.channels[0][static_cast<size_t> (start + i)];
        const double ph = 2.0 * std::numbers::pi * f * i / c.sampleRate;
        re += x * std::cos (ph);
        im -= x * std::sin (ph);
    }
    return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / n + 1e-12);
}

double folded (double f, double sr)
{
    f = std::fmod (f, sr);
    return f > sr / 2 ? sr - f : f;
}

SaturationParams with (SaturationMode m, double drive, double mix = 100.0)
{
    SaturationParams p;
    p.mode = m;
    p.driveDb = drive;
    p.mix = mix;
    return p;
}

} // namespace

TEST_CASE ("Saturation at Drive 0 or Mix 0 is an exact, delayed pass-through", "[saturation]")
{
    const auto mix = testsig::musicLike (48000.0, 1.0, -3.0);
    for (const auto& p : { SaturationParams {}, with (SaturationMode::tube, 12.0, 0.0), [] { auto q = with (SaturationMode::clip, 12.0); q.enabled = false; return q; }() })
    {
        const auto out = saturate (mix, p);
        for (int c = 0; c < 2; ++c)
            for (int i = Saturation::kLatency; i < mix.numSamples(); ++i)
                REQUIRE (out.channels[static_cast<size_t> (c)][static_cast<size_t> (i)]
                         == mix.channels[static_cast<size_t> (c)][static_cast<size_t> (i - Saturation::kLatency)]);
    }
}

TEST_CASE ("Saturation keeps the reported latency and leaves quiet signals alone", "[saturation]")
{
    // A quiet mix at full Drive: the curve is linear there, so the output is the input, delayed.
    const auto quiet = testsig::musicLike (48000.0, 1.0, -50.0);
    for (auto m : { SaturationMode::tape, SaturationMode::tube, SaturationMode::clip })
    {
        const auto out = saturate (quiet, with (m, Saturation::kMaxDriveDb));
        double err = 0.0, sig = 0.0;
        for (int i = 4800; i < quiet.numSamples(); ++i)
        {
            const double d = out.channels[0][static_cast<size_t> (i)] - quiet.channels[0][static_cast<size_t> (i - Saturation::kLatency)];
            err += d * d;
            sig += static_cast<double> (quiet.channels[0][static_cast<size_t> (i)]) * quiet.channels[0][static_cast<size_t> (i)];
        }
        INFO ("mode " << static_cast<int> (m));
        CHECK (10.0 * std::log10 (err / sig) < -50.0);
    }
}

TEST_CASE ("Tape and clip add odd harmonics, tube adds mostly the 2nd", "[saturation]")
{
    const auto tone = testsig::sine (48000.0, 2, 1000.0, -3.0, 2.0);
    const auto tape = saturate (tone, with (SaturationMode::tape, 12.0));
    const auto tube = saturate (tone, with (SaturationMode::tube, 12.0));
    const auto clip = saturate (tone, with (SaturationMode::clip, 12.0));

    INFO ("tape h2 " << levelAt (tape, 2000) << " h3 " << levelAt (tape, 3000));
    INFO ("tube h2 " << levelAt (tube, 2000) << " h3 " << levelAt (tube, 3000));
    INFO ("clip h2 " << levelAt (clip, 2000) << " h3 " << levelAt (clip, 3000));
    CHECK (levelAt (tape, 3000) > -45.0);
    CHECK (levelAt (tape, 2000) < -90.0);
    CHECK (levelAt (clip, 3000) > -45.0);
    CHECK (levelAt (clip, 2000) < -90.0);
    CHECK (levelAt (tube, 2000) > -40.0);
    CHECK (levelAt (tube, 2000) > levelAt (tube, 3000) + 3.0);

    // More Drive, more harmonics; the fundamental stays within a couple of dB.
    const auto hot = saturate (tone, with (SaturationMode::tape, 18.0));
    CHECK (levelAt (hot, 3000) > levelAt (tape, 3000) + 3.0);
    CHECK_THAT (levelAt (saturate (tone, with (SaturationMode::tape, 6.0)), 1000), WithinAbs (-3.0, 1.0));

    // Mix 50 %: half the harmonics.
    const auto half = saturate (tone, with (SaturationMode::tape, 12.0, 50.0));
    CHECK_THAT (levelAt (half, 3000), WithinAbs (levelAt (tape, 3000) - 6.02, 0.3));
}

TEST_CASE ("Saturation doesn't alias and adds no DC", "[saturation]")
{
    for (double sr : { 44100.0, 48000.0 })
    {
        const double f = 7000.0;
        const auto tone = testsig::sine (sr, 2, f, -1.0, 2.0);
        for (auto m : { SaturationMode::tape, SaturationMode::tube, SaturationMode::clip })
        {
            const auto out = saturate (tone, with (m, Saturation::kMaxDriveDb));
            double worst = -200.0;
            for (int k = 2; k <= 40; ++k)
            {
                const double h = k * f;
                if (h < sr / 2) continue;                       // real harmonics are fine
                const double a = folded (h, sr);
                if (std::abs (a - f) < 50.0) continue;
                worst = std::max (worst, levelAt (out, std::round (a)));
            }
            INFO ("sr " << sr << " mode " << static_cast<int> (m) << " worst alias " << worst);
            CHECK (worst < -80.0);

            double dc = 0.0;
            for (int i = out.numSamples() / 2; i < out.numSamples(); ++i) dc += out.channels[0][static_cast<size_t> (i)];
            CHECK (std::abs (dc / (out.numSamples() / 2)) < 1e-4);
        }
    }
}

TEST_CASE ("Saturation glides: no clicks when Drive or Mix move, stable at every rate", "[saturation]")
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto c = testsig::sine (sr, 2, 100.0, -6.0, 1.0);
        Saturation s;
        s.prepare (sr, 2);
        const int n = c.numSamples();
        float* ch[2] = { c.channels[0].data(), c.channels[1].data() };
        const int block = 256;
        for (int start = 0; start < n; start += block)
        {
            // Jump between off and full Drive every 0.1 s.
            const bool on = (start / static_cast<int> (0.1 * sr)) % 2 == 1;
            s.setParams (with (SaturationMode::clip, on ? Saturation::kMaxDriveDb : 0.0));
            float* p[2] = { ch[0] + start, ch[1] + start };
            s.process (p, 2, std::min (block, n - start));
        }
        double maxStep = 0.0;
        for (int i = Saturation::kLatency + 1; i < n; ++i)
        {
            const double v = c.channels[0][static_cast<size_t> (i)];
            REQUIRE (std::isfinite (v));
            maxStep = std::max (maxStep, std::abs (v - c.channels[0][static_cast<size_t> (i - 1)]));
        }
        // A 100 Hz sine at -6 dBFS moves at most 2*pi*100/sr*0.5 per sample; allow a little more.
        INFO ("sr " << sr << " max step " << maxStep);
        CHECK (maxStep < 1.3 * 2.0 * std::numbers::pi * 100.0 / sr * 0.5);
    }
}

TEST_CASE ("Saturation reports the harmonics it adds", "[saturation]")
{
    Saturation s;
    s.prepare (48000.0, 2);
    auto tone = testsig::sine (48000.0, 2, 1000.0, -3.0, 0.5);
    float* ch[2] = { tone.channels[0].data(), tone.channels[1].data() };
    s.process (ch, 2, tone.numSamples());
    CHECK (s.takeHarmonicsDb() <= -99.0);
    s.setParams (with (SaturationMode::tape, 12.0));
    s.process (ch, 2, tone.numSamples());
    const double h = s.takeHarmonicsDb();
    INFO (h);
    CHECK (h > -40.0);
    CHECK (h < -5.0);
}

TEST_CASE ("Saturation follows its curve on bass too (no phase shift), and starts at once offline", "[saturation]")
{
    for (auto m : { SaturationMode::tape, SaturationMode::clip, SaturationMode::tube })
        for (double f : { 30.0, 50.0 })
        {
            const auto tone = testsig::sine (48000.0, 2, f, 20.0 * std::log10 (0.9), 3.0);
            const auto out = saturate (tone, with (m, Saturation::kMaxDriveDb));
            const double s = std::pow (10.0, (Saturation::kHeadroomDb - Saturation::kMaxDriveDb) / 20.0);
            // Compare with the DC removed (the tube curve's DC is taken out on purpose).
            double wantDc = 0.0, outDc = 0.0;
            const int from = 96000, n = tone.numSamples() - from;
            for (int i = from; i < tone.numSamples(); ++i)
            {
                wantDc += Saturation::shape (tone.channels[0][static_cast<size_t> (i - Saturation::kLatency)], m, s) / n;
                outDc += out.channels[0][static_cast<size_t> (i)] / static_cast<double> (n);
            }
            double err = 0.0, sig = 0.0;
            for (int i = from; i < tone.numSamples(); ++i)
            {
                const double want = Saturation::shape (tone.channels[0][static_cast<size_t> (i - Saturation::kLatency)], m, s) - wantDc;
                const double d = out.channels[0][static_cast<size_t> (i)] - outDc - want;
                err += d * d;
                sig += want * want;
            }
            INFO ("mode " << static_cast<int> (m) << " f " << f << " err " << 10.0 * std::log10 (err / sig));
            CHECK (10.0 * std::log10 (err / sig) < -25.0);   // tube: its 1 Hz DC filter
        }

    // After reset() the set drive applies from the first samples (offline renders).
    const auto tone = testsig::sine (48000.0, 2, 1000.0, -1.0, 0.1);
    const auto out = saturate (tone, with (SaturationMode::clip, Saturation::kMaxDriveDb));
    double early = 0.0;
    for (int i = Saturation::kLatency + 100; i < Saturation::kLatency + 600; ++i)
        early = std::max (early, static_cast<double> (std::abs (out.channels[0][static_cast<size_t> (i)])));
    CHECK (20.0 * std::log10 (early) < -2.0);   // peaks already rounded (input peaks at -1 dBFS)
}

TEST_CASE ("Saturation Drive is calibrated for a mix peaking near full scale", "[saturation]")
{
    // A mix like the v0.9 test: about -17 LUFS, peaks around -1 dBFS.
    auto mix = testsig::musicLike (48000.0, 4.0, -6.0);
    double peak = 0.0;
    for (auto& ch : mix.channels) for (float v : ch) peak = std::max (peak, static_cast<double> (std::abs (v)));
    const auto g = static_cast<float> (std::pow (10.0, -1.0 / 20.0) / peak);
    for (auto& ch : mix.channels) for (auto& v : ch) v *= g;

    auto harmonics = [&] (SaturationParams p)
    {
        Saturation s;
        s.prepare (48000.0, 2);
        s.setParams (p);
        s.reset();
        auto c = mix;
        float* ch[2] = { c.channels[0].data(), c.channels[1].data() };
        s.process (ch, 2, c.numSamples());
        return s.takeHarmonicsDb() + 20.0 * std::log10 (p.mix / 100.0);   // the meter shows the full-wet level
    };
    const double light = harmonics (with (SaturationMode::tape, 6.0, 50.0));
    const double medium = harmonics (with (SaturationMode::tape, 12.0, 100.0));
    const double tubeLight = harmonics (with (SaturationMode::tube, 6.0, 50.0));
    INFO ("tape +6 50% " << light << ", tape +12 " << medium << ", tube +6 50% " << tubeLight);
    CHECK (light > -45.0);        // audible as density...
    CHECK (light < -28.0);        // ...not as distortion
    CHECK (medium > -30.0);       // clearly audible
    CHECK (tubeLight > -45.0);
}

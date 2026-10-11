#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Signals.h"
#include "SwitchLevel.h"
#include "polisher/BassControl.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

using namespace polisher;
using Catch::Matchers::WithinAbs;

namespace {

/** A stereo clip whose mid and side each carry their own sine. */
AudioClip midSide (double sr, double seconds, double midHz, double midDb, double sideHz, double sideDb)
{
    AudioClip clip = testsig::silence (sr, 2, seconds);
    const double am = midDb > -200 ? std::pow (10.0, midDb / 20.0) : 0.0;
    const double as = sideDb > -200 ? std::pow (10.0, sideDb / 20.0) : 0.0;
    for (int i = 0; i < clip.numSamples(); ++i)
    {
        const double t = i / sr;
        const double m = am * std::sin (2.0 * std::numbers::pi * midHz * t);
        const double s = as * std::sin (2.0 * std::numbers::pi * sideHz * t);
        clip.channels[0][static_cast<size_t> (i)] = static_cast<float> (m + s);
        clip.channels[1][static_cast<size_t> (i)] = static_cast<float> (m - s);
    }
    return clip;
}

/** RMS in dB of mid or side over [from, to) seconds. */
double levelDb (const AudioClip& c, bool side, double from, double to)
{
    const int a = static_cast<int> (from * c.sampleRate), b = static_cast<int> (to * c.sampleRate);
    double sum = 0.0;
    for (int i = a; i < b; ++i)
    {
        const double l = c.channels[0][static_cast<size_t> (i)], r = c.channels[1][static_cast<size_t> (i)];
        const double v = side ? 0.5 * (l - r) : 0.5 * (l + r);
        sum += v * v;
    }
    return 10.0 * std::log10 (sum / (b - a) + 1.0e-30);
}

/** One kick: a 55 Hz thump with a fast decay, plus the short click (1.7-4.3 kHz) every kick has. */
double kickAt (double t)
{
    if (t < 0.0) return 0.0;
    const double w = 2.0 * std::numbers::pi * t;
    return 0.5 * std::exp (-t * 8.0) * std::sin (55.0 * w)
         + 0.25 * std::exp (-t / 0.0015) * (std::sin (1700.0 * w) + std::sin (2900.0 * w) + std::sin (4300.0 * w)) / 1.5;
}

/** One clean 808 note: a low sine with a 2 ms fade-in, a slow decay and a 5 ms fade-out. */
double note808At (double t, double length, double hz, double peakDb, double decaySeconds)
{
    if (t < 0.0 || t >= length) return 0.0;
    const double env = std::pow (10.0, peakDb / 20.0) * std::exp (-t / decaySeconds)
                     * std::min (1.0, t / 0.002) * std::min (1.0, (length - t) / 0.005);
    return env * std::sin (2.0 * std::numbers::pi * hz * t);
}

/** A mono clip from a function of time. */
template <typename F>
AudioClip monoClip (double sr, double seconds, F f)
{
    AudioClip clip = testsig::silence (sr, 2, seconds);
    for (int i = 0; i < clip.numSamples(); ++i)
        clip.channels[0][static_cast<size_t> (i)] = clip.channels[1][static_cast<size_t> (i)] = static_cast<float> (f (i / sr));
    return clip;
}

/** Kick hits every 0.5 s. */
AudioClip kicks (double sr, double seconds)
{
    return monoClip (sr, seconds, [] (double t) { return kickAt (std::fmod (t, 0.5)); });
}

/** Runs Bass Control live in chunks of `period` s and returns the biggest Punch boost (dB) and the
    deepest Sub Tame cut (dB) in each chunk. */
std::vector<std::pair<double, double>> perChunk (AudioClip clip, const BassParams& p, double period)
{
    BassControl b;
    b.prepare (clip.sampleRate, 2);
    b.setParams (p);
    b.reset();
    std::vector<std::pair<double, double>> out;
    const int n = clip.numSamples(), chunk = static_cast<int> (period * clip.sampleRate);
    for (int start = 0; start + chunk <= n; start += chunk)
    {
        for (int j = 0; j < chunk; j += 256)
        {
            float* ch[2] = { clip.channels[0].data() + start + j, clip.channels[1].data() + start + j };
            b.process (ch, 2, std::min (256, chunk - j));
        }
        const double punch = b.takeMaxPunchDb();
        out.emplace_back (punch, b.takeMaxTameDb());
    }
    return out;
}

/** Level (dB, peak-calibrated) of one tone in one channel over [from, to) seconds. */
double toneDb (const AudioClip& c, int channel, double hz, double from, double to)
{
    double re = 0.0, im = 0.0;
    const int a = static_cast<int> (from * c.sampleRate), b = static_cast<int> (to * c.sampleRate);
    for (int i = a; i < b; ++i)
    {
        const double w = 2.0 * std::numbers::pi * hz * i / c.sampleRate;
        re += c.channels[static_cast<size_t> (channel)][static_cast<size_t> (i)] * std::cos (w);
        im += c.channels[static_cast<size_t> (channel)][static_cast<size_t> (i)] * std::sin (w);
    }
    return 20.0 * std::log10 (2.0 * std::hypot (re, im) / (b - a) + 1.0e-12);
}

} // namespace

TEST_CASE ("Bass Control at its neutral settings is an exact pass-through", "[bass]")
{
    const auto mix = testsig::musicLike (48000.0, 3.0, -6.0);
    CHECK (renderBass (mix, BassParams {}).channels == mix.channels);

    BassParams off;                 // settings present but the module switched off
    off.enabled = false;
    off.monoHz = 120.0; off.tame = 1.0; off.punch = 1.0;
    CHECK (renderBass (mix, off).channels == mix.channels);

    // Running live, with only measurement happening, the audio is also untouched.
    BassControl b;
    b.prepare (48000.0, 2);
    b.setParams (BassParams {});
    auto copy = mix;
    std::vector<float*> ptrs { copy.channels[0].data(), copy.channels[1].data() };
    b.process (ptrs.data(), 2, copy.numSamples());
    CHECK (copy.channels == mix.channels);
    CHECK_FALSE (b.isActive());
}

TEST_CASE ("Mono Below removes stereo bass and keeps everything else", "[bass]")
{
    const double sr = 48000.0;
    BassParams p;
    p.monoHz = 120.0;

    // Side-only 50 Hz: removed. Side-only 1 kHz: kept.
    const auto lowSide = renderBass (midSide (sr, 2.0, 1000.0, -300, 50.0, -12.0), p);
    CHECK (levelDb (lowSide, true, 1.0, 2.0) < -12.0 - 3.0 - 24.0);
    const auto highSide = renderBass (midSide (sr, 2.0, 1000.0, -300, 1000.0, -12.0), p);
    CHECK_THAT (levelDb (highSide, true, 1.0, 2.0), WithinAbs (-12.0 - 3.01, 0.05));

    // Mid (mono) keeps its level at every frequency (it only gets the matching phase shift).
    for (double hz : { 30.0, 50.0, 120.0, 200.0, 1000.0, 8000.0 })
    {
        const auto monoOut = renderBass (midSide (sr, 2.0, hz, -12.0, 1000.0, -300), p);
        INFO (hz << " Hz");
        CHECK_THAT (levelDb (monoOut, false, 1.0, 2.0), WithinAbs (-12.0 - 3.01, 0.02));
        CHECK (levelDb (monoOut, true, 1.0, 2.0) < -200.0);   // and stays mono
    }
}

TEST_CASE ("Mono Below keeps a hard-panned sound on its side above the frequency", "[bass]")
{
    // A sine on the left only, Mono Below at 100 Hz. Below 100 Hz it becomes mono (on both sides);
    // above, it has to stay on the left: the right side only gets the LR4 low-pass's share.
    const double sr = 48000.0;
    BassParams p;
    p.monoHz = 100.0;
    struct Point { double hz, maxRightDb; };
    for (auto [hz, maxRightDb] : { Point { 150.0, -20.5 }, Point { 200.0, -29.0 }, Point { 400.0, -52.0 }, Point { 2000.0, -90.0 } })
    {
        AudioClip in = testsig::silence (sr, 2, 2.0);
        for (int i = 0; i < in.numSamples(); ++i)
            in.channels[0][static_cast<size_t> (i)] = static_cast<float> (std::sin (2.0 * std::numbers::pi * hz * i / sr));
        const auto out = renderBass (in, p);
        const double left = toneDb (out, 0, hz, 1.0, 2.0), right = toneDb (out, 1, hz, 1.0, 2.0);
        INFO (hz << " Hz: left " << left << " dB, right " << right << " dB");
        CHECK (right < maxRightDb);
        CHECK (right < left - 20.0);   // never louder on the wrong side
        CHECK (left > -1.0);
    }
    // Well below the frequency: mono, half on each side.
    AudioClip in = testsig::silence (sr, 2, 2.0);
    for (int i = 0; i < in.numSamples(); ++i)
        in.channels[0][static_cast<size_t> (i)] = static_cast<float> (std::sin (2.0 * std::numbers::pi * 30.0 * i / sr));
    const auto out = renderBass (in, p);
    CHECK_THAT (toneDb (out, 0, 30.0, 1.0, 2.0), WithinAbs (-6.02, 0.3));
    CHECK_THAT (toneDb (out, 1, 30.0, 1.0, 2.0), WithinAbs (-6.02, 0.3));
}

TEST_CASE ("Bass width is measured", "[bass]")
{
    const double sr = 48000.0;
    for (auto [sideDb, lo, hi] : { std::tuple { -300.0, 0.0, 1.0 }, std::tuple { -12.0, 95.0, 100.0 } })
    {
        BassControl b;
        b.prepare (sr, 2);
        auto clip = midSide (sr, 4.0, 60.0, -12.0, 70.0, sideDb);
        std::vector<float*> ptrs { clip.channels[0].data(), clip.channels[1].data() };
        b.process (ptrs.data(), 2, clip.numSamples());
        INFO ("side " << sideDb << " -> " << b.bassWidthPercent() << " %");
        CHECK (b.bassWidthPercent() >= lo);
        CHECK (b.bassWidthPercent() <= hi);
    }
}

TEST_CASE ("Sub Tame turns down a bass note that jumps out, not the rest", "[bass]")
{
    const double sr = 48000.0;
    // 4 s of 60 Hz at -18 dBFS, then 1 s at -8 dBFS (an 808 note that leaps out), plus a steady
    // 3 kHz tone that must not change.
    AudioClip clip = testsig::silence (sr, 2, 5.0);
    for (int i = 0; i < clip.numSamples(); ++i)
    {
        const double t = i / sr;
        const double bass = std::pow (10.0, (t < 4.0 ? -18.0 : -8.0) / 20.0) * std::sin (2.0 * std::numbers::pi * 60.0 * t);
        const double hi = 0.05 * std::sin (2.0 * std::numbers::pi * 3000.0 * t);
        clip.channels[0][static_cast<size_t> (i)] = clip.channels[1][static_cast<size_t> (i)] = static_cast<float> (bass + hi);
    }
    BassParams p;
    p.tame = 1.0;
    const auto out = renderBass (clip, p);

    // Level of one tone (by projection onto it), so the bass and the 3 kHz tone are measured apart.
    auto tone = [&] (const AudioClip& c, double hz, double from, double to)
    {
        double re = 0.0, im = 0.0;
        const int a = static_cast<int> (from * sr), b = static_cast<int> (to * sr);
        for (int i = a; i < b; ++i)
        {
            const double w = 2.0 * std::numbers::pi * hz * i / sr;
            re += c.channels[0][static_cast<size_t> (i)] * std::cos (w);
            im += c.channels[0][static_cast<size_t> (i)] * std::sin (w);
        }
        return 20.0 * std::log10 (2.0 * std::hypot (re, im) / (b - a));
    };
    const double jumpBefore = tone (clip, 60.0, 4.0, 4.3), jumpAfter = tone (out, 60.0, 4.0, 4.3);
    const double steadyBefore = tone (clip, 60.0, 2.0, 3.8), steadyAfter = tone (out, 60.0, 2.0, 3.8);
    INFO ("jump " << jumpBefore << " -> " << jumpAfter << ", steady " << steadyBefore << " -> " << steadyAfter);
    CHECK (jumpAfter < jumpBefore - 3.0);                        // the leap is tamed
    CHECK_THAT (steadyAfter, WithinAbs (steadyBefore, 0.3));      // settled notes left alone
    CHECK_THAT (tone (out, 3000.0, 4.0, 4.3), WithinAbs (tone (clip, 3000.0, 4.0, 4.3), 0.3));   // highs untouched

    BassControl live;
    live.prepare (sr, 2);
    live.setParams (p);
    auto copy = clip;
    std::vector<float*> ptrs { copy.channels[0].data(), copy.channels[1].data() };
    live.process (ptrs.data(), 2, copy.numSamples());
    CHECK (live.takeMaxTameDb() < -3.0);
}

TEST_CASE ("Punch lifts the start of each kick, not its tail", "[bass]")
{
    const double sr = 48000.0;
    const auto in = kicks (sr, 4.0);
    BassParams p;
    p.punch = 1.0;
    const auto out = renderBass (in, p);
    // Average over the hits after the first second.
    double attackGain = 0.0, tailGain = 0.0;
    int hits = 0;
    for (double start = 1.0; start < 3.9; start += 0.5, ++hits)
    {
        attackGain += levelDb (out, false, start + 0.002, start + 0.03) - levelDb (in, false, start + 0.002, start + 0.03);
        tailGain += levelDb (out, false, start + 0.25, start + 0.45) - levelDb (in, false, start + 0.25, start + 0.45);
    }
    attackGain /= hits;
    tailGain /= hits;
    INFO ("attack +" << attackGain << " dB, tail +" << tailGain << " dB");
    CHECK (attackGain > 1.5);
    CHECK (std::abs (tailGain) < 0.5);
}

TEST_CASE ("Punch leaves 808 notes alone and still finds a kick over a long 808", "[bass]")
{
    const double sr = 48000.0;
    BassParams p;
    p.punch = 1.0;

    // Clean 808 notes on their own (several pitches and levels): no click, so no boost.
    for (double hz : { 30.0, 45.0, 60.0 })
        for (double db : { -12.0, -6.0, 0.0 })
        {
            const auto in = monoClip (sr, 4.0, [=] (double t) { return note808At (std::fmod (t, 0.5), 0.5, hz, db, 0.6); });
            double worst = 0.0;
            for (auto [punch, tame] : perChunk (in, p, 0.5))
                worst = std::max (worst, punch);
            INFO (hz << " Hz at " << db << " dB: up to +" << worst << " dB");
            CHECK (worst < 1.0);
        }

    // A kick every 0.5 s over a long 808 note (re-struck every 2 s): every kick gets a real boost.
    const auto mix = monoClip (sr, 6.0, [] (double t) {
        return note808At (std::fmod (t, 2.0), 2.0, 45.0, -8.0, 3.0) + kickAt (std::fmod (t, 0.5)); });
    const auto hits = perChunk (mix, p, 0.5);
    double weakest = 99.0;
    for (size_t k = 2; k < hits.size(); ++k)
        weakest = std::min (weakest, hits[k].first);
    INFO ("weakest kick boost +" << weakest << " dB");
    CHECK (weakest > 2.0);
}

TEST_CASE ("Sub Tame treats the first notes like the rest", "[bass]")
{
    // Identical 808 notes from the very start: the first one must not be cut harder than later
    // ones (the average isn't judged from the note's own first 10 ms). Sparse, short notes do get
    // a little cut once the average has settled; the first, heard before that, may get less.
    const double sr = 48000.0;
    BassParams p;
    p.tame = 1.0;
    for (auto [period, decay] : { std::pair { 0.5, 0.8 }, std::pair { 1.0, 0.5 } })
    {
        const auto in = monoClip (sr, 8.0, [=] (double t) { return note808At (std::fmod (t, period), period, 45.0, -8.0, decay); });
        const auto notes = perChunk (in, p, period);
        const double first = notes.front().second, later = notes.back().second;
        INFO ("every " << period << " s: first note " << first << " dB, later " << later << " dB");
        CHECK (first > later - 1.0);
        CHECK (first > -2.0);
        CHECK (later > -2.0);
    }

    // A drop after an intro with no bass: judged on its own, not ducked.
    const auto drop = monoClip (sr, 10.0, [] (double t) {
        return t < 5.0 ? 0.05 * std::sin (2.0 * std::numbers::pi * 3000.0 * t)
                       : note808At (std::fmod (t, 0.5), 0.5, 45.0, -8.0, 0.8); });
    double deepest = 0.0;
    for (auto [punch, tame] : perChunk (drop, p, 0.5))
        deepest = std::min (deepest, tame);
    INFO ("drop cut " << deepest << " dB");
    CHECK (deepest > -1.0);
}

TEST_CASE ("Bass Control never clicks when switched and stays stable", "[bass]")
{
    for (double sr : { 44100.0, 96000.0, 192000.0 })
    {
        auto clip = midSide (sr, 3.0, 55.0, -6.0, 65.0, -9.0);
        BassControl b;
        b.prepare (sr, 2);
        std::vector<float*> ptrs { clip.channels[0].data(), clip.channels[1].data() };
        BassParams on;
        on.monoHz = 250.0; on.tame = 1.0; on.punch = 1.0;
        const int block = 256;
        const int n = clip.numSamples();
        for (int start = 0; start < n; start += block)
        {
            if (start == static_cast<int> (0.5 * sr) / block * block) b.setParams (on);
            if (start == static_cast<int> (1.5 * sr) / block * block) { auto moved = on; moved.monoHz = 40.0; b.setParams (moved); }
            if (start == static_cast<int> (2.2 * sr) / block * block) b.setParams (BassParams {});
            const int len = std::min (block, n - start);
            b.process (ptrs.data(), 2, len);
            for (auto& ptr : ptrs) ptr += len;
        }
        // Steepest step two low sines can take is tiny; a click would be large.
        double worst = 0.0;
        for (const auto& ch : clip.channels)
            for (size_t i = 1; i < ch.size(); ++i)
            {
                REQUIRE (std::isfinite (ch[i]));
                worst = std::max (worst, static_cast<double> (std::abs (ch[i] - ch[i - 1])));
            }
        const double maxStep = 2.0 * (0.5 + 0.36) * 2.0 * std::numbers::pi * 65.0 / sr * 1.6;   // with up to +4 dB punch
        INFO ("sr " << sr << " worst step " << worst << " limit " << maxStep);
        CHECK (worst < maxStep);
    }
}

namespace {

/** A tone (or anything) through Bass Control in 64-sample blocks, the settings changing from `from` to
    `to` at sample `at` (no change when at < 0). Starts from reset() in `from`. */
std::array<std::vector<double>, 2> switchBass (std::array<std::vector<double>, 2> x, double sr, const BassParams& from, const BassParams& to, int at, BassControl* keep = nullptr)
{
    BassControl local;
    BassControl& b = keep != nullptr ? *keep : local;
    b.prepare (sr, 2);
    b.setParams (from);
    b.reset();
    const int n = static_cast<int> (x[0].size());
    for (int s = 0; s < n; s += 64)
    {
        if (s == at) b.setParams (to);
        double* ch[2] = { x[0].data() + s, x[1].data() + s };
        b.process (ch, 2, std::min (64, n - s));
    }
    return x;
}

std::array<std::vector<double>, 2> stereoTone (double sr, double seconds, double hz, double left, double right)
{
    std::array<std::vector<double>, 2> x;
    const int n = static_cast<int> (seconds * sr);
    for (auto& ch : x) ch.resize (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        const double v = std::sin (2.0 * std::numbers::pi * hz * i / sr);
        x[0][static_cast<size_t> (i)] = left * v;
        x[1][static_cast<size_t> (i)] = right * v;
    }
    return x;
}

} // namespace

TEST_CASE ("Mono Below switches on and off without a hole in the centre, and the stereo stays put", "[bass][switching]")
{
    // Blending the plain centre with the all-passed one at the Mono Below frequency used to cancel it: a
    // 120 Hz centre tone fell 18 dB for ~50 ms on every switch (A / B, a snapshot, undo, Auto-Master).
    // Now (park and glide) the short-time level of a centre tone below, at and above the frequency stays
    // within 1 dB through the switch, at every rate; once through, it is exactly the steady setting.
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (double monoHz : { 40.0, 120.0, 250.0 })
        {
            BassParams on;
            on.monoHz = monoHz;
            for (double hz : { std::max (30.0, 0.5 * monoHz), monoHz, 2.0 * monoHz })
            {
                const int n = static_cast<int> (2.0 * sr), at = static_cast<int> (0.5 * sr) / 64 * 64;
                const auto tone = stereoTone (sr, 2.0, hz, 0.5, 0.5);
                const auto steadyOn = switchBass (tone, sr, on, on, -1);
                for (bool switchOn : { true, false })
                {
                    BassControl b;
                    const auto out = switchBass (tone, sr, switchOn ? BassParams {} : on, switchOn ? on : BassParams {}, at, &b);
                    const auto r = switchlevel::measure (out[0], sr, hz, at);
                    INFO ("sr " << sr << ", Mono Below " << monoHz << " Hz switched " << (switchOn ? "on" : "off") << ", " << hz << " Hz tone: dip "
                          << r.dipDb << " dB, swell +" << r.bumpDb << " dB");
                    CHECK (r.dipDb > -1.0);
                    CHECK (r.bumpDb < 1.0);
                    if (hz <= 1000.0)   // and no click: no sample-to-sample step beyond the tone's own
                        CHECK (switchlevel::stepRatio (out[0], at) < 1.2);
                    // Through: switched on, it is the steady setting (to rounding); switched off, an exact pass-through.
                    double diff = 0.0;
                    for (int i = static_cast<int> (1.5 * sr); i < n; ++i)
                        diff = std::max (diff, std::abs (out[0][static_cast<size_t> (i)] - (switchOn ? steadyOn[0] : tone[0])[static_cast<size_t> (i)]));
                    if (switchOn)
                        CHECK (diff < 1.0e-7);
                    else
                    {
                        CHECK (diff == 0.0);
                        CHECK_FALSE (b.isActive());
                    }
                }
            }
        }

    // A sound panned hard left stays on the left while Mono Below switches: no more on the right at any
    // moment than once it is on (mid and side go through the same all-pass while the filters glide).
    for (double hz : { 400.0, 1000.0, 4000.0 })
    {
        const double sr = 48000.0;
        const int at = static_cast<int> (0.5 * sr) / 64 * 64, w = switchlevel::windowFor (hz, sr);
        BassParams on;
        on.monoHz = 120.0;
        const auto steady = switchBass (stereoTone (sr, 2.0, hz, 0.5, 0.0), sr, on, on, -1);
        const int last = static_cast<int> (steady[0].size()) - w;
        const double steadyRight = switchlevel::levelDb (steady[1], last, w) - switchlevel::levelDb (steady[0], last, w);
        for (bool switchOn : { true, false })
        {
            const auto out = switchBass (stereoTone (sr, 2.0, hz, 0.5, 0.0), sr, switchOn ? BassParams {} : on, switchOn ? on : BassParams {}, at);
            double worst = -300.0;
            for (int s = at; s + w <= static_cast<int> (out[0].size()); s += w)
                worst = std::max (worst, switchlevel::levelDb (out[1], s, w) - switchlevel::levelDb (out[0], s, w));
            INFO (hz << " Hz hard left, Mono Below 120 switched " << (switchOn ? "on" : "off") << ": right " << worst << " dB re left (steady on: " << steadyRight << " dB)");
            CHECK (worst < std::max (steadyRight, -120.0) + 1.0);
            CHECK (worst < -45.0);
        }
    }
}

TEST_CASE ("Punch and Sub Tame do the same at every sample rate", "[bass]")
{
    // Their detectors and timings are set in seconds and Hz; nothing may depend on the sample rate.
    BassParams punch, tame;
    punch.punch = 1.0;
    tame.tame = 1.0;
    std::vector<double> firstNote, laterNote;
    for (double sr : { 48000.0, 44100.0, 96000.0, 192000.0 })
    {
        INFO ("sr " << sr);
        // Punch: every kick gets its boost; clean 808 notes get none.
        const auto hits = perChunk (kicks (sr, 4.0), punch, 0.5);
        double weakest = 99.0;
        for (size_t k = 2; k < hits.size(); ++k)
            weakest = std::min (weakest, hits[k].first);
        double on808 = 0.0;
        for (auto [boost, cut] : perChunk (monoClip (sr, 4.0, [] (double t) { return note808At (std::fmod (t, 0.5), 0.5, 45.0, -6.0, 0.6); }), punch, 0.5))
            on808 = std::max (on808, boost);
        INFO ("kicks: weakest boost +" << weakest << " dB; clean 808: up to +" << on808 << " dB");
        CHECK (weakest > 3.5);
        CHECK (on808 < 0.5);

        // Sub Tame: a note that leaps 10 dB out of a steady line is cut by the same amount at every rate,
        // and the steady line before it is left alone.
        const auto leap = monoClip (sr, 5.0, [] (double t) { return std::pow (10.0, (t < 4.0 ? -18.0 : -8.0) / 20.0) * std::sin (2.0 * std::numbers::pi * 60.0 * t); });
        const auto cuts = perChunk (leap, tame, 0.5);
        double steadyCut = 0.0;
        for (size_t k = 2; k < 8; ++k)
            steadyCut = std::min (steadyCut, cuts[k].second);
        firstNote.push_back (cuts[8].second);   // the chunk where the note leaps
        const auto out = renderBass (leap, tame);
        laterNote.push_back (toneDb (out, 0, 60.0, 4.1, 4.4) - toneDb (leap, 0, 60.0, 4.1, 4.4));   // (once the cut is in)
        INFO ("Sub Tame: deepest cut on the leap " << firstNote.back() << " dB (48 kHz " << firstNote.front() << "), its level "
              << laterNote.back() << " dB (48 kHz " << laterNote.front() << "); on the steady line " << steadyCut << " dB");
        CHECK (firstNote.back() < -4.0);
        CHECK (laterNote.back() < -3.0);
        CHECK (std::abs (firstNote.back() - firstNote.front()) < 0.25);
        CHECK (std::abs (laterNote.back() - laterNote.front()) < 0.25);
        CHECK (steadyCut > -0.5);
    }
}

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Signals.h"
#include "polisher/BassControl.h"

#include <cmath>
#include <numbers>

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

/** Kick-like hits: a 55 Hz sine burst with a fast decay, every 0.5 s. */
AudioClip kicks (double sr, double seconds)
{
    AudioClip clip = testsig::silence (sr, 2, seconds);
    for (int i = 0; i < clip.numSamples(); ++i)
    {
        const double t = std::fmod (i / sr, 0.5);
        const double v = 0.5 * std::exp (-t * 8.0) * std::sin (2.0 * std::numbers::pi * 55.0 * t);
        clip.channels[0][static_cast<size_t> (i)] = clip.channels[1][static_cast<size_t> (i)] = static_cast<float> (v);
    }
    return clip;
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

    // Mid (mono) bass is untouched, sample for sample.
    const auto monoIn = midSide (sr, 2.0, 50.0, -12.0, 1000.0, -300);
    const auto monoOut = renderBass (monoIn, p);
    double worst = 0.0;
    for (size_t i = 0; i < monoIn.channels[0].size(); ++i)
        worst = std::max (worst, static_cast<double> (std::abs (monoOut.channels[0][i] - monoIn.channels[0][i])));
    CHECK (worst < 1.0e-6);
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

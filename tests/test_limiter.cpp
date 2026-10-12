#include "Signals.h"
#include "SwitchLevel.h"
#include "polisher/LoudnessMeter.h"
#include "polisher/TruePeakLimiter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using namespace polisher;

TEST_CASE ("Quiet material passes through untouched, just delayed", "[limiter]")
{
    const auto clip = testsig::sine (48000.0, 2, 440.0, -20.0, 1.0);
    LimiterParams p;
    p.ceilingDbtp = -1.0;
    const auto out = renderLimited (clip, p);

    double maxErr = 0.0;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < clip.numSamples(); ++i)
            maxErr = std::max (maxErr, std::abs ((double) out.channels[c][i] - clip.channels[c][i]));
    CHECK (maxErr < 1e-6);
}

TEST_CASE ("Driven hard, the output never exceeds the true-peak ceiling", "[limiter]")
{
    const auto mix = testsig::musicLike (44100.0, 20.0, 0.0);

    for (double drive : { 3.0, 9.0, 15.0 })
        for (double ceiling : { -1.0, -0.3, -2.0 })
        {
            LimiterParams p;
            p.driveDb = drive;
            p.ceilingDbtp = ceiling;
            const double tp = testsig::truePeakOf (renderLimited (mix, p));
            INFO ("drive " << drive << " dB, ceiling " << ceiling << " dBTP, measured " << tp);
            CHECK (tp <= ceiling + 0.05);
        }
}

TEST_CASE ("Inter-sample overs are limited too", "[limiter]")
{
    // Samples at -3 dBFS, true peak at 0 dBTP: a sample-peak limiter would let this through.
    const auto clip = testsig::sine (48000.0, 2, 12000.0, 0.0, 2.0, std::numbers::pi / 4.0);
    LimiterParams p;
    p.ceilingDbtp = -1.0;
    CHECK (testsig::truePeakOf (renderLimited (clip, p)) <= -0.95);
}

TEST_CASE ("Reported latency matches the actual delay", "[limiter]")
{
    TruePeakLimiter lim;
    lim.prepare (48000.0, 1);
    LimiterParams p;
    p.ceilingDbtp = 0.0;
    lim.setParams (p);

    int arrival = -1;
    for (int i = 0; i < 1000 && arrival < 0; ++i)
    {
        double in = (i == 0) ? 0.25 : 0.0, out = 0.0;
        lim.processFrame (&in, &out, 1);
        if (std::abs (out) > 0.1)
            arrival = i;
    }
    CHECK (arrival == lim.latencySamples());
}

TEST_CASE ("Bypass keeps the same latency and leaves level alone", "[limiter]")
{
    const auto mix = testsig::musicLike (48000.0, 3.0, 6.0);
    LimiterParams p;
    p.bypassed = true;
    p.driveDb = 12.0;
    const auto out = renderLimited (mix, p);
    CHECK (out.channels[0][1000] == mix.channels[0][1000]);
}

TEST_CASE ("Switching to bypass never lets an unlimited peak out", "[limiter]")
{
    // B: a mix driven 12 dB into a -1 dBTP ceiling. At the switch to A (bypass) the delay line still
    // held driven samples, and they came out unlimited (a +6 dBFS tone: +6 dBFS for ~3 ms).
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 1, 64, 512, 4096 })
        {
            const auto mix = testsig::musicLike (sr, 3.0, -16.0);
            TruePeakLimiter lim;
            lim.prepare (sr, 2);
            LimiterParams p;
            p.driveDb = 12.0;
            p.ceilingDbtp = -1.0;
            lim.setParams (p);
            AudioClip out = mix;
            const int n = mix.numSamples(), at = static_cast<int> (1.5 * sr) / block * block, lat = lim.latencySamples();
            std::vector<float> l (static_cast<size_t> (block)), r (static_cast<size_t> (block));
            for (int pos = 0; pos < n; pos += block)
            {
                if (pos == at) { p.bypassed = true; lim.setParams (p); }
                const int len = std::min (block, n - pos);
                std::copy_n (mix.channels[0].begin() + pos, len, l.begin());
                std::copy_n (mix.channels[1].begin() + pos, len, r.begin());
                float* ch[2] = { l.data(), r.data() };
                lim.process (ch, 2, len);
                std::copy_n (l.begin(), len, out.channels[0].begin() + pos);
                std::copy_n (r.begin(), len, out.channels[1].begin() + pos);
            }
            double peak = 0.0, aPeak = 0.0;
            for (int i = at; i < at + static_cast<int> (0.2 * sr); ++i)
                for (size_t c = 0; c < 2; ++c) peak = std::max (peak, static_cast<double> (std::abs (out.channels[c][static_cast<size_t> (i)])));
            for (int i = 0; i < n; ++i)
                for (size_t c = 0; c < 2; ++c) aPeak = std::max (aPeak, static_cast<double> (std::abs (mix.channels[c][static_cast<size_t> (i)])));
            INFO (sr << " Hz, block " << block << ": peak after the switch " << 20.0 * std::log10 (peak) << " dBFS, the mix (A) peaks at "
                  << 20.0 * std::log10 (aPeak) << " dBFS");
            CHECK (20.0 * std::log10 (peak) <= std::max (-1.0, 20.0 * std::log10 (aPeak)) + 0.01);
            // ...and A soon is the plain mix again, just delayed.
            const int late = at + block + static_cast<int> (0.1 * sr);
            double err = 0.0;
            for (int i = late; i < n; ++i)
                err = std::max (err, std::abs (static_cast<double> (out.channels[0][static_cast<size_t> (i)]) - mix.channels[0][static_cast<size_t> (i - lat)]));
            CHECK (err == 0.0);
        }
}

TEST_CASE ("Adaptive release recovers from a single transient quickly", "[limiter]")
{
    // One loud click, then quiet tone: gain should be back near unity within ~200 ms.
    auto clip = testsig::sine (48000.0, 1, 200.0, -20.0, 1.0);
    clip.channels[0][4800] = 1.0f;
    LimiterParams p;
    p.ceilingDbtp = -6.0;
    p.releaseMs = 100.0;

    TruePeakLimiter lim;
    lim.prepare (48000.0, 1);
    lim.setParams (p);
    double in, out;
    for (int i = 0; i < clip.numSamples(); ++i)
    {
        in = clip.channels[0][i];
        lim.processFrame (&in, &out, 1);
        if (i == 4800 + 200 + 9600)   // 200 ms after the click passed
            CHECK (lim.currentGainReductionDb() > -0.5);
    }
}

namespace {
// An 808 (50 Hz, 300 ms decay, twice a second) under a steady 2 kHz "vocal", pushed into the limiter.
AudioClip eightOhEightUnderVocal (double sr, double seconds)
{
    AudioClip c = testsig::silence (sr, 2, seconds);
    const int period = static_cast<int> (0.5 * sr);
    for (int i = 0; i < c.numSamples(); ++i)
    {
        const double t = static_cast<double> (i % period) / sr, tt = static_cast<double> (i) / sr;
        const double v = 1.2 * std::min (1.0, t / 0.005) * std::exp (-t / 0.3) * std::sin (2.0 * std::numbers::pi * 50.0 * t)
                       + 0.35 * std::sin (2.0 * std::numbers::pi * 2000.0 * tt);
        c.channels[0][static_cast<size_t> (i)] = c.channels[1][static_cast<size_t> (i)] = static_cast<float> (v);
    }
    return c;
}

/** How far the 2 kHz tone dips (dB) at its lowest 10 ms window vs its loudest, after the first second. */
double vocalDuckDb (const AudioClip& c)
{
    const int win = static_cast<int> (0.01 * c.sampleRate);
    double lo = 1.0e9, hi = 0.0;
    for (int s = static_cast<int> (c.sampleRate); s + win <= c.numSamples(); s += win)
    {
        double re = 0.0, im = 0.0;
        for (int i = s; i < s + win; ++i)
        {
            const double w = 2.0 * std::numbers::pi * 2000.0 * i / c.sampleRate;
            re += c.channels[0][static_cast<size_t> (i)] * std::cos (w);
            im += c.channels[0][static_cast<size_t> (i)] * std::sin (w);
        }
        const double a = std::hypot (re, im);
        lo = std::min (lo, a);
        hi = std::max (hi, a);
    }
    return 20.0 * std::log10 (lo / hi);
}
} // namespace

TEST_CASE ("Bass First: the 808 takes the limiting, the vocal stops pumping, the ceiling holds", "[limiter][bassfirst]")
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const auto mix = eightOhEightUnderVocal (sr, 4.0);
        LimiterParams p;
        p.ceilingDbtp = -1.0;
        const auto plain = renderLimited (mix, p);
        p.bassFirst = 1.0;
        const auto first = renderLimited (mix, p);
        const double duckPlain = vocalDuckDb (plain), duckFirst = vocalDuckDb (first);
        const double tpPlain = testsig::truePeakOf (plain), tpFirst = testsig::truePeakOf (first);
        INFO ("sr " << sr << ": vocal dips " << duckPlain << " dB plain, " << duckFirst << " dB with Bass First; true peak "
              << tpPlain << " / " << tpFirst << " dBTP");
        CHECK (duckPlain < -3.0);               // the plain limiter ducks the vocal with every 808
        CHECK (duckFirst > duckPlain + 2.5);    // Bass First keeps most of it
        CHECK (tpFirst <= -1.0 + 0.05);
        CHECK (tpPlain <= -1.0 + 0.05);
    }
}

TEST_CASE ("Bass First off is an exact delay in front of the limiter; on, it keeps the level", "[limiter][bassfirst]")
{
    const double sr = 48000.0;
    const auto mix = testsig::musicLike (sr, 3.0, -20.0);   // quiet: nothing to limit
    LimiterParams p;
    p.ceilingDbtp = -1.0;
    p.bassFirst = 0.0;
    const auto out = renderLimited (mix, p);
    double worst = 0.0;
    for (int c = 0; c < 2; ++c)
        for (size_t i = 0; i < out.channels[0].size(); ++i)
            worst = std::max (worst, std::abs (static_cast<double> (out.channels[static_cast<size_t> (c)][i]) - mix.channels[static_cast<size_t> (c)][i]));
    CHECK (worst == 0.0);   // under the ceiling, Bass First off: untouched, sample for sample
    // On, under the ceiling: only the split's all-pass (phase), the level stays.
    p.bassFirst = 1.0;
    const auto on = renderLimited (mix, p);
    CHECK_THAT (measure (on).integratedLufs, WithinAbs (measure (mix).integratedLufs, 0.05));
    TruePeakLimiter lim;
    lim.prepare (sr, 2);
    lim.setParams (p);
    CHECK (lim.takeMaxBassCutDb() == 0.0);
}

TEST_CASE ("Bass First switches on and off without a hole or a click", "[limiter][bassfirst][switching]")
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (double hz : { 50.0, 120.0, 1000.0 })
        {
            const auto tone = testsig::sine (sr, 2, hz, -12.0, 3.0);   // under the ceiling: only the switching is heard
            const int n = tone.numSamples(), on = static_cast<int> (0.5 * sr) / 64 * 64, off = static_cast<int> (1.8 * sr) / 64 * 64;
            TruePeakLimiter lim;
            lim.prepare (sr, 2);
            LimiterParams p;
            lim.setParams (p);
            lim.reset();
            AudioClip c = tone;
            for (int s = 0; s < n; s += 64)
            {
                if (s == on || s == off) { p.bassFirst = s == on ? 1.0 : 0.0; lim.setParams (p); }
                float* ch[2] = { c.channels[0].data() + s, c.channels[1].data() + s };
                lim.process (ch, 2, std::min (64, n - s));
            }
            const std::vector<double> out (c.channels[0].begin(), c.channels[0].end());
            for (int at : { on, off })
            {
                const auto r = switchlevel::measure (out, sr, hz, at + lim.latencySamples());
                INFO ("sr " << sr << ", " << hz << " Hz, switched " << (at == on ? "on" : "off") << ": dip " << r.dipDb << " dB, swell +" << r.bumpDb << " dB");
                CHECK (r.dipDb > -1.0);
                CHECK (r.bumpDb < 1.0);
                CHECK (switchlevel::stepRatio (out, at + lim.latencySamples()) < 1.2);
            }
            // Back off: an exact delay again.
            const int lat = lim.latencySamples();
            double worst = 0.0;
            for (int i = static_cast<int> (2.6 * sr); i < n; ++i)
                worst = std::max (worst, std::abs (out[static_cast<size_t> (i)] - tone.channels[0][static_cast<size_t> (i - lat)]));
            CHECK (worst == 0.0);
        }
}

TEST_CASE ("Bass First lets go at once in A or when turned off, and A is plain again soon", "[limiter][bassfirst]")
{
    const double sr = 48000.0;
    const auto hot = testsig::sine (sr, 2, 50.0, 3.0, 4.0);   // a hot 808-like tone: Bass First cuts hard
    TruePeakLimiter lim;
    lim.prepare (sr, 2);
    LimiterParams p;
    p.releaseMs = 1000.0;   // the slowest release: the cut must still let go fast in A
    p.bassFirst = 1.0;
    lim.setParams (p);
    lim.reset();
    AudioClip c = hot;
    const int at = static_cast<int> (2.0 * sr) / 64 * 64, lat = lim.latencySamples();
    for (int s = 0; s < c.numSamples(); s += 64)
    {
        if (s == at) { p.bypassed = true; lim.setParams (p); }
        float* ch[2] = { c.channels[0].data() + s, c.channels[1].data() + s };
        lim.process (ch, 2, std::min (64, c.numSamples() - s));
    }
    CHECK (lim.takeMaxBassCutDb() < -3.0);   // it was cutting before the switch
    // 0.25 s after the switch A plays the tone at its own level (the main stage's drain is long over).
    auto levelDb = [&] (double from, double to)
    {
        double e = 0.0, ei = 0.0;
        for (int i = static_cast<int> (from * sr); i < static_cast<int> (to * sr); ++i)
        {
            e += c.channels[0][static_cast<size_t> (i)] * c.channels[0][static_cast<size_t> (i)];
            ei += hot.channels[0][static_cast<size_t> (i - lat)] * hot.channels[0][static_cast<size_t> (i - lat)];
        }
        return 10.0 * std::log10 (e / ei);
    };
    CHECK_THAT (levelDb (2.25, 2.5), WithinAbs (0.0, 0.2));
    // ...and within a second the stage is out of the path: an exact delay.
    double worst = 0.0;
    for (int i = static_cast<int> (3.2 * sr); i < c.numSamples(); ++i)
        worst = std::max (worst, std::abs (static_cast<double> (c.channels[0][static_cast<size_t> (i)]) - hot.channels[0][static_cast<size_t> (i - lat)]));
    CHECK (worst == 0.0);
}

TEST_CASE ("Bass First: the no-limiting reference lines up with the output", "[limiter][bassfirst]")
{
    // Nothing to limit: what the Maximizer "takes off" (reference minus output) must be silence, even though
    // the split turns the low end's phase.
    const double sr = 48000.0;
    AudioClip c = testsig::sine (sr, 2, 80.0, -20.0, 1.5);
    const auto hi = testsig::sine (sr, 2, 1000.0, -20.0, 1.5);
    for (int k = 0; k < 2; ++k)
        for (size_t i = 0; i < c.channels[0].size(); ++i) c.channels[static_cast<size_t> (k)][i] += hi.channels[static_cast<size_t> (k)][i];
    TruePeakLimiter lim;
    lim.prepare (sr, 2);
    LimiterParams p;
    p.bassFirst = 1.0;
    lim.setParams (p);
    lim.reset();
    std::vector<double> r0 (64), r1 (64);
    double* ref[2] = { r0.data(), r1.data() };
    double worst = 0.0;
    for (int s = 0; s + 64 <= c.numSamples(); s += 64)
    {
        float* ch[2] = { c.channels[0].data() + s, c.channels[1].data() + s };
        lim.process (ch, 2, 64, nullptr, ref);
        for (int i = 0; i < 64; ++i) worst = std::max (worst, std::abs (r0[static_cast<size_t> (i)] - ch[0][i]));
    }
    CHECK (worst < 1.0e-6);   // (the output is float)
}

TEST_CASE ("Bass First switching back in never replays old audio", "[limiter][bassfirst]")
{
    const double sr = 48000.0;
    TruePeakLimiter lim;
    lim.prepare (sr, 2);
    LimiterParams p;
    p.bassFirst = 1.0;
    lim.setParams (p);
    lim.reset();
    AudioClip loud = testsig::sine (sr, 2, 60.0, -2.0, 1.0);
    float* ch[2] = { loud.channels[0].data(), loud.channels[1].data() };
    lim.process (ch, 2, loud.numSamples());
    // Off (the stage leaves the path), silence, then on again in the silence.
    p.bassFirst = 0.0;
    lim.setParams (p);
    AudioClip quiet = testsig::silence (sr, 2, 2.0);
    float* q[2] = { quiet.channels[0].data(), quiet.channels[1].data() };
    const int half = quiet.numSamples() / 2;
    lim.process (q, 2, half);
    p.bassFirst = 1.0;
    lim.setParams (p);
    float* q2[2] = { q[0] + half, q[1] + half };
    lim.process (q2, 2, quiet.numSamples() - half);
    double worst = 0.0;
    for (int i = half; i < quiet.numSamples(); ++i) worst = std::max (worst, std::abs (static_cast<double> (quiet.channels[0][static_cast<size_t> (i)])));
    CHECK (worst < 1.0e-6);
}

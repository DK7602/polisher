#include "Signals.h"
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

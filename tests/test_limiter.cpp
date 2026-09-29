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

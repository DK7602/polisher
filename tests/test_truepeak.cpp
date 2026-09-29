#include "Signals.h"
#include "polisher/LoudnessMeter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <numbers>

using Catch::Matchers::WithinAbs;
using namespace polisher;

TEST_CASE ("Inter-sample peak between samples is caught", "[truepeak]")
{
    // A full-scale sine at fs/4 sampled at 45 degrees: every sample is 0.707 (-3.01 dBFS) but the
    // waveform itself peaks at 0 dBFS between samples.
    const auto clip = testsig::sine (48000.0, 2, 12000.0, 0.0, 1.0, std::numbers::pi / 4.0);

    LoudnessMeter m;
    m.prepare (48000.0, 2);
    const float* ptrs[] = { clip.channels[0].data(), clip.channels[1].data() };
    m.process (ptrs, 2, clip.numSamples());

    CHECK_THAT (m.samplePeakDbfs(), WithinAbs (-3.01, 0.05));
    CHECK_THAT (m.truePeakDbtp(), WithinAbs (0.0, 0.2));
}

TEST_CASE ("True peak of a low-frequency sine equals its sample peak", "[truepeak]")
{
    for (double level : { -6.0, -12.0, -0.5 })
    {
        INFO ("level " << level);
        CHECK_THAT (testsig::truePeakOf (testsig::sine (44100.0, 2, 997.0, level, 1.0)), WithinAbs (level, 0.05));
    }
}

TEST_CASE ("Detector latency: an impulse reads exactly full scale at kLatency", "[truepeak]")
{
    TruePeakDetector d;
    int peakStep = -1;
    double peak = 0.0;
    for (int i = 0; i < 40; ++i)
    {
        const double v = d.process (i == 0 ? 1.0 : 0.0);
        if (v > peak) { peak = v; peakStep = i; }
    }
    CHECK (peakStep == TruePeakDetector::kLatency);
    CHECK_THAT (peak, WithinAbs (1.0, 0.01));
}

#include "Signals.h"
#include "polisher/SoftClipper.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <numbers>

using Catch::Matchers::WithinAbs;
using namespace polisher;

namespace {

AudioClip runClipper (const AudioClip& in, const ClipperParams& p)
{
    SoftClipper clipper;
    clipper.prepare (in.numChannels());
    clipper.setParams (p);
    AudioClip out = in;
    const int n = in.numSamples(), lat = SoftClipper::latencySamples(), nch = in.numChannels();
    std::vector<double> frame (static_cast<size_t> (nch));
    for (int i = 0; i < n + lat; ++i)
    {
        for (int c = 0; c < nch; ++c) frame[c] = i < n ? in.channels[c][i] : 0.0;
        clipper.processFrame (frame.data(), nch);
        if (i >= lat) for (int c = 0; c < nch; ++c) out.channels[c][i - lat] = (float) frame[c];
    }
    return out;
}

/** Magnitude (dB re full-scale sine) of one frequency, Goertzel on the middle of a clip. */
double toneLevelDb (const std::vector<float>& x, double sr, double freq)
{
    const size_t start = x.size() / 4, n = x.size() / 2;
    const double w = 2.0 * std::numbers::pi * freq / sr;
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / (n - 1));   // Hann
        re += x[start + i] * win * std::cos (w * i);
        im -= x[start + i] * win * std::sin (w * i);
    }
    return 20.0 * std::log10 (std::sqrt (re * re + im * im) / (n * 0.25) + 1e-12);
}

double samplePeakDb (const AudioClip& c)
{
    double p = 0.0;
    for (const auto& ch : c.channels) for (float v : ch) p = std::max (p, (double) std::abs (v));
    return 20.0 * std::log10 (p + 1e-12);
}

} // namespace

TEST_CASE ("Clipper curve: transparent below the knee, never above the threshold", "[clipper]")
{
    for (double soft : { 0.0, 0.3, 0.7, 1.0 })
        for (double x = -4.0; x <= 4.0; x += 0.001)
        {
            const double y = SoftClipper::shape (x, 0.5, soft);
            REQUIRE (std::abs (y) <= 0.5 + 1e-12);
            if (std::abs (x) <= SoftClipper::kneeFor (0.5, soft))
                REQUIRE (y == x);
        }
    // Continuous at the knee (no step that would click).
    const double knee = SoftClipper::kneeFor (0.5, 0.3);
    CHECK_THAT (SoftClipper::shape (knee + 1e-7, 0.5, 0.3), WithinAbs (knee, 1e-6));
}

TEST_CASE ("Clipper latency is identical for every setting, and quiet audio passes unchanged", "[clipper]")
{
    const auto tone = testsig::sine (48000.0, 2, 1000.0, -20.0, 0.5);
    for (bool enabled : { false, true })
        for (int os : { 1, 2, 4, 8 })
        {
            ClipperParams p;
            p.enabled = enabled;
            p.oversampling = os;
            p.thresholdDb = -6.0;
            const auto out = runClipper (tone, p);
            double maxErr = 0.0;
            for (int i = 2000; i < tone.numSamples() - 2000; ++i)
                maxErr = std::max (maxErr, std::abs ((double) out.channels[0][i] - tone.channels[0][i]));
            INFO ("enabled " << enabled << ", " << os << "x: max error " << maxErr);
            CHECK (maxErr < 2e-3);
        }
}

TEST_CASE ("Clipping holds peaks near the threshold", "[clipper]")
{
    const auto loud = testsig::sine (48000.0, 2, 200.0, 0.0, 1.0);
    for (int os : { 1, 4, 8 })
    {
        ClipperParams p;
        p.enabled = true;
        p.thresholdDb = -6.0;
        p.softness = 0.4;
        p.oversampling = os;
        const double peak = samplePeakDb (runClipper (loud, p));
        INFO (os << "x: peak " << peak << " dBFS");
        CHECK (peak <= -5.7);   // a little filter overshoot allowed; the limiter follows anyway
        CHECK (peak >= -6.5);
    }
}

TEST_CASE ("Oversampling removes most of the aliasing", "[clipper]")
{
    // Hard-clipping a 7 kHz tone creates a 5th harmonic at 35 kHz. Without oversampling it folds
    // back to 13 kHz (48 - 35), an inharmonic, harsh tone. With 8x it should be far quieter.
    const double sr = 48000.0;
    const auto tone = testsig::sine (sr, 1, 7000.0, 0.0, 1.0);
    ClipperParams p;
    p.enabled = true;
    p.thresholdDb = -8.0;
    p.softness = 0.0;

    p.oversampling = 1;
    const double alias1x = toneLevelDb (runClipper (tone, p).channels[0], sr, 13000.0);
    p.oversampling = 8;
    const double alias8x = toneLevelDb (runClipper (tone, p).channels[0], sr, 13000.0);
    INFO ("13 kHz alias: 1x " << alias1x << " dB, 8x " << alias8x << " dB");
    CHECK (alias8x < alias1x - 20.0);
}

TEST_CASE ("Clipper + Maximizer gets louder than the Maximizer alone, under the same ceiling", "[clipper][targeting]")
{
    const auto mix = testsig::musicLike (44100.0, 20.0, -6.0);

    MasterChainParams limiterOnly;
    limiterOnly.driveDb = 18.0;
    limiterOnly.limiter.ceilingDbtp = -1.0;
    const auto a = measure (renderMaster (mix, limiterOnly));

    MasterChainParams withClipper = limiterOnly;
    withClipper.clipper.enabled = true;
    withClipper.clipper.thresholdDb = -4.0;
    withClipper.clipper.softness = 0.3;
    withClipper.clipper.oversampling = 4;
    const auto b = measure (renderMaster (mix, withClipper));

    INFO ("limiter only " << a.integratedLufs << " LUFS / " << a.truePeakDbtp << " dBTP; with clipper "
          << b.integratedLufs << " LUFS / " << b.truePeakDbtp << " dBTP");
    CHECK (b.integratedLufs > a.integratedLufs + 1.0);
    CHECK (b.truePeakDbtp <= -0.95);
    CHECK (a.truePeakDbtp <= -0.95);
}

TEST_CASE ("Targeting through the full chain reaches a loud target the limiter alone can't", "[clipper][targeting]")
{
    const auto mix = testsig::musicLike (44100.0, 20.0, -6.0);
    MasterChainParams chain;
    chain.limiter.ceilingDbtp = -1.0;
    chain.clipper.enabled = true;
    chain.clipper.thresholdDb = -4.0;
    chain.clipper.softness = 0.3;
    chain.clipper.oversampling = 4;

    const auto r = findDriveForTarget (mix, -8.0, chain);
    INFO ("drive " << r.driveDb << " -> " << r.output.integratedLufs << " LUFS, TP " << r.output.truePeakDbtp);
    CHECK (r.reachedTarget);
    CHECK (r.output.truePeakDbtp <= -0.95);
}

TEST_CASE ("Clip level below the ceiling makes the master louder, not quieter", "[clipper][targeting]")
{
    // The plug-in's "Clip Level" is relative to the Maximizer ceiling. Below 0 dB the clipper works
    // harder and makeup gain brings the clipped peaks back up to the ceiling, so no headroom is lost.
    const auto mix = testsig::musicLike (44100.0, 20.0, -6.0);
    const double ceiling = -1.0;
    auto chainFor = [&] (double clipLevel)
    {
        MasterChainParams p;
        p.driveDb = 6.0;
        p.limiter.ceilingDbtp = ceiling;
        p.clipper.enabled = true;
        p.clipper.softness = 0.5;
        p.clipper.oversampling = 4;
        p.clipper.thresholdDb = ceiling + clipLevel;
        p.clipper.makeupDb = std::max (0.0, -clipLevel);
        return measure (renderMaster (mix, p));
    };
    const auto at0 = chainFor (0.0), atMinus4 = chainFor (-4.0);
    INFO ("clip level 0: " << at0.integratedLufs << " LUFS, TP " << at0.truePeakDbtp
          << "; clip level -4: " << atMinus4.integratedLufs << " LUFS, TP " << atMinus4.truePeakDbtp);
    CHECK (atMinus4.integratedLufs > at0.integratedLufs + 1.5);
    CHECK (atMinus4.truePeakDbtp <= ceiling + 0.05);
    CHECK (atMinus4.truePeakDbtp > ceiling - 0.5);   // peaks still use the headroom
}

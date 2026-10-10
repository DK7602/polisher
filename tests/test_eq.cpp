#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Signals.h"
#include "polisher/TonalEQ.h"

#include <cmath>
#include <numbers>

using namespace polisher;
using Catch::Matchers::WithinAbs;

namespace {

/** RMS level (dB) of the second half of a processed sine, after the filters have settled. */
double sineGainDb (TonalEQ& eq, double sr, double freq)
{
    AudioClip clip = testsig::sine (sr, 2, freq, -12.0, 1.0);
    std::vector<float*> ptrs { clip.channels[0].data(), clip.channels[1].data() };
    eq.reset();
    eq.process (ptrs.data(), 2, clip.numSamples());
    double sum = 0.0;
    const int half = clip.numSamples() / 2;
    for (int i = half; i < clip.numSamples(); ++i)
        sum += static_cast<double> (clip.channels[0][static_cast<size_t> (i)]) * clip.channels[0][static_cast<size_t> (i)];
    const double rms = std::sqrt (sum / (clip.numSamples() - half));
    return 20.0 * std::log10 (rms * std::sqrt (2.0)) + 12.0;
}

TonalEQParams flat() { return TonalEQParams {}; }

} // namespace

TEST_CASE ("A flat or disabled EQ is an exact pass-through", "[eq]")
{
    const auto mix = testsig::musicLike (48000.0, 2.0, -6.0);

    for (bool enabled : { true, false })
    {
        auto p = flat();
        p.enabled = enabled;
        if (! enabled)
            p.gainDb = { 3, -2, 1, 4, -5, 2, 6 };   // ignored while off
        const auto out = renderEq (mix, p);
        CHECK (out.channels == mix.channels);

        TonalEQ eq;
        eq.prepare (48000.0, 2);
        eq.setParams (p);
        CHECK_FALSE (eq.isActive());
    }
}

TEST_CASE ("Each band hits its exact gain at its frequency", "[eq]")
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int b = 0; b < kEqBands; ++b)
            for (double g : { -6.0, 4.5 })
            {
                const auto& spec = kEqBandSpecs[static_cast<size_t> (b)];
                const auto c = TonalEQ::designBand (b, g, spec.defaultHz, sr);
                INFO ("sr " << sr << " band " << b << " gain " << g);
                if (spec.shape == EqBandSpec::Shape::bell)
                    CHECK_THAT (c.magnitudeDb (spec.defaultHz, sr), WithinAbs (g, 0.01));
                else
                    CHECK_THAT (c.magnitudeDb (spec.defaultHz, sr), WithinAbs (g / 2.0, 0.05));   // shelf midpoint
                // Far from the band the response returns to 0 dB (bells) or the full shelf gain.
                if (spec.shape == EqBandSpec::Shape::bell)
                    CHECK (std::abs (c.magnitudeDb (spec.defaultHz / 16.0, sr)) < 0.35);
                if (spec.shape == EqBandSpec::Shape::lowShelf)
                    CHECK_THAT (c.magnitudeDb (spec.defaultHz / 8.0, sr), WithinAbs (g, 0.15));
                if (spec.shape == EqBandSpec::Shape::highShelf && sr > 90000.0)
                    CHECK_THAT (c.magnitudeDb (spec.defaultHz * 3.0, sr), WithinAbs (g, 0.3));
            }
}

TEST_CASE ("Real audio through the EQ matches the designed response", "[eq]")
{
    const double sr = 48000.0;
    auto p = flat();
    p.gainDb = { 2.0, 3.0, -2.0, 1.5, -1.0, 2.5, 3.0 };
    TonalEQ eq;
    eq.prepare (sr, 2);
    eq.setParams (p);
    for (double f : { 40.0, 100.0, 350.0, 1000.0, 2500.0, 5000.0, 14000.0 })
    {
        INFO ("f " << f);
        CHECK_THAT (sineGainDb (eq, sr, f), WithinAbs (TonalEQ::responseDb (p, f, sr), 0.05));
    }
}

TEST_CASE ("Moving a band's frequency moves the peak", "[eq]")
{
    const double sr = 48000.0;
    auto p = flat();
    p.gainDb[3] = 6.0;
    p.freqHz[3] = 2000.0;
    CHECK_THAT (TonalEQ::responseDb (p, 2000.0, sr), WithinAbs (6.0, 0.01));
    CHECK (TonalEQ::responseDb (p, 1000.0, sr) < 4.0);
    // Out-of-range frequencies are held inside the band's range.
    p.freqHz[3] = 50000.0;
    CHECK_THAT (TonalEQ::responseDb (p, 2500.0, sr), WithinAbs (6.0, 0.01));
}

TEST_CASE ("Gain changes glide without clicks and settle back to pass-through", "[eq]")
{
    const double sr = 48000.0;
    AudioClip clip = testsig::sine (sr, 1, 100.0, -6.0, 2.0);
    const auto original = clip.channels[0];
    std::vector<float*> ptrs { clip.channels[0].data() };

    TonalEQ eq;
    eq.prepare (sr, 1);
    eq.setParams (flat());

    auto boosted = flat();
    boosted.gainDb[1] = 9.0;   // +9 dB at 100 Hz, switched in abruptly
    const int block = 256;
    const int n = clip.numSamples();
    for (int start = 0; start < n; start += block)
    {
        if (start == 4800)  eq.setParams (boosted);
        if (start == 48000) eq.setParams (flat());
        eq.process (ptrs.data() + 0, 1, std::min (block, n - start));
        ptrs[0] += std::min (block, n - start);
    }

    // The steepest step a +9 dB 100 Hz sine can take; a click would far exceed it.
    const double amp = std::pow (10.0, (-6.0 + 9.0) / 20.0);
    const double maxStep = amp * 2.0 * std::numbers::pi * 100.0 / sr;
    double worst = 0.0;
    for (int i = 1; i < n; ++i)
        worst = std::max (worst, std::abs (static_cast<double> (clip.channels[0][static_cast<size_t> (i)]) - clip.channels[0][static_cast<size_t> (i - 1)]));
    CHECK (worst < maxStep * 1.1);

    // Well after returning to flat, the EQ is out of the path again: bit-exact.
    CHECK_FALSE (eq.isActive());
    for (int i = n - 4800; i < n; ++i)
        REQUIRE (clip.channels[0][static_cast<size_t> (i)] == original[static_cast<size_t> (i)]);
}

TEST_CASE ("Extreme settings stay stable at every sample rate", "[eq]")
{
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (double g : { -9.0, 9.0 })
            for (int edge = 0; edge < 2; ++edge)
            {
                auto p = flat();
                for (int b = 0; b < kEqBands; ++b)
                {
                    p.gainDb[static_cast<size_t> (b)] = g;
                    const auto& spec = kEqBandSpecs[static_cast<size_t> (b)];
                    p.freqHz[static_cast<size_t> (b)] = edge ? spec.maxHz : spec.minHz;
                }
                const auto out = renderEq (testsig::musicLike (sr, 1.0, -6.0), p);
                double peak = 0.0;
                for (const auto& ch : out.channels)
                    for (float v : ch)
                    {
                        REQUIRE (std::isfinite (v));
                        peak = std::max (peak, static_cast<double> (std::abs (v)));
                    }
                INFO ("sr " << sr << " gain " << g << " edge " << edge << " peak " << peak);
                CHECK (peak < 30.0);
            }
}

TEST_CASE ("The loudness search accounts for the EQ", "[eq][targeting]")
{
    const auto mix = testsig::musicLike (44100.0, 20.0, -6.0);
    MasterChainParams chain;
    chain.limiter.ceilingDbtp = -1.0;
    chain.eq.gainDb[0] = 4.0;   // a big low-end boost costs loudness headroom
    const auto r = findDriveForTarget (mix, -11.0, chain);
    CHECK (r.reachedTarget);
    chain.driveDb = r.driveDb;
    const auto check = measure (renderMaster (mix, chain));
    CHECK_THAT (check.integratedLufs, WithinAbs (-11.0, 0.1));
    CHECK (check.truePeakDbtp <= -0.95);
}

TEST_CASE ("After prepare, the next change glides whether the settings came before or after prepare", "[eq]")
{
    // The plug-in hands the EQ its settings before prepare(); a later knob move must still glide.
    TonalEQParams a, b;
    b.gainDb[3] = -6.0;
    const auto noise = testsig::musicLike (48000.0, 0.3, -6.0);
    auto run = [&] (bool settingsFirst)
    {
        TonalEQ eq;
        if (settingsFirst) eq.setParams (a);
        eq.prepare (48000.0, 2);
        if (! settingsFirst) eq.setParams (a);
        AudioClip c = noise;
        float* ptrs[] = { c.channels[0].data(), c.channels[1].data() };
        const int half = c.numSamples() / 2;
        eq.process (ptrs, 2, half);
        eq.setParams (b);
        float* rest[] = { ptrs[0] + half, ptrs[1] + half };
        eq.process (rest, 2, c.numSamples() - half);
        return c;
    };
    const auto first = run (true), after = run (false);
    double d = 0.0;
    for (size_t i = 0; i < first.channels[0].size(); ++i)
        d = std::max (d, std::abs ((double) first.channels[0][i] - after.channels[0][i]));
    CHECK (d < 1.0e-9);
}

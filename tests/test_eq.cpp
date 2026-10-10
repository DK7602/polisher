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

/** The band's analog (RBJ prototype) response in dB: what the digital band should follow. */
double analogDb (int band, double gainDb, double f0, double f)
{
    const auto& spec = kEqBandSpecs[static_cast<size_t> (band)];
    const double A = std::pow (10.0, gainDb / 40.0), x = f / f0, x2 = x * x, q = spec.q;
    double p = 1.0;
    if (spec.shape == EqBandSpec::Shape::bell)
        p = ((1 - x2) * (1 - x2) + (A * x / q) * (A * x / q)) / ((1 - x2) * (1 - x2) + (x / (A * q)) * (x / (A * q)));
    else if (spec.shape == EqBandSpec::Shape::lowShelf)
        p = A * A * ((A - x2) * (A - x2) + A * x2 / (q * q)) / ((1 - A * x2) * (1 - A * x2) + A * x2 / (q * q));
    else
        p = A * A * ((1 - A * x2) * (1 - A * x2) + A * x2 / (q * q)) / ((A - x2) * (A - x2) + A * x2 / (q * q));
    return 10.0 * std::log10 (p);
}

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

TEST_CASE ("High bands keep their analog shape up to 20 kHz at 44.1 and 48 kHz", "[eq]")
{
    // The audit case: Presence +6 dB at 10 kHz and Air +6 dB at 12 kHz at 44.1 kHz. Plain RBJ gave
    // 2.70 dB at 14 kHz (analog 4.30) and 0.54 dB at 18 kHz (analog 2.67), and the Air shelf
    // overshot to 5.74 dB at 16.8 kHz (analog 4.72).
    const double sr = 44100.0;
    const auto presence = TonalEQ::designBand (5, 6.0, 10000.0, sr);
    for (double f = 1000.0; f <= 18000.0; f *= 1.05)
    {
        INFO ("f " << f);
        CHECK_THAT (presence.magnitudeDb (f, sr), WithinAbs (analogDb (5, 6.0, 10000.0, f), 0.5));
    }
    CHECK_THAT (presence.magnitudeDb (14000.0, sr), WithinAbs (4.30, 0.2));
    CHECK_THAT (presence.magnitudeDb (18000.0, sr), WithinAbs (2.67, 0.3));
    const auto air = TonalEQ::designBand (6, 6.0, 12000.0, sr);
    CHECK_THAT (air.magnitudeDb (16800.0, sr), WithinAbs (analogDb (6, 6.0, 12000.0, 16800.0), 0.2));

    // Every band, gain and frequency in range: within 0.75 dB of analog up to 20 kHz at 44.1 / 48 kHz,
    // and within 0.05 dB at 96 kHz.
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        double worst = 0.0;
        for (int b = 0; b < kEqBands; ++b)
        {
            const auto& spec = kEqBandSpecs[static_cast<size_t> (b)];
            for (double f0 : { spec.minHz, spec.defaultHz, spec.maxHz })
                for (double g : { -9.0, -4.0, -1.0, 1.0, 4.0, 9.0 })
                {
                    const auto c = TonalEQ::designBand (b, g, f0, rate);
                    for (double f = 20.0; f <= 20000.0; f *= 1.02)
                        worst = std::max (worst, std::abs (c.magnitudeDb (f, rate) - analogDb (b, g, f0, f)));
                }
        }
        INFO ("sr " << rate << " worst " << worst);
        CHECK (worst < (rate > 90000.0 ? 0.05 : 0.75));
    }
}

TEST_CASE ("Every band design is stable and minimum-phase from 44.1 to 192 kHz", "[eq]")
{
    // Poles and zeros strictly inside the unit circle: a2 < 1 and |a1| < 1 + a2 (and the same for
    // the numerator, normalised by b0), for every band across its whole gain and frequency range.
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (int b = 0; b < kEqBands; ++b)
        {
            const auto& spec = kEqBandSpecs[static_cast<size_t> (b)];
            for (double f0 = spec.minHz; f0 <= spec.maxHz * 1.0001; f0 *= 1.1)
                for (double g = -9.0; g <= 9.0; g += 0.5)
                {
                    const auto c = TonalEQ::designBand (b, g, f0, sr);
                    INFO ("sr " << sr << " band " << b << " f0 " << f0 << " g " << g);
                    REQUIRE (c.b0 > 0.0);
                    REQUIRE (std::abs (c.a2) < 1.0);
                    REQUIRE (std::abs (c.a1) < 1.0 + c.a2);
                    REQUIRE (std::abs (c.b2 / c.b0) < 1.0);
                    REQUIRE (std::abs (c.b1 / c.b0) < 1.0 + c.b2 / c.b0);
                }
        }
}

TEST_CASE ("A Low End boost brings in the subsonic filter; it leaves the audible range alone", "[eq]")
{
    const double sr = 44100.0;
    auto p = flat();
    p.gainDb[0] = 3.0;   // +3 dB Low End shelf at 45 Hz
    CHECK (TonalEQ::subsonicEngaged (p));
    // Without the filter the shelf would add ~3 dB from 5 to 20 Hz; with it, rumble is cut instead.
    CHECK (TonalEQ::responseDb (p, 5.0, sr) < -30.0);
    CHECK (TonalEQ::responseDb (p, 10.0, sr) < -10.0);
    // Audible range: < 0.5 dB off the shelf alone at 20 Hz, nothing measurable from 30 Hz.
    const auto shelf = TonalEQ::designBand (0, 3.0, 45.0, sr);
    CHECK (std::abs (TonalEQ::responseDb (p, 20.0, sr) - shelf.magnitudeDb (20.0, sr)) < 0.5);
    for (double f : { 30.0, 45.0, 100.0, 1000.0 })
        CHECK (std::abs (TonalEQ::responseDb (p, f, sr) - shelf.magnitudeDb (f, sr)) < 0.03);

    // Cuts and other bands don't engage it.
    p.gainDb = { -3.0, 4.0, 0, 0, 0, 0, 2.0 };
    CHECK_FALSE (TonalEQ::subsonicEngaged (p));
    p.gainDb[0] = 3.0;
    p.enabled = false;
    CHECK_FALSE (TonalEQ::subsonicEngaged (p));

    // Real audio: a 7 Hz rumble comes down by the designed amount.
    p = flat();
    p.gainDb[0] = 3.0;
    TonalEQ eq;
    eq.prepare (sr, 2);
    eq.setParams (p);
    CHECK_THAT (sineGainDb (eq, sr, 7.0), WithinAbs (TonalEQ::responseDb (p, 7.0, sr), 0.3));
}

TEST_CASE ("The subsonic filter fades in and out without clicks and leaves a bit-exact path", "[eq]")
{
    const double sr = 44100.0;
    // A 30 Hz tone riding on a DC offset: the worst case for switching a high-pass in.
    AudioClip clip = testsig::sine (sr, 1, 30.0, -6.0, 2.0);
    for (auto& v : clip.channels[0])
        v += 0.25f;
    const auto original = clip.channels[0];
    std::vector<float*> ptrs { clip.channels[0].data() };

    TonalEQ eq;
    eq.prepare (sr, 1);
    eq.setParams (flat());
    auto boosted = flat();
    boosted.gainDb[0] = 6.0;
    const int block = 256, n = clip.numSamples();
    for (int start = 0; start < n; start += block)
    {
        if (start == 4352)  eq.setParams (boosted);
        if (start == 44032) eq.setParams (flat());
        eq.process (ptrs.data(), 1, std::min (block, n - start));
        ptrs[0] += std::min (block, n - start);
    }
    // Biggest sample-to-sample step a +6 dB 30 Hz tone can take, plus a little for the DC fading
    // out over the 20 ms glide; a click would be far larger.
    const double amp = std::pow (10.0, (-6.0 + 6.0) / 20.0);
    const double maxStep = amp * 2.0 * std::numbers::pi * 30.0 / sr;
    double worst = 0.0;
    for (int i = 1; i < n; ++i)
        worst = std::max (worst, std::abs (static_cast<double> (clip.channels[0][static_cast<size_t> (i)]) - clip.channels[0][static_cast<size_t> (i - 1)]));
    INFO ("worst step " << worst << " limit " << maxStep);
    CHECK (worst < maxStep * 1.2);

    CHECK_FALSE (eq.isActive());
    for (int i = n - 4410; i < n; ++i)
        REQUIRE (clip.channels[0][static_cast<size_t> (i)] == original[static_cast<size_t> (i)]);
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

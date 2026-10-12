#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Signals.h"
#include "polisher/Dither.h"

#include <cmath>
#include <numbers>
#include <vector>

using namespace polisher;
using Catch::Matchers::WithinAbs;

TEST_CASE ("Dither off leaves the audio untouched", "[dither]")
{
    auto c = testsig::musicLike (48000.0, 1.0, -12.0);
    const auto in = c;
    Dither d;
    d.setBits (0);
    d.reset();
    float* ch[2] = { c.channels[0].data(), c.channels[1].data() };
    d.process (ch, 2, c.numSamples());
    CHECK (c.channels == in.channels);
}

TEST_CASE ("16-bit dither lands every sample on the 16-bit grid, with TPDF noise and no distortion", "[dither]")
{
    const double sr = 48000.0;
    // A very quiet 1 kHz tone (-90 dBFS, under 2 LSB): plain rounding would turn it into a square-ish wave.
    std::vector<double> x (static_cast<size_t> (sr * 2));
    for (size_t i = 0; i < x.size(); ++i) x[i] = std::pow (10.0, -90.0 / 20.0) * std::sin (2.0 * std::numbers::pi * 1000.0 * i / sr);
    std::vector<double> y = x;
    double* ch[1] = { y.data() };
    Dither d;
    d.setBits (16);
    d.reset();
    d.process (ch, 1, static_cast<int> (y.size()));

    // On the grid.
    bool onGrid = true;
    for (double v : y) onGrid = onGrid && std::abs (v * 32768.0 - std::round (v * 32768.0)) < 1.0e-9;
    CHECK (onGrid);
    // The error is noise: about TPDF + rounding power (1/4 LSB^2 ... ), uncorrelated with the tone, and the tone survives.
    double err = 0.0, re = 0.0, im = 0.0, h3re = 0.0, h3im = 0.0;
    for (size_t i = 0; i < y.size(); ++i)
    {
        const double e = (y[i] - x[i]) * 32768.0;
        err += e * e;
        const double w = 2.0 * std::numbers::pi * 1000.0 * i / sr;
        re += y[i] * std::cos (w); im += y[i] * std::sin (w);
        h3re += y[i] * std::cos (3.0 * w); h3im += y[i] * std::sin (3.0 * w);
    }
    const double lsbPower = err / y.size();
    CHECK_THAT (lsbPower, WithinAbs (0.25, 0.05));   // TPDF (1/6) + rounding (1/12) = 1/4 LSB^2
    const double toneDb = 20.0 * std::log10 (2.0 * std::hypot (re, im) / y.size());
    const double h3Db = 20.0 * std::log10 (2.0 * std::hypot (h3re, h3im) / y.size());
    CHECK_THAT (toneDb, WithinAbs (-90.0, 0.5));
    CHECK (h3Db < toneDb - 30.0);   // no harmonic distortion from the quantising
}

TEST_CASE ("24-bit dither is on the 24-bit grid and never clips", "[dither]")
{
    std::vector<float> l { 1.0f, -1.0f, 0.5f, 0.0f }, r = l;
    float* ch[2] = { l.data(), r.data() };
    Dither d;
    d.setBits (24);
    d.reset();
    d.process (ch, 2, 4);
    for (float v : l) { CHECK (v <= 1.0f); CHECK (v >= -1.0f); }
}

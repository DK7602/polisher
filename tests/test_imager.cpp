#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Signals.h"
#include "polisher/StereoImager.h"

#include <cmath>
#include <numbers>

using namespace polisher;
using Catch::Matchers::WithinAbs;

namespace {
// Side-only sine at f; returns side level (dB) after the imager, measured over the last second.
double sideAfter (const ImagerParams& p, double f, double sr = 48000.0)
{
    AudioClip c = testsig::silence (sr, 2, 2.0);
    for (int i = 0; i < c.numSamples(); ++i)
    {
        const auto s = static_cast<float> (0.25 * std::sin (2.0 * std::numbers::pi * f * i / sr));
        c.channels[0][static_cast<size_t> (i)] = s;
        c.channels[1][static_cast<size_t> (i)] = -s;
    }
    StereoImager im; im.prepare (sr); im.setParams (p); im.reset();
    float* ch[2] = { c.channels[0].data(), c.channels[1].data() };
    im.process (ch, 2, c.numSamples());
    double sum = 0.0; const int a = c.numSamples() / 2;
    for (int i = a; i < c.numSamples(); ++i)
    {
        const double s = 0.5 * (c.channels[0][static_cast<size_t> (i)] - c.channels[1][static_cast<size_t> (i)]);
        sum += s * s;
    }
    return 10.0 * std::log10 (sum / (c.numSamples() - a)) - 10.0 * std::log10 (0.25 * 0.25 / 2.0);
}
}

TEST_CASE ("Imager at 100/100/100 is an exact pass-through", "[imager]")
{
    auto mix = testsig::musicLike (48000.0, 2.0, -6.0);
    const auto original = mix;
    StereoImager im; im.prepare (48000.0);
    float* ch[2] = { mix.channels[0].data(), mix.channels[1].data() };
    im.process (ch, 2, mix.numSamples());
    CHECK (mix.channels == original.channels);
    CHECK_FALSE (im.isActive());
}

TEST_CASE ("Imager widens or narrows each band, mid untouched", "[imager]")
{
    ImagerParams p; p.lowWidth = 0.0; p.midWidth = 100.0; p.highWidth = 150.0;
    CHECK (sideAfter (p, 50.0) < -25.0);                                          // lows made mono
    CHECK_THAT (sideAfter (p, 1000.0), WithinAbs (0.0, 1.0));                      // mids unchanged
    CHECK_THAT (sideAfter (p, 14000.0), WithinAbs (20.0 * std::log10 (1.5), 0.6)); // highs wider

    ImagerParams narrow; narrow.midWidth = 50.0;
    CHECK_THAT (sideAfter (narrow, 1000.0), WithinAbs (-6.0, 0.8));

    // A mono (mid-only) signal is left alone whatever the widths.
    auto mono = testsig::sine (48000.0, 2, 1000.0, -6.0, 1.0);
    const auto before = mono;
    StereoImager im; im.prepare (48000.0); im.setParams (p); im.reset();
    float* ch[2] = { mono.channels[0].data(), mono.channels[1].data() };
    im.process (ch, 2, mono.numSamples());
    double worst = 0.0;
    for (size_t i = 0; i < before.channels[0].size(); ++i)
        worst = std::max (worst, static_cast<double> (std::abs (mono.channels[0][i] - before.channels[0][i])));
    CHECK (worst < 1.0e-6);
    CHECK (im.correlation() > 0.99);
}

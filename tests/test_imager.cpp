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

TEST_CASE ("Imager widens or narrows each band, mid level untouched", "[imager]")
{
    ImagerParams p; p.lowWidth = 0.0; p.midWidth = 100.0; p.highWidth = 150.0;
    CHECK (sideAfter (p, 50.0) < -25.0);                                          // lows made mono
    CHECK_THAT (sideAfter (p, 1000.0), WithinAbs (0.0, 1.0));                      // mids unchanged
    CHECK_THAT (sideAfter (p, 14000.0), WithinAbs (20.0 * std::log10 (1.5), 0.6)); // highs wider

    ImagerParams narrow; narrow.midWidth = 50.0;
    CHECK_THAT (sideAfter (narrow, 1000.0), WithinAbs (-6.0, 0.8));

    // A mono (mid-only) signal keeps its level and stays mono whatever the widths (it only goes through
    // the same all-passes as the side, which change phase, never level).
    for (double f : { 60.0, 1000.0, 12000.0 })
    {
        auto mono = testsig::sine (48000.0, 2, f, -6.0, 1.0);
        StereoImager im; im.prepare (48000.0); im.setParams (p); im.reset();
        float* ch[2] = { mono.channels[0].data(), mono.channels[1].data() };
        im.process (ch, 2, mono.numSamples());
        double ms = 0.0, side = 0.0;
        const int a = mono.numSamples() / 2;
        for (int i = a; i < mono.numSamples(); ++i)
        {
            const double l = mono.channels[0][static_cast<size_t> (i)], r = mono.channels[1][static_cast<size_t> (i)];
            ms += l * l; side += (l - r) * (l - r);
        }
        INFO ("f " << f);
        CHECK_THAT (10.0 * std::log10 (ms / (mono.numSamples() - a)), WithinAbs (-6.0 - 3.0103, 0.05));
        CHECK (side < 1.0e-9);
        CHECK (im.correlation() > 0.99);
    }
}

TEST_CASE ("Imager keeps a hard-panned sound on its side", "[imager]")
{
    // A sound only in the left speaker must not leak into the right one when the widths move a little:
    // the mid goes through the same all-passes as the split side, so mid and side stay in phase.
    const double sr = 48000.0;
    for (double f : { 100.0, 250.0, 1000.0, 5000.0, 10000.0 })
        for (const ImagerParams p : { ImagerParams { true, 100.0, 100.0, 101.0 }, ImagerParams { true, 101.0, 100.0, 100.0 }, ImagerParams { true, 100.0, 101.0, 100.0 } })
        {
            AudioClip c = testsig::silence (sr, 2, 2.0);
            for (int i = 0; i < c.numSamples(); ++i)
                c.channels[0][static_cast<size_t> (i)] = static_cast<float> (0.25 * std::sin (2.0 * std::numbers::pi * f * i / sr));
            StereoImager im; im.prepare (sr); im.setParams (p); im.reset();
            float* ch[2] = { c.channels[0].data(), c.channels[1].data() };
            im.process (ch, 2, c.numSamples());
            double l = 0.0, r = 0.0;
            for (int i = c.numSamples() / 2; i < c.numSamples(); ++i)
            {
                l += c.channels[0][static_cast<size_t> (i)] * c.channels[0][static_cast<size_t> (i)];
                r += c.channels[1][static_cast<size_t> (i)] * c.channels[1][static_cast<size_t> (i)];
            }
            INFO ("f " << f << " Hz");
            CHECK (10.0 * std::log10 (r / l) < -40.0);
        }
}

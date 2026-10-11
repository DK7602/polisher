#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Signals.h"
#include "SwitchLevel.h"
#include "polisher/StereoImager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <random>
#include <vector>

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

namespace {

/** Stereo signal through the Imager in 64-sample blocks; `paramsAt` gives the settings for each block. */
template <typename ParamsAt>
std::array<std::vector<double>, 2> runImager (std::array<std::vector<double>, 2> x, double sr, const ImagerParams& start, ParamsAt paramsAt, StereoImager* keep = nullptr)
{
    StereoImager local;
    StereoImager& im = keep != nullptr ? *keep : local;
    im.prepare (sr);
    im.setParams (start);
    im.reset();
    const int n = static_cast<int> (x[0].size());
    for (int s = 0; s < n; s += 64)
    {
        im.setParams (paramsAt (s));
        double* ch[2] = { x[0].data() + s, x[1].data() + s };
        im.process (ch, 2, std::min (64, n - s));
    }
    return x;
}

std::array<std::vector<double>, 2> tonePair (double sr, double seconds, double hz, double left, double right)
{
    std::array<std::vector<double>, 2> x;
    const int n = static_cast<int> (seconds * sr);
    for (auto& ch : x) ch.resize (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        const double v = std::sin (2.0 * std::numbers::pi * hz * i / sr);
        x[0][static_cast<size_t> (i)] = left * v;
        x[1][static_cast<size_t> (i)] = right * v;
    }
    return x;
}

} // namespace

TEST_CASE ("Imager switches on and off without a hole in the centre", "[imager][switching]")
{
    // The centre goes through the 250 Hz and 5 kHz all-passes while the Imager works. Blending it with the
    // plain centre used to cancel at those frequencies: a centre tone fell ~20 dB for ~50 ms whenever the
    // Imager left 100 / 100 / 100 (even 100 -> 101 %) or came back, or was switched. Now the short-time level
    // stays within 1 dB, and once through it is exactly the steady setting (or an exact pass-through).
    ImagerParams tiny;
    tiny.highWidth = 101.0;
    ImagerParams wide { true, 0.0, 120.0, 150.0 };
    ImagerParams wideOff = wide;
    wideOff.enabled = false;
    struct Switch { const char* what; ImagerParams off, on; };
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const auto& sw : { Switch { "highs 100 -> 101 %", ImagerParams {}, tiny }, Switch { "module switch (0 / 120 / 150 %)", wideOff, wide } })
            for (double hz : { 60.0, 250.0, 1000.0, 5000.0, 12000.0 })
            {
                const int n = static_cast<int> (2.0 * sr), at = static_cast<int> (0.5 * sr) / 64 * 64;
                const auto tone = tonePair (sr, 2.0, hz, 0.5, 0.5);
                const auto steadyOn = runImager (tone, sr, sw.on, [&] (int) { return sw.on; });
                for (bool switchOn : { true, false })
                {
                    StereoImager im;
                    const auto out = runImager (tone, sr, switchOn ? sw.off : sw.on, [&] (int s) { return (s >= at) == switchOn ? sw.on : sw.off; }, &im);
                    const auto r = switchlevel::measure (out[0], sr, hz, at);
                    INFO ("sr " << sr << ", " << sw.what << " switched " << (switchOn ? "on" : "off") << ", " << hz << " Hz centre tone: dip " << r.dipDb << " dB, swell +" << r.bumpDb << " dB");
                    CHECK (r.dipDb > -1.0);
                    CHECK (r.bumpDb < 1.0);
                    if (hz <= 1000.0)   // and no click: no sample-to-sample step beyond the tone's own
                        CHECK (switchlevel::stepRatio (out[0], at) < 1.2);
                    double diff = 0.0;
                    for (int i = static_cast<int> (1.5 * sr); i < n; ++i)
                        diff = std::max (diff, std::abs (out[0][static_cast<size_t> (i)] - (switchOn ? steadyOn[0] : tone[0])[static_cast<size_t> (i)]));
                    if (switchOn)
                        CHECK (diff < 1.0e-7);
                    else
                    {
                        CHECK (diff == 0.0);
                        CHECK_FALSE (im.isActive());
                    }
                }
            }

    // A hard-left sound stays on the left while the Imager switches: no more of it on the right at any
    // moment than once the widths are set (narrower lows and wider highs do move some across; that is the
    // effect). The widths only move once the crossovers are in place.
    const double sr = 48000.0;
    const int at = static_cast<int> (0.5 * sr) / 64 * 64;
    const ImagerParams p { true, 50.0, 100.0, 150.0 };
    for (double hz : { 400.0, 1000.0, 2000.0, 8000.0 })
    {
        const int w = switchlevel::windowFor (hz, sr);
        const auto steady = runImager (tonePair (sr, 2.0, hz, 0.5, 0.0), sr, p, [&] (int) { return p; });
        const int last = static_cast<int> (steady[0].size()) - w;
        const double steadyRight = switchlevel::levelDb (steady[1], last, w) - switchlevel::levelDb (steady[0], last, w);
        for (bool switchOn : { true, false })
        {
            const auto out = runImager (tonePair (sr, 2.0, hz, 0.5, 0.0), sr, switchOn ? ImagerParams {} : p, [&] (int s) { return (s >= at) == switchOn ? p : ImagerParams {}; });
            double worst = -300.0;
            for (int s = at; s + w <= static_cast<int> (out[0].size()); s += w)
                worst = std::max (worst, switchlevel::levelDb (out[1], s, w) - switchlevel::levelDb (out[0], s, w));
            INFO (hz << " Hz hard left, Imager switched " << (switchOn ? "on" : "off") << ": right " << worst << " dB re left (set: " << steadyRight << " dB)");
            CHECK (worst < steadyRight + 1.0);
        }
    }
}

TEST_CASE ("Imager works the same at every sample rate", "[imager]")
{
    // The crossovers sit at 250 Hz and 5 kHz at every rate: lows made mono, mids kept, highs widened.
    for (double sr : { 44100.0, 88200.0, 96000.0, 192000.0 })
    {
        ImagerParams p; p.lowWidth = 0.0; p.midWidth = 100.0; p.highWidth = 150.0;
        INFO ("sr " << sr);
        CHECK (sideAfter (p, 50.0, sr) < -25.0);
        CHECK_THAT (sideAfter (p, 1000.0, sr), WithinAbs (0.0, 1.0));
        CHECK_THAT (sideAfter (p, 14000.0, sr), WithinAbs (20.0 * std::log10 (1.5), 0.6));
    }
}

TEST_CASE ("Imager stays stable when every control jumps at random", "[imager]")
{
    // Widths jumping anywhere in 0-200 %, the module switching on and off, every 10-80 ms, on a loud wide
    // mix: always finite, never a click (no sample-to-sample step beyond what the music and the widths
    // themselves allow), the correlation meter stays in range, and back at 100 % it is an exact
    // pass-through again.
    for (double sr : { 44100.0, 192000.0 })
    {
        auto mix = testsig::musicLike (sr, 6.0, -6.0);
        const auto original = mix;
        StereoImager im;
        im.prepare (sr);
        std::mt19937 rng (5);
        std::uniform_real_distribution<double> width (0.0, 200.0), hold (0.01, 0.08);
        const int n = mix.numSamples(), settle = static_cast<int> (5.0 * sr);
        int next = 0;
        ImagerParams p;
        double maxIn = 0.0, maxOut = 0.0;
        for (int s = 0; s < n; s += 64)
        {
            if (s >= next && s < settle)
            {
                p = { rng() % 5 != 0, width (rng), width (rng), width (rng) };
                next = s + static_cast<int> (hold (rng) * sr);
            }
            if (s >= settle) p = ImagerParams {};
            im.setParams (p);
            float* ch[2] = { mix.channels[0].data() + s, mix.channels[1].data() + s };
            im.process (ch, 2, std::min (64, n - s));
            CHECK (std::isfinite (im.correlation()));
            CHECK (im.correlation() >= -1.0);
            CHECK (im.correlation() <= 1.0);
        }
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = 1; i < static_cast<size_t> (settle); ++i)
            {
                REQUIRE (std::isfinite (mix.channels[c][i]));
                maxIn = std::max (maxIn, static_cast<double> (std::abs (original.channels[c][i] - original.channels[c][i - 1])));
                maxOut = std::max (maxOut, static_cast<double> (std::abs (mix.channels[c][i] - mix.channels[c][i - 1])));
            }
        INFO ("sr " << sr << ": largest step in " << maxIn << ", out " << maxOut);
        CHECK (maxOut < 3.0 * maxIn);   // widths up to 200 % can double the side; a click would be far larger
        CHECK_FALSE (im.isActive());
        const int tail = static_cast<int> (0.3 * sr);
        for (size_t c = 0; c < 2; ++c)
            CHECK (std::equal (mix.channels[c].end() - tail, mix.channels[c].end(), original.channels[c].end() - tail));
    }
}

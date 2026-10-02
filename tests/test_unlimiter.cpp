#include <catch2/catch_test_macros.hpp>

#include "Signals.h"
#include "polisher/LoudnessTargeting.h"
#include "polisher/Unlimiter.h"

#include <cmath>
#include <numbers>

using namespace polisher;

namespace {
// Runs the Unlimiter and removes its latency, so the result lines up with the input.
AudioClip runUnlimiter (const AudioClip& in, const UnlimiterParams& p, Unlimiter* keep = nullptr)
{
    Unlimiter local;
    Unlimiter& u = keep ? *keep : local;
    u.prepare (in.sampleRate, in.numChannels());
    u.setParams (p);
    u.reset();
    const int lat = u.latencySamples(), n = in.numSamples(), nch = in.numChannels();
    AudioClip out = in;
    std::vector<double> frame (static_cast<size_t> (nch));
    for (int i = 0; i < n + lat; ++i)
    {
        for (int c = 0; c < nch; ++c) frame[static_cast<size_t> (c)] = i < n ? in.channels[static_cast<size_t> (c)][static_cast<size_t> (i)] : 0.0;
        u.processFrame (frame.data(), nch);
        if (i >= lat)
            for (int c = 0; c < nch; ++c) out.channels[static_cast<size_t> (c)][static_cast<size_t> (i - lat)] = static_cast<float> (frame[static_cast<size_t> (c)]);
    }
    return out;
}

// Kick hits every 0.5 s in musicLike: level of the hit (first 30 ms) minus the level between hits
// (300 ... 450 ms after), averaged over the clip from 1 s on (dB).
double hitContrastDb (const AudioClip& c)
{
    const double sr = c.sampleRate;
    double hit = 0.0, between = 0.0;
    for (size_t i = static_cast<size_t> (sr); i < c.channels[0].size(); ++i)
    {
        const double t = std::fmod (static_cast<double> (i) / sr, 0.5);
        const double e = static_cast<double> (c.channels[0][i]) * c.channels[0][i];
        if (t < 0.03) hit += e / 0.03;
        else if (t >= 0.3 && t < 0.45) between += e / 0.15;
    }
    return 10.0 * std::log10 (hit / between);
}

double rmsDb (const AudioClip& c, size_t a, size_t b)
{
    double e = 0.0;
    for (size_t i = a; i < b; ++i) e += static_cast<double> (c.channels[0][i]) * c.channels[0][i];
    return 10.0 * std::log10 (e / static_cast<double> (b - a) + 1e-30);
}

AudioClip brickwalled (double sr, double seconds, double pushDb)
{
    const auto mix = testsig::musicLike (sr, seconds, -6.0, 4);
    LimiterParams lp; lp.driveDb = pushDb; lp.ceilingDbtp = -0.3; lp.releaseMs = 30.0; lp.adaptiveRelease = false;
    return renderLimited (mix, lp);
}
}

TEST_CASE ("Unlimiter off or at Amount 0 is an exact delayed pass-through", "[unlimiter]")
{
    const auto in = testsig::musicLike (48000.0, 3.0, -10.0);
    for (int mode = 0; mode < 2; ++mode)
    {
        UnlimiterParams p; p.amount = mode == 0 ? 0.0 : 80.0; p.enabled = mode == 0;
        Unlimiter u;
        const auto out = runUnlimiter (in, p, &u);
        CHECK (u.latencySamples() == 72);   // 1.5 ms at 48 kHz
        bool same = true;
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = 0; i < in.channels[c].size(); ++i) same = same && out.channels[c][i] == in.channels[c][i];
        CHECK (same);
    }
}

TEST_CASE ("Unlimiter puts back the punch a limiter squashed out", "[unlimiter]")
{
    const double sr = 48000.0;
    const auto original = testsig::musicLike (sr, 8.0, -6.0, 4);
    const auto squashed = brickwalled (sr, 8.0, 14.0);
    const double c0 = hitContrastDb (original), cl = hitContrastDb (squashed);
    UNSCOPED_INFO ("hit contrast: original " << c0 << " dB, squashed " << cl << " dB");
    REQUIRE (c0 - cl > 3.0);   // the limiter really did flatten the hits

    double last = cl;
    for (double amount : { 50.0, 100.0 })
    {
        UnlimiterParams p; p.amount = amount;
        const auto out = runUnlimiter (squashed, p);
        const double cu = hitContrastDb (out);
        // Level between the hits stays put: only the hits come back up.
        double between = 0.0, betweenIn = 0.0;
        for (int k = 2; k < 15; ++k)
        {
            const auto a = static_cast<size_t> ((k * 0.5 + 0.3) * sr), b = static_cast<size_t> ((k * 0.5 + 0.45) * sr);
            between += rmsDb (out, a, b) / 13.0; betweenIn += rmsDb (squashed, a, b) / 13.0;
        }
        UNSCOPED_INFO ("Amount " << amount << " %: contrast " << cu << " dB, level between hits " << between - betweenIn << " dB");
        CHECK (cu > last + 1.0);                       // more Amount, more punch
        CHECK (cu < c0 + 2.0);                         // never wildly past the original
        CHECK (std::abs (between - betweenIn) < 0.5);
        last = cu;
    }
    UnlimiterParams p; p.amount = 100.0;
    CHECK (hitContrastDb (runUnlimiter (squashed, p)) - cl > 0.5 * (c0 - cl));   // at 100 % at least half of what was lost

    // A lightly limited beat (pushed 4 dB) is not overdone at 100 %.
    const auto light = brickwalled (sr, 8.0, 4.0);
    const double lightOut = hitContrastDb (runUnlimiter (light, p));
    UNSCOPED_INFO ("pushed 4 dB: contrast " << hitContrastDb (light) << " dB, at 100 % " << lightOut << " dB");
    CHECK (lightOut < c0 + 2.0);
}

TEST_CASE ("Unlimiter never lifts a held note (808, pad)", "[unlimiter]")
{
    const double sr = 48000.0;
    const auto in = testsig::sine (sr, 2, 55.0, -6.0, 2.0);
    UnlimiterParams p; p.amount = 100.0;
    Unlimiter u;
    const auto out = runUnlimiter (in, p, &u);
    const double change = rmsDb (out, static_cast<size_t> (0.3 * sr), static_cast<size_t> (1.9 * sr))
                        - rmsDb (in, static_cast<size_t> (0.3 * sr), static_cast<size_t> (1.9 * sr));
    UNSCOPED_INFO ("held 55 Hz note level change " << change << " dB");
    CHECK (std::abs (change) < 0.1);
    // Distortion check: the output is the input times a gain; on the held part that gain must be flat.
    double worst = 0.0;
    for (size_t i = static_cast<size_t> (0.3 * sr); i < static_cast<size_t> (1.9 * sr); ++i)
        if (std::abs (in.channels[0][i]) > 0.1) worst = std::max (worst, std::abs (20.0 * std::log10 (std::abs (out.channels[0][i] / in.channels[0][i]))));
    CHECK (worst < 0.05);
}

TEST_CASE ("Unlimiter Focus listens to kicks or to snaps", "[unlimiter]")
{
    const double sr = 48000.0;
    const auto squashed = brickwalled (sr, 6.0, 14.0);
    const double cl = hitContrastDb (squashed);
    UnlimiterParams p; p.amount = 100.0;
    p.focus = UnlimiterFocus::kick;
    const double kick = hitContrastDb (runUnlimiter (squashed, p));
    p.focus = UnlimiterFocus::snap;
    const double snap = hitContrastDb (runUnlimiter (squashed, p));
    UNSCOPED_INFO ("contrast: squashed " << cl << ", Focus Kick " << kick << ", Focus Snap " << snap);
    CHECK (kick > cl + 1.0);    // the test's hits are 55 Hz kicks: Kick finds them...
    CHECK (snap < kick);        // ...Snap (above 1 kHz) hears little of them
}

TEST_CASE ("Squash reading tells a brickwalled beat from an open mix", "[unlimiter]")
{
    const double sr = 48000.0;
    const auto open = testsig::musicLike (sr, 8.0, -6.0, 4);
    const auto wall = brickwalled (sr, 8.0, 16.0);
    const double so = Unlimiter::measureSquashDb (open.channels, sr), sw = Unlimiter::measureSquashDb (wall.channels, sr);
    UNSCOPED_INFO ("squash: open mix " << so << " dB, brickwalled " << sw << " dB");
    CHECK (so > sw + 3.0);
    CHECK (sw < 9.0);

    // The live meter agrees with the offline reading.
    UnlimiterParams p;
    Unlimiter u;
    runUnlimiter (wall, p, &u);
    UNSCOPED_INFO ("live squash " << u.squashDb());
    CHECK (std::abs (u.squashDb() - sw) < 1.5);
}

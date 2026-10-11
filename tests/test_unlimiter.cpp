#include <catch2/catch_test_macros.hpp>

#include "Signals.h"
#include "polisher/FilterDesign.h"
#include "polisher/LoudnessTargeting.h"
#include "polisher/Unlimiter.h"

#include <cmath>
#include <numbers>
#include <random>

using namespace polisher;

namespace {
// Runs the Unlimiter and removes its latency, so the result lines up with the input. With keep, the
// caller's instance is used (prepared again unless fresh is false, to run on from where it was); with
// lift, the lift (dB) applied to each output sample is returned too.
AudioClip runUnlimiter (const AudioClip& in, const UnlimiterParams& p, Unlimiter* keep = nullptr, std::vector<double>* lift = nullptr, bool fresh = true)
{
    Unlimiter local;
    Unlimiter& u = keep ? *keep : local;
    if (fresh || ! keep) u.prepare (in.sampleRate, in.numChannels());
    u.setParams (p);
    if (fresh || ! keep) u.reset();
    const int lat = u.latencySamples(), n = in.numSamples(), nch = in.numChannels();
    AudioClip out = in;
    if (lift) lift->assign (static_cast<size_t> (n), 0.0);
    std::vector<double> frame (static_cast<size_t> (nch));
    for (int i = 0; i < n + lat; ++i)
    {
        for (int c = 0; c < nch; ++c) frame[static_cast<size_t> (c)] = i < n ? in.channels[static_cast<size_t> (c)][static_cast<size_t> (i)] : 0.0;
        u.processFrame (frame.data(), nch);
        const double g = u.takeMaxBoostDb();
        if (i >= lat)
        {
            for (int c = 0; c < nch; ++c) out.channels[static_cast<size_t> (c)][static_cast<size_t> (i - lat)] = static_cast<float> (frame[static_cast<size_t> (c)]);
            if (lift) (*lift)[static_cast<size_t> (i - lat)] = g;
        }
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

AudioClip limited (const AudioClip& mix, double pushDb)
{
    LimiterParams lp; lp.driveDb = pushDb; lp.ceilingDbtp = -0.3; lp.releaseMs = 30.0; lp.adaptiveRelease = false;
    return renderLimited (mix, lp);
}

AudioClip brickwalled (double sr, double seconds, double pushDb) { return limited (testsig::musicLike (sr, seconds, -6.0, 4), pushDb); }

// The largest lift in each of the windows [k * period + from, k * period + to) seconds, k = first ... last.
std::vector<double> liftPerHit (const std::vector<double>& lift, double sr, double period, int first, int last, double from = -0.01, double to = 0.1)
{
    std::vector<double> v;
    for (int k = first; k <= last; ++k)
    {
        double m = 0.0;
        for (auto i = static_cast<size_t> ((k * period + from) * sr); i < static_cast<size_t> ((k * period + to) * sr) && i < lift.size(); ++i) m = std::max (m, lift[i]);
        v.push_back (m);
    }
    return v;
}

double meanOf (const std::vector<double>& v) { double s = 0.0; for (double x : v) s += x; return s / static_cast<double> (v.size()); }
double spreadOf (const std::vector<double>& v)
{
    const double m = meanOf (v);
    double s = 0.0;
    for (double x : v) s += (x - m) * (x - m);
    return std::sqrt (s / static_cast<double> (v.size()));
}

// Kicks (identical, every 0.5 s) over noise whose level steps between -30 and -18 dBFS from hit to
// hit, pushed into a limiter: the same hit over a quiet and over a busy background.
AudioClip kicksOverNoise (double sr, double seconds)
{
    std::mt19937 rng (1);
    std::normal_distribution<double> white (0.0, 1.0);
    AudioClip c = testsig::silence (sr, 2, seconds);
    double phase = 0.0;
    for (size_t i = 0; i < c.channels[0].size(); ++i)
    {
        const double t = static_cast<double> (i) / sr, tk = std::fmod (t, 0.5);
        const int k = static_cast<int> (t / 0.5);
        const double noiseDb = -30.0 + 12.0 * ((k * 7) % 5) / 4.0;
        phase += 2.0 * std::numbers::pi * (55.0 + 90.0 * std::exp (-tk * 40.0)) / sr;
        c.channels[0][i] = c.channels[1][i] = static_cast<float> (0.5 * std::exp (-tk * 12.0) * std::sin (phase) + std::pow (10.0, noiseDb / 20.0) * white (rng));
    }
    return limited (c, 10.0);
}

// A trap-style loop: a 45 Hz 808 (held, or 400 ms notes with 100 ms gaps), optionally rolling hats
// (16ths, 32nds in the last beat of each bar) and a kick every second.
AudioClip trapLoop (double sr, double seconds, bool held, bool hats, bool kicks)
{
    std::mt19937 rng (3);
    std::normal_distribution<double> white (0.0, 1.0);
    AudioClip c = testsig::silence (sr, 2, seconds);
    double ph = 0.0, phk = 0.0, lp = 0.0;
    for (size_t i = 0; i < c.channels[0].size(); ++i)
    {
        const double t = static_cast<double> (i) / sr, tn = std::fmod (t, 0.5);
        ph += 2.0 * std::numbers::pi * 45.0 / sr;
        const double env = held ? 1.0 : tn < 0.4 ? std::min (1.0, tn / 0.003) : std::max (0.0, 1.0 - (tn - 0.4) / 0.003);
        double v = 0.6 * std::tanh (1.5 * std::sin (ph)) * env;
        if (hats)
        {
            const double th = std::fmod (t, std::fmod (t, 2.0) > 1.5 ? 0.0625 : 0.125), w = white (rng);
            const double hp = w - lp;
            lp = 0.6 * lp + 0.4 * w;
            v += 0.25 * hp * std::exp (-th * 120.0);
        }
        if (kicks)
        {
            const double tk = std::fmod (t, 1.0);
            phk += 2.0 * std::numbers::pi * (50.0 + 120.0 * std::exp (-tk * 40.0)) / sr;
            v += 0.6 * std::exp (-tk * 18.0) * std::sin (phk);
        }
        c.channels[0][i] = c.channels[1][i] = static_cast<float> (v);
    }
    return c;
}

// One channel through a Linkwitz-Riley low or high pass.
std::vector<double> bandOf (const AudioClip& c, bool highPass, double hz)
{
    Biquad a, b;
    design::apply (a, design::butterworth (highPass, hz, c.sampleRate));
    design::apply (b, design::butterworth (highPass, hz, c.sampleRate));
    std::vector<double> y (c.channels[0].size());
    for (size_t i = 0; i < y.size(); ++i) y[i] = b.process (a.process (c.channels[0][i]));
    return y;
}

// Largest level difference (dB) between two signals over windows of win samples, from sample from on
// (windows more than 40 dB below the loudest one don't count: there's nothing there to hear move).
double worstLevelChangeDb (const std::vector<double>& in, const std::vector<double>& out, size_t from, size_t win)
{
    std::vector<std::pair<double, double>> w;
    double loudest = 0.0;
    for (size_t a = from; a + win <= in.size(); a += win)
    {
        double ei = 0.0, eo = 0.0;
        for (size_t i = a; i < a + win; ++i) { ei += in[i] * in[i]; eo += out[i] * out[i]; }
        w.emplace_back (ei, eo);
        loudest = std::max (loudest, ei);
    }
    double worst = 0.0;
    for (auto [ei, eo] : w)
        if (ei > loudest * 1.0e-4) worst = std::max (worst, std::abs (10.0 * std::log10 (eo / ei)));
    return worst;
}
}

TEST_CASE ("Unlimiter off or at Amount 0 is an exact delayed pass-through", "[unlimiter]")
{
    const auto in = testsig::musicLike (48000.0, 3.0, -10.0);
    for (int mode = 0; mode < 4; ++mode)
    {
        UnlimiterParams p; p.amount = mode % 2 == 0 ? 0.0 : 80.0; p.enabled = mode % 2 == 0;
        p.focus = mode < 2 ? UnlimiterFocus::full : UnlimiterFocus::kick;   // the band split is not in the path either
        Unlimiter u;
        const auto out = runUnlimiter (in, p, &u);
        CHECK (u.latencySamples() == 240);   // 5 ms at 48 kHz
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
        CHECK (cu > last + 0.7);                       // more Amount, more punch
        CHECK (cu < c0 + 2.0);                         // never wildly past the original
        CHECK (std::abs (between - betweenIn) < 0.5);
        last = cu;
    }
    UnlimiterParams p; p.amount = 100.0;
    // At 100 % about half of what was lost comes back (measured 0.44-0.51 depending on the noise in the test
    // signal, which differs between standard libraries): at least 40 %.
    CHECK (hitContrastDb (runUnlimiter (squashed, p)) - cl > 0.4 * (c0 - cl));

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

TEST_CASE ("Unlimiter leaves 808 notes alone once they've started, also at full strength", "[unlimiter]")
{
    // The old held-note test above runs while the Squash meter is still warming up (strength 0). Here
    // the strength is full from the first sample (as after Auto-Master), on 808 notes with gaps.
    const double sr = 48000.0;
    const auto in = trapLoop (sr, 6.0, false, false, false);
    UnlimiterParams p; p.amount = 100.0; p.squashDb = Unlimiter::kFullSquashDb;
    std::vector<double> lift;
    const auto out = runUnlimiter (in, p, nullptr, &lift);
    double note = 0.0, sustain = 0.0;
    for (int k = 1; k < 11; ++k)
    {
        const auto a = static_cast<size_t> (k * 0.5 * sr);
        note = std::max (note, rmsDb (out, a, a + static_cast<size_t> (0.4 * sr)) - rmsDb (in, a, a + static_cast<size_t> (0.4 * sr)));
        const auto s = a + static_cast<size_t> (0.1 * sr);
        sustain = std::max (sustain, std::abs (rmsDb (out, s, s + static_cast<size_t> (0.28 * sr)) - rmsDb (in, s, s + static_cast<size_t> (0.28 * sr))));
    }
    UNSCOPED_INFO ("808 notes: note level up to +" << note << " dB, held part changed by " << sustain << " dB");
    CHECK (sustain < 0.05);   // the held part of each note is untouched
    CHECK (note < 2.0);       // only its first Length is lifted
}

TEST_CASE ("Unlimiter lifts identical hits alike, whatever plays around them", "[unlimiter]")
{
    const double sr = 48000.0;
    const auto in = kicksOverNoise (sr, 20.0);
    double atHalf = 0.0;
    for (double amount : { 50.0, 100.0 })
    {
        UnlimiterParams p; p.amount = amount; p.squashDb = Unlimiter::kFullSquashDb;
        std::vector<double> lift;
        runUnlimiter (in, p, nullptr, &lift);
        const auto hits = liftPerHit (lift, sr, 0.5, 2, 38);
        UNSCOPED_INFO ("Amount " << amount << " %: lift per hit " << meanOf (hits) << " dB on average, spread " << spreadOf (hits) << " dB");
        CHECK (spreadOf (hits) < 0.5);                                          // the old law: 1.6 ... 3.1 dB
        CHECK (*std::max_element (hits.begin(), hits.end()) <= Unlimiter::kMaxBoostDb * amount / 100.0 + 0.01);   // Amount sets the ceiling
        if (amount == 50.0) atHalf = meanOf (hits);
        else CHECK (std::abs (atHalf - 0.5 * meanOf (hits)) < 0.5);              // ...so half the Amount, half the lift
    }
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

TEST_CASE ("Unlimiter Focus Kick and Snap lift only their own band", "[unlimiter]")
{
    const double sr = 48000.0;
    // Kick + 808 + hats, limited, at full strength from the start.
    const auto in = limited (trapLoop (sr, 6.0, true, true, true), 8.0);
    UnlimiterParams p; p.amount = 100.0; p.squashDb = Unlimiter::kFullSquashDb;
    const auto from = static_cast<size_t> (0.5 * sr);

    // Kick: the kicks come up, the hats above 2 kHz never move.
    p.focus = UnlimiterFocus::kick;
    std::vector<double> lift;
    auto out = runUnlimiter (in, p, nullptr, &lift);
    const double kickLift = *std::max_element (lift.begin(), lift.end());
    const double hatsMove = worstLevelChangeDb (bandOf (in, true, 2000.0), bandOf (out, true, 2000.0), from, 960);
    UNSCOPED_INFO ("Focus Kick: lift up to " << kickLift << " dB, hats above 2 kHz move up to " << hatsMove << " dB");
    CHECK (kickLift > 3.0);
    CHECK (hatsMove < 0.3);

    // Snap: the hats over a held 808 come up, the 808 below 100 Hz never moves (windows of 3 cycles
    // of 45 Hz, so the band filter's phase shift doesn't count as a change).
    const auto held = limited (trapLoop (sr, 6.0, true, true, false), 8.0);
    p.focus = UnlimiterFocus::snap;
    out = runUnlimiter (held, p, nullptr, &lift);
    const double snapLift = *std::max_element (lift.begin(), lift.end());
    const double bassMove = worstLevelChangeDb (bandOf (held, false, 100.0), bandOf (out, false, 100.0), 3200 * 8, 3200);
    UNSCOPED_INFO ("held 808 under hats, Focus Snap: lift up to " << snapLift << " dB, 808 moves up to " << bassMove << " dB");
    CHECK (snapLift > 3.0);
    CHECK (bassMove < 0.2);

    // Kick on the same held 808 under rolling hats: nothing to lift, so no tremolo (it used to lift
    // more than 3 dB 40 % of the time).
    p.focus = UnlimiterFocus::kick;
    runUnlimiter (held, p, nullptr, &lift);
    const auto over3 = std::count_if (lift.begin() + static_cast<long> (sr), lift.end(), [] (double v) { return v > 3.0; });
    UNSCOPED_INFO ("held 808 under hats, Focus Kick: lift over 3 dB " << 100.0 * static_cast<double> (over3) / (static_cast<double> (lift.size()) - sr) << " % of the time");
    CHECK (static_cast<double> (over3) < 0.01 * static_cast<double> (lift.size()));
}

TEST_CASE ("Unlimiter Focus switches never cut a band out (no cross-fade cancellation)", "[unlimiter]")
{
    // The band split adds up to an all-pass: cross-fading it with the plain signal used to cancel the
    // split frequency half way (150 Hz -17 dB with Kick, 1 kHz -15 dB with Snap, ~11 ms in). Steady
    // tones through every kind of switch: Amount leaving 0, Focus changes, on / off (A / B).
    const double sr = 48000.0;
    for (double hz : { 40.0, 150.0, 400.0, 1000.0, 3000.0 })
    {
        const auto tone = testsig::sine (sr, 2, hz, -12.0, 5.0);
        Unlimiter u;
        u.prepare (sr, 2);
        UnlimiterParams p;
        p.amount = 0.0;
        p.focus = UnlimiterFocus::kick;
        p.squashDb = Unlimiter::kFullSquashDb;
        u.setParams (p);
        u.reset();
        struct Change { double at; int what; };
        const Change changes[] = { { 0.5, 0 }, { 1.2, 1 }, { 1.9, 2 }, { 2.4, 3 }, { 2.9, 4 }, { 3.6, 5 }, { 4.3, 6 } };
        std::vector<double> out (static_cast<size_t> (tone.numSamples()));
        std::vector<double> frame (2);
        size_t next = 0;
        for (int i = 0; i < tone.numSamples(); ++i)
        {
            if (next < std::size (changes) && i == static_cast<int> (changes[next].at * sr))
            {
                switch (changes[next].what)
                {
                    case 0: p.amount = 100.0; break;                    // Amount leaves 0 (Focus Kick)
                    case 1: p.focus = UnlimiterFocus::snap; break;      // Kick -> Snap
                    case 2: p.enabled = false; break;                   // A (or the module off)
                    case 3: p.enabled = true; break;                    // back to B
                    case 4: p.focus = UnlimiterFocus::full; break;      // Snap -> Full
                    case 5: p.focus = UnlimiterFocus::kick; break;      // Full -> Kick
                    default: p.amount = 0.0; break;                     // Amount back to 0
                }
                u.setParams (p);
                ++next;
            }
            frame[0] = frame[1] = tone.channels[0][static_cast<size_t> (i)];
            u.processFrame (frame.data(), 2);
            out[static_cast<size_t> (i)] = frame[0];
        }
        // Level over two periods (at least 5 ms), against the steady tone's.
        const int w = std::max (static_cast<int> (2.0 * sr / hz), static_cast<int> (0.005 * sr));
        const double steady = 0.5 * std::pow (10.0, -12.0 / 10.0);
        double worst = 0.0, worstAt = 0.0;
        for (int a = static_cast<int> (0.3 * sr); a + w < tone.numSamples(); a += w / 4)
        {
            double e = 0.0;
            for (int i = a; i < a + w; ++i) e += out[static_cast<size_t> (i)] * out[static_cast<size_t> (i)];
            const double db = 10.0 * std::log10 (e / w / steady);
            if (db < worst) { worst = db; worstAt = a / sr; }
        }
        INFO (hz << " Hz tone: worst dip " << worst << " dB at " << worstAt << " s");
        CHECK (worst > -1.0);
    }
}

TEST_CASE ("Unlimiter lift arrives with the hit and lasts about Length", "[unlimiter]")
{
    const double sr = 48000.0;
    const auto squashed = brickwalled (sr, 8.0, 14.0);
    double lastDuration = 0.0;
    for (double length : { 5.0, 15.0, 50.0 })
    {
        UnlimiterParams p; p.amount = 100.0; p.lengthMs = length; p.squashDb = Unlimiter::kFullSquashDb;
        std::vector<double> lift;
        runUnlimiter (squashed, p, nullptr, &lift);
        double onset = 0.0, duration = 0.0;
        for (int k = 2; k < 14; ++k)
        {
            const auto a = static_cast<long> (k * 0.5 * sr);
            double peak = 0.0;
            for (long i = a - 480; i < a + static_cast<long> (0.2 * sr); ++i) peak = std::max (peak, lift[static_cast<size_t> (i)]);
            long on = -1, off = 0;
            for (long i = a - 480; i < a + static_cast<long> (0.2 * sr); ++i)
                if (lift[static_cast<size_t> (i)] > 0.5 * peak) { if (on < 0) on = i; off = i; }
            onset += static_cast<double> (on - a) / sr * 1000.0 / 12.0;
            duration += static_cast<double> (off - on) / sr * 1000.0 / 12.0;
        }
        UNSCOPED_INFO ("Length " << length << " ms: lift at half by " << onset << " ms after the hit, above half for " << duration << " ms");
        CHECK (onset < 1.0);                         // up with the hit, not after it (it was +7.7 ms)
        CHECK (onset > -3.0);                        // and not long before it
        CHECK (duration > length);                   // Length is how long it lasts...
        CHECK (duration < length + 15.0);
        CHECK (duration > lastDuration);             // ...so a longer Length lasts longer
        lastDuration = duration;
    }
}

TEST_CASE ("Unlimiter does the same at 44.1 and 96 kHz", "[unlimiter]")
{
    // A beat built the same at every rate: kicks, a held chord and noise shaped to the same spectrum,
    // pushed 6 dB into a limiter.
    auto beat = [] (double sr)
    {
        std::mt19937 rng (5);
        std::normal_distribution<double> white (0.0, std::sqrt (sr / 48000.0));
        AudioClip c = testsig::silence (sr, 2, 8.0);
        Biquad a, b;
        design::apply (a, design::butterworth (false, 4000.0, sr));
        design::apply (b, design::butterworth (false, 4000.0, sr));
        double phase = 0.0;
        for (size_t i = 0; i < c.channels[0].size(); ++i)
        {
            const double t = static_cast<double> (i) / sr, tk = std::fmod (t, 0.5);
            phase += 2.0 * std::numbers::pi * (55.0 + 90.0 * std::exp (-tk * 40.0)) / sr;
            const double chord = 0.08 * (std::sin (2.0 * std::numbers::pi * 220.0 * t) + std::sin (2.0 * std::numbers::pi * 277.0 * t) + std::sin (2.0 * std::numbers::pi * 330.0 * t));
            const double noise = 0.06 * b.process (a.process (white (rng)));
            c.channels[0][i] = c.channels[1][i] = static_cast<float> (0.8 * std::exp (-tk * 9.0) * std::sin (phase) + chord + noise);
        }
        return limited (c, 6.0);
    };
    double gain[2] {}, squash[2] {};
    for (int k = 0; k < 2; ++k)
    {
        const double sr = k == 0 ? 44100.0 : 96000.0;
        const auto in = beat (sr);
        UnlimiterParams p; p.amount = 100.0;
        Unlimiter u;
        gain[k] = hitContrastDb (runUnlimiter (in, p, &u)) - hitContrastDb (in);
        squash[k] = Unlimiter::measureSquashDb (in.channels, sr);
        UNSCOPED_INFO (sr << " Hz: Squash " << squash[k] << " dB (live " << u.squashDb() << "), contrast gained " << gain[k] << " dB");
    }
    CHECK (gain[0] > 1.0);
    CHECK (std::abs (gain[0] - gain[1]) < 1.0);       // it was 5.95 vs 4.39 dB at 44.1 vs 192 kHz
    CHECK (std::abs (squash[0] - squash[1]) < 0.5);
}

TEST_CASE ("Unlimiter lifts the first bars after a restart like the rest", "[unlimiter]")
{
    // Reference: the lift on each hit from an instance that has already been playing (it runs on
    // through the clip a second time, no prepare or reset in between). The first two seconds after a
    // fresh start must match it; they used to be left unlifted for about a second.
    const double sr = 48000.0;
    const auto squashed = brickwalled (sr, 6.0, 14.0);
    UnlimiterParams p; p.amount = 100.0;
    std::vector<double> lift;
    Unlimiter warm;
    runUnlimiter (squashed, p, &warm);
    runUnlimiter (squashed, p, &warm, &lift, false);
    const auto reference = liftPerHit (lift, sr, 0.5, 1, 4);
    auto worstGap = [&] (const std::vector<double>& l)
    {
        const auto first = liftPerHit (l, sr, 0.5, 1, 4);
        double worst = 0.0;
        for (size_t k = 0; k < first.size(); ++k) worst = std::max (worst, std::abs (first[k] - reference[k]));
        return worst;
    };
    UNSCOPED_INFO ("warmed up: first hits lifted " << meanOf (reference) << " dB on average");
    REQUIRE (meanOf (reference) > 3.0);

    // With Auto-Master's Squash reading: the same strength from the first sample, in playback or export.
    p.squashDb = Unlimiter::measureSquashDb (squashed.channels, sr);
    runUnlimiter (squashed, p, nullptr, &lift);
    UNSCOPED_INFO ("with Auto-Master's Squash: first hits differ by up to " << worstGap (lift) << " dB");
    CHECK (worstGap (lift) < 1.0);

    // Without it: once the meter has heard the song, a new prepare() (an export, a restart) keeps the
    // reading, so the first hits aren't left flat while it listens again.
    p.squashDb = 0.0;
    Unlimiter u;
    runUnlimiter (squashed, p, &u);
    runUnlimiter (squashed, p, &u, &lift);
    UNSCOPED_INFO ("after a restart: first hits differ by up to " << worstGap (lift) << " dB");
    CHECK (worstGap (lift) < 1.0);
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

TEST_CASE ("Squash reading: a loud 808 alone doesn't make an open mix read squashed", "[unlimiter]")
{
    // An open, unlimited trap loop whose held 808 sits close to its peaks: peak minus average alone
    // says brickwalled (about 9 dB), but its peaks don't press against one ceiling. Limited, they do.
    const double sr = 48000.0;
    const auto open = trapLoop (sr, 8.0, true, true, true);
    const auto wall = limited (open, 9.0);
    const double so = Unlimiter::measureSquashDb (open.channels, sr), sw = Unlimiter::measureSquashDb (wall.channels, sr);
    UNSCOPED_INFO ("808 loop: open " << so << " dB, limited " << sw << " dB");
    CHECK (Unlimiter::squashScale (so) == 0.0);
    CHECK (Unlimiter::squashScale (sw) > 0.75);
}

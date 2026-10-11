#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Signals.h"
#include "polisher/AutoMaster.h"
#include "polisher/FilterDesign.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <random>

using namespace polisher;
using Catch::Matchers::WithinAbs;

namespace {

AudioClip lowPassed (AudioClip c, double hz)
{
    for (auto& ch : c.channels)
    {
        std::array<Biquad, 4> f {};
        for (auto& q : f) design::apply (q, design::butterworth (false, hz, c.sampleRate));
        for (auto& v : ch)
        {
            double x = v;
            for (auto& q : f) x = q.process (x);
            v = static_cast<float> (x);
        }
    }
    return c;
}

/** Adds a stereo (side-only) 50 Hz tone: bass that isn't mono. */
AudioClip withStereoSub (AudioClip c, double db)
{
    const double a = std::pow (10.0, db / 20.0);
    for (int i = 0; i < c.numSamples(); ++i)
    {
        const auto s = static_cast<float> (a * std::sin (2.0 * std::numbers::pi * 50.0 * i / c.sampleRate));
        c.channels[0][static_cast<size_t> (i)] += s;
        c.channels[1][static_cast<size_t> (i)] -= s;
    }
    return c;
}

void checkWellFormed (const AutoMasterResult& r)
{
    REQUIRE (r.ok);
    CHECK_FALSE (r.summary.empty());
    for (const char* module : { "eq", "ts", "rt", "bass", "dyn", "img", "sat", "clip", "max" })
    {
        INFO ("module " << module);
        CHECK (std::any_of (r.reasons.begin(), r.reasons.end(), [&] (const auto& x) { return x.module == module; }));
    }
    for (const auto& x : r.reasons)
    {
        INFO (x.module << " / " << x.control);
        CHECK_FALSE (x.why.empty());
        CHECK_FALSE (x.value.empty());
    }
    // Every warning carries a plan: whether to act, and how.
    for (const auto& n : r.notes)
    {
        INFO (n);
        CHECK (n.find ("\nNEED: ") != std::string::npos);
        CHECK (n.find ("\nSTEP: ") != std::string::npos);
    }
    // What was promised is what the chain delivers.
    CHECK (r.output.truePeakDbtp <= r.chain.limiter.ceilingDbtp + 0.05);
}

} // namespace

TEST_CASE ("Auto-Master refuses to guess on silence", "[automaster]")
{
    const auto r = autoMaster (testsig::silence (48000.0, 2, 12.0), {});
    CHECK_FALSE (r.ok);
    CHECK_FALSE (r.summary.empty());
}

TEST_CASE ("Auto-Master lands the target and explains every module", "[automaster]")
{
    const auto mix = testsig::musicLike (48000.0, 12.0, -14.0);
    for (auto target : { LoudnessTarget::stream, LoudnessTarget::loud })
    {
        AutoMasterSettings s;
        s.target = target;
        s.intensity = Intensity::punchy;
        const auto t0 = std::chrono::steady_clock::now();
        const auto r = autoMaster (mix, s);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        INFO (r.summary << " (" << secs << " s)");
        checkWellFormed (r);
        if (r.reachedTarget)
            CHECK_THAT (r.output.integratedLufs, WithinAbs (targetLufs (target), 0.3));
        else
            CHECK (r.loudnessCostDb <= 5.5 + 0.3);

        // Rendering the chosen settings reproduces the reported result.
        const auto check = measure (renderMaster (mix, r.chain));
        CHECK_THAT (check.integratedLufs, WithinAbs (r.output.integratedLufs, 0.05));
    }
}

TEST_CASE ("Auto-Master sets EQ bands together, accounting for their overlap", "[automaster][eq]")
{
    auto rmsError = [] (const std::array<double, kEqBands>& got, const std::array<double, kEqBands>& want)
    {
        double e = 0.0;
        for (size_t b = 0; b < want.size(); ++b)
            e += (got[b] - want[b]) * (got[b] - want[b]);
        return std::sqrt (e / static_cast<double> (want.size()));
    };
    std::array<double, kEqBands> limits {};
    limits.fill (4.0);

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const std::array<std::array<double, kEqBands>, 3> wishes { {
            { 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0 },     // all bands +2: set alone, Upper Mid lands at ~3 dB
            { 0.0, 1.5, -1.5, 0.0, 0.0, 2.0, 2.5 },
            { -2.5, 1.0, 0.0, -1.0, 0.0, 0.0, 3.0 },
        } };
        for (const auto& want : wishes)
        {
            TonalEQParams naive;
            naive.gainDb = want;   // the old way: each band set to its own wish
            const double before = rmsError (eqBandChangeDb (naive, sr), want);

            TonalEQParams solved;
            solved.gainDb = solveEqGains (want, limits, sr);
            const auto got = eqBandChangeDb (solved, sr);
            const double after = rmsError (got, want);
            INFO ("sr " << sr << " before " << before << " after " << after);
            CHECK (after < 0.75 * before);
            CHECK (after < 0.4);
            for (size_t b = 0; b < want.size(); ++b)
            {
                // Bands move only the way they were asked to, and never past the limit.
                CHECK (solved.gainDb[b] * want[b] >= 0.0);
                CHECK (std::abs (solved.gainDb[b]) <= limits[b]);
            }
        }
    }

    // The audit case: all bands +2 at 44.1 kHz used to put 3.1 dB into Upper Mid.
    std::array<double, kEqBands> plus2 {};
    plus2.fill (2.0);
    TonalEQParams solved;
    solved.gainDb = solveEqGains (plus2, limits, 44100.0);
    CHECK_THAT (eqBandChangeDb (solved, 44100.0)[4], WithinAbs (2.0, 0.3));
}

TEST_CASE ("Auto-Master's EQ lands the band changes it plans on the mix itself", "[automaster]")
{
    // The band-overlap model assumed a spectrum falling 4.5 dB/oct: on an 808-heavy mix it predicted
    // +2.0 dB for Low End +3 where the band really moved +1.3 (on musicLike +0.9). With the mix's own
    // spectrum the prediction is what analyseMix() then measures.
    for (double sr : { 44100.0, 48000.0 })
        for (const auto& clip : { testsig::trapLike (sr, 12.0), testsig::musicLike (sr, 12.0, -6.0, 3) })
        {
            const auto before = analyseMix (clip);
            REQUIRE (before.spectrum.valid);
            std::array<double, kEqBands> limits {};
            limits.fill (6.0);
            for (const auto& want : { std::array<double, kEqBands> { 2.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 },
                                      std::array<double, kEqBands> { 0.0, 1.5, -1.5, 0.0, 0.0, 2.0, 2.5 },
                                      std::array<double, kEqBands> { -2.5, 1.0, 0.0, -1.0, 0.0, 0.0, 3.0 } })
            {
                TonalEQParams eq;
                eq.gainDb = solveEqGains (want, limits, sr, &before.spectrum);
                const auto planned = eqBandChangeDb (eq, sr, &before.spectrum);
                const auto after = analyseMix (renderEq (clip, eq));
                // analyseMix reports levels relative to the mean of bands 1-5: compare the same way.
                auto rel = [] (const std::array<double, kEqBands>& x, size_t b) { return x[b] - (x[1] + x[2] + x[3] + x[4] + x[5]) / 5.0; };
                double worst = 0.0;
                for (size_t b = 0; b < static_cast<size_t> (kEqBands); ++b)
                    worst = std::max (worst, std::abs ((after.bandDb[b] - before.bandDb[b]) - rel (planned, b)));
                INFO (sr << " Hz, wish " << want[0] << " / " << want[5] << ": worst gap between planned and measured band change " << worst << " dB");
                CHECK (worst < 0.25);
                if (want[0] > 0.0)
                {
                    // The Low End wish itself: the typical-spectrum model landed 45-65 % of it. (Not all of it
                    // now either where the shelf barely reaches the mix's sub: the solver prefers the smaller
                    // of near-equal fits, so a band that needs +6 dB for +2 gets a little less.)
                    TonalEQParams typical;
                    typical.gainDb = solveEqGains (want, limits, sr);
                    const double oldLow = analyseMix (renderEq (clip, typical)).bandDb[0] - before.bandDb[0];
                    const double newLow = after.bandDb[0] - before.bandDb[0];
                    INFO ("Low End +2 wished: lands " << newLow << " dB (typical-spectrum model: " << oldLow << " dB)");
                    CHECK (newLow > 1.4);
                    CHECK (std::abs (newLow - want[0]) < std::abs (oldLow - want[0]) - 0.2);
                }
            }
        }
}

TEST_CASE ("Auto-Master calls a flat band 'covered' only when its neighbours really move it", "[automaster]")
{
    // A band the solver leaves at 0 dB is "covered" when the neighbours' moves give at least half of its
    // wished-for change, the right way; otherwise the report says it was left flat. 1,600 random wishes
    // (the audit found 2-4 per 400 labelled covered that moved the wrong way or not at all).
    const auto mix = analyseMix (testsig::trapLike (48000.0, 8.0));
    std::mt19937 rng (5);
    std::uniform_real_distribution<double> u (-1.0, 1.0);
    int covered = 0, held = 0;
    for (int t = 0; t < 1600; ++t)
    {
        std::array<double, kEqBands> wanted {}, lim {};
        const double eqMax = t % 3 == 0 ? 1.5 : t % 3 == 1 ? 3.0 : 4.0;
        for (size_t b = 0; b < static_cast<size_t> (kEqBands); ++b)
        {
            double w = u (rng) * eqMax * 1.3;
            if (std::abs (u (rng)) < 0.35 || std::abs (w) < 0.3) w = 0.0;
            wanted[b] = std::clamp (w, -eqMax, eqMax);
            lim[b] = eqMax;
        }
        TonalEQParams eq;
        eq.gainDb = solveEqGains (wanted, lim, 48000.0, t % 2 == 0 ? &mix.spectrum : nullptr);
        const auto moved = eqBandChangeDb (eq, 48000.0, t % 2 == 0 ? &mix.spectrum : nullptr);
        for (size_t b = 0; b < static_cast<size_t> (kEqBands); ++b)
            if (eq.gainDb[b] == 0.0 && std::abs (wanted[b]) >= 0.4)
            {
                if (eqBandCovered (wanted[b], moved[b]))
                {
                    ++covered;
                    CHECK (moved[b] * wanted[b] > 0.0);
                }
                else
                {
                    ++held;
                }
            }
    }
    INFO ("covered " << covered << ", left flat " << held);
    CHECK (covered > 0);
    CHECK (held > 0);

    // ...and the report uses the same rule: every band it calls covered moved toward its target.
    for (auto genre : { Genre::trapHipHop, Genre::pop, Genre::acoustic })
        for (const auto& clip : { testsig::trapLike (48000.0, 12.0), lowPassed (testsig::musicLike (48000.0, 12.0, -14.0), 3000.0) })
        {
            AutoMasterSettings s;
            s.genre = genre;
            const auto r = autoMaster (clip, s);
            REQUIRE (r.ok);
            const auto moved = eqBandChangeDb (r.chain.eq, 48000.0, &r.analysis.spectrum);
            const auto target = genreTargetCurve (genre);
            const double tRef = (target[1] + target[2] + target[3] + target[4] + target[5]) / 5.0;
            for (const auto& x : r.reasons)
                if (x.module == "eq" && x.control.rfind ("Covered", 0) == 0)
                    for (int b = 0; b < kEqBands; ++b)
                    {
                        static const char* const names[kEqBands] = { "Low End", "Bass", "Low Mid", "Mid", "Upper Mid", "Presence", "Air" };
                        if (x.why.find (names[b]) != 0 && x.why.find (std::string (", ") + names[b]) == std::string::npos
                            && x.why.find (std::string (" and ") + names[b]) == std::string::npos)
                            continue;
                        const auto i = static_cast<size_t> (b);
                        const double diff = (target[i] - tRef) - r.analysis.bandDb[i];
                        INFO (genreName (genre) << ": " << x.why << " (" << names[b] << " moved " << moved[i] << " dB, mix vs target " << diff << ")");
                        CHECK (moved[i] * diff > 0.0);
                        CHECK (std::abs (moved[i]) >= 0.2);
                    }
        }
}

TEST_CASE ("Auto-Master brightens a dull mix and calms a harsh one", "[automaster]")
{
    const auto base = testsig::musicLike (48000.0, 12.0, -14.0);
    const auto dull = autoMaster (lowPassed (base, 2500.0), {});
    checkWellFormed (dull);
    CHECK (dull.chain.eq.gainDb[5] > 0.0);   // Presence up
    CHECK (dull.chain.eq.gainDb[6] > 0.0);   // Air up
    CHECK (dull.chain.eq.gainDb[5] <= 3.0);  // within the Balanced cap

    // Pink noise is much brighter than music in this view: highs come down.
    const auto bright = autoMaster (base, {});
    CHECK (bright.chain.eq.gainDb[6] < 0.0);
}

TEST_CASE ("Auto-Master makes stereo bass mono and explains why", "[automaster]")
{
    AutoMasterSettings s;
    s.genre = Genre::rock;   // not a bass-heavy default: only acts because the bass is stereo
    const auto mono = autoMaster (testsig::musicLike (48000.0, 12.0, -14.0), s);
    CHECK (mono.chain.bass.monoHz < kBassMonoOffHz);

    const auto wide = autoMaster (withStereoSub (testsig::musicLike (48000.0, 12.0, -14.0), -16.0), s);
    INFO ("width " << wide.analysis.bassWidthPercent);
    CHECK (wide.analysis.bassWidthPercent > 20.0);
    CHECK (wide.chain.bass.monoHz >= 80.0);
    const auto it = std::find_if (wide.reasons.begin(), wide.reasons.end(), [] (const auto& x) { return x.control == "Mono Below"; });
    REQUIRE (it != wide.reasons.end());
    CHECK (it->why.find ("stereo") != std::string::npos);
}

TEST_CASE ("Gentle stops at its limiting budget and says so", "[automaster]")
{
    AutoMasterSettings s;
    s.intensity = Intensity::gentle;
    s.target = LoudnessTarget::club;
    const auto r = autoMaster (testsig::musicLike (48000.0, 12.0, -20.0), s);
    checkWellFormed (r);
    CHECK_FALSE (r.chain.clipper.enabled);
    CHECK (r.loudnessCostDb <= 2.0 + 0.3);
    if (! r.reachedTarget)
    {
        const auto it = std::find_if (r.reasons.begin(), r.reasons.end(), [] (const auto& x) { return x.module == "max" && x.control == "Drive"; });
        REQUIRE (it != r.reasons.end());
        INFO (it->why);
        CHECK (it->why.find ("stopped") != std::string::npos);
    }
}

TEST_CASE ("Acoustic masters are gentler than trap on the same mix", "[automaster]")
{
    const auto mix = testsig::musicLike (48000.0, 12.0, -14.0);
    AutoMasterSettings acoustic, trap;
    acoustic.genre = Genre::acoustic;
    trap.genre = Genre::trapHipHop;
    const auto a = autoMaster (mix, acoustic);
    const auto t = autoMaster (mix, trap);
    checkWellFormed (a);
    checkWellFormed (t);

    CHECK (a.chain.bass.tame == 0.0);                                   // played bass isn't "tamed"
    if (a.chain.clipper.enabled)                                        // Balanced: the clipper never adds density...
        CHECK (a.chain.clipper.makeupDb == 0.0);                        // ...and the Maximizer keeps a share
    CHECK (a.output.integratedLufs <= t.output.integratedLufs + 0.05);  // acoustic stops earlier, never louder
    CHECK (std::any_of (a.notes.begin(), a.notes.end(), [] (const auto& n) { return n.find ("acoustic releases") != std::string::npos; }));
}

TEST_CASE ("Auto-Master won't master a single instrument", "[automaster]")
{
    // A lone synth line (what Auto-Master heard when Polisher sat on a HALion track): harmonics
    // between 500 Hz and 2 kHz, nothing below or above.
    AudioClip synth = testsig::silence (48000.0, 2, 12.0);
    for (int i = 0; i < synth.numSamples(); ++i)
    {
        const double t = i / synth.sampleRate;
        const double env = 0.6 + 0.4 * std::sin (2.0 * std::numbers::pi * 2.0 * t);
        double x = 0.0;
        for (double f : { 660.0, 990.0, 1320.0, 1650.0 })
            x += std::sin (2.0 * std::numbers::pi * f * t) / (f / 660.0);
        synth.channels[0][static_cast<size_t> (i)] = synth.channels[1][static_cast<size_t> (i)] = static_cast<float> (0.2 * env * x);
    }
    for (auto genre : { Genre::trapHipHop, Genre::acoustic })
    {
        AutoMasterSettings s;
        s.genre = genre;
        const auto r = autoMaster (synth, s);
        INFO (r.summary);
        CHECK_FALSE (r.ok);                 // nothing gets applied
        CHECK (r.notAFullMix);
        CHECK (r.analysis.usable);
        CHECK (r.summary.find ("full mix") != std::string::npos);
        CHECK (r.tip.find ("Stereo Out") != std::string::npos);
    }
}

TEST_CASE ("A honky or muddy mix is still a mix", "[automaster]")
{
    // Real mixes can be badly balanced: +8 dB of honk at 1 kHz or of mud at 300 Hz gets fixed, not refused.
    for (double hz : { 1000.0, 300.0 })
    {
        auto mix = testsig::musicLike (48000.0, 12.0, -14.0);
        for (auto& ch : mix.channels)
        {
            Biquad f;
            design::apply (f, design::bell (hz, 8.0, 0.7, mix.sampleRate));
            for (auto& v : ch) v = static_cast<float> (f.process (v));
        }
        const auto r = autoMaster (mix, {});
        INFO (hz << " Hz: " << r.summary);
        CHECK_FALSE (r.notAFullMix);
        checkWellFormed (r);
    }
}

TEST_CASE ("Auto-Master sets Dynamics, Stereo Imager and Saturation and explains them", "[automaster]")
{
    const auto mix = testsig::musicLike (48000.0, 12.0, -14.0);
    for (auto genre : { Genre::trapHipHop, Genre::acoustic })
    {
        AutoMasterSettings s;
        s.genre = genre;
        const auto r = autoMaster (mix, s);
        INFO (genreName (genre) << ": " << r.summary);
        for (const auto& x : r.reasons) if (x.module == "dyn" || x.module == "img" || x.module == "sat") INFO (x.module << " " << x.control << " " << x.value << ": " << x.why);
        checkWellFormed (r);
        for (const char* module : { "dyn", "img", "sat" })
            CHECK (std::any_of (r.reasons.begin(), r.reasons.end(), [&] (const auto& x) { return x.module == module; }));

        // Saturation: the mode fits the genre and the drive lands near the genre's harmonics target.
        CHECK (r.chain.saturation.mode == (genre == Genre::trapHipHop ? SaturationMode::tube : SaturationMode::tape));
        CHECK (r.chain.saturation.focus == (genre == Genre::trapHipHop ? SaturationFocus::bass : SaturationFocus::full));
        CHECK (r.chain.saturation.driveDb > 0.0);
        CHECK (r.chain.saturation.mix == 50.0);

        // Imager never widens the lows, and keeps widening modest.
        CHECK (r.chain.imager.lowWidth == 100.0);
        CHECK (r.chain.imager.highWidth <= 125.0);
        CHECK (r.chain.imager.midWidth <= 110.0);

        // Dynamics: gentle ratios, levels given back within a few dB.
        for (const auto& b : r.chain.dynamics.bands)
        {
            CHECK (b.ratio <= 2.5);
            CHECK (b.gainDb >= 0.0);
            CHECK (b.gainDb <= 3.0);
        }

        // What the plug-in will play (the whole chain) measures what Auto-Master promised.
        const auto check = measure (renderMaster (mix, r.chain));
        CHECK_THAT (check.integratedLufs, WithinAbs (r.output.integratedLufs, 0.05));
        CHECK (check.truePeakDbtp <= r.chain.limiter.ceilingDbtp + 0.05);
    }
}

TEST_CASE ("Auto-Master widens a narrow mix's highs but leaves a wide one alone", "[automaster]")
{
    auto narrow = testsig::musicLike (48000.0, 12.0, -14.0);
    for (int i = 0; i < narrow.numSamples(); ++i)   // narrow: mostly center, a little stereo
    {
        const float l = narrow.channels[0][static_cast<size_t> (i)], r = narrow.channels[1][static_cast<size_t> (i)];
        const float m = 0.5f * (l + r), sd = 0.5f * (l - r) * 0.2f;
        narrow.channels[0][static_cast<size_t> (i)] = m + sd;
        narrow.channels[1][static_cast<size_t> (i)] = m - sd;
    }
    const auto n = autoMaster (narrow, {});
    INFO ("narrow: high side " << n.analysis.highSideDb << " corr " << n.analysis.correlation);
    CHECK (n.chain.imager.highWidth > 105.0);

    const auto wide = autoMaster (testsig::musicLike (48000.0, 12.0, -14.0), {});   // independent L/R noise: very wide
    INFO ("wide: high side " << wide.analysis.highSideDb << " mid side " << wide.analysis.midSideDb << " corr " << wide.analysis.correlation);
    CHECK (wide.chain.imager.highWidth <= 100.0);

    // A mono mix has nothing to widen: left alone, and Learn says why.
    auto mono = testsig::musicLike (48000.0, 12.0, -14.0);
    mono.channels[1] = mono.channels[0];
    const auto m = autoMaster (mono, {});
    CHECK (m.chain.imager.highWidth == 100.0);
    CHECK (m.chain.imager.midWidth == 100.0);
}

TEST_CASE ("Auto-Master turns the Unlimiter on only for squashed audio", "[automaster]")
{
    const auto open = testsig::musicLike (48000.0, 12.0, -14.0, 4);
    const auto r = autoMaster (open, {});
    REQUIRE (r.ok);
    UNSCOPED_INFO ("open squash " << Unlimiter::measureSquashDb (open.channels, 48000.0));
    CHECK (r.chain.unlimiter.amount == 0.0);
    CHECK (std::any_of (r.reasons.begin(), r.reasons.end(), [] (const AutoMasterReason& x) { return x.module == "ul"; }));

    // An open trap loop whose held 808 sits close to its peaks (peak minus average alone reads about
    // 9 dB, "squashed"): nothing limited it, so the Unlimiter stays off and doesn't claim otherwise.
    {
        AudioClip trap = testsig::silence (48000.0, 2, 12.0);
        std::mt19937 rng (3);
        std::normal_distribution<double> white (0.0, 1.0);
        double ph = 0.0, phk = 0.0;
        for (size_t i = 0; i < trap.channels[0].size(); ++i)
        {
            const double t = static_cast<double> (i) / 48000.0, tk = std::fmod (t, 1.0), th = std::fmod (t, 0.125);
            ph += 2.0 * std::numbers::pi * 45.0 / 48000.0;
            phk += 2.0 * std::numbers::pi * (50.0 + 120.0 * std::exp (-tk * 40.0)) / 48000.0;
            const double v = 0.3 * std::tanh (1.5 * std::sin (ph)) + 0.3 * std::exp (-tk * 18.0) * std::sin (phk) + 0.1 * white (rng) * std::exp (-th * 120.0)
                           + 0.05 * std::sin (2.0 * std::numbers::pi * 330.0 * t);
            trap.channels[0][i] = trap.channels[1][i] = static_cast<float> (v);
        }
        double e = 0.0;
        for (float v : trap.channels[0]) e += static_cast<double> (v) * v;
        const double crest = measure (trap).truePeakDbtp - 10.0 * std::log10 (e / static_cast<double> (trap.channels[0].size()));
        UNSCOPED_INFO ("808 loop: peak minus average " << crest << " dB, Squash " << Unlimiter::measureSquashDb (trap.channels, 48000.0) << " dB");
        CHECK (crest < Unlimiter::kOpenSquashDb - 2.0);   // read on peaks and average alone, it would count as squashed
        const auto t = autoMaster (trap, {});
        REQUIRE (t.ok);
        CHECK (t.chain.unlimiter.amount == 0.0);
        for (const auto& x : t.reasons)
            if (x.module == "ul") { INFO (x.why); CHECK (x.why.find ("limited") == std::string::npos); }
        for (const auto& n : t.notes) CHECK (n.find ("SQUASHED INPUT") == std::string::npos);
    }

    // A brickwalled beat (about -7.7 LUFS): mastered for Stream -14 the Unlimiter wins back punch the
    // Maximizer keeps. For Loud -9 or Club -7 the Maximizer would press the restored hits straight back
    // down (it used to switch on anyway and leave the hits flatter than without it), so it stays off
    // and says why. Either way its Squash reading is stored, so playback and export lift alike.
    LimiterParams lp; lp.driveDb = 16.0; lp.ceilingDbtp = -0.3; lp.releaseMs = 30.0; lp.adaptiveRelease = false;
    const auto wall = renderLimited (testsig::musicLike (48000.0, 12.0, -6.0, 4), lp);
    for (auto target : { LoudnessTarget::stream, LoudnessTarget::loud, LoudnessTarget::club })
    {
        AutoMasterSettings s;
        s.target = target;
        const auto w = autoMaster (wall, s);
        REQUIRE (w.ok);
        CHECK (w.chain.unlimiter.amount == (target == LoudnessTarget::stream ? 75.0 : 0.0));
        CHECK (w.chain.unlimiter.squashDb > 0.0);
        CHECK (w.chain.unlimiter.squashDb < Unlimiter::kOpenSquashDb);
        CHECK (w.output.truePeakDbtp <= w.chain.limiter.ceilingDbtp + 0.05);
        for (const auto& x : w.reasons)
            if (x.module == "ul")
            {
                INFO (x.why);
                CHECK (x.why.find (target == LoudnessTarget::stream ? "MATCH" : "straight back down") != std::string::npos);
            }
    }
}

TEST_CASE ("Auto-Master's loudness matches the plug-in with Stem EQ moves on squashed input", "[automaster]")
{
    LimiterParams lp; lp.driveDb = 16.0; lp.ceilingDbtp = -0.3; lp.releaseMs = 30.0; lp.adaptiveRelease = false;
    const auto wall = renderLimited (testsig::musicLike (48000.0, 12.0, -6.0, 4), lp);
    for (double move : { -6.0, 6.0 })
    {
        AutoMasterSettings s;
        s.target = LoudnessTarget::stream;   // where the Unlimiter pays off on a beat this loud
        s.stem.gainDb[kStemDrums] = move;
        s.stem.gainDb[kStemBass] = -move;
        const auto r = autoMaster (wall, s);
        REQUIRE (r.ok);
        CHECK (r.chain.unlimiter.amount > 0.0);
        const auto check = measure (renderMaster (wall, r.chain));
        CHECK_THAT (check.integratedLufs, WithinAbs (r.output.integratedLufs, 0.05));
    }
}

TEST_CASE ("A mix already louder than the target is turned down with Input Trim and lands on it", "[automaster]")
{
    // A finished-sounding, hot mix (limited to about -6 LUFS) mastered for Stream -14: Drive can't go
    // below 0 dB, so without Input Trim the master would stay ~8 dB over the target.
    LimiterParams hot;
    hot.driveDb = 14.0;
    hot.ceilingDbtp = -0.3;
    const auto mix = renderLimited (testsig::musicLike (48000.0, 12.0, -6.0), hot);
    const double mixLufs = measure (mix).integratedLufs;
    REQUIRE (mixLufs > -9.0);

    AutoMasterSettings s;
    s.target = LoudnessTarget::stream;
    const auto r = autoMaster (mix, s);
    checkWellFormed (r);
    INFO ("mix " << mixLufs << " LUFS, trim " << r.chain.trimDb << " dB, drive " << r.chain.driveDb << " dB, master " << r.output.integratedLufs);
    CHECK (r.chain.trimDb < -4.0);
    CHECK (r.chain.trimDb >= kMinTrimDb);
    CHECK (r.reachedTarget);
    CHECK_THAT (r.output.integratedLufs, WithinAbs (-14.0, 0.3));
    // The plug-in's chain (trim included) gives what Auto-Master measured...
    CHECK_THAT (measure (renderMaster (mix, r.chain)).integratedLufs, WithinAbs (r.output.integratedLufs, 0.1));
    // ...the report describes the mix as it arrives, and explains the trim.
    AutoMasterSettings club = s;
    club.target = LoudnessTarget::club;                       // louder than this mix: no trim
    const auto untrimmed = autoMaster (mix, club);
    CHECK (untrimmed.chain.trimDb == 0.0);
    CHECK_THAT (r.analysis.loudness.integratedLufs, WithinAbs (untrimmed.analysis.loudness.integratedLufs, 0.01));
    CHECK (r.summary.find ("Input Trim") != std::string::npos);
    CHECK (std::any_of (r.reasons.begin(), r.reasons.end(), [] (const auto& x) { return x.module == "max" && x.control == "Input Trim"; }));

    // Punchy on the same brickwalled beat: the clipper's make-up moves with the trim, so the search
    // settles a little differently, but the report must not claim the trim ran out (review finding).
    AutoMasterSettings punchy = s;
    punchy.intensity = Intensity::punchy;
    const auto rp = autoMaster (mix, punchy);
    checkWellFormed (rp);
    CHECK (rp.reachedTarget);
    for (const auto& x : rp.reasons)
        if (x.module == "max")
        {
            INFO (x.control << ": " << x.why);
            CHECK (x.why.find ("limit") == std::string::npos);
        }

    // A mix below the target keeps Input Trim at 0 dB.
    const auto quiet = autoMaster (testsig::musicLike (48000.0, 12.0, -20.0), s);
    CHECK (quiet.chain.trimDb == 0.0);
}

TEST_CASE ("Auto-Master gives the 808 Bass only harmonics for trap and lands the harmonics target", "[automaster][saturation]")
{
    const auto beat = testsig::trapLike (48000.0, 12.0);
    for (auto genre : { Genre::trapHipHop, Genre::rnb, Genre::pop })
    {
        AutoMasterSettings s;
        s.genre = genre;
        const auto r = autoMaster (beat, s);
        INFO (genreName (genre) << ": " << r.summary);
        for (const auto& x : r.reasons) if (x.module == "sat") INFO ("sat " << x.control << " " << x.value << ": " << x.why);
        checkWellFormed (r);
        const auto& sat = r.chain.saturation;
        const bool bassLed = genre != Genre::pop;
        CHECK (sat.focus == (bassLed ? SaturationFocus::bass : SaturationFocus::full));
        const auto focus = std::find_if (r.reasons.begin(), r.reasons.end(), [] (const auto& x) { return x.module == "sat" && x.control == "Focus"; });
        REQUIRE (focus != r.reasons.end());
        CHECK (focus->value == (bassLed ? "Bass only" : "Full"));
        if (bassLed) CHECK (focus->why.find ("phone") != std::string::npos);

        // What the plug-in's Harmonics meter will read on the music reaching Saturation, against the target.
        const auto drive = std::find_if (r.reasons.begin(), r.reasons.end(), [] (const auto& x) { return x.module == "sat" && x.control == "Drive"; });
        REQUIRE (drive != r.reasons.end());
        const bool dense = drive->why.find ("already dense") != std::string::npos;
        const double target = (genre == Genre::trapHipHop ? -25.0 : -28.0) - (dense ? 4.0 : 0.0);
        MasterChainParams pre = r.chain;
        pre.saturation = SaturationParams {};
        const auto beforeSat = renderPreDrive (beat, pre);
        Saturation meter;
        meter.prepare (beforeSat.sampleRate, beforeSat.numChannels());
        meter.setParams (sat);
        meter.reset();
        auto c = beforeSat;
        float* ch[2] = { c.channels[0].data(), c.channels[1].data() };
        meter.process (ch, 2, c.numSamples());
        const double reads = meter.takeHarmonicsDb();
        INFO ("Drive " << sat.driveDb << " dB: the meter reads " << reads << " dB, target " << target);
        CHECK (sat.driveDb > 0.0);
        CHECK (sat.driveDb < Saturation::kMaxDriveDb);
        CHECK_THAT (reads, WithinAbs (target, 2.0));
    }

    // A trap song with no 808 in the part heard (everything below 150 Hz filtered out): Bass only would have
    // nothing to work on, so the whole mix gets the harmonics, and the report says why.
    AudioClip thin = beat;
    for (auto& ch : thin.channels)
    {
        std::array<Biquad, 4> f {};
        for (auto& q : f) design::apply (q, design::butterworth (true, 150.0, thin.sampleRate));
        for (auto& v : ch)
        {
            double x = v;
            for (auto& q : f) x = q.process (x);
            v = static_cast<float> (x);
        }
    }
    AutoMasterSettings s;
    const auto r = autoMaster (thin, s);
    INFO (r.summary);
    REQUIRE (r.ok);
    CHECK (r.chain.saturation.focus == SaturationFocus::full);
    const auto focus = std::find_if (r.reasons.begin(), r.reasons.end(), [] (const auto& x) { return x.module == "sat" && x.control == "Focus"; });
    REQUIRE (focus != r.reasons.end());
    INFO (focus->why);
    CHECK (focus->why.find ("very little low end") != std::string::npos);
}

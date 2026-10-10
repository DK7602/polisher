#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Signals.h"
#include "polisher/AutoMaster.h"
#include "polisher/FilterDesign.h"

#include <chrono>
#include <cmath>
#include <numbers>

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

    LimiterParams lp; lp.driveDb = 16.0; lp.ceilingDbtp = -0.3; lp.releaseMs = 30.0; lp.adaptiveRelease = false;
    const auto wall = renderLimited (testsig::musicLike (48000.0, 12.0, -6.0, 4), lp);
    const auto w = autoMaster (wall, {});
    REQUIRE (w.ok);
    CHECK (w.chain.unlimiter.amount == 75.0);
    CHECK (w.output.truePeakDbtp <= w.chain.limiter.ceilingDbtp + 0.05);
}

TEST_CASE ("Auto-Master's loudness matches the plug-in with Stem EQ moves on squashed input", "[automaster]")
{
    LimiterParams lp; lp.driveDb = 16.0; lp.ceilingDbtp = -0.3; lp.releaseMs = 30.0; lp.adaptiveRelease = false;
    const auto wall = renderLimited (testsig::musicLike (48000.0, 12.0, -6.0, 4), lp);
    for (double move : { -6.0, 6.0 })
    {
        AutoMasterSettings s;
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

    // A mix below the target keeps Input Trim at 0 dB.
    const auto quiet = autoMaster (testsig::musicLike (48000.0, 12.0, -20.0), s);
    CHECK (quiet.chain.trimDb == 0.0);
}

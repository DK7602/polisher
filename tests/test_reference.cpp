#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Signals.h"
#include "polisher/AutoMaster.h"
#include "polisher/FilterDesign.h"

#include <cmath>

using namespace polisher;
using Catch::Matchers::WithinAbs;

// Match ref: the reference song's analysis (its loudest part) and Auto-Master aiming for it.

namespace {

AudioClip withBell (AudioClip c, double hz, double db, double q = 0.5)
{
    for (auto& ch : c.channels)
    {
        Biquad f;
        design::apply (f, design::bell (hz, db, q, c.sampleRate));
        for (auto& v : ch) v = static_cast<float> (f.process (v));
    }
    return c;
}

AudioClip lowPassed (AudioClip c, double hz)
{
    for (auto& ch : c.channels)
    {
        std::array<Biquad, 2> f {};
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

/** Side scaled by `side` (1 = as it is, 0 = mono). */
AudioClip withSide (AudioClip c, float side)
{
    for (size_t i = 0; i < c.channels[0].size(); ++i)
    {
        const float l = c.channels[0][i], r = c.channels[1][i];
        const float m = 0.5f * (l + r), sd = 0.5f * (l - r) * side;
        c.channels[0][i] = m + sd;
        c.channels[1][i] = m - sd;
    }
    return c;
}

AudioClip concat (std::initializer_list<AudioClip> parts)
{
    AudioClip out;
    out.sampleRate = parts.begin()->sampleRate;
    out.channels.resize (2);
    for (const auto& p : parts)
        for (size_t c = 0; c < 2; ++c)
            out.channels[c].insert (out.channels[c].end(), p.channels[c].begin(), p.channels[c].end());
    return out;
}

/** A master-like reference: the clip limited to about `lufs`. */
AudioClip mastered (const AudioClip& clip, double lufs)
{
    LimiterParams lp;
    lp.ceilingDbtp = -1.0;
    const AudioClip in = withGain (clip, lufs - 8.0 - measure (clip).integratedLufs);   // Drive does the last 8 dB
    lp.driveDb = findDriveForTarget (in, lufs, lp).driveDb;
    return renderLimited (in, lp);
}

double toneDistance (const std::array<double, kEqBands>& a, const std::array<double, kEqBands>& b)
{
    double e = 0.0;
    for (size_t k = 0; k < a.size(); ++k) e += (a[k] - b[k]) * (a[k] - b[k]);
    return std::sqrt (e / static_cast<double> (a.size()));
}

ReferenceProfile named (ReferenceProfile p, const char* name) { p.name = name; return p; }

} // namespace

TEST_CASE ("Reference analysis reads the loudest part, the same way every time", "[reference]")
{
    // Quiet intro, a loud 30 s chorus, a quieter outro.
    const double sr = 48000.0;
    const auto song = concat ({ testsig::musicLike (sr, 20.0, -32.0, 1), testsig::musicLike (sr, 30.0, -12.0, 2),
                                testsig::musicLike (sr, 15.0, -24.0, 3) });
    const auto p = analyseReference (song);
    REQUIRE (p.valid);
    CHECK_THAT (p.startSeconds, WithinAbs (20.0, 0.25));
    CHECK_THAT (p.partSeconds, WithinAbs (kReferencePartSeconds, 0.01));
    CHECK_THAT (p.trackSeconds, WithinAbs (65.0, 0.01));
    const double chorus = measure (testsig::musicLike (sr, 30.0, -12.0, 2)).integratedLufs;
    CHECK_THAT (p.lufs, WithinAbs (chorus, 0.3));        // the chorus, not the whole song
    CHECK (p.trackLufs < p.lufs);                          // the whole song is quieter on average
    CHECK (p.displayDb.size() == static_cast<size_t> (kDisplayPoints));

    // Deterministic: the same file gives the same profile, bit for bit.
    const auto again = analyseReference (song);
    CHECK (again.lufs == p.lufs);
    CHECK (again.bandDb == p.bandDb);
    CHECK (again.displayDb == p.displayDb);
    CHECK (again.subVsBassDb == p.subVsBassDb);

    // Streaming the song through in blocks (as the plug-in reads a file) finds the same part.
    LoudestPartFinder finder;
    finder.prepare (sr, 2);
    for (int start = 0; start < song.numSamples(); start += 65536)
    {
        const int len = std::min (65536, song.numSamples() - start);
        const float* ptrs[] = { song.channels[0].data() + start, song.channels[1].data() + start };
        finder.process (ptrs, 2, len);
    }
    CHECK_THAT (static_cast<double> (finder.loudestStart (kReferencePartSeconds)) / sr, WithinAbs (p.startSeconds, 1.0e-9));
    CHECK_THAT (finder.integratedLufs(), WithinAbs (p.trackLufs, 1.0e-9));

    // A song shorter than the part is used whole.
    const auto shortSong = analyseReference (testsig::musicLike (sr, 12.0, -14.0));
    REQUIRE (shortSong.valid);
    CHECK (shortSong.startSeconds == 0.0);
    CHECK_THAT (shortSong.partSeconds, WithinAbs (12.0, 0.01));

    CHECK_FALSE (analyseReference (testsig::silence (sr, 2, 40.0)).valid);
}

TEST_CASE ("The reference's spectrum curve sits where the live display would draw it", "[reference]")
{
    // A full-scale-ish 1 kHz sine reads its level in dB at 1 kHz (no tilt there), as the display does.
    const auto tone = testsig::sine (48000.0, 2, 1000.0, -6.0, 5.0);
    const auto curve = spectrumDisplayCurve (tone);
    REQUIRE (curve.size() == static_cast<size_t> (kDisplayPoints));
    // The display point nearest 1 kHz: 20 * 1000^(i/95) = 1000 at i = 95 * log(50)/log(1000) = 53.8.
    double peak = -200.0;
    for (int i = 52; i <= 55; ++i) peak = std::max (peak, curve[static_cast<size_t> (i)]);
    CHECK (peak > -16.0);   // -6 dB peak, spread over the +/- 1/12 octave average of the point
    CHECK (peak < -4.0);
    CHECK (curve.front() < peak - 30.0);   // nothing at 20 Hz
}

TEST_CASE ("Auto-Master moves the tone toward a brighter or darker reference", "[reference][automaster]")
{
    const auto mix = testsig::musicLike (48000.0, 12.0, -14.0);
    const auto genreOnly = autoMaster (mix, {});
    REQUIRE (genreOnly.ok);
    const auto genreMaster = analyseMix (renderMaster (mix, genreOnly.chain)).bandDb;

    const auto bright = named (analyseReference (mastered (withBell (testsig::musicLike (48000.0, 40.0, -14.0, 11), 9000.0, 6.0), -9.0)), "Bright Song");
    const auto dark = named (analyseReference (mastered (lowPassed (testsig::musicLike (48000.0, 40.0, -14.0, 12), 3000.0), -9.0)), "Dark Song");
    REQUIRE (bright.valid);
    REQUIRE (dark.valid);

    for (const auto* ref : { &bright, &dark })
    {
        AutoMasterSettings s;
        s.reference = *ref;
        const auto r = autoMaster (mix, s);
        REQUIRE (r.ok);
        const auto master = analyseMix (renderMaster (mix, r.chain)).bandDb;
        const double withRef = toneDistance (master, ref->bandDb), without = toneDistance (genreMaster, ref->bandDb);
        INFO (ref->name << ": distance to the reference " << without << " dB (genre) -> " << withRef << " dB (matched)");
        CHECK (withRef < without - 0.3);
        // The master the report describes is the one the chain makes.
        CHECK_THAT (toneDistance (r.masterAnalysis.bandDb, master), WithinAbs (0.0, 0.05));

        // The report says which song it matched, and explains the EQ against it.
        CHECK (r.summary.find (ref->name) != std::string::npos);
        CHECK (std::any_of (r.reasons.begin(), r.reasons.end(), [&] (const auto& x) { return x.module == "eq" && x.why.find (ref->name) != std::string::npos; }));
        for (const auto& n : r.notes)
        {
            INFO (n);
            CHECK (n.find ("\nNEED: ") != std::string::npos);
            CHECK (n.find ("\nSTEP: ") != std::string::npos);
        }
    }
    // Direction: the bright reference gets a brighter top (Presence + Air) than the genre would give, the dark one a darker one.
    AutoMasterSettings sb, sd;
    sb.reference = bright;
    sd.reference = dark;
    const auto top = [] (const AutoMasterResult& r) { return r.chain.eq.gainDb[5] + r.chain.eq.gainDb[6]; };
    CHECK (top (autoMaster (mix, sb)) > top (genreOnly));
    CHECK (top (autoMaster (mix, sd)) < top (genreOnly));
}

TEST_CASE ("Auto-Master aims for the reference's loudness, and says so when it can't get there", "[reference][automaster]")
{
    const auto mix = testsig::musicLike (48000.0, 12.0, -16.0);
    const auto song = testsig::musicLike (48000.0, 40.0, -14.0, 21);

    // A quieter reference: lands on it.
    AutoMasterSettings quiet;
    quiet.reference = named (analyseReference (mastered (song, -12.5)), "Quiet Song");
    REQUIRE (quiet.reference.valid);
    const auto q = autoMaster (mix, quiet);
    REQUIRE (q.ok);
    CHECK_THAT (q.targetLufs, WithinAbs (quiet.reference.lufs, 0.06));
    INFO (q.summary);
    CHECK (q.reachedTarget);
    CHECK_THAT (q.output.integratedLufs, WithinAbs (quiet.reference.lufs, 0.3));

    // A louder reference than the Loud -9 target: the master gets louder than the genre target would make
    // it, or the report says plainly that it stopped short of the reference.
    AutoMasterSettings loud;
    loud.intensity = Intensity::punchy;
    // (This test signal's big kicks won't limit past about -9 LUFS, so the reading is set by hand: the
    // loudness is the only thing Auto-Master takes from it here.)
    loud.reference = named (analyseReference (mastered (song, -9.5)), "Loud Song");
    loud.reference.lufs = -7.0;
    AutoMasterSettings plain;
    plain.intensity = Intensity::punchy;
    const auto l = autoMaster (mix, loud), g = autoMaster (mix, plain);
    REQUIRE (l.ok);
    CHECK (l.targetLufs == -7.0);
    INFO (l.summary);
    CHECK (l.output.integratedLufs > g.output.integratedLufs + 0.5);
    // (Short of it: either the peaks or the squash limit stopped it, or, on this -28 LUFS test mix at a
    // -1.5 dBTP ceiling, even the maximum Drive: the quiet-mix note says so.)
    if (! l.reachedTarget)
        CHECK (std::any_of (l.notes.begin(), l.notes.end(), [] (const auto& n) {
            return n.find ("NOT AS LOUD AS THE REFERENCE") != std::string::npos || n.find ("arrives very quiet") != std::string::npos; }));
    CHECK (l.output.truePeakDbtp <= l.chain.limiter.ceilingDbtp + 0.05);

    // Out-of-range references are clamped, and the report explains why.
    ReferenceProfile crushed = loud.reference;
    crushed.lufs = -4.0;
    AutoMasterSettings c;
    c.reference = crushed;
    const auto cr = autoMaster (mix, c);
    CHECK (cr.targetLufs == kRefMaxLufs);
    CHECK (std::any_of (cr.notes.begin(), cr.notes.end(), [] (const auto& n) { return n.find ("REFERENCE LOUDNESS") != std::string::npos; }));
}

TEST_CASE ("Auto-Master sets the stereo width toward the reference's", "[reference][automaster]")
{
    const auto song = testsig::musicLike (48000.0, 40.0, -14.0, 31);

    // A narrow mix and a wide reference: opens up at least as much as the genre rule, toward the reference.
    const auto narrow = withSide (testsig::musicLike (48000.0, 12.0, -14.0), 0.2f);
    AutoMasterSettings wideRef;
    wideRef.reference = named (analyseReference (mastered (song, -9.0)), "Wide Song");
    const auto wn = autoMaster (narrow, wideRef), gn = autoMaster (narrow, {});
    INFO ("narrow mix: genre " << gn.chain.imager.highWidth << " %, matched " << wn.chain.imager.highWidth << " %");
    CHECK (wn.chain.imager.highWidth > 100.0);
    CHECK (wn.chain.imager.highWidth >= gn.chain.imager.highWidth);
    CHECK (wn.chain.imager.highWidth <= 125.0);
    CHECK (wn.chain.imager.midWidth <= 110.0);
    CHECK (wn.masterAnalysis.highSideDb > analyseMix (narrow).highSideDb);   // the master really is wider

    // A wide mix and a narrow reference: pulled in, where the genre rule leaves it alone.
    const auto wide = testsig::musicLike (48000.0, 12.0, -14.0);
    AutoMasterSettings narrowRef;
    narrowRef.reference = named (analyseReference (mastered (withSide (song, 0.3f), -9.0)), "Narrow Song");
    const auto ww = autoMaster (wide, narrowRef), gw = autoMaster (wide, {});
    INFO ("wide mix: genre " << gw.chain.imager.highWidth << " %, matched " << ww.chain.imager.highWidth << " %");
    CHECK (gw.chain.imager.highWidth <= 100.0);
    CHECK (ww.chain.imager.highWidth < 100.0);
    CHECK (ww.chain.imager.highWidth >= 85.0);
    CHECK (ww.chain.imager.lowWidth == 100.0);   // the lows are never touched
    const auto it = std::find_if (ww.reasons.begin(), ww.reasons.end(), [] (const auto& x) { return x.module == "img" && x.control == "Highs"; });
    REQUIRE (it != ww.reasons.end());
    CHECK (it->why.find ("Narrow Song") != std::string::npos);
}

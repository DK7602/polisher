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
    CHECK_THAT (q.targetLufs, WithinAbs (referenceLoudness (quiet.reference), 0.06));
    INFO (q.summary);
    CHECK (q.reachedTarget);
    CHECK_THAT (q.output.integratedLufs, WithinAbs (referenceLoudness (quiet.reference), 0.3));

    // A louder reference than the Loud -9 target: the master gets louder than the genre target would make
    // it, or the report says plainly that it stopped short of the reference.
    AutoMasterSettings loud;
    loud.intensity = Intensity::punchy;
    // (This test signal's big kicks won't limit past about -9 LUFS, so the reading is set by hand: the
    // loudness is the only thing Auto-Master takes from it here.)
    loud.reference = named (analyseReference (mastered (song, -9.5)), "Loud Song");
    loud.reference.hookLufs = -7.0;
    AutoMasterSettings plain;
    plain.intensity = Intensity::punchy;
    const auto l = autoMaster (mix, loud), g = autoMaster (mix, plain);
    REQUIRE (l.ok);
    CHECK (l.targetLufs == -7.0);
    INFO (l.summary);
    CHECK (l.output.integratedLufs > g.output.integratedLufs + 0.5);
    if (! l.reachedTarget)
        CHECK (std::any_of (l.notes.begin(), l.notes.end(), [] (const auto& n) { return n.find ("NOT AS LOUD AS THE REFERENCE") != std::string::npos; }));
    CHECK (l.output.truePeakDbtp <= l.chain.limiter.ceilingDbtp + 0.05);

    // Out-of-range references are clamped, and the report explains why.
    ReferenceProfile crushed = loud.reference;
    crushed.hookLufs = -4.0;
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

TEST_CASE ("The reference's loudness is its loudest 12 s, compared hook with hook", "[reference][automaster]")
{
    // A released song: 24 s verses 3 dB under 14 s hooks. Its loudest 30 s hold the hook and some verse,
    // so they read lower than the hook itself; the master is set from the loudest 12 s of the user's song.
    const double sr = 48000.0;
    auto song = concat ({ withGain (testsig::trapLike (sr, 24.0, 101), -3.0), testsig::trapLike (sr, 14.0, 111),
                          withGain (testsig::trapLike (sr, 24.0, 102), -3.0), testsig::trapLike (sr, 14.0, 112),
                          withGain (testsig::trapLike (sr, 8.0, 120), -9.0) });
    song = withGain (song, -11.0 - measure (song).integratedLufs);   // hook about -9 LUFS: inside the range Auto-Master aims for
    const auto ref = named (analyseReference (song), "Hook Song");
    REQUIRE (ref.valid);
    CHECK (ref.sampleRate == sr);

    // The song's loudest 12 s, cut out and measured on their own.
    LoudestPartFinder finder;
    finder.prepare (sr, 2);
    const float* ptrs[] = { song.channels[0].data(), song.channels[1].data() };
    finder.process (ptrs, 2, song.numSamples());
    const auto at = static_cast<size_t> (finder.loudestStart (kHookSeconds));
    AudioClip hook;
    hook.sampleRate = sr;
    for (const auto& ch : song.channels)
        hook.channels.emplace_back (ch.begin() + static_cast<std::ptrdiff_t> (at), ch.begin() + static_cast<std::ptrdiff_t> (at + static_cast<size_t> (kHookSeconds * sr)));
    const double hookLufs = measure (hook).integratedLufs;
    INFO ("hook " << hookLufs << " LUFS, profile hook " << ref.hookLufs << ", loudest 30 s " << ref.lufs);
    CHECK_THAT (ref.hookLufs, WithinAbs (hookLufs, 0.05));
    CHECK (ref.lufs < ref.hookLufs - 0.5);                  // the 30 s part reads lower: it holds some verse
    CHECK (referenceLoudness (ref) == ref.hookLufs);

    // Auto-Master aims for the hook, not the 30 s reading, and the report quotes it as 12 seconds.
    AutoMasterSettings s;
    s.reference = ref;
    const auto mix = testsig::musicLike (sr, 12.0, -16.0);
    const auto r = autoMaster (mix, s);
    REQUIRE (r.ok);
    CHECK_THAT (r.targetLufs, WithinAbs (std::round (ref.hookLufs * 10.0) / 10.0, 1.0e-9));
    INFO (r.summary);
    CHECK (r.summary.find ("loudest 12 seconds") != std::string::npos);
    // Only the target is landed on and called reached: never the 30 s figure when the hook is louder.
    if (r.reachedTarget)
        CHECK_THAT (r.output.integratedLufs, WithinAbs (r.targetLufs, 0.3));
    else
        CHECK (std::any_of (r.notes.begin(), r.notes.end(), [] (const auto& n) { return n.find ("NOT AS LOUD AS THE REFERENCE") != std::string::npos; }));

    // A profile saved before the hook was measured: its loudest part stands in.
    ReferenceProfile old = ref;
    old.hookLufs = -100.0;
    CHECK (referenceLoudness (old) == ref.lufs);
    AutoMasterSettings so;
    so.reference = old;
    CHECK_THAT (autoMaster (mix, so).targetLufs, WithinAbs (std::round (ref.lufs * 10.0) / 10.0, 1.0e-9));

    // The part on its own (no whole-song finder at hand) measures its own loudest 12 s.
    double start = 0.0;
    const auto part = loudestPart (song, start);
    const auto fromPart = analyseReference (part, 0.0, -20.0, start);
    CHECK (fromPart.hookLufs >= fromPart.lufs - 0.01);
}

TEST_CASE ("The reference's curve follows the project's sample rate like the live display", "[reference]")
{
    // The same sound read by the display at another rate: each of its 4096 bins spans a wider band, so
    // it reads 10 log10 (rate ratio) higher. The curve, measured at the file's rate and shifted by
    // displayRateShiftDb, lands on the display's reading at the project's rate.
    auto shaped = [] (double sr)
    {
        auto c = testsig::musicLike (sr, 20.0, -14.0, 3);
        return withGain (c, -14.0 - measure (c).integratedLufs);
    };
    const auto at44 = spectrumDisplayCurve (shaped (44100.0));
    CHECK (displayRateShiftDb (44100.0, 44100.0) == 0.0);
    CHECK (displayRateShiftDb (0.0, 96000.0) == 0.0);                     // an older profile without its rate
    for (const double sr : { 48000.0, 96000.0, 192000.0 })
    {
        const auto live = spectrumDisplayCurve (shaped (sr));
        const double shift = displayRateShiftDb (44100.0, sr);
        // The genre line's model of the display at both rates agrees with the constant shift.
        const auto g44 = genreDisplayCurve (Genre::trapHipHop, 44100.0), gsr = genreDisplayCurve (Genre::trapHipHop, sr);
        double worstModel = 0.0, worstLive = 0.0, meanLive = 0.0;
        int n = 0;
        for (int i = 0; i < kDisplayPoints; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, static_cast<double> (i) / (kDisplayPoints - 1));
            if (f < 500.0 || f > 16000.0) continue;   // lower down, at 96 kHz and up, a point holds only a few bins (see displayRateShiftDb)
            const auto k = static_cast<size_t> (i);
            worstModel = std::max (worstModel, std::abs ((gsr[k] - g44[k]) - shift));
            const double d = live[k] - (at44[k] + shift);
            worstLive = std::max (worstLive, std::abs (d));
            meanLive += d;
            ++n;
        }
        meanLive /= n;
        INFO (sr << " Hz: shift " << shift << " dB; model off by " << worstModel << " dB at most; live mean " << meanLive << ", worst " << worstLive);
        CHECK (worstModel < 0.35);
        CHECK (std::abs (meanLive) < 0.7);     // unshifted it would be off by the whole shift (3.4 dB at 96 kHz)
        CHECK (worstLive < 1.5);               // two different renders of a random-ish signal, point by point
    }
}

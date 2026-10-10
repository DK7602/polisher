#include <catch2/catch_test_macros.hpp>

#include "Signals.h"
#include "polisher/AutoMaster.h"
#include "polisher/LoudnessTargeting.h"
#include "polisher/SongListener.h"
#include "polisher/StemEQ.h"

#include <cmath>
#include <numbers>
#include <random>

using namespace polisher;

namespace {

void append (AudioClip& song, const AudioClip& part)
{
    if (song.channels.empty()) { song = part; return; }
    for (size_t c = 0; c < song.channels.size(); ++c)
        song.channels[c].insert (song.channels[c].end(), part.channels[c].begin(), part.channels[c].end());
}

AudioClip slice (const AudioClip& clip, double from, double to)
{
    AudioClip out;
    out.sampleRate = clip.sampleRate;
    const auto a = static_cast<size_t> (std::lround (from * clip.sampleRate));
    const auto b = std::min (clip.channels[0].size(), static_cast<size_t> (std::lround (to * clip.sampleRate)));
    for (const auto& ch : clip.channels) out.channels.emplace_back (ch.begin() + static_cast<std::ptrdiff_t> (a), ch.begin() + static_cast<std::ptrdiff_t> (b));
    return out;
}

/** A 2:30 song: quiet intro, verse, chorus (the loudest), verse, a second chorus a little quieter, outro. */
AudioClip longSong (double sr)
{
    AudioClip song;
    append (song, testsig::musicLike (sr, 15.0, -30.0, 1));   //   0-15  intro
    append (song, testsig::musicLike (sr, 40.0, -20.0, 2));   //  15-55  verse
    append (song, testsig::musicLike (sr, 20.0, -12.0, 3));   //  55-75  chorus
    append (song, testsig::musicLike (sr, 40.0, -20.0, 4));   //  75-115 verse
    append (song, testsig::musicLike (sr, 20.0, -13.0, 5));   // 115-135 chorus 2
    append (song, testsig::musicLike (sr, 15.0, -24.0, 6));   // 135-150 outro
    return song;
}

/** Streams a clip through a listener in random block sizes, the DAW's timeline starting at startSeconds. */
void listen (SongListener& l, const AudioClip& clip, double startSeconds, unsigned seed = 5)
{
    std::mt19937 rng (seed);
    std::uniform_int_distribution<int> size (1, 4096);
    const int n = clip.numSamples();
    for (int pos = 0; pos < n;)
    {
        const int len = std::min (size (rng), n - pos);
        l.process (clip.channels[0].data() + pos, clip.channels[1].data() + pos, len, startSeconds < 0.0 ? -1.0 : startSeconds + pos / clip.sampleRate);
        pos += len;
    }
}

// The 808 of the Tone Steady tests: notes of a sine 808 (with a little 2nd harmonic) from `from` s on.
void add808 (AudioClip& c, double from, double to = 1.0e9)
{
    const double sr = c.sampleRate, notes[3] = { 45.0, 50.0, 41.0 };
    for (auto& ch : c.channels)
    {
        double ph = 0.0;
        for (size_t i = static_cast<size_t> (from * sr); i < ch.size() && static_cast<double> (i) < to * sr; ++i)
        {
            const double t = static_cast<double> (i) / sr - from, tn = std::fmod (t, 1.0);
            ph += 2.0 * std::numbers::pi * notes[static_cast<int> (t) % 3] / sr;
            ch[i] += static_cast<float> (0.35 * std::exp (-tn * 1.2) * std::min (1.0, tn * 200.0) * (std::sin (ph) + 0.15 * std::sin (2.0 * ph)));
        }
    }
}

// Energy below ~130 Hz (4th-order low-pass) of channel 0 over [from, to) s, dB.
double lowDb (const AudioClip& c, double from, double to)
{
    const double sr = c.sampleRate, w0 = 2.0 * std::numbers::pi * 130.0 / sr, alpha = std::sin (w0) / std::sqrt (2.0), cw = std::cos (w0);
    const double b0 = (1.0 - cw) / 2.0, b1 = 1.0 - cw, a0 = 1.0 + alpha, a1 = -2.0 * cw, a2 = 1.0 - alpha;
    double x1[2] {}, x2[2] {}, y1[2] {}, y2[2] {}, e = 0.0;
    for (size_t i = 0; i < c.channels[0].size(); ++i)
    {
        double x = c.channels[0][i];
        for (size_t st = 0; st < 2; ++st)
        {
            const double y = (b0 * x + b1 * x1[st] + b0 * x2[st] - a1 * y1[st] - a2 * y2[st]) / a0;
            x2[st] = x1[st]; x1[st] = x; y2[st] = y1[st]; y1[st] = y; x = y;
        }
        if (i >= static_cast<size_t> (from * sr) && i < static_cast<size_t> (to * sr)) e += x * x;
    }
    return 10.0 * std::log10 (e + 1e-30);
}

// Energy around `hz` (2nd-order band-pass, Q 2) of channel 0 over [from, to) s, dB.
double bandDb (const AudioClip& c, double hz, double from, double to)
{
    const double sr = c.sampleRate, w0 = 2.0 * std::numbers::pi * hz / sr, alpha = std::sin (w0) / 4.0;
    const double b0 = alpha, a0 = 1.0 + alpha, a1 = -2.0 * std::cos (w0), a2 = 1.0 - alpha;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0, e = 0;
    for (size_t i = 0; i < c.channels[0].size(); ++i)
    {
        const double v = c.channels[0][i];
        const double y = (b0 * v - b0 * x2 - a1 * y1 - a2 * y2) / a0;
        x2 = x1; x1 = v; y2 = y1; y1 = y;
        if (i >= static_cast<size_t> (from * sr) && i < static_cast<size_t> (to * sr)) e += y * y;
    }
    return 10.0 * std::log10 (e + 1e-30);
}

ToneTarget learnTone (const AudioClip& song)
{
    SongListener l;
    l.prepare (song.sampleRate, 12.0, 600.0);
    listen (l, song, 0.0);
    return l.toneTarget();
}

} // namespace

TEST_CASE ("Song listener finds the chorus of a long song and measures the whole song, in fixed memory", "[songlistener]")
{
    const double sr = 48000.0;
    const auto song = longSong (sr);
    SongListener l;
    l.prepare (sr, 12.0, 15.0 * 60.0);
    const auto memory = l.memoryBytes();
    CHECK (memory < 16u * 1024u * 1024u);                     // ~24 s of audio, whatever the song's length

    listen (l, song, 3.0);                                    // playback started at 0:03 on the timeline
    CHECK (l.memoryBytes() == memory);                        // nothing grew while it listened
    CHECK (std::abs (l.heardSeconds() - 150.0) < 1.0e-6);
    REQUIRE (l.hasWindow());
    const auto st = l.stats();
    INFO ("loudest part at " << st.loudestStartSeconds << " s, " << st.loudestLufs << " LUFS; song " << st.integratedLufs
          << " LUFS, LRA " << st.loudnessRangeLu);
    CHECK (st.positionFromHost);
    CHECK (st.loudestStartSeconds >= 3.0 + 55.0 - 0.05);      // inside the first (louder) chorus...
    CHECK (st.loudestStartSeconds + st.loudestSeconds <= 3.0 + 75.0 + 0.05);
    CHECK (std::abs (st.loudestSeconds - 12.0) < 1.0e-9);

    // The window is the song's own audio at that place, sample for sample.
    const auto window = l.loudestWindow();
    REQUIRE (window.numSamples() == static_cast<int> (12.0 * sr));
    const auto at = static_cast<size_t> (std::lround ((st.loudestStartSeconds - 3.0) * sr));
    bool same = true;
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = 0; i < window.channels[c].size(); ++i)
            same = same && window.channels[c][i] == song.channels[c][at + i];
    CHECK (same);
    CHECK (std::abs (st.loudestLufs - measure (window).integratedLufs) < 0.2);

    // The whole song's loudness, as a loudness meter reads it over the whole song.
    const auto whole = measure (song);
    CHECK (std::abs (st.integratedLufs - whole.integratedLufs) < 0.05);
    CHECK (std::abs (st.loudnessRangeLu - whole.loudnessRangeLu) < 0.3);

    // Twice as long a listen holds no more memory.
    SongListener l2;
    l2.prepare (sr, 12.0, 15.0 * 60.0);
    auto twice = song;
    append (twice, song);
    listen (l2, twice, 0.0, 9);
    CHECK (l2.memoryBytes() == memory);
    CHECK (std::abs (l2.heardSeconds() - 300.0) < 1.0e-6);
    CHECK (std::abs (l2.stats().loudestStartSeconds - 55.0) < 8.0);   // the first chorus still (the earliest of equals)
}

TEST_CASE ("Song listener skips stops and silence, and knows where the loudest part is on the timeline", "[songlistener]")
{
    const double sr = 44100.0;
    SongListener l;
    l.prepare (sr, 12.0, 600.0);
    // 20 s of verse from 1:00, a stop (silent blocks), then the chorus played from 2:10.
    listen (l, testsig::musicLike (sr, 20.0, -20.0, 1), 60.0);
    auto gap = testsig::silence (sr, 2, 4.0);
    listen (l, gap, 80.0);
    CHECK (l.silentSamples() == gap.numSamples());
    listen (l, testsig::musicLike (sr, 16.0, -11.0, 2), 130.0);
    CHECK (l.silentSamples() == 0);
    CHECK (std::abs (l.heardSeconds() - 36.0) < 1.0e-6);
    const auto st = l.stats();
    CHECK (st.positionFromHost);
    CHECK (st.loudestStartSeconds >= 130.0 - 0.01);
    CHECK (st.loudestStartSeconds <= 134.0 + 0.01);

    // Without a timeline (a host that doesn't tell), the position is counted in music heard.
    SongListener n;
    n.prepare (sr, 12.0, 600.0);
    listen (n, testsig::musicLike (sr, 20.0, -20.0, 1), -1.0);
    listen (n, testsig::musicLike (sr, 16.0, -11.0, 2), -1.0);
    CHECK_FALSE (n.stats().positionFromHost);
    CHECK (n.stats().loudestStartSeconds >= 20.0 - 0.01);
    CHECK (n.stats().loudestStartSeconds <= 24.0 + 0.01);
}

TEST_CASE ("Song listener: finishing early, after 12 s of music, gives the 12 s it heard", "[songlistener]")
{
    const double sr = 48000.0;
    SongListener l;
    l.prepare (sr, 12.0, 600.0);
    const auto first = testsig::musicLike (sr, 8.0, -14.0, 3);
    listen (l, first, 0.0);
    CHECK_FALSE (l.hasWindow());
    CHECK (l.loudestStartSeconds() < 0.0);
    CHECK (l.loudestWindow().numSamples() == first.numSamples());    // all of it, so far
    listen (l, testsig::musicLike (sr, 5.0, -14.0, 4), 8.0);
    REQUIRE (l.hasWindow());
    CHECK (l.loudestWindow().numSamples() == static_cast<int> (12.0 * sr));
    CHECK (l.toneTarget().valid);                                     // 13 s of music: enough for a tone

    // It stops at its limit and says so.
    SongListener s;
    s.prepare (sr, 12.0, 20.0);
    listen (s, testsig::musicLike (sr, 25.0, -14.0, 5), 0.0);
    CHECK (s.isFull());
    CHECK (std::abs (s.heardSeconds() - 20.0) < 1.0e-6);
}

TEST_CASE ("Tone Steady with the song's own tone leaves a steady song alone", "[songlistener][spectral]")
{
    const double sr = 48000.0;
    const auto song = testsig::musicLike (sr, 40.0, -14.0, 9);
    const auto tone = learnTone (song);
    REQUIRE (tone.valid);
    ToneSteadyParams ts; ts.amount = 100.0; ts.rangeDb = 6.0; ts.target = tone;
    StemEQ s;
    s.prepare (sr, 2);
    s.setToneSteady (ts);
    s.reset();
    auto out = song;
    float* ch[2] = { out.channels[0].data(), out.channels[1].data() };
    double worst = 0.0;
    for (int i = 0; i < out.numSamples(); i += 4800)
    {
        s.process (std::array<float*, 2> { ch[0] + i, ch[1] + i }.data(), 2, std::min (4800, out.numSamples() - i));
        double d = 0.0, hz = 0.0;
        s.takeToneSteadyMeter (d, hz);
        if (i > static_cast<int> (3.0 * sr)) worst = std::max (worst, std::abs (d));
    }
    UNSCOPED_INFO ("largest Tone Steady move " << worst << " dB");
    CHECK (worst < 1.0);
}

TEST_CASE ("Tone Steady with a stored target gives the same master wherever playback or export starts", "[songlistener][spectral]")
{
    // A 20 s intro without an 808, then a minute with it, and a passage in it that drifts muddier.
    const double sr = 44100.0;
    auto song = testsig::musicLike (sr, 80.0, -18.0, 3);
    add808 (song, 20.0);
    std::mt19937 rng (4);
    std::normal_distribution<double> wn (0.0, 1.0);
    for (auto& ch : song.channels)
    {
        const double w0 = 2.0 * std::numbers::pi * 300.0 / sr, alpha = std::sin (w0) / 4.0;
        const double b0 = alpha, a0 = 1.0 + alpha, a1 = -2.0 * std::cos (w0), a2 = 1.0 - alpha;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (size_t i = static_cast<size_t> (50.0 * sr); i < static_cast<size_t> (56.0 * sr); ++i)
        {
            const double x = wn (rng), y = (b0 * x - b0 * x2 - a1 * y1 - a2 * y2) / a0;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            ch[i] += static_cast<float> (0.2 * y);
        }
    }
    const auto tone = learnTone (song);
    REQUIRE (tone.valid);

    // Playback from the chorus starts on the same STFT frame grid as the export (a whole number of
    // frames in): any spectral processor's output depends a little on where its frames fall, which is
    // not what this is about.
    const int grid = StemEQ::fftSizeFor (sr);
    const double chorusAt = std::floor (20.0 * sr / grid) * grid / sr;
    auto worstAfter = [&] (const ToneSteadyParams& ts, double warmSeconds)
    {
        const auto fromStart = renderSpectral (song, {}, ts, {});                        // export from bar 1
        const auto fromChorus = renderSpectral (slice (song, chorusAt, 80.0), {}, ts, {});   // playback from the chorus
        const auto warm = static_cast<size_t> (std::lround (warmSeconds * sr));
        const auto off = static_cast<size_t> (std::lround (chorusAt * sr));
        double worst = 0.0;
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = warm; i < fromChorus.channels[c].size(); ++i)
                worst = std::max (worst, static_cast<double> (std::abs (fromStart.channels[c][i + off] - fromChorus.channels[c][i])));
        return worst;
    };
    ToneSteadyParams running; running.amount = 100.0; running.rangeDb = 6.0;
    ToneSteadyParams held = running;
    held.target = tone;
    for (double warm : { 10.0, 20.0 })
    {
        const double withTarget = worstAfter (held, warm), following = worstAfter (running, warm);
        UNSCOPED_INFO ("largest difference after " << warm << " s of the chorus, from bar 1 vs from the chorus: with the target "
                       << withTarget << ", following the music " << following);
        CHECK (withTarget < (warm < 15.0 ? 1.0e-3 : 1.0e-4));
        CHECK (following > 10.0 * withTarget);   // following the music, where playback started still shows
    }

    // The muddy passage is still pulled back with the target.
    const auto out = renderSpectral (song, {}, held, {});
    const double drift = bandDb (song, 300.0, 51.0, 55.5) - bandDb (song, 300.0, 40.0, 44.5);
    const double after = bandDb (out, 300.0, 51.0, 55.5) - bandDb (song, 300.0, 51.0, 55.5);
    UNSCOPED_INFO ("low mids drift " << drift << " dB; Tone Steady moved them " << after << " dB");
    CHECK (after < -1.0);
}

TEST_CASE ("Tone Steady with a stored target doesn't cut an 808 that enters after the intro", "[songlistener][spectral]")
{
    const double sr = 44100.0;
    ToneSteadyParams ts; ts.amount = 100.0; ts.rangeDb = 3.0;

    // The 808 plays most of the song (from 0:16 on): it is part of the song's tone.
    auto song = testsig::musicLike (sr, 70.0, -18.0, 3);
    add808 (song, 16.0);
    ts.target = learnTone (song);
    REQUIRE (ts.target.valid);
    auto out = renderSpectral (song, {}, ts, {});
    for (double t0 : { 16.0, 18.0, 22.0, 40.0, 60.0 })
    {
        const double change = lowDb (out, t0, t0 + 2.0) - lowDb (song, t0, t0 + 2.0);
        UNSCOPED_INFO ("808 from 0:16, at " << t0 << " s: low end " << change << " dB");
        CHECK (change > -0.5);
    }
    const double intro = lowDb (out, 4.0, 15.0) - lowDb (song, 4.0, 15.0);
    UNSCOPED_INFO ("intro low end " << intro << " dB");
    CHECK (std::abs (intro) < 1.0);    // the intro without it is lifted a little at most, not toward the 808

    // The 808 only in a short section (a fifth of the song): not the song's usual tone, but far from
    // it, so it is the arrangement, not a drift: left alone too.
    auto rare = testsig::musicLike (sr, 75.0, -18.0, 5);
    add808 (rare, 30.0, 45.0);
    ts.target = learnTone (rare);
    REQUIRE (ts.target.valid);
    out = renderSpectral (rare, {}, ts, {});
    for (double t0 : { 30.0, 34.0, 40.0 })
    {
        const double change = lowDb (out, t0, t0 + 2.0) - lowDb (rare, t0, t0 + 2.0);
        UNSCOPED_INFO ("808 for 15 s only, at " << t0 << " s: low end " << change << " dB");
        CHECK (change > -0.5);
    }
}

TEST_CASE ("A stored tone follows the Tonal EQ in front of Tone Steady", "[songlistener][spectral]")
{
    // A low-mid EQ boost before the stage is the producer's choice: with the target moved by the EQ
    // (toneTargetAtStage) Tone Steady keeps it; holding the EQ'd song to the raw tone would take it back.
    const double sr = 48000.0;
    const auto song = testsig::musicLike (sr, 30.0, -16.0, 6);
    const auto tone = learnTone (song);
    REQUIRE (tone.valid);
    TonalEQParams eq;
    eq.gainDb[2] = 4.0;   // Low Mid +4 dB at 350 Hz
    const auto eqd = renderEq (song, eq);
    ToneSteadyParams ts; ts.amount = 100.0; ts.rangeDb = 6.0;
    ts.target = toneTargetAtStage (tone, eq, sr);
    const auto kept = renderSpectral (eqd, {}, ts, {});
    ts.target = tone;
    const auto fought = renderSpectral (eqd, {}, ts, {});
    const double keptDb = bandDb (kept, 350.0, 5.0, 29.0) - bandDb (eqd, 350.0, 5.0, 29.0);
    const double foughtDb = bandDb (fought, 350.0, 5.0, 29.0) - bandDb (eqd, 350.0, 5.0, 29.0);
    UNSCOPED_INFO ("low mids after the EQ boost: target moved with the EQ " << keptDb << " dB, raw target " << foughtDb << " dB");
    CHECK (std::abs (keptDb) < 0.3);
    CHECK (foughtDb < -1.0);

    // A flat or switched-off EQ leaves the target as it is.
    TonalEQParams flat;
    CHECK (toneTargetAtStage (tone, flat, sr).bandDb == tone.bandDb);
    eq.enabled = false;
    CHECK (toneTargetAtStage (tone, eq, sr).bandDb == tone.bandDb);
}

TEST_CASE ("Auto-Master holds the song to its own tone when it listened to the song", "[songlistener][automaster]")
{
    const double sr = 48000.0;
    auto song = testsig::musicLike (sr, 40.0, -16.0, 7);
    add808 (song, 10.0);
    AutoMasterSettings s;
    s.songTone = learnTone (song);
    REQUIRE (s.songTone.valid);
    const auto r = autoMaster (slice (song, 20.0, 32.0), s);
    REQUIRE (r.ok);
    CHECK (r.chain.steady.target.valid);
    // As the stage hears it: the song's tone moved by the chosen Tonal EQ.
    const auto expected = toneTargetAtStage (s.songTone, r.chain.eq, sr);
    CHECK (r.chain.steady.target.bandDb == expected.bandDb);
    bool explained = false;
    for (const auto& x : r.reasons) explained = explained || (x.module == "ts" && x.control == "Target");
    CHECK (explained);

    // Without a listen of the song (a state from before, or nothing learned): following the music, as before.
    const auto plain = autoMaster (slice (song, 20.0, 32.0), AutoMasterSettings {});
    CHECK_FALSE (plain.chain.steady.target.valid);
}

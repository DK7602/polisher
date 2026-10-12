#include <catch2/catch_test_macros.hpp>

#include "Signals.h"
#include "polisher/AutoMaster.h"
#include "polisher/Biquad.h"
#include "polisher/FilterDesign.h"
#include "polisher/LoudnessMeter.h"
#include "polisher/LoudnessTargeting.h"
#include "polisher/SongListener.h"
#include "polisher/StemEQ.h"

#include <cmath>
#include <functional>
#include <memory>
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

// Energy (dB) of both channels in [lo, hi) Hz (two 2nd-order Butterworth high- and low-passes) over [from, to) s.
double rangeDb (const AudioClip& c, double lo, double hi, double from, double to)
{
    double e = 0.0;
    for (const auto& ch : c.channels)
    {
        Biquad f[4];
        design::apply (f[0], design::butterworth (true, lo, c.sampleRate));
        design::apply (f[1], design::butterworth (true, lo, c.sampleRate));
        design::apply (f[2], design::butterworth (false, hi, c.sampleRate));
        design::apply (f[3], design::butterworth (false, hi, c.sampleRate));
        for (size_t i = 0; i < ch.size(); ++i)
        {
            double v = ch[i];
            for (auto& b : f) v = b.process (v);
            const double t = static_cast<double> (i) / c.sampleRate;
            if (t >= from && t < to) e += v * v;
        }
    }
    return 10.0 * std::log10 (e + 1.0e-30);
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

    // The muddy passage is still pulled back with the target. (This noise lifts most of the spectrum
    // above 160 Hz, 200-500 Hz by 9.7 dB, 2-5 kHz by 2 dB: the offset then reads part of it as a thinner
    // low end, which is lifted a little; a passage muddier only in the low mids is cut fully.)
    const auto out = renderSpectral (song, {}, held, {});
    const double drift = rangeDb (song, 200.0, 500.0, 51.0, 55.5) - rangeDb (song, 200.0, 500.0, 40.0, 44.5);
    const double after = rangeDb (out, 200.0, 500.0, 51.0, 55.5) - rangeDb (song, 200.0, 500.0, 51.0, 55.5);
    UNSCOPED_INFO ("low mids drift " << drift << " dB; Tone Steady moved them " << after << " dB, the low end "
                   << lowDb (out, 51.0, 55.5) - lowDb (song, 51.0, 55.5) << " dB");
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

    // A mid or side band moves the target by its part of the energy only (Air +6 dB: stereo > mid > side > 0).
    TonalEQParams air;
    air.gainDb[6] = 6.0;
    auto at12k = [&] (EqChannels ch)
    {
        air.channels[6] = ch;
        const auto t = toneTargetAtStage (tone, air, sr);
        double moved = 0.0;
        for (int band = 0; band < kToneBands; ++band)
            if (std::abs (StemEQ::bandCentreHz (band, sr) - 12000.0) < 1500.0)
                moved = std::max (moved, static_cast<double> (t.bandDb[static_cast<size_t> (band)] - tone.bandDb[static_cast<size_t> (band)]));
        return moved;
    };
    const double st = at12k (EqChannels::stereo), mid = at12k (EqChannels::mid), side = at12k (EqChannels::side);
    UNSCOPED_INFO ("target move near 12 kHz: stereo " << st << ", mid " << mid << ", side " << side);
    CHECK (st > mid);
    CHECK (mid > side);
    CHECK (side > 0.3);
    CHECK (side < 3.0);
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

    // A tone learned earlier from more of the song (a short listen kept it): the report says so.
    s.songSeconds = 15.0;
    bool earlier = false;
    for (const auto& x : autoMaster (slice (song, 20.0, 32.0), s).reasons)
        earlier = earlier || (x.module == "ts" && x.control == "Target" && x.why.find ("learned earlier from 0:40") != std::string::npos);
    CHECK (earlier);

    // Without a listen of the song (a state from before, or nothing learned): following the music, as before.
    const auto plain = autoMaster (slice (song, 20.0, 32.0), AutoMasterSettings {});
    CHECK_FALSE (plain.chain.steady.target.valid);
}

TEST_CASE ("Song listener: tiny and changing block sizes hear exactly the same music", "[songlistener]")
{
    // Silence is judged per 100 ms piece, not per host block: a 1-sample block near a zero crossing
    // inside music is still music. A song with a stop in the middle (4 s of silence) and a quiet
    // breakdown, streamed at 1, 4, 16, 64 and 4096 samples and at random sizes.
    const double sr = 48000.0;
    AudioClip song;
    append (song, testsig::musicLike (sr, 20.0, -16.0, 1));
    append (song, testsig::silence (sr, 2, 4.0));
    append (song, testsig::musicLike (sr, 6.0, -38.0, 2));   // quiet, but music (peaks above -50 dBFS)
    append (song, testsig::musicLike (sr, 20.0, -12.0, 3));
    auto run = [&] (std::function<int()> nextBlock)
    {
        auto l = std::make_unique<SongListener>();
        l->prepare (sr, 12.0, 600.0);
        for (int pos = 0; pos < song.numSamples();)
        {
            const int len = std::min (nextBlock(), song.numSamples() - pos);
            l->process (song.channels[0].data() + pos, song.channels[1].data() + pos, len, pos / sr);
            pos += len;
        }
        return l;
    };
    const auto ref = run ([] { return 4096; });
    CHECK (std::abs (ref->heardSeconds() - 46.0) < 1.0e-6);
    const auto refTone = ref->toneTarget();
    REQUIRE (refTone.valid);
    const auto refWindow = ref->loudestWindow();
    std::mt19937 rng (3);
    std::uniform_int_distribution<int> any (1, 700);
    for (const int block : { 1, 4, 16, 64, 0 })
    {
        const auto l = block > 0 ? run ([block] { return block; }) : run ([&] { return any (rng); });
        INFO ("block " << block << " (0: random 1-700)");
        CHECK (l->heardSamples() == ref->heardSamples());
        const auto tone = l->toneTarget();
        REQUIRE (tone.valid);
        double worst = 0.0;
        for (size_t b = 0; b < tone.bandDb.size(); ++b) worst = std::max (worst, std::abs ((double) tone.bandDb[b] - refTone.bandDb[b]));
        CHECK (worst < 1.0e-3);
        CHECK (l->loudestWindow().channels == refWindow.channels);
        CHECK (std::abs (l->stats().integratedLufs - ref->stats().integratedLufs) < 1.0e-9);
    }
}

TEST_CASE ("Song listener on a mono bus measures one channel and keeps a mono window", "[songlistener]")
{
    const double sr = 44100.0;
    AudioClip mono = testsig::musicLike (sr, 30.0, -14.0, 4);
    mono.channels.resize (1);
    SongListener l;
    l.prepare (sr, 12.0, 600.0);
    for (int pos = 0; pos < mono.numSamples(); pos += 512)
        l.process (mono.channels[0].data() + pos, static_cast<const float*> (nullptr), std::min (512, mono.numSamples() - pos), pos / sr);
    const auto st = l.stats();
    CHECK (st.channels == 1);
    CHECK (std::abs (st.integratedLufs - measure (mono).integratedLufs) < 0.05);   // not 3 dB louder
    const auto w = l.loudestWindow();
    REQUIRE (w.numChannels() == 1);
    CHECK (std::abs (l.loudestLufs() - measure (w).integratedLufs) < 0.3);
    // The same pointer twice is a mono bus too (what some hosts hand over).
    SongListener same;
    same.prepare (sr, 12.0, 600.0);
    same.process (mono.channels[0].data(), mono.channels[0].data(), mono.numSamples(), 0.0);
    CHECK (std::abs (same.stats().integratedLufs - st.integratedLufs) < 1.0e-9);
}

TEST_CASE ("Song listener notes jumps on the timeline and where the loudest part's audio came from", "[songlistener]")
{
    const double sr = 48000.0;
    const auto dash = [] (const TimeSpan& t) { return std::make_pair (t.start, t.end); };
    {
        // Played 0:40-0:50, then the playhead jumped to 2:00 and played to 2:10 (the audit's case: the
        // report used to say 0:43-0:55, which was never played).
        SongListener l;
        l.prepare (sr, 12.0, 600.0);
        listen (l, testsig::musicLike (sr, 10.0, -14.0, 1), 40.0);
        listen (l, testsig::musicLike (sr, 10.0, -13.0, 2), 120.0);
        const auto st = l.stats();
        CHECK (st.timelineJumps == 1);
        REQUIRE (st.loudestSpans.size() == 2);
        const auto [a0, a1] = dash (st.loudestSpans[0]);
        const auto [b0, b1] = dash (st.loudestSpans[1]);
        INFO ("spans " << a0 << "-" << a1 << ", " << b0 << "-" << b1);
        CHECK (a0 >= 40.0 - 1.0e-6);
        CHECK (std::abs (a1 - 50.0) < 0.11);
        CHECK (std::abs (b0 - 120.0) < 0.11);
        CHECK (b1 <= 130.0 + 1.0e-6);
        CHECK (std::abs ((a1 - a0) + (b1 - b0) - 12.0) < 0.15);
        CHECK (l.loudestJumped());
        CHECK (l.loudestEndSeconds() < 0.0);
    }
    {
        // A cycle loop 1:00-1:16 played three times.
        SongListener l;
        l.prepare (sr, 12.0, 600.0);
        const auto loop = testsig::musicLike (sr, 16.0, -14.0, 5);
        for (int k = 0; k < 3; ++k) listen (l, loop, 60.0, 5u + static_cast<unsigned> (k));
        const auto st = l.stats();
        CHECK (st.timelineJumps == 2);
        for (const auto& sp : st.loudestSpans)
        {
            INFO ("span " << sp.start << "-" << sp.end);
            CHECK (sp.start >= 60.0 - 1.0e-6);   // only what was played: inside the loop
            CHECK (sp.end <= 76.0 + 0.11);
        }
    }
    {
        // Played straight through, with a stop (no position while stopped) and play on from the same
        // place: no jump, one span, and the end is known.
        SongListener l;
        l.prepare (sr, 12.0, 600.0);
        listen (l, testsig::musicLike (sr, 8.0, -14.0, 7), 30.0);
        const auto quiet = testsig::silence (sr, 2, 2.0);
        listen (l, quiet, -1.0);   // stopped: the host gives no position
        listen (l, testsig::musicLike (sr, 8.0, -14.0, 8), 38.0);
        const auto st = l.stats();
        CHECK (st.timelineJumps == 0);
        REQUIRE (st.loudestSpans.size() == 1);
        CHECK (std::abs (st.loudestSpans[0].end - st.loudestSpans[0].start - 12.0) < 0.15);
        CHECK (std::abs (l.loudestEndSeconds() - st.loudestSpans[0].end) < 1.0e-9);
    }
}

// --- Tone Steady with a stored target: sections, parts above the bass, quiet passages, other songs ---

namespace {
// A synth lead (six harmonics of C5 / D5) from `from` to `to` s: a part that comes in above the bass.
void addLead (AudioClip& c, double from, double to, double amp)
{
    const double sr = c.sampleRate;
    for (auto& ch : c.channels)
    {
        double ph = 0.0;
        for (size_t i = static_cast<size_t> (from * sr); i < ch.size() && static_cast<double> (i) < to * sr; ++i)
        {
            const double t = static_cast<double> (i) / sr;
            ph += 2.0 * std::numbers::pi * (std::fmod (t, 2.0) < 1.0 ? 523.25 : 587.33) / sr;
            double v = 0.0;
            for (int n = 1; n <= 6; ++n) v += std::sin (n * ph) / n;
            ch[i] += static_cast<float> (amp * v);
        }
    }
}

// Difference energy of two renders over [from, to) s of the song (b starts `offset` s into it), dB re a's energy.
double diffDb (const AudioClip& a, const AudioClip& b, double offset, double from, double to)
{
    const double sr = a.sampleRate;
    const auto off = static_cast<size_t> (std::lround (offset * sr));
    double e = 0.0, d = 0.0;
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = static_cast<size_t> (from * sr); i < static_cast<size_t> (to * sr); ++i)
        {
            const double x = a.channels[c][i], y = b.channels[c][i - off];
            e += x * x;
            d += (x - y) * (x - y);
        }
    return 10.0 * std::log10 (d / (e + 1.0e-30) + 1.0e-30);
}
} // namespace

TEST_CASE ("Tone Steady with a target leaves the rest of the spectrum alone where the song's 808 doesn't play", "[songlistener][spectral]")
{
    // The overall offset used to be set mostly by the 808 bands: in an intro without the 808 everything
    // else read as too loud and was cut 1.5-3 dB (6 dB at full strength), the intro 0.6 LU quieter.
    const double sr = 44100.0;
    const double bands[][2] = { { 200.0, 500.0 }, { 500.0, 2000.0 }, { 2000.0, 5000.0 }, { 5000.0, 16000.0 } };
    auto song = testsig::musicLike (sr, 70.0, -18.0, 3);
    add808 (song, 16.0);
    const auto tone = learnTone (song);
    REQUIRE (tone.valid);
    struct Setting { double amount, range; int speed; };
    for (const auto set : { Setting { 40.0, 3.0, 0 }, Setting { 100.0, 6.0, 1 } })
    {
        ToneSteadyParams ts; ts.amount = set.amount; ts.rangeDb = set.range; ts.speed = set.speed; ts.target = tone;
        const auto out = renderSpectral (song, {}, ts, {});
        for (const auto& b : bands)
        {
            const double change = rangeDb (out, b[0], b[1], 4.0, 15.0) - rangeDb (song, b[0], b[1], 4.0, 15.0);
            UNSCOPED_INFO ("amount " << set.amount << ", intro without the 808, " << b[0] << "-" << b[1] << " Hz: " << change << " dB");
            CHECK (std::abs (change) < 0.5);
        }
        const double lu = measure (slice (out, 4.0, 15.0)).integratedLufs - measure (slice (song, 4.0, 15.0)).integratedLufs;
        UNSCOPED_INFO ("intro loudness " << lu << " LU");
        CHECK (std::abs (lu) < 0.2);
    }

    // The 808 leaves for the second half (it was cut -1.3 to -1.8 dB there).
    auto half = testsig::musicLike (sr, 60.0, -18.0, 3);
    add808 (half, 0.0, 30.0);
    ToneSteadyParams ts; ts.amount = 40.0; ts.rangeDb = 3.0; ts.speed = 0; ts.target = learnTone (half);
    REQUIRE (ts.target.valid);
    const auto out = renderSpectral (half, {}, ts, {});
    for (const auto& b : bands)
    {
        const double change = rangeDb (out, b[0], b[1], 36.0, 58.0) - rangeDb (half, b[0], b[1], 36.0, 58.0);
        UNSCOPED_INFO ("after the 808 leaves, " << b[0] << "-" << b[1] << " Hz: " << change << " dB");
        CHECK (std::abs (change) < 0.3);
    }
}

TEST_CASE ("Tone Steady with a target: a part entering above the bass is pulled down, at most half the Range", "[songlistener][spectral]")
{
    // A new part above 160 Hz can't be told from a harsher passage: it is treated like a drift for as
    // long as it plays, but cuts there go at most half the Range (they went the full Range: -1.3 dB at
    // Amount 40, Range 3, for all 20 s).
    const double sr = 44100.0;
    auto song = testsig::musicLike (sr, 90.0, -18.0, 4);
    add808 (song, 0.0);
    addLead (song, 30.0, 50.0, 0.03);
    const auto tone = learnTone (song);
    REQUIRE (tone.valid);
    for (double amount : { 40.0, 100.0 })
    {
        ToneSteadyParams ts; ts.amount = amount; ts.rangeDb = 3.0; ts.speed = 1; ts.target = tone;
        const auto out = renderSpectral (song, {}, ts, {});
        const double lead = rangeDb (out, 500.0, 3000.0, 36.0, 49.0) - rangeDb (song, 500.0, 3000.0, 36.0, 49.0);
        const double after = rangeDb (out, 500.0, 3000.0, 60.0, 80.0) - rangeDb (song, 500.0, 3000.0, 60.0, 80.0);
        UNSCOPED_INFO ("amount " << amount << ": lead band while it plays " << lead << " dB, after it " << after << " dB");
        CHECK (lead < -0.2);                // treated like a drift (as Learn and the manual say)
        CHECK (lead > -0.5 * ts.rangeDb);   // but never more than half the Range
        CHECK (std::abs (after) < 0.3);
    }
}

TEST_CASE ("Tone Steady with a stored target: a quiet intro and breakdown, and a stop and locate, don't change the master", "[songlistener][spectral]")
{
    // Quiet passages are judged against the song's stored level, not the last ~10 s: before, a breakdown
    // 17 dB under the chorus came out differently from bar 1 than from the chorus (-18 dB re signal).
    const double sr = 48000.0;
    AudioClip song;
    append (song, testsig::musicLike (sr, 20.0, -36.0, 21));     //  0-20  quiet intro
    auto chorus = testsig::musicLike (sr, 20.0, -20.0, 22);
    add808 (chorus, 0.0);
    append (song, chorus);                                        // 20-40  chorus
    auto breakdown = testsig::musicLike (sr, 12.0, -37.0, 23);
    std::mt19937 rng (4);
    std::normal_distribution<double> wn (0.0, 1.0);
    for (auto& ch : breakdown.channels)                           // a muddier breakdown
    {
        const double w0 = 2.0 * std::numbers::pi * 300.0 / sr, alpha = std::sin (w0) / 4.0;
        const double b0 = alpha, a0 = 1.0 + alpha, a1 = -2.0 * std::cos (w0), a2 = 1.0 - alpha;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (auto& v : ch)
        {
            const double x = wn (rng), y = (b0 * x - b0 * x2 - a1 * y1 - a2 * y2) / a0;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            v += static_cast<float> (0.004 * y);
        }
    }
    append (song, breakdown);                                     // 40-52  breakdown
    auto chorus2 = testsig::musicLike (sr, 28.0, -20.0, 24);
    add808 (chorus2, 0.0);
    append (song, chorus2);                                       // 52-80  chorus
    ToneSteadyParams ts; ts.amount = 100.0; ts.rangeDb = 6.0; ts.target = learnTone (song);
    REQUIRE (ts.target.valid);
    REQUIRE (ts.target.hasLevel());

    const int grid = StemEQ::fftSizeFor (sr);
    const double at = std::floor (20.0 * sr / grid) * grid / sr;   // the chorus, on the export's frame grid
    const auto fromStart = renderSpectral (song, {}, ts, {});
    const auto fromChorus = renderSpectral (slice (song, at, 80.0), {}, ts, {});
    for (auto [a, b] : { std::pair { 40.0, 46.0 }, { 46.0, 52.0 }, { 60.0, 78.0 } })
    {
        const double d = diffDb (fromStart, fromChorus, at, a, b);
        UNSCOPED_INFO (a << "-" << b << " s: from bar 1 vs from the chorus " << d << " dB re signal");
        CHECK (d < -60.0);
    }

    // Real time: play the second chorus, stop (silent blocks), locate back into the first chorus, play.
    StemEQ s;
    s.prepare (sr, 2);
    s.setToneSteady (ts);
    s.reset();
    auto run = [&] (AudioClip c)
    {
        for (int pos = 0; pos < c.numSamples(); pos += 512)
        {
            float* ch[2] = { c.channels[0].data() + pos, c.channels[1].data() + pos };
            s.process (ch, 2, std::min (512, c.numSamples() - pos));
        }
        return c;
    };
    run (slice (song, 60.0, 75.0));
    run (testsig::silence (sr, 2, 2.0));
    const auto live = run (slice (song, 25.0, 35.0));
    const double lat = s.latencySamples() / sr;
    // Tone Steady's short-term average needs its own few seconds; from then on live = export.
    const double d = diffDb (slice (fromStart, 28.0, 35.0), slice (live, 3.0 + lat, 10.0), 0.0, 0.0, 6.9 - lat);
    UNSCOPED_INFO ("after a stop and locate, 3-10 s in: live vs export " << d << " dB re signal");
    CHECK (d < -60.0);
}

TEST_CASE ("A tone learned on another song is told apart from the song's own", "[songlistener][spectral]")
{
    const double sr = 48000.0;
    const auto trap = testsig::trapLike (sr, 45.0);
    auto other = testsig::musicLike (sr, 45.0, -18.0, 9);
    const auto own = learnTone (trap), foreign = learnTone (other);
    REQUIRE (own.valid);
    REQUIRE (foreign.valid);
    // A short listen of the same song sits close to its whole-song tone; another song's tone far.
    const auto part = learnTone (slice (trap, 20.0, 35.0));
    REQUIRE (part.valid);
    const double same = toneDistanceDb (own, part, sr), apart = toneDistanceDb (foreign, part, sr);
    UNSCOPED_INFO ("15 s of the song vs its tone " << same << " dB, vs another song's " << apart << " dB");
    CHECK (same < 3.0);
    CHECK (apart > 4.0);
    CHECK (toneDistanceDb (own, own, sr) == 0.0);
    CHECK (toneDistanceDb (ToneTarget {}, part, sr) == 0.0);

    // Live: held to its own tone it reads close; held to the other song's, Tone Steady says so (after
    // 20 s of music and 10 s far from it).
    auto play = [&] (const ToneTarget& target, double& distance)
    {
        StemEQ s;
        s.prepare (sr, 2);
        ToneSteadyParams ts; ts.amount = 40.0; ts.rangeDb = 3.0; ts.speed = 0; ts.target = target;
        s.setToneSteady (ts);
        s.reset();
        AudioClip c = trap;
        bool far = false, early = false;
        for (int pos = 0; pos < c.numSamples(); pos += 4800)
        {
            float* ch[2] = { c.channels[0].data() + pos, c.channels[1].data() + pos };
            s.process (ch, 2, std::min (4800, c.numSamples() - pos));
            far = s.toneTargetFar();
            early = early || (far && pos < static_cast<int> (25.0 * sr));
        }
        distance = s.toneTargetDistanceDb();
        CHECK_FALSE (early);   // not before ~30 s
        return far;
    };
    double dOwn = 0.0, dForeign = 0.0;
    CHECK_FALSE (play (own, dOwn));
    CHECK (play (foreign, dForeign));
    UNSCOPED_INFO ("live: own tone " << dOwn << " dB, another song's " << dForeign << " dB");
    CHECK (dOwn < 2.5);
    CHECK (dForeign > StemEQ::kTargetFarDb);
    // Without a target there is nothing to be far from.
    StemEQ none;
    none.prepare (sr, 2);
    CHECK (none.toneTargetDistanceDb() < 0.0);
    CHECK_FALSE (none.toneTargetFar());
}

TEST_CASE ("A stored tone keeps the song's level, moved by Input Trim and the Tonal EQ", "[songlistener][spectral]")
{
    const double sr = 48000.0;
    const auto song = testsig::musicLike (sr, 30.0, -16.0, 6);
    const auto tone = learnTone (song);
    REQUIRE (tone.valid);
    REQUIRE (tone.hasLevel());
    // The same song 6 dB quieter reads 6 dB lower, at any rate (the level is per FFT size).
    auto quieter = song;
    for (auto& ch : quieter.channels) for (auto& v : ch) v *= 0.5f;
    CHECK (std::abs (learnTone (quieter).levelDb - (tone.levelDb - 6.02)) < 0.05);
    CHECK (toneTargetAtStage (tone, {}, sr, -6.0).levelDb == tone.levelDb - 6.0);
    TonalEQParams eq;
    eq.gainDb[2] = 4.0;   // Low Mid +4 dB
    const double up = toneTargetAtStage (tone, eq, sr).levelDb - tone.levelDb;
    UNSCOPED_INFO ("Low Mid +4 dB moves the song's level " << up << " dB");
    CHECK (up > 0.2);
    CHECK (up < 4.0);
    ToneTarget old = tone;
    old.levelDb = ToneTarget::kNoLevel;   // a target saved before the level: stays without one
    CHECK_FALSE (toneTargetAtStage (old, eq, sr, -6.0).hasLevel());
}

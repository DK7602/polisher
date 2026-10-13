// Release validation beyond the per-module tests: the true-peak meter and the Maximizer checked by an
// independent 16x meter (ReferenceTruePeak.h), and more of the EBU Tech 3341 / 3342 loudness cases.

#include "ReferenceTruePeak.h"
#include "Signals.h"
#include "polisher/LoudnessMeter.h"
#include "polisher/TruePeakLimiter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using namespace polisher;

namespace {

/** Raised-cosine fade in and out, so a test tone has no hard edges (an abrupt start is itself a
    real inter-sample over, and would hide what is being measured). */
AudioClip faded (AudioClip clip, double seconds = 0.1)
{
    const int n = static_cast<int> (clip.sampleRate * seconds);
    for (auto& ch : clip.channels)
    {
        const int len = static_cast<int> (ch.size());
        for (int i = 0; i < n && i < len; ++i)
        {
            const float w = static_cast<float> (0.5 - 0.5 * std::cos (std::numbers::pi * i / n));
            ch[static_cast<size_t> (i)] *= w;
            ch[static_cast<size_t> (len - 1 - i)] *= w;
        }
    }
    return clip;
}

/** A mix pushed 12 dB into a hard clip: flat tops and sharp corners, the hardest material for any
    true-peak detector (and what a brickwalled beat looks like). */
AudioClip hardClipped (double sr, unsigned seed)
{
    auto clip = testsig::musicLike (sr, 3.0, 12.0, seed);
    for (auto& ch : clip.channels)
        for (auto& v : ch)
            v = std::clamp (v, -1.0f, 1.0f);
    return faded (clip);
}

double readAfter (LoudnessMeter& m, const AudioClip& clip, int fromSample, int toSample, bool shortTerm, double& lo, double& hi)
{
    // Feeds [fromSample, toSample) in 100 ms steps and widens [lo, hi] with each reading.
    const int step = static_cast<int> (clip.sampleRate / 10);
    for (int i = fromSample; i < toSample; i += step)
    {
        const int n = std::min (step, toSample - i);
        const float* ptrs[] = { clip.channels[0].data() + i, clip.channels[1].data() + i };
        m.process (ptrs, 2, n);
        const double v = shortTerm ? m.shortTermLufs() : m.momentaryLufs();
        lo = std::min (lo, v);
        hi = std::max (hi, v);
    }
    return hi;
}

} // namespace

TEST_CASE ("The reference meter reads tones exactly (checks the checker)", "[validation][truepeak]")
{
    for (double sr : { 44100.0, 48000.0 })
        for (double f : { 997.0, 12000.0, 19000.0 })
            for (double phase : { 0.0, 0.7, std::numbers::pi / 4.0 })
            {
                const auto tone = faded (testsig::sine (sr, 2, f, -3.0, 1.0, phase));
                INFO (sr << " Hz rate, " << f << " Hz tone, phase " << phase);
                CHECK_THAT (refpeak::truePeakDb (tone), WithinAbs (-3.0, 0.01));
            }
}

TEST_CASE ("The true-peak meter agrees with an independent 16x meter on tones up to 20 kHz", "[validation][truepeak]")
{
    // BS.1770-4 allows a 4x meter to read a little low near Nyquist; ours stays within 0.1 dB.
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (double f : { 60.0, 1000.0, 8000.0, 15000.0, 18000.0, 19500.0 })
            for (double phase : { 0.1, std::numbers::pi / 4.0, 1.3 })
            {
                const auto tone = faded (testsig::sine (sr, 2, f, 0.0, 1.0, phase));
                const double ref = refpeak::truePeakDb (tone), own = testsig::truePeakOf (tone);
                INFO (sr << " Hz rate, " << f << " Hz, reference " << ref << " dBTP, meter " << own);
                CHECK (own <= ref + 0.05);
                CHECK (own >= ref - 0.1);
            }
}

TEST_CASE ("The Maximizer holds its ceiling on tones up to 20 kHz, by an independent 16x meter", "[validation][limiter]")
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (double f : { 100.0, 3000.0, 11025.0, 12000.0, 17000.0, 19500.0 })
            for (double ceiling : { -1.0, -0.1 })
            {
                // A 0 dBTP tone (at fs / 4 with this phase its samples sit at -3 dBFS), driven 6 dB into the ceiling.
                const auto tone = faded (testsig::sine (sr, 2, f, 0.0, 1.5, std::numbers::pi / 4.0));
                LimiterParams p;
                p.ceilingDbtp = ceiling;
                p.driveDb = 6.0;
                const double ref = refpeak::truePeakDb (renderLimited (tone, p));
                INFO (sr << " Hz rate, " << f << " Hz, ceiling " << ceiling << ", reference reads " << ref);
                CHECK (ref <= ceiling + 0.02);
            }
}

TEST_CASE ("The Maximizer on hard-clipped audio: at the ceiling by BS.1770, within 1 dB on a wideband meter", "[validation][limiter]")
{
    // Hard clipping fills the top few kHz below Nyquist. A BS.1770 true-peak meter (what streaming
    // services read, and what the Maximizer limits by) shows the output at the ceiling. An ideal
    // wideband reconstruction of those flat tops reads up to ~0.9 dB higher: the reason the default
    // ceiling is -1 dBTP. This pins both numbers so neither can quietly get worse.
    for (double sr : { 44100.0, 48000.0 })
        for (unsigned seed : { 1u, 2u, 3u })
        {
            LimiterParams p;
            p.ceilingDbtp = -1.0;
            const auto out = renderLimited (hardClipped (sr, seed), p);
            const double own = testsig::truePeakOf (out), ref = refpeak::truePeakDb (out);
            INFO (sr << " Hz rate, seed " << seed << ": BS.1770 meter " << own << " dBTP, wideband 16x " << ref << " dBTP");
            CHECK (own <= -0.95);
            CHECK (ref <= 0.0);
        }
}

TEST_CASE ("The Maximizer keeps its ceiling at every supported sample rate", "[validation][limiter]")
{
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        LimiterParams p;
        p.ceilingDbtp = -1.0;
        p.driveDb = 12.0;
        const double tp = testsig::truePeakOf (renderLimited (testsig::musicLike (sr, 6.0, 0.0), p));
        INFO ("sample rate " << sr << ": " << tp << " dBTP");
        CHECK (tp <= -0.95);
    }
}

TEST_CASE ("1 kHz at -23 dBFS reads -23 LUFS at every supported sample rate (EBU 3341 case 1)", "[validation][loudness]")
{
    for (double sr : { 88200.0, 176400.0, 192000.0 })
    {
        const auto s = measure (testsig::sine (sr, 2, 1000.0, -23.0, 20.0));
        INFO ("sample rate " << sr);
        CHECK_THAT (s.integratedLufs, WithinAbs (-23.0, 0.1));
    }
}

TEST_CASE ("Integrated loudness gates a -26 / -20 / -26 programme to -23 (EBU 3341 case 5)", "[validation][loudness]")
{
    auto clip = testsig::silence (48000.0, 2, 0.0);
    testsig::appendSine (clip, 1000.0, -26.0, 20.0);
    testsig::appendSine (clip, 1000.0, -20.0, 20.1);
    testsig::appendSine (clip, 1000.0, -26.0, 20.0);
    CHECK_THAT (measure (clip).integratedLufs, WithinAbs (-23.0, 0.1));
}

TEST_CASE ("Short-term loudness of a 3 s on/off pattern holds -23 LUFS", "[validation][loudness]")
{
    // EBU 3341 short-term case structure: 1.34 s at -20 dBFS then 1.66 s at -30 dBFS, repeated. Every
    // 3 s window holds one whole period, whose mean power is -23 LUFS.
    for (double sr : { 44100.0, 48000.0 })
    {
        auto clip = testsig::silence (sr, 2, 0.0);
        for (int r = 0; r < 20; ++r)
        {
            testsig::appendSine (clip, 1000.0, -20.0, 1.34);
            testsig::appendSine (clip, 1000.0, -30.0, 1.66);
        }
        LoudnessMeter m;
        m.prepare (sr, 2);
        double lo = 1e9, hi = -1e9;
        readAfter (m, clip, 0, static_cast<int> (3.3 * sr), true, lo, hi);    // fill the 3 s window
        lo = 1e9; hi = -1e9;
        readAfter (m, clip, static_cast<int> (3.3 * sr), clip.numSamples(), true, lo, hi);
        INFO ("sample rate " << sr << ": short-term " << lo << " .. " << hi);
        CHECK_THAT (lo, WithinAbs (-23.0, 0.1));
        CHECK_THAT (hi, WithinAbs (-23.0, 0.1));
    }
}

TEST_CASE ("Momentary loudness of a 0.4 s on/off pattern holds -23 LUFS", "[validation][loudness]")
{
    // EBU 3341 momentary case structure: 0.18 s at -20 dBFS then 0.22 s at -30 dBFS, repeated; every
    // 400 ms window holds one whole period. Read at 100 ms steps, as the plug-in's meter updates.
    auto clip = testsig::silence (48000.0, 2, 0.0);
    for (int r = 0; r < 50; ++r)
    {
        testsig::appendSine (clip, 1000.0, -20.0, 0.18);
        testsig::appendSine (clip, 1000.0, -30.0, 0.22);
    }
    LoudnessMeter m;
    m.prepare (48000.0, 2);
    double lo = 1e9, hi = -1e9;
    readAfter (m, clip, 0, 48000, false, lo, hi);
    lo = 1e9; hi = -1e9;
    readAfter (m, clip, 48000, clip.numSamples(), false, lo, hi);
    INFO ("momentary " << lo << " .. " << hi);
    CHECK_THAT (lo, WithinAbs (-23.0, 0.1));
    CHECK_THAT (hi, WithinAbs (-23.0, 0.1));
}

TEST_CASE ("Loudness range of a 20 LU step (EBU 3342 case 3)", "[validation][lra]")
{
    auto clip = testsig::silence (48000.0, 2, 0.0);
    testsig::appendSine (clip, 1000.0, -40.0, 20.0);
    testsig::appendSine (clip, 1000.0, -20.0, 20.0);
    CHECK_THAT (measure (clip).loudnessRangeLu, WithinAbs (20.0, 1.0));
}

TEST_CASE ("Loudness range of a five-step programme (EBU 3342 case 4)", "[validation][lra]")
{
    auto clip = testsig::silence (48000.0, 2, 0.0);
    for (double level : { -50.0, -35.0, -20.0, -35.0, -50.0 })
        testsig::appendSine (clip, 1000.0, level, 20.0);
    CHECK_THAT (measure (clip).loudnessRangeLu, WithinAbs (15.0, 1.0));
}

#include "../tools/blindtest/BlindTestCore.h"

TEST_CASE ("Blind test: every sample plays at the same loudness, turned down only, never clipping", "[validation][blindtest]")
{
    // A quiet mix, a loud master and a clipping one: all land on the mix's loudness...
    std::vector<blindtest::Measured> files { { -16.9, -6.1 }, { -8.0, -1.0 }, { 1.2, 12.0 } };
    auto g = blindtest::matchGains (files);
    for (size_t i = 0; i < files.size(); ++i)
    {
        CHECK (g[i] <= 0.0);
        CHECK_THAT (files[i].integratedLufs + g[i], WithinAbs (-16.9, 1e-9));
        CHECK (files[i].truePeakDbtp + g[i] <= -1.0 + 1e-9);
    }
    // ...unless that would leave a peak above -1 dBTP: then all move down together, still matched.
    files = { { -10.0, 0.5 }, { -6.0, -1.0 } };
    g = blindtest::matchGains (files);
    CHECK_THAT (files[0].integratedLufs + g[0], WithinAbs (files[1].integratedLufs + g[1], 1e-9));
    CHECK_THAT (std::max (files[0].truePeakDbtp + g[0], files[1].truePeakDbtp + g[1]), WithinAbs (-1.0, 1e-9));
    // Silence is left alone; the shuffle covers every file once.
    files = { { -INFINITY, -INFINITY }, { -12.0, -3.0 } };
    g = blindtest::matchGains (files);
    CHECK (g[0] == 0.0);
    CHECK (g[1] == 0.0);
    auto order = blindtest::shuffledOrder (8, 123);
    std::sort (order.begin(), order.end());
    for (int i = 0; i < 8; ++i)
        CHECK (order[(size_t) i] == i);
}

#include "../tools/blindtest/ShootoutReport.h"

TEST_CASE ("Shoot-out: the fairness check matches the masters to Polisher's and finds the mix by name", "[validation][blindtest]")
{
    CHECK (blindtest::namedAsMix ("my mix.wav"));
    CHECK (blindtest::namedAsMix ("Trap_MIX_v2.wav"));
    CHECK_FALSE (blindtest::namedAsMix ("Mixdown.wav"));
    CHECK_FALSE (blindtest::namedAsMix ("remixed.wav"));

    const std::vector<std::string> names { "my mix.wav", "polisher.wav", "ozone.wav", "tdr.wav" };
    std::vector<blindtest::Measured> files { { -17.0, -1.0 }, { -9.7, -1.5 }, { -10.6, -1.5 }, { -9.0, -1.5 } };
    auto f = blindtest::checkFairness (files, names);
    CHECK (f.mixes == std::vector<size_t> { 0 });
    CHECK (f.masters == std::vector<size_t> { 1, 2, 3 });
    CHECK (f.reference == 1);
    CHECK_THAT (f.spreadLu, WithinAbs (1.6, 1e-9));
    CHECK_FALSE (f.fair());
    auto text = blindtest::fairnessText (f, files, names);
    INFO (text);
    CHECK (text.find ("my mix.wav (-17.0 LUFS): your unmastered mix") != std::string::npos);
    CHECK (text.find ("polisher.wav (-9.7 LUFS): the one the others are matched to") != std::string::npos);
    CHECK (text.find ("ozone.wav (-10.6 LUFS): needs 0.9 LU more") != std::string::npos);
    CHECK (text.find ("tdr.wav (-9.0 LUFS): 0.7 LU too loud") != std::string::npos);
    CHECK (text.find ("Sample") == std::string::npos);   // never a sample letter: it goes on the score sheet

    // Matched loudness but a different ceiling (Auto-Master's -1.5 vs another tool's -1.0): not fair either.
    files = { { -17.0, -1.0 }, { -9.7, -1.5 }, { -9.6, -1.0 }, { -9.8, -1.5 } };
    f = blindtest::checkFairness (files, names);
    CHECK (f.loudnessFair());
    CHECK_FALSE (f.peaksFair());
    CHECK (blindtest::fairnessText (f, files, names).find ("ceilings differ") != std::string::npos);

    // 0.54 LU shows as 0.5, so it counts as 0.5: fair.
    files = { { -9.70, -1.0 }, { -10.24, -1.0 } };
    f = blindtest::checkFairness (files, { "polisher.wav", "ozone.wav" });
    CHECK (f.fair());
    CHECK (blindtest::fairnessText (f, files, { "polisher.wav", "ozone.wav" }).rfind ("Fair fight", 0) == 0);
}

TEST_CASE ("Shoot-out: the measurements show punch, tone and width differences", "[validation][blindtest]")
{
    const double sr = 48000.0;
    const auto beat = testsig::trapLike (sr, 12.0);
    const auto r = blindtest::analyse (beat);
    CHECK_THAT (r.lufs, WithinAbs (measure (beat).integratedLufs, 1e-9));
    REQUIRE (std::isfinite (r.kickPunchDb));
    REQUIRE (std::isfinite (r.snarePunchDb));
    const auto gained = [] (polisher::AudioClip c, double db)
    {
        const auto g = static_cast<float> (std::pow (10.0, db / 20.0));
        for (auto& ch : c.channels) for (auto& v : ch) v *= g;
        return c;
    };
    const auto atLoudness = [&] (const polisher::AudioClip& c, double lufs) { return gained (c, lufs - measure (c).integratedLufs); };

    // Only the sound counts, not the level: the same beat 6 dB down reads the same punch and tone.
    const auto rq = blindtest::analyse (gained (beat, -6.0));
    CHECK_THAT (rq.kickPunchDb, WithinAbs (r.kickPunchDb, 0.01));
    CHECK_THAT (rq.bandDb[2], WithinAbs (r.bandDb[2], 0.01));

    // Limited 6 dB harder, at the same loudness as a gentle master: less punch.
    LimiterParams gentle, hard;
    gentle.driveDb = 2.0;
    hard.driveDb = 8.0;
    const auto soft = renderLimited (beat, gentle);
    const auto squashed = atLoudness (renderLimited (beat, hard), measure (soft).integratedLufs);
    const auto rs = blindtest::analyse (soft), rh = blindtest::analyse (squashed);
    INFO ("kick punch: gentle " << rs.kickPunchDb << ", hard " << rh.kickPunchDb << "; snare: gentle " << rs.snarePunchDb << ", hard " << rh.snarePunchDb);
    CHECK (rh.kickPunchDb < rs.kickPunchDb - 0.5);
    CHECK (rh.snarePunchDb < rs.snarePunchDb - 0.5);

    // A plain hard clip at the same loudness chops the kick's peaks: its kick punch reads lower than the
    // limited master's (a peak-minus-loudness number would have rewarded the clipped one). The snare,
    // under the clip level, keeps its punch: the number doesn't hear distortion, the ears do.
    auto clipped = gained (beat, 8.0);
    for (auto& ch : clipped.channels)
        for (auto& v : ch) v = std::clamp (v, -0.891f, 0.891f);
    const auto rc = blindtest::analyse (atLoudness (clipped, rh.lufs));
    INFO ("hard clip: kick " << rc.kickPunchDb << ", snare " << rc.snarePunchDb << ", true peak " << rc.truePeakDbtp
          << "; limited: kick " << rh.kickPunchDb << ", snare " << rh.snarePunchDb << ", true peak " << rh.truePeakDbtp);
    CHECK (rc.kickPunchDb < rh.kickPunchDb);

    // +6 dB at 5 kHz (a wide bell): much more Presence against the loudness.
    auto boosted = beat;
    for (auto& ch : boosted.channels)
    {
        polisher::Biquad bell;
        polisher::design::apply (bell, polisher::design::bell (5000.0, 6.0, 0.7, sr));
        for (auto& v : ch) v = static_cast<float> (bell.process (v));
    }
    const auto rb = blindtest::analyse (boosted);
    INFO ("Pres " << r.bandDb[4] << " -> " << rb.bandDb[4] << " dB, Sub " << r.bandDb[0] << " -> " << rb.bandDb[0] << " dB");
    CHECK (rb.bandDb[4] > r.bandDb[4] + 3.0);
    const auto table = blindtest::reportTable ({ r, rb });
    INFO (table);
    CHECK (table.find ("  A ") != std::string::npos);
    CHECK (table.find ("  B ") != std::string::npos);
    CHECK (table.find ("-0.0") == std::string::npos);

    // Width: the beat's hats differ between L and R; a mono copy has none; a 50 Hz tone in opposite
    // polarity on L / R makes the bass wide.
    CHECK (std::isfinite (r.widthDb));
    CHECK (r.bassWidthDb < -30.0);
    auto mono = beat;
    mono.channels[1] = mono.channels[0];
    CHECK (std::isinf (blindtest::analyse (mono).widthDb));
    auto wideBass = beat;
    for (size_t i = 0; i < wideBass.channels[0].size(); ++i)
    {
        const auto v = static_cast<float> (0.2 * std::sin (2.0 * std::numbers::pi * 50.0 * static_cast<double> (i) / sr));
        wideBass.channels[0][i] += v;
        wideBass.channels[1][i] -= v;
    }
    CHECK (blindtest::analyse (wideBass).bassWidthDb > -10.0);
}

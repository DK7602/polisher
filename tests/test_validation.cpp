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
                // 0 dBFS samples, up to +3 dBTP between them at fs / 4 with this phase.
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

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Signals.h"
#include "StemNetReference.h"
#include "polisher/AutoMaster.h"
#include "polisher/FilterDesign.h"
#include "polisher/LoudnessTargeting.h"
#include "polisher/StemEQ.h"

#include <cmath>
#include <cstdint>
#include <numbers>

using namespace polisher;
using Catch::Matchers::WithinAbs;

namespace {
// Same signal as test_signal() in tools/stem/export.py: 110 Hz tone (left-heavy), 2.5 kHz tone
// gated every 0.25 s (right-heavy), white noise from an integer hash; float32.
AudioClip testSignal (double sr, double seconds)
{
    AudioClip c = testsig::silence (sr, 2, seconds);
    const auto quarter = static_cast<std::int64_t> (sr / 4);
    for (std::int64_t n = 0; n < c.numSamples(); ++n)
    {
        const auto h = static_cast<std::uint64_t> (n) * 2654435761ull + 12345ull;
        const double noise = (static_cast<double> (h % 4294967296ull) / 4294967296.0 - 0.5) * 0.1;
        const double t = static_cast<double> (n) / sr;
        const double tone = 0.3 * std::sin (2.0 * std::numbers::pi * 110.0 * t);
        const double hi = 0.2 * std::sin (2.0 * std::numbers::pi * 2500.0 * t) * static_cast<double> ((n / quarter) % 2);
        c.channels[0][static_cast<size_t> (n)] = static_cast<float> (0.8 * tone + 0.3 * hi + noise);
        c.channels[1][static_cast<size_t> (n)] = static_cast<float> (0.4 * tone + 0.9 * hi + noise);
    }
    return c;
}

AudioClip run (StemEQ& s, AudioClip c)
{
    float* ch[2] = { c.channels[0].data(), c.channels[1].data() };
    s.process (ch, 2, c.numSamples());
    return c;
}

double energy (const std::vector<float>& x, size_t from, size_t to)
{
    double e = 0.0;
    for (size_t i = from; i < to; ++i) e += static_cast<double> (x[i]) * x[i];
    return e;
}
}

TEST_CASE ("Stem EQ latency is NFFT + 3 hops at every rate", "[stemeq]")
{
    CHECK (StemEQ::latencyFor (44100.0) == 1792);
    CHECK (StemEQ::latencyFor (48000.0) == 1792);
    CHECK (StemEQ::latencyFor (88200.0) == 3584);
    CHECK (StemEQ::latencyFor (96000.0) == 3584);
    CHECK (StemEQ::latencyFor (176400.0) == 7168);
    CHECK (StemEQ::latencyFor (192000.0) == 7168);
    StemEQ s; s.prepare (48000.0, 2);
    CHECK (s.latencySamples() == 1792);
    // About 40 ms at every rate (the model sees the same Hz per bin and frames per second).
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        const double ms = 1000.0 * StemEQ::latencyFor (sr) / sr;
        CHECK ((ms > 36.0 && ms < 42.0));
    }
}

TEST_CASE ("Stem EQ Bass +6 lifts an 808 as much at 176.4 / 192 kHz as at 44.1 kHz", "[stemeq]")
{
    // At NFFT 2048 the model ran ~4x faster in time than it was trained to and hardly moved the bass
    // (+0.7 dB at 192 kHz); the NFFT must grow with the rate.
    auto lift = [] (double sr)
    {
        AudioClip in = testsig::musicLike (sr, 4.0, -18.0, 3);
        for (auto& ch : in.channels)                                   // a sustained 808: 45 Hz notes, 1 s each
            for (size_t i = 0; i < ch.size(); ++i)
            {
                const double t = static_cast<double> (i) / sr, tn = std::fmod (t, 1.0);
                ch[i] += static_cast<float> (0.3 * std::exp (-tn * 1.2) * std::min (1.0, tn * 200.0) * std::sin (2.0 * std::numbers::pi * 45.0 * t));
            }
        StemEQParams p; p.gainDb[kStemBass] = 6.0;
        const AudioClip out = renderStemEq (in, p);                    // lined up with the input
        auto tone = [&] (const AudioClip& c)
        {
            double cs = 0.0, ss = 0.0;
            for (size_t i = static_cast<size_t> (2.0 * sr); i < static_cast<size_t> (3.0 * sr); ++i)
            {
                const double ph = 2.0 * std::numbers::pi * 45.0 * static_cast<double> (i) / sr;
                cs += c.channels[0][i] * std::cos (ph);
                ss += c.channels[0][i] * std::sin (ph);
            }
            return cs * cs + ss * ss;
        };
        return 10.0 * std::log10 (tone (out) / tone (in));
    };
    const double base = lift (44100.0);
    for (double sr : { 176400.0, 192000.0 })
    {
        const double l = lift (sr);
        UNSCOPED_INFO ("bass +6: 808 " << base << " dB at 44.1 kHz, " << l << " dB at " << sr);
        CHECK (l > 4.5);
        CHECK (std::abs (l - base) < 1.0);
    }
}

TEST_CASE ("Stem EQ at 0 dB (or off) is an exact delayed pass-through", "[stemeq]")
{
    for (bool enabled : { true, false })
    {
        StemEQ s; s.prepare (48000.0, 2);
        StemEQParams p; p.enabled = enabled;
        if (! enabled) p.gainDb = { 6.0, -6.0, 3.0, 0.0 };   // off ignores the gains
        s.setParams (p); s.reset();
        const auto in = testsig::musicLike (48000.0, 2.0, -6.0);
        const auto out = run (s, in);
        const auto L = static_cast<size_t> (s.latencySamples());
        bool exact = true;
        for (int c = 0; c < 2; ++c)
            for (size_t i = 0; i < out.channels[0].size(); ++i)
                exact = exact && out.channels[static_cast<size_t> (c)][i] == (i < L ? 0.0f : in.channels[static_cast<size_t> (c)][i - L]);
        CHECK (exact);
        CHECK_FALSE (s.isActive());
        CHECK (s.framesAnalysed() > 300);   // the model keeps running while neutral
    }
}

TEST_CASE ("Stem EQ model matches PyTorch frame by frame", "[stemeq]")
{
    const double sr = stemref::kSampleRate;
    StemEQ s; s.prepare (sr, 2);
    const auto clip = testSignal (sr, 2.0);
    const int N = StemEQ::fftSizeFor (sr), H = N / 4;
    std::vector<double> l (static_cast<size_t> (N)), r (static_cast<size_t> (N));
    std::array<float, StemNet::kIn> feat {};
    std::array<float, kStems * StemNet::kBands> shares {};
    StemNet net;
    double worstFeat = 0.0, worstShare = 0.0;
    int checked = 0;
    // Frames as tools/stem/common.py cuts them: frame k = samples [k H, k H + N).
    for (int k = 0; k + 1 < 1 + (clip.numSamples() - N) / H; ++k)
    {
        for (int i = 0; i < N; ++i)
        {
            l[static_cast<size_t> (i)] = clip.channels[0][static_cast<size_t> (k * H + i)];
            r[static_cast<size_t> (i)] = clip.channels[1][static_cast<size_t> (k * H + i)];
        }
        s.computeFeatures (l.data(), r.data(), feat.data());
        const bool ready = net.push (feat.data(), shares.data());
        for (int f = 0; f < 3; ++f)
        {
            if (k == stemref::kFrames[f])
                for (int i = 0; i < StemNet::kIn; ++i)
                    worstFeat = std::max (worstFeat, static_cast<double> (std::abs (feat[static_cast<size_t> (i)] - stemref::features[f * StemNet::kIn + i])));
            if (ready && k - StemNet::kLookahead == stemref::kFrames[f])
            {
                for (int i = 0; i < kStems * StemNet::kBands; ++i)
                    worstShare = std::max (worstShare, static_cast<double> (std::abs (shares[static_cast<size_t> (i)] - stemref::shares[f * kStems * StemNet::kBands + i])));
                ++checked;
            }
        }
    }
    CHECK (checked == 3);
    CHECK (worstFeat < 1.0e-3);     // log10 energies
    CHECK (worstShare < 1.0e-3);    // shares 0..1
    UNSCOPED_INFO ("worst feature diff " << worstFeat << ", worst share diff " << worstShare);
}

TEST_CASE ("Stem EQ: the same gain on every stem is a plain gain", "[stemeq]")
{
    StemEQ s; s.prepare (48000.0, 2);
    StemEQParams p; p.gainDb = { 6.0, 6.0, 6.0, 6.0 };
    s.setParams (p); s.reset();
    const auto in = testsig::musicLike (48000.0, 2.0, -12.0);
    const auto out = run (s, in);
    const auto L = static_cast<size_t> (s.latencySamples());
    const double g = std::pow (10.0, 6.0 / 20.0);
    double worst = 0.0;
    for (int c = 0; c < 2; ++c)
        for (size_t i = L + 4096; i < out.channels[0].size(); ++i)
            worst = std::max (worst, std::abs (out.channels[static_cast<size_t> (c)][i] - g * in.channels[static_cast<size_t> (c)][i - L]));
    CHECK (worst < 1.0e-5);
}

TEST_CASE ("Stem EQ moves follow the model's shares", "[stemeq]")
{
    // Bass +6 dB must lift the 110 Hz tone far more than the 2.5 kHz tone; drums / vocals / other
    // moves stay within the +/-12 dB range, and nothing blows up.
    const double sr = 44100.0;
    const auto in = testSignal (sr, 3.0);
    auto lowHigh = [&] (const AudioClip& c) {
        // crude split: 110 Hz tone vs everything else, by correlation with the tone
        double lo = 0.0, all = 0.0;
        const size_t a = static_cast<size_t> (sr), b = static_cast<size_t> (3.0 * sr) - 4096;
        double cs = 0.0, ss = 0.0;
        for (size_t i = a; i < b; ++i)
        {
            const double ph = 2.0 * std::numbers::pi * 110.0 * static_cast<double> (i) / sr;
            cs += c.channels[0][i] * std::cos (ph);
            ss += c.channels[0][i] * std::sin (ph);
            all += static_cast<double> (c.channels[0][i]) * c.channels[0][i];
        }
        lo = 2.0 * (cs * cs + ss * ss) / static_cast<double> (b - a);
        return std::pair { lo, all - lo };
    };
    StemEQ ref; ref.prepare (sr, 2);
    const auto [lo0, rest0] = lowHigh (run (ref, in));

    StemEQ s; s.prepare (sr, 2);
    StemEQParams p; p.gainDb[kStemBass] = 6.0;
    s.setParams (p); s.reset();
    const auto [lo1, rest1] = lowHigh (run (s, in));
    const double loGain = 10.0 * std::log10 (lo1 / lo0), restGain = 10.0 * std::log10 (rest1 / rest0);
    UNSCOPED_INFO ("bass +6: 110 Hz tone " << loGain << " dB, rest " << restGain << " dB");
    CHECK (loGain > 1.0);
    CHECK (loGain <= 6.01);
    CHECK (loGain > restGain + 0.5);

    for (int stem = 0; stem < kStems; ++stem)
        for (double db : { -12.0, 12.0 })
        {
            StemEQ t; t.prepare (sr, 2);
            StemEQParams q; q.gainDb[static_cast<size_t> (stem)] = db;
            t.setParams (q); t.reset();
            const auto out = run (t, in);
            const double ratio = 10.0 * std::log10 (energy (out.channels[0], 44100, 120000) / energy (in.channels[0], 44100 - 1792, 120000 - 1792));
            CHECK (std::isfinite (ratio));
            CHECK (ratio >= -12.01);
            CHECK (ratio <= 12.01);
        }
}

TEST_CASE ("Stem EQ switches on and off without clicks", "[stemeq]")
{
    const double sr = 48000.0;
    StemEQ s; s.prepare (sr, 2);
    auto clip = testsig::sine (sr, 2, 200.0, -6.0, 3.0);
    const int block = 256;
    double worstStep = 0.0;
    float prev = 0.0f;
    for (int start = 0; start < clip.numSamples(); start += block)
    {
        StemEQParams p;
        if (start > sr && start < 2.0 * sr) p.gainDb = { 12.0, 12.0, 12.0, 12.0 };   // on for one second
        s.setParams (p);
        float* ch[2] = { clip.channels[0].data() + start, clip.channels[1].data() + start };
        const int n = std::min (block, clip.numSamples() - start);
        s.process (ch, 2, n);
        for (int i = 0; i < n; ++i)
        {
            if (start + i > s.latencySamples()) worstStep = std::max (worstStep, static_cast<double> (std::abs (ch[0][i] - prev)));
            prev = ch[0][i];
        }
    }
    // A 200 Hz sine at up to +12 dB (amplitude ~2) moves at most 2 * 2 pi 200 / 48000 ~ 0.053 per sample.
    CHECK (worstStep < 0.06);
}

TEST_CASE ("Stem EQ handles mono and extra channels with the same latency", "[stemeq]")
{
    StemEQ s; s.prepare (48000.0, 4);
    StemEQParams p; p.gainDb = { 0.0, 0.0, 6.0, 0.0 };
    s.setParams (p); s.reset();
    auto clip = testsig::musicLike (48000.0, 1.0, -12.0);
    clip.channels.push_back (clip.channels[0]);
    clip.channels.push_back (clip.channels[1]);
    const auto in = clip;
    float* ch[4] = { clip.channels[0].data(), clip.channels[1].data(), clip.channels[2].data(), clip.channels[3].data() };
    s.process (ch, 4, clip.numSamples());
    const auto L = static_cast<size_t> (s.latencySamples());
    bool exact = true;
    for (int c = 2; c < 4; ++c)
        for (size_t i = L; i < clip.channels[0].size(); ++i)
            exact = exact && clip.channels[static_cast<size_t> (c)][i] == in.channels[static_cast<size_t> (c)][i - L];
    CHECK (exact);   // channels 3 and 4 are only delayed

    StemEQ m; m.prepare (48000.0, 1);
    m.setParams (p); m.reset();
    auto mono = testsig::musicLike (48000.0, 1.0, -12.0);
    mono.channels.resize (1);
    float* mc[1] = { mono.channels[0].data() };
    m.process (mc, 1, mono.numSamples());
    double e = energy (mono.channels[0], L, mono.channels[0].size());
    CHECK (std::isfinite (e));
    CHECK (e > 0.0);
}

TEST_CASE ("Stem EQ offline render lines up with the input and matches real time", "[stemeq]")
{
    const auto in = testsig::musicLike (48000.0, 2.0, -12.0);
    StemEQParams p;
    CHECK (renderStemEq (in, p).channels == in.channels);           // neutral: an exact copy

    p.gainDb = { 0.0, 3.0, -4.0, 0.0 };
    const auto off = renderStemEq (in, p);
    StemEQ s; s.prepare (48000.0, 2); s.setParams (p); s.reset();
    const auto rt = run (s, in);
    const auto L = static_cast<size_t> (s.latencySamples());
    double worst = 0.0;
    for (int c = 0; c < 2; ++c)
        for (size_t i = 0; i + L < rt.channels[0].size(); ++i)
            worst = std::max (worst, static_cast<double> (std::abs (off.channels[static_cast<size_t> (c)][i] - rt.channels[static_cast<size_t> (c)][i + L])));
    CHECK (worst == 0.0);
}

TEST_CASE ("Stem EQ meter credits nothing during silence, and something during music", "[stemeq]")
{
    StemEQ s; s.prepare (48000.0, 2);
    std::array<double, kStems> e {};
    run (s, testsig::silence (48000.0, 2, 1.0));
    s.takeStemEnergies (e);
    CHECK (e[0] + e[1] + e[2] + e[3] == 0.0);
    run (s, testsig::musicLike (48000.0, 1.0, -12.0));
    s.takeStemEnergies (e);
    CHECK (e[0] + e[1] + e[2] + e[3] > 0.0);
}

TEST_CASE ("Auto-Master sets the Tonal EQ for the mix before the user's Stem EQ moves", "[stemeq]")
{
    // The Tonal EQ comes before Stem EQ in the plug-in: Bass +12 must not read as a sub-heavy mix
    // that the Tonal EQ then cuts back down.
    const auto mix = testsig::musicLike (44100.0, 14.0, -16.0, 3);
    AutoMasterSettings plain, boosted;
    boosted.stem.gainDb[kStemBass] = 12.0;
    const auto r0 = autoMaster (mix, plain), r12 = autoMaster (mix, boosted);
    REQUIRE (r0.ok);
    REQUIRE (r12.ok);
    for (size_t b = 0; b < r0.chain.eq.gainDb.size(); ++b)
    {
        CHECK_THAT (r12.analysis.bandDb[b], WithinAbs (r0.analysis.bandDb[b], 1e-9));
        CHECK (r12.chain.eq.gainDb[b] == r0.chain.eq.gainDb[b]);
    }
    CHECK (r12.chain.stem.gainDb == boosted.stem.gainDb);
}

TEST_CASE ("Stem levels and Auto-Master's vocal suggestion", "[stemeq]")
{
    // The rule: inside the genre's range -> no move; low -> up (max +3); loud -> down; instrumental -> none.
    CHECK (suggestVocalMoveDb (-3.0, Genre::trapHipHop) == 0.0);
    CHECK (suggestVocalMoveDb (-4.4, Genre::trapHipHop) == 0.0);
    CHECK (suggestVocalMoveDb (-5.0, Genre::trapHipHop) == 2.0);
    CHECK (suggestVocalMoveDb (-9.0, Genre::trapHipHop) == 3.0);
    CHECK (suggestVocalMoveDb (0.0, Genre::trapHipHop) == -2.0);
    CHECK (suggestVocalMoveDb (-20.0, Genre::trapHipHop) == 0.0);
    CHECK (suggestVocalMoveDb (-7.0, Genre::edm) == 0.0);

    // Measurement: silence hears nothing; music hears every stem at a finite level below the whole.
    for (double v : measureStemLevels (testsig::silence (48000.0, 2, 1.0))) CHECK (v == -100.0);
    const auto lv = measureStemLevels (testsig::musicLike (48000.0, 4.0, -12.0));
    double sum = 0.0;
    for (double v : lv) { CHECK (v <= 0.0); CHECK (v > -100.0); sum += std::pow (10.0, v / 10.0); }
    CHECK_THAT (sum, WithinAbs (1.0, 1e-6));

    // Auto-Master always reports on Stem EQ, keeps the user's setting, and only ever suggests.
    AutoMasterSettings s; s.stem.gainDb = { 1.5, 0.0, 0.0, 0.0 };
    const auto r = autoMaster (testsig::musicLike (48000.0, 14.0, -14.0), s);
    REQUIRE (r.ok);
    CHECK (r.chain.stem.gainDb == s.stem.gainDb);
    bool reported = false;
    for (const auto& x : r.reasons) reported = reported || x.module == "stem";
    CHECK (reported);
    if (r.hasStemSuggestion)
    {
        CHECK (r.stemSuggestion.gainDb[1] == 0.0);
        CHECK (std::abs (r.stemSuggestion.gainDb[0] - 1.5) <= 3.0);
    }
}

// ---------------------------------------------------------------------------------------------
// Keep loudness

namespace {
// A trap-like mix: pink noise and kicks (musicLike), a sustained 45 Hz 808 (1 s notes), a sung
// line (220 / 294 Hz harmonics with vibrato, in phrases) and eighth-note hats.
AudioClip trapMix (double sr, double seconds)
{
    AudioClip c = testsig::musicLike (sr, seconds, -20.0, 11);
    std::uint64_t h = 99;
    double vPhase = 0.0, hpL = 0.0, hpR = 0.0, prevL = 0.0, prevR = 0.0;
    for (size_t i = 0; i < c.channels[0].size(); ++i)
    {
        const double t = static_cast<double> (i) / sr, tn = std::fmod (t, 1.0);
        const double sub = 0.22 * std::exp (-tn * 1.2) * std::min (1.0, tn * 200.0) * std::sin (2.0 * std::numbers::pi * 45.0 * t);
        const double f0 = (std::fmod (t, 4.0) < 2.0 ? 220.0 : 294.0) * (1.0 + 0.01 * std::sin (2.0 * std::numbers::pi * 5.5 * t));
        vPhase += 2.0 * std::numbers::pi * f0 / sr;
        double voice = 0.0;
        for (int n = 1; n * f0 < 4000.0; ++n) voice += std::sin (n * vPhase) / n;
        const double phrase = std::fmod (t, 2.0) < 1.6 ? std::min (1.0, std::fmod (t, 2.0) * 20.0) : 0.0;
        voice *= 0.035 * phrase;
        h = h * 6364136223846793005ull + 1442695040888963407ull;
        const double wL = static_cast<double> (h >> 11) / 9007199254740992.0 - 0.5;
        h = h * 6364136223846793005ull + 1442695040888963407ull;
        const double wR = static_cast<double> (h >> 11) / 9007199254740992.0 - 0.5;
        hpL = 0.6 * (hpL + wL - prevL); prevL = wL;                   // crude high-pass
        hpR = 0.6 * (hpR + wR - prevR); prevR = wR;
        const double hat = 0.12 * std::exp (-std::fmod (t, 0.125) * 60.0);
        c.channels[0][i] += static_cast<float> (sub + voice + hat * hpL);
        c.channels[1][i] += static_cast<float> (sub + voice + hat * hpR);
    }
    return c;
}

// Energy (dB) of both channels in a band (two 2nd-order Butterworth high- and low-passes; 0 = open),
// from fromS seconds on.
double bandDb (const AudioClip& c, double lo, double hi, double fromS = 1.0)
{
    double e = 0.0;
    for (const auto& ch : c.channels)
    {
        Biquad f[4];
        design::apply (f[0], design::butterworth (true, lo > 0.0 ? lo : 1.0, c.sampleRate));
        design::apply (f[1], design::butterworth (true, lo > 0.0 ? lo : 1.0, c.sampleRate));
        design::apply (f[2], design::butterworth (false, hi > 0.0 ? hi : 0.45 * c.sampleRate, c.sampleRate));
        design::apply (f[3], design::butterworth (false, hi > 0.0 ? hi : 0.45 * c.sampleRate, c.sampleRate));
        for (size_t i = 0; i < ch.size(); ++i)
        {
            double v = ch[i];
            for (auto& b : f) v = b.process (v);
            if (static_cast<double> (i) >= fromS * c.sampleRate) e += v * v;
        }
    }
    return 10.0 * std::log10 (e + 1.0e-30);
}
}

TEST_CASE ("Stem EQ Keep loudness: moves change the balance, not the loudness", "[stemeq][keep]")
{
    const double sr = 48000.0;
    const auto in = trapMix (sr, 8.0);
    const double inLufs = measure (in).integratedLufs;
    for (int stem = 0; stem < kStems; ++stem)
        for (double db : { -12.0, -6.0, 6.0, 12.0 })
        {
            StemEQParams p; p.gainDb[static_cast<size_t> (stem)] = db;
            const double raw = measure (renderStemEq (in, p)).integratedLufs - inLufs;
            p.keepLoudness = true;
            const double kept = measure (renderStemEq (in, p)).integratedLufs - inLufs;
            UNSCOPED_INFO ("stem " << stem << " " << db << " dB: loudness change " << raw << " LU, with Keep loudness " << kept << " LU");
            CHECK (std::abs (kept) < 0.5);
        }

    // Without it a big Bass move changes the loudness a lot; with it the 808 still moves against the rest.
    StemEQParams p; p.gainDb[kStemBass] = 6.0;
    const auto off = renderStemEq (in, p);
    p.keepLoudness = true;
    const auto on = renderStemEq (in, p);
    const double offLu = measure (off).integratedLufs - inLufs, onLu = measure (on).integratedLufs - inLufs;
    const double lowOn = bandDb (on, 0.0, 80.0) - bandDb (in, 0.0, 80.0), restOn = bandDb (on, 300.0, 16000.0) - bandDb (in, 300.0, 16000.0);
    UNSCOPED_INFO ("bass +6: loudness off " << offLu << " LU, on " << onLu << " LU; with keep: < 80 Hz " << lowOn << " dB, 300 Hz-16 kHz " << restOn << " dB");
    CHECK (offLu > 2.0);
    CHECK (std::abs (onLu) < 0.5);
    CHECK (lowOn - restOn > 3.0);
}

TEST_CASE ("Stem EQ Keep loudness: exact pass-through at 0 dB, offline = real time", "[stemeq][keep]")
{
    const double sr = 48000.0;
    const auto in = trapMix (sr, 3.0);
    StemEQParams p; p.keepLoudness = true;
    CHECK (renderStemEq (in, p).channels == in.channels);
    {
        StemEQ s; s.prepare (sr, 2); s.setParams (p); s.reset();
        const auto out = run (s, in);
        const auto L = static_cast<size_t> (s.latencySamples());
        bool exact = true;
        for (int c = 0; c < 2; ++c)
            for (size_t i = 0; i < out.channels[0].size(); ++i)
                exact = exact && out.channels[static_cast<size_t> (c)][i] == (i < L ? 0.0f : in.channels[static_cast<size_t> (c)][i - L]);
        CHECK (exact);
        CHECK (s.keepLevelDb() == 0.0);
    }

    p.gainDb = { 2.0, -3.0, 6.0, 0.0 };
    const auto off = renderStemEq (in, p);
    StemEQ s; s.prepare (sr, 2); s.setParams (p); s.reset();
    AudioClip rt = in;
    for (int start = 0; start < rt.numSamples(); start += 333)                          // odd block size
    {
        float* ch[2] = { rt.channels[0].data() + start, rt.channels[1].data() + start };
        s.process (ch, 2, std::min (333, rt.numSamples() - start));
    }
    const auto L = static_cast<size_t> (s.latencySamples());
    double worst = 0.0;
    for (int c = 0; c < 2; ++c)
        for (size_t i = 0; i + L < rt.channels[0].size(); ++i)
            worst = std::max (worst, static_cast<double> (std::abs (off.channels[static_cast<size_t> (c)][i] - rt.channels[static_cast<size_t> (c)][i + L])));
    CHECK (worst == 0.0);
    UNSCOPED_INFO ("keeping level " << s.keepLevelDb() << " dB");
    CHECK (s.keepLevelDb() < -0.5);   // bass +6 on an 808 mix: it is turning the stage down
}

TEST_CASE ("Stem EQ Keep loudness: no clicks when it is switched or the levels move", "[stemeq][keep]")
{
    const double sr = 48000.0;
    StemEQ s; s.prepare (sr, 2);
    // 200 Hz sine plus a 45 Hz one: the model hears bass and something else, so the gains differ.
    auto clip = testsig::sine (sr, 2, 200.0, -12.0, 6.0);
    for (auto& ch : clip.channels)
        for (size_t i = 0; i < ch.size(); ++i)
            ch[i] += static_cast<float> (0.25 * std::sin (2.0 * std::numbers::pi * 45.0 * static_cast<double> (i) / sr));
    const int block = 256;
    double worstStep = 0.0;
    float prev = 0.0f;
    for (int start = 0; start < clip.numSamples(); start += block)
    {
        const double t = start / sr;
        StemEQParams p;
        p.keepLoudness = std::fmod (t, 1.0) < 0.5;                                       // switched twice a second
        p.gainDb[kStemBass] = t < 2.0 ? 12.0 : t < 4.0 ? -12.0 : 0.0;                   // and big jumps in level
        p.gainDb[kStemOther] = t > 3.0 && t < 5.0 ? 12.0 : 0.0;
        s.setParams (p);
        float* ch[2] = { clip.channels[0].data() + start, clip.channels[1].data() + start };
        const int n = std::min (block, clip.numSamples() - start);
        s.process (ch, 2, n);
        for (int i = 0; i < n; ++i)
        {
            if (start + i > s.latencySamples()) worstStep = std::max (worstStep, static_cast<double> (std::abs (ch[0][i] - prev)));
            prev = ch[0][i];
        }
    }
    // The input moves at most 0.25 * 2 pi 200 / 48000 + 0.25 * 2 pi 45 / 48000 ~ 0.008 per sample; at
    // up to 4x gain (+12 dB) that is ~0.032: a click would jump far past it.
    UNSCOPED_INFO ("worst sample-to-sample step " << worstStep);
    CHECK (worstStep < 0.04);
}

TEST_CASE ("Stem EQ Keep loudness through the chain at a fixed Drive", "[stemeq][keep]")
{
    // The user's situation: a mix at about -17 LUFS, mastered to -9 LUFS, then Bass turned up with
    // the Drive left where it was.
    const double sr = 48000.0;
    const auto raw = trapMix (sr, 8.0);
    const auto in = withGain (raw, -17.0 - measure (raw).integratedLufs);
    MasterChainParams chain;
    chain.limiter.ceilingDbtp = -1.0;
    const auto target = findDriveForTarget (in, -9.0, chain.limiter);
    chain.driveDb = target.driveDb;
    const auto neutral = renderMaster (in, chain);
    const double low0 = bandDb (neutral, 30.0, 60.0), rest0 = bandDb (neutral, 300.0, 16000.0);
    struct Result { double low, rest, master, stage, stageTp, limiterDb, drive, lowAt9, restAt9; };
    auto moved = [&] (bool keep, double db)
    {
        MasterChainParams c = chain;
        c.stem.gainDb[kStemBass] = db;
        c.stem.keepLoudness = keep;
        const auto stage = renderPreDrive (in, c);
        const auto out = renderMaster (in, c);
        const auto st = measure (stage);
        const double master = measure (out).integratedLufs;
        c.driveDb = findDriveForTarget (stage, -9.0, chain.limiter).driveDb;      // what Auto-Master's search would pick
        const auto at9 = renderMaster (in, c);
        return Result { bandDb (out, 30.0, 60.0) - low0, bandDb (out, 300.0, 16000.0) - rest0, master, st.integratedLufs, st.truePeakDbtp,
                        st.integratedLufs + chain.driveDb - master, c.driveDb, bandDb (at9, 30.0, 60.0) - low0, bandDb (at9, 300.0, 16000.0) - rest0 };
    };
    const auto n = moved (false, 0.0);
    UNSCOPED_INFO ("neutral: drive " << chain.driveDb << " dB, master " << n.master << " LUFS, stage " << n.stage << " LUFS / " << n.stageTp << " dBTP, limiter takes " << n.limiterDb << " LU");
    for (double db : { 6.0, 12.0 })
    {
        const auto off = moved (false, db), on = moved (true, db);
        for (const auto& [name, r] : { std::pair { "off", off }, std::pair { "on ", on } })
            UNSCOPED_INFO ("bass +" << db << " keep " << name << ": 30-60 Hz " << r.low << " dB, 300 Hz-16 kHz " << r.rest << " dB, master " << r.master
                           << " LUFS; stage " << r.stage << " LUFS / " << r.stageTp << " dBTP; limiter takes " << r.limiterDb << " LU; drive for -9: " << r.drive
                           << " -> 30-60 Hz " << r.lowAt9 << " dB, 300 Hz-16 kHz " << r.restAt9 << " dB");
        // Keep loudness: the stage stays at the mix's loudness, the Maximizer works less, Auto-Master's
        // loudness search needs about the same Drive as before the move...
        CHECK (std::abs (on.stage - n.stage) < 0.5);
        CHECK (on.limiterDb < off.limiterDb - 1.0);
        CHECK (on.drive > off.drive + 3.0);
        // ...and the 808 moves against the rest at least as much as without it.
        CHECK ((on.low - on.rest) > (off.low - off.rest) - 0.5);
    }
}

TEST_CASE ("Model Hears: each stem's share is its share of the loudness, so it predicts what a move does", "[stemeq]")
{
    // Weighted like a loudness meter (K): Bass +6 on an 808 mix adds what its share says. A-weighted it
    // read 5.9 % bass for a move that adds 4.8 LU; raw power credits the sub with more than it adds.
    const double sr = 48000.0;
    for (int which = 0; which < 2; ++which)
    {
        const auto in = which == 0 ? trapMix (sr, 10.0) : testsig::trapLike (sr, 10.0);
        StemEQ s;
        s.prepare (sr, 2);
        s.reset();
        AudioClip c = in;
        for (int pos = 0; pos < c.numSamples(); pos += 512)
        {
            float* ch[2] = { c.channels[0].data() + pos, c.channels[1].data() + pos };
            s.process (ch, 2, std::min (512, c.numSamples() - pos));
            if (pos < static_cast<int> (2.0 * sr)) { std::array<double, kStems> skip {}; s.takeStemLoudness (skip); }   // settle first
        }
        std::array<double, kStems> l {}, e {};
        s.takeStemLoudness (l);
        s.takeStemEnergies (e);
        const double tl = l[0] + l[1] + l[2] + l[3], te = e[0] + e[1] + e[2] + e[3];
        REQUIRE (tl > 0.0);
        const double inLufs = measure (in).integratedLufs;
        for (int stem : { kStemVocals, kStemBass })
        {
            const double share = l[static_cast<size_t> (stem)] / tl;
            StemEQParams p; p.gainDb[static_cast<size_t> (stem)] = 6.0;
            const double moved = measure (renderStemEq (in, p)).integratedLufs - inLufs;
            const double predicted = 10.0 * std::log10 (share * std::pow (10.0, 0.6) + 1.0 - share);
            UNSCOPED_INFO ((which ? "trapLike" : "trapMix") << " stem " << stem << ": share " << 100.0 * share << " % (raw power "
                           << 100.0 * e[static_cast<size_t> (stem)] / te << " %) predicts +6 dB = " << predicted << " LU; it moved " << moved << " LU");
            CHECK (std::abs (predicted - moved) < 0.75);
        }
    }
    // Silence credits nothing.
    StemEQ quiet;
    quiet.prepare (44100.0, 2);
    run (quiet, testsig::silence (44100.0, 2, 1.0));
    std::array<double, kStems> l {};
    quiet.takeStemLoudness (l);
    CHECK (l[0] + l[1] + l[2] + l[3] == 0.0);
}

TEST_CASE ("Stem EQ Keep loudness: a quieter section after a loud one takes over within a few seconds", "[stemeq][keep]")
{
    // Bass +6 on a trap mix, then a breakdown 6 dB down without the 808 (vocal and hats). The loud part's
    // energy used to rule the ~2 s average for 8-10 s: the breakdown played -4.0 LU for 4 s, -2.3 LU after.
    const double sr = 48000.0;
    AudioClip song = trapMix (sr, 15.0);
    {
        AudioClip bd = trapMix (sr, 15.0);
        std::uint64_t h = 7;
        double vPhase = 0.0, hpL = 0.0, hpR = 0.0, prevL = 0.0, prevR = 0.0;
        for (size_t i = 0; i < bd.channels[0].size(); ++i)            // the trap mix without its bed and 808: vocal + hats, 6 dB down
        {
            const double t = static_cast<double> (i) / sr;
            const double f0 = (std::fmod (t, 4.0) < 2.0 ? 220.0 : 294.0) * (1.0 + 0.01 * std::sin (2.0 * std::numbers::pi * 5.5 * t));
            vPhase += 2.0 * std::numbers::pi * f0 / sr;
            double voice = 0.0;
            for (int n = 1; n * f0 < 4000.0; ++n) voice += std::sin (n * vPhase) / n;
            h = h * 6364136223846793005ull + 1442695040888963407ull;
            const double wL = static_cast<double> (h >> 11) / 9007199254740992.0 - 0.5;
            h = h * 6364136223846793005ull + 1442695040888963407ull;
            const double wR = static_cast<double> (h >> 11) / 9007199254740992.0 - 0.5;
            hpL = 0.6 * (hpL + wL - prevL); prevL = wL;
            hpR = 0.6 * (hpR + wR - prevR); prevR = wR;
            const double hat = 0.12 * std::exp (-std::fmod (t, 0.125) * 60.0);
            bd.channels[0][i] = static_cast<float> (0.5 * (0.035 * voice + hat * hpL));
            bd.channels[1][i] = static_cast<float> (0.5 * (0.035 * voice + hat * hpR));
        }
        for (size_t c = 0; c < 2; ++c) song.channels[c].insert (song.channels[c].end(), bd.channels[c].begin(), bd.channels[c].end());
    }
    StemEQParams p; p.gainDb[kStemBass] = 6.0; p.keepLoudness = true;
    StemEQ s;
    s.prepare (sr, 2);
    s.setParams (p);
    s.reset();
    AudioClip out = song;
    std::vector<std::pair<double, double>> keep;   // (time, Keeping level)
    for (int pos = 0; pos < out.numSamples(); pos += 480)
    {
        float* ch[2] = { out.channels[0].data() + pos, out.channels[1].data() + pos };
        s.process (ch, 2, std::min (480, out.numSamples() - pos));
        keep.emplace_back ((pos + 480) / sr, s.keepLevelDb());
    }
    auto keepAt = [&] (double t) { double v = 0.0; for (auto [tt, k] : keep) if (tt <= t) v = k; return v; };
    auto lu = [&] (const AudioClip& c, double a, double b)
    {
        AudioClip x;
        x.sampleRate = sr;
        for (const auto& ch : c.channels)
            x.channels.emplace_back (ch.begin() + static_cast<std::ptrdiff_t> (a * sr), ch.begin() + static_cast<std::ptrdiff_t> (b * sr));
        return measure (x).integratedLufs;
    };
    const double lat = s.latencySamples() / sr;
    const double loud = keepAt (14.0), seven = keepAt (22.0);
    const double first = lu (out, 15.0 + lat, 19.0 + lat) - lu (song, 15.0, 19.0), next = lu (out, 19.0 + lat, 23.0 + lat) - lu (song, 19.0, 23.0);
    UNSCOPED_INFO ("keeping " << loud << " dB in the loud part, " << seven << " dB 7 s into the breakdown; breakdown "
                   << first << " LU for its first 4 s, " << next << " LU for the next 4");
    CHECK (loud < -3.0);
    CHECK (seven > -1.2);         // was -2.2
    CHECK (first > -3.2);         // was -4.0
    CHECK (std::abs (next) < 1.0);   // was -2.3
    CHECK (std::abs (lu (out, 5.0 + lat, 15.0) - lu (song, 5.0, 15.0 - lat)) < 0.5);   // the loud part stays put
}

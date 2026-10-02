#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Signals.h"
#include "StemNetReference.h"
#include "polisher/AutoMaster.h"
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
    CHECK (StemEQ::latencyFor (96000.0) == 3584);
    StemEQ s; s.prepare (48000.0, 2);
    CHECK (s.latencySamples() == 1792);
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

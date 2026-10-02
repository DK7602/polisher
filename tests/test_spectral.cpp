#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Signals.h"
#include "polisher/LoudnessTargeting.h"
#include "polisher/StemEQ.h"

#include <array>

#include <cmath>
#include <numbers>

using namespace polisher;

namespace {
AudioClip runStage (const AudioClip& in, const ToneSteadyParams& ts, const ResonanceParams& rt, StemEQ* keep = nullptr)
{
    StemEQ local;
    StemEQ& s = keep ? *keep : local;
    s.prepare (in.sampleRate, in.numChannels());
    s.setToneSteady (ts);
    s.setResonance (rt);
    s.reset();
    AudioClip c = in;
    std::vector<float*> ch;
    for (auto& x : c.channels) ch.push_back (x.data());
    s.process (ch.data(), c.numChannels(), c.numSamples());
    return c;
}

// Level (dB) of a sine at f in channel 0 over [a, b), by correlation.
double toneDb (const AudioClip& c, double f, size_t a, size_t b, double delay = 0.0)
{
    double cs = 0.0, ss = 0.0;
    for (size_t i = a; i < b; ++i)
    {
        const double ph = 2.0 * std::numbers::pi * f * (static_cast<double> (i) - delay) / c.sampleRate;
        cs += c.channels[0][i] * std::cos (ph);
        ss += c.channels[0][i] * std::sin (ph);
    }
    return 10.0 * std::log10 (2.0 * (cs * cs + ss * ss) / static_cast<double> (b - a) / static_cast<double> (b - a) + 1e-30);
}

double energyDb (const std::vector<float>& x, size_t a, size_t b)
{
    double e = 0.0;
    for (size_t i = a; i < b; ++i) e += static_cast<double> (x[i]) * x[i];
    return 10.0 * std::log10 (e / static_cast<double> (b - a) + 1e-30);
}

// Pink-ish noise with a level step in a band: from `from` s on, noise around `hz` is added at `boostDb`.
AudioClip noiseWithBandStep (double sr, double seconds, double from, double hz, double boostDb)
{
    AudioClip c = testsig::musicLike (sr, seconds, -18.0, 3);
    std::mt19937 rng (11);
    std::normal_distribution<double> wn (0.0, 1.0);
    const double w0 = 2.0 * std::numbers::pi * hz / sr, q = 2.0, alpha = std::sin (w0) / (2.0 * q);
    const double b0 = alpha, a0 = 1.0 + alpha, a1 = -2.0 * std::cos (w0), a2 = 1.0 - alpha;   // band-pass
    const double g = std::pow (10.0, boostDb / 20.0) * 0.05;
    for (auto& ch : c.channels)
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (size_t i = 0; i < ch.size(); ++i)
        {
            const double x = wn (rng);
            const double y = (b0 * x - b0 * x2 - a1 * y1 - a2 * y2) / a0;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            if (static_cast<double> (i) >= from * sr) ch[i] += static_cast<float> (g * y);
        }
    }
    return c;
}
}

TEST_CASE ("Tone Steady and Resonance Tamer off are an exact delayed pass-through", "[spectral]")
{
    const auto in = testsig::musicLike (48000.0, 2.0, -12.0);
    ToneSteadyParams ts; ts.enabled = false; ts.amount = 100.0;
    ResonanceParams rt; rt.enabled = false; rt.amount = 100.0;
    StemEQ s;
    const auto out = runStage (in, ts, rt, &s);
    const auto L = static_cast<size_t> (s.latencySamples());
    bool exact = true;
    for (int c = 0; c < 2; ++c)
        for (size_t i = L; i < out.channels[0].size(); ++i)
            exact = exact && out.channels[static_cast<size_t> (c)][i] == in.channels[static_cast<size_t> (c)][i - L];
    CHECK (exact);
    CHECK (s.latencySamples() == StemEQ::latencyFor (48000.0));   // no extra latency for the new modules
}

TEST_CASE ("Resonance Tamer cuts a ringing tone, not the music around it", "[spectral]")
{
    const double sr = 48000.0;
    auto in = testsig::musicLike (sr, 4.0, -18.0, 5);
    for (auto& ch : in.channels)                                   // a steady 2.4 kHz ring well above the noise
        for (size_t i = 0; i < ch.size(); ++i)
            ch[i] += static_cast<float> (0.05 * std::sin (2.0 * std::numbers::pi * 2400.0 * static_cast<double> (i) / sr));
    ResonanceParams rt; rt.amount = 100.0;
    StemEQ s;
    const auto out = runStage (in, {}, rt, &s);
    const auto L = static_cast<double> (s.latencySamples());
    const size_t a = static_cast<size_t> (sr), b = static_cast<size_t> (3.5 * sr);
    const double toneIn = toneDb (in, 2400.0, a, b), toneOut = toneDb (out, 2400.0, a + static_cast<size_t> (L), b + static_cast<size_t> (L), L);
    UNSCOPED_INFO ("tone " << toneIn << " -> " << toneOut << " dB");
    CHECK (toneOut < toneIn - 4.0);

    double deepest = 0.0, at = 0.0;
    s.takeResonanceMeter (deepest, at);
    CHECK (deepest < -4.0);
    CHECK_THAT (at, Catch::Matchers::WithinAbs (2400.0, 150.0));

    // The same noise without the ring: almost untouched.
    const auto clean = testsig::musicLike (sr, 4.0, -18.0, 5);
    const auto cleanOut = runStage (clean, {}, rt);
    const double change = energyDb (cleanOut.channels[0], a + static_cast<size_t> (L), b + static_cast<size_t> (L)) - energyDb (clean.channels[0], a, b);
    UNSCOPED_INFO ("clean noise level change " << change << " dB");
    CHECK (std::abs (change) < 0.6);
}

TEST_CASE ("Resonance Tamer Focus keeps cuts in its range", "[spectral]")
{
    const double sr = 48000.0;
    auto in = testsig::musicLike (sr, 3.0, -18.0, 5);
    for (auto& ch : in.channels)
        for (size_t i = 0; i < ch.size(); ++i)
            ch[i] += static_cast<float> (0.05 * std::sin (2.0 * std::numbers::pi * 2400.0 * static_cast<double> (i) / sr));
    ResonanceParams rt; rt.amount = 100.0; rt.focus = ResonanceFocus::boxiness;   // 150-800 Hz: the 2.4 kHz ring is outside
    StemEQ s;
    runStage (in, {}, rt, &s);
    double deepest = 0.0, at = 0.0;
    s.takeResonanceMeter (deepest, at);
    CHECK ((deepest > -0.5 || at < 1300.0));
}

TEST_CASE ("Tone Steady pulls a passage that drifts back toward the song's balance", "[spectral]")
{
    const double sr = 48000.0;
    // 8 s of steady noise, then 3 s with the low mids (300 Hz) pushed up.
    const auto in = noiseWithBandStep (sr, 11.0, 8.0, 300.0, 12.0);
    const auto flat = noiseWithBandStep (sr, 11.0, 8.0, 300.0, -100.0);
    ToneSteadyParams ts; ts.amount = 100.0; ts.rangeDb = 6.0;
    StemEQ s;
    const auto out = runStage (in, ts, {}, &s);
    const auto L = static_cast<size_t> (s.latencySamples());
    // Low-mid energy in the drifted passage, with and without Tone Steady (after it has reacted).
    auto lowMid = [&] (const AudioClip& c, size_t off) {
        AudioClip x = c;   // crude 300 Hz measurement: band-limit by a 2nd-order band-pass
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0, e = 0;
        const double w0 = 2.0 * std::numbers::pi * 300.0 / sr, alpha = std::sin (w0) / 4.0;
        const double b0 = alpha, a0 = 1.0 + alpha, a1 = -2.0 * std::cos (w0), a2 = 1.0 - alpha;
        for (size_t i = 0; i < x.channels[0].size(); ++i)
        {
            const double v = x.channels[0][i];
            const double y = (b0 * v - b0 * x2 - a1 * y1 - a2 * y2) / a0;
            x2 = x1; x1 = v; y2 = y1; y1 = y;
            if (i >= static_cast<size_t> (9.0 * sr) + off && i < static_cast<size_t> (10.8 * sr) + off) e += y * y;
        }
        return 10.0 * std::log10 (e + 1e-30);
    };
    const double before = lowMid (in, 0), after = lowMid (out, L), base = lowMid (flat, 0);
    UNSCOPED_INFO ("low mids: drifted " << before - base << " dB over the song, after Tone Steady " << after - base);
    CHECK (after < before - 2.0);       // a clear pull back...
    CHECK (after > base - 0.5);         // ...but never past the song's own balance
    double largest = 0.0, at = 0.0;
    s.takeToneSteadyMeter (largest, at);
    CHECK (largest < -2.0);
    CHECK ((at > 150.0 && at < 600.0));

    // A song that doesn't drift is left (almost) alone.
    const auto steady = runStage (flat, ts, {});
    const double change = energyDb (steady.channels[0], static_cast<size_t> (3.0 * sr) + L, static_cast<size_t> (10.0 * sr) + L)
                        - energyDb (flat.channels[0], static_cast<size_t> (3.0 * sr), static_cast<size_t> (10.0 * sr));
    UNSCOPED_INFO ("steady song level change " << change << " dB");
    CHECK (std::abs (change) < 0.5);
}

TEST_CASE ("Tone Steady leaves a quiet breakdown alone", "[spectral]")
{
    const double sr = 48000.0;
    auto in = testsig::musicLike (sr, 10.0, -18.0, 2);
    for (auto& ch : in.channels)                                   // last 3 s: 25 dB quieter
        for (size_t i = static_cast<size_t> (7.0 * sr); i < ch.size(); ++i) ch[i] *= 0.056f;
    ToneSteadyParams ts; ts.amount = 100.0;
    StemEQ s;
    const auto out = runStage (in, ts, {}, &s);
    const auto L = static_cast<size_t> (s.latencySamples());
    const double change = energyDb (out.channels[0], static_cast<size_t> (8.0 * sr) + L, static_cast<size_t> (9.5 * sr) + L)
                        - energyDb (in.channels[0], static_cast<size_t> (8.0 * sr), static_cast<size_t> (9.5 * sr));
    CHECK (std::abs (change) < 0.5);
}

TEST_CASE ("Spectral stage offline render matches real time with all three on", "[spectral]")
{
    const auto in = testsig::musicLike (48000.0, 3.0, -12.0);
    StemEQParams st; st.gainDb = { 2.0, 0.0, -2.0, 0.0 };
    ToneSteadyParams ts; ts.amount = 60.0;
    ResonanceParams rt; rt.amount = 70.0;
    const auto off = renderSpectral (in, st, ts, rt);
    StemEQ s; s.prepare (48000.0, 2); s.setParams (st); s.setToneSteady (ts); s.setResonance (rt); s.reset();
    AudioClip rtc = in;
    float* ch[2] = { rtc.channels[0].data(), rtc.channels[1].data() };
    s.process (ch, 2, rtc.numSamples());
    const auto L = static_cast<size_t> (s.latencySamples());
    double worst = 0.0;
    for (size_t i = 0; i + L < rtc.channels[0].size(); ++i)
        worst = std::max (worst, static_cast<double> (std::abs (off.channels[0][i] - rtc.channels[0][i + L])));
    CHECK (worst == 0.0);
}

TEST_CASE ("Tone Steady learns nothing from silence (stopped transport, gaps)", "[spectral]")
{
    // 5 s of silence, then a steady bass-heavy song: nothing to steady.
    const double sr = 48000.0;
    auto song = testsig::musicLike (sr, 20.0, -14.0, 9);
    for (auto& ch : song.channels)
        for (size_t i = 0; i < static_cast<size_t> (5.0 * sr); ++i) ch[i] = 0.0f;
    for (auto& ch : song.channels)                                   // and a 4 s gap later on
        for (size_t i = static_cast<size_t> (11.0 * sr); i < static_cast<size_t> (15.0 * sr); ++i) ch[i] = 0.0f;
    ToneSteadyParams ts; ts.amount = 100.0; ts.rangeDb = 6.0;
    StemEQ s; s.prepare (sr, 2); s.setToneSteady (ts); s.reset();
    float* ch[2] = { song.channels[0].data(), song.channels[1].data() };
    const int block = 4800;
    double worst = 0.0;
    for (int i = 0; i < song.numSamples(); i += block)
    {
        s.process (std::array<float*, 2> { ch[0] + i, ch[1] + i }.data(), 2, std::min (block, song.numSamples() - i));
        double d = 0.0, hz = 0.0;
        s.takeToneSteadyMeter (d, hz);
        worst = std::max (worst, std::abs (d));
    }
    UNSCOPED_INFO ("largest Tone Steady move " << worst << " dB");
    CHECK (worst < 1.5);
    const auto st = measureSpectral (song, ts, {});
    CHECK (st.steadyMoveDb < 0.35);   // Auto-Master would leave it off
}

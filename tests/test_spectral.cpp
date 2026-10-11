#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Signals.h"
#include "polisher/AutoMaster.h"
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
    CHECK_THAT (at, Catch::Matchers::WithinAbs (2400.0, 30.0));

    // The same noise without the ring: almost untouched.
    const auto clean = testsig::musicLike (sr, 4.0, -18.0, 5);
    const auto cleanOut = runStage (clean, {}, rt);
    const double change = energyDb (cleanOut.channels[0], a + static_cast<size_t> (L), b + static_cast<size_t> (L)) - energyDb (clean.channels[0], a, b);
    UNSCOPED_INFO ("clean noise level change " << change << " dB");
    CHECK (std::abs (change) < 0.6);
}

TEST_CASE ("Resonance Tamer meter names the ringing tone's frequency, low rings too", "[spectral]")
{
    // The cut spans the tone's bin and both neighbours; the meter must name the tone, not the lower
    // neighbour (a 90 Hz ring used to read 43-47 Hz, 250 Hz read 188-215 Hz).
    for (double hz : { 90.0, 250.0, 1000.0 })
        for (double sr : { 44100.0, 48000.0 })
        {
            auto in = testsig::musicLike (sr, 3.0, -18.0, 5);
            for (auto& ch : in.channels)
                for (size_t i = 0; i < ch.size(); ++i)
                    ch[i] += static_cast<float> (0.05 * std::sin (2.0 * std::numbers::pi * hz * static_cast<double> (i) / sr));
            ResonanceParams rt; rt.amount = 100.0;
            StemEQ s;
            runStage (in, {}, rt, &s);
            double deepest = 0.0, at = 0.0;
            s.takeResonanceMeter (deepest, at);
            UNSCOPED_INFO (hz << " Hz ring at " << sr << ": meter " << deepest << " dB at " << at << " Hz");
            CHECK (deepest < -1.0);
            CHECK_THAT (at, Catch::Matchers::WithinAbs (hz, 0.1 * hz + 5.0));
        }
}

TEST_CASE ("Resonance Tamer's statistics hear a ringing tone over a loud 808", "[spectral]")
{
    // Loudness-weighted: an 808 carrying most of the energy must not hide a 3 kHz ring the ear picks
    // out at once (power-weighted it read 0.44 dB without the 808 and 0.01 dB with it).
    const double sr = 44100.0;
    auto mix = [&] (bool with808, bool ring)
    {
        AudioClip c = testsig::musicLike (sr, 14.0, -16.0, 3);
        for (auto& ch : c.channels)
            for (size_t i = 0; i < ch.size(); ++i)
            {
                const double t = static_cast<double> (i) / sr, tn = std::fmod (t, 1.0);
                if (with808) ch[i] += static_cast<float> (0.3 * std::exp (-tn * 1.2) * std::min (1.0, tn * 200.0) * std::sin (2.0 * std::numbers::pi * 45.0 * t));
                if (ring) ch[i] += static_cast<float> (0.012 * std::sin (2.0 * std::numbers::pi * 3000.0 * t));
            }
        return c;
    };
    ResonanceParams probe; probe.amount = 100.0;
    const double without808 = -measureSpectral (mix (false, true), {}, probe).resonanceCutDb;
    const SpectralStats with = measureSpectral (mix (true, true), {}, probe);
    const double clean808 = -measureSpectral (mix (true, false), {}, probe).resonanceCutDb;
    UNSCOPED_INFO ("full cut: ring " << without808 << " dB, ring + 808 " << -with.resonanceCutDb << " dB, 808 alone " << clean808 << " dB");
    CHECK (-with.resonanceCutDb > 0.5 * without808);
    CHECK (with.harshnessShare > 0.65);
    CHECK (clean808 < 0.1);

    // And Auto-Master turns the tamer on for the ring, 808 or not, and leaves it off without one.
    AutoMasterSettings s;
    const auto ringing = autoMaster (mix (true, true), s), clean = autoMaster (mix (true, false), s);
    REQUIRE (ringing.ok);
    REQUIRE (clean.ok);
    CHECK (ringing.chain.resonance.amount > 0.0);
    CHECK (ringing.chain.resonance.focus == ResonanceFocus::harshness);
    CHECK (clean.chain.resonance.amount == 0.0);
}

TEST_CASE ("Tone Steady re-learns when an 808 comes in instead of cutting it", "[spectral]")
{
    // 16 s intro without an 808, then a chorus with one. The 808 is the arrangement, not a drift:
    // before, Range 3 cut it by ~2.3-2.9 dB for the first 8 s of the chorus.
    const double sr = 44100.0;
    AudioClip in = testsig::musicLike (sr, 26.0, -18.0, 3);
    const double notes[3] = { 45.0, 50.0, 41.0 };
    for (auto& ch : in.channels)
    {
        double ph = 0.0;
        for (size_t i = static_cast<size_t> (16.0 * sr); i < ch.size(); ++i)
        {
            const double t = static_cast<double> (i) / sr - 16.0, tn = std::fmod (t, 1.0);
            ph += 2.0 * std::numbers::pi * notes[static_cast<int> (t) % 3] / sr;
            ch[i] += static_cast<float> (0.35 * std::exp (-tn * 1.2) * std::min (1.0, tn * 200.0) * (std::sin (ph) + 0.15 * std::sin (2.0 * ph)));
        }
    }
    auto lowDb = [&] (const AudioClip& c, double from, double to)   // energy below ~130 Hz (4th-order low-pass)
    {
        const double w0 = 2.0 * std::numbers::pi * 130.0 / sr, alpha = std::sin (w0) / std::sqrt (2.0), cw = std::cos (w0);
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
    };
    ToneSteadyParams ts; ts.amount = 100.0; ts.rangeDb = 3.0;
    const auto out = renderSpectral (in, {}, ts, {});
    for (double t0 : { 16.0, 18.0, 20.0, 22.0 })
    {
        const double change = lowDb (out, t0, t0 + 2.0) - lowDb (in, t0, t0 + 2.0);
        UNSCOPED_INFO ("chorus " << t0 - 16.0 << "-" << t0 - 14.0 << " s: low end " << change << " dB");
        CHECK (change > -1.0);
    }
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

TEST_CASE ("Tone Steady leaves busy trap hats alone (no duller top)", "[spectral]")
{
    // Hats come and go every 8th note: short-term top-end bursts that are the song's own balance,
    // not a drift. Tone Steady must not dull them (the user heard it on Addie).
    const double sr = 48000.0;
    AudioClip in = testsig::musicLike (sr, 14.0, -18.0, 5);
    std::mt19937 rng (3);
    std::normal_distribution<double> wn (0.0, 1.0);
    // Trap hats: each beat picks a pattern (rest, 8ths, 16ths or a 32nd roll) and an accent, so the
    // top end comes and goes from beat to beat as it does in a real song.
    const double beat = 60.0 / 140.0;
    std::vector<int> hitsPerBeat; std::vector<double> accent;
    std::uniform_int_distribution<int> pick (0, 4);
    std::uniform_real_distribution<double> acc (0.4, 1.0);
    for (int b = 0; b < 40; ++b) { static const int kHits[5] = { 0, 2, 2, 4, 8 }; hitsPerBeat.push_back (kHits[pick (rng)]); accent.push_back (acc (rng)); }
    for (auto& ch : in.channels)
    {
        double hp1 = 0.0, hpx = 0.0;
        for (size_t i = 0; i < ch.size(); ++i)
        {
            const double t = static_cast<double> (i) / sr;
            const auto b = static_cast<size_t> (t / beat) % hitsPerBeat.size();
            const int n = hitsPerBeat[b];
            const double step = beat / std::max (1, n);
            const double env = n == 0 ? 0.0 : accent[b] * std::exp (-std::fmod (t, step) / 0.025);
            const double x = wn (rng);
            hp1 = 0.2 * (hp1 + x - hpx); hpx = x;                 // crude high-pass: hat-like hiss
            ch[i] += static_cast<float> (0.25 * env * hp1);
        }
    }
    auto topDb = [&] (const AudioClip& c, size_t off) {             // energy above ~5 kHz, 4 s ... 13 s
        double y = 0.0, xp = 0.0, e = 0.0;
        for (size_t i = 0; i < c.channels[0].size(); ++i)
        {
            const double x = c.channels[0][i];
            y = 0.5 * (y + x - xp); xp = x;                       // one-pole high-pass
            double z = y;
            if (i >= static_cast<size_t> (4.0 * sr) + off && i < static_cast<size_t> (13.0 * sr) + off) e += z * z;
        }
        return 10.0 * std::log10 (e + 1e-30);
    };
    for (int speed = 0; speed < 3; ++speed)
    {
        ToneSteadyParams ts; ts.amount = 100.0; ts.rangeDb = 6.0; ts.speed = speed;
        StemEQ s;
        const auto out = runStage (in, ts, {}, &s);
        const double change = topDb (out, static_cast<size_t> (s.latencySamples())) - topDb (in, 0);
        UNSCOPED_INFO ("speed " << speed << ": top end change with busy hats " << change << " dB");
        CHECK (std::abs (change) < 0.3);
    }
}

TEST_CASE ("Tone Steady still pulls back a top end that stays harsher", "[spectral]")
{
    const double sr = 48000.0;
    const auto in = noiseWithBandStep (sr, 12.0, 7.0, 6000.0, 12.0);    // from 7 s the 6 kHz area is up
    ToneSteadyParams ts; ts.amount = 100.0; ts.rangeDb = 6.0;
    StemEQ s;
    const auto out = runStage (in, ts, {}, &s);
    const auto L = static_cast<size_t> (s.latencySamples());
    const double change = energyDb (out.channels[0], static_cast<size_t> (9.5 * sr) + L, static_cast<size_t> (11.5 * sr) + L)
                        - energyDb (in.channels[0], static_cast<size_t> (9.5 * sr), static_cast<size_t> (11.5 * sr));
    double largest = 0.0, at = 0.0;
    s.takeToneSteadyMeter (largest, at);
    UNSCOPED_INFO ("harsher section: level change " << change << " dB, largest move " << largest << " dB at " << at << " Hz");
    CHECK (largest < -1.5);
    CHECK ((at > 2500.0 && at < 10000.0));
}

TEST_CASE ("Tone Steady doesn't touch the top end when the song gets louder", "[spectral]")
{
    // A chorus 6 dB louder than the verse, same tone: nothing to steady, at any speed.
    const double sr = 48000.0;
    auto in = testsig::musicLike (sr, 14.0, -20.0, 6);
    for (auto& ch : in.channels)
        for (size_t i = static_cast<size_t> (8.0 * sr); i < ch.size(); ++i) ch[i] *= 2.0f;
    for (int speed = 0; speed < 3; ++speed)
    {
        ToneSteadyParams ts; ts.amount = 100.0; ts.rangeDb = 6.0; ts.speed = speed;
        StemEQ s; s.prepare (sr, 2); s.setToneSteady (ts); s.reset();
        AudioClip c = in;
        float* ch[2] = { c.channels[0].data(), c.channels[1].data() };
        const int block = 2400;
        double worst = 0.0;
        for (int i = 0; i < c.numSamples(); i += block)
        {
            s.process (std::array<float*, 2> { ch[0] + i, ch[1] + i }.data(), 2, std::min (block, c.numSamples() - i));
            double d = 0.0, hz = 0.0;
            s.takeToneSteadyMeter (d, hz);
            if (i > static_cast<int> (7.5 * sr) && hz > 2500.0) worst = std::max (worst, std::abs (d));
        }
        UNSCOPED_INFO ("speed " << speed << ": largest top-end move after the level step " << worst << " dB");
        CHECK (worst < 0.5);
    }
}

TEST_CASE ("Resonance Tamer statistics: a clean line well under the mix isn't ringing, a steady whistle is", "[spectral]")
{
    // What Auto-Master's 0.3 dB threshold sees (full strength, loudness-weighted). A clean sung line
    // (harmonics, 1 % vibrato, in phrases) 9 LU or more under the mix reads under it; a steady 3 kHz
    // whistle 9-19 LU under the mix reads over it, 808 or not. (At lead-vocal level a line this clean
    // reads 0.5-1.2 dB: no threshold tells that from a faint ring; see AutoMaster.cpp.)
    const double sr = 44100.0;
    ResonanceParams probe; probe.amount = 100.0;
    for (bool with808 : { false, true })
    {
        auto bed = testsig::musicLike (sr, 14.0, -18.0, 3);
        if (with808)
            for (auto& ch : bed.channels)
                for (size_t i = 0; i < ch.size(); ++i)
                {
                    const double t = static_cast<double> (i) / sr, tn = std::fmod (t, 1.0);
                    ch[i] += static_cast<float> (0.3 * std::exp (-tn * 1.2) * std::min (1.0, tn * 200.0) * std::sin (2.0 * std::numbers::pi * 45.0 * t));
                }
        auto line = bed, whistle = bed;
        double ph = 0.0;
        for (size_t i = 0; i < bed.channels[0].size(); ++i)
        {
            const double t = static_cast<double> (i) / sr;
            const double f0 = (std::fmod (t, 4.0) < 2.0 ? 220.0 : 294.0) * (1.0 + 0.01 * std::sin (2.0 * std::numbers::pi * 5.5 * t));
            ph += 2.0 * std::numbers::pi * f0 / sr;
            double v = 0.0;
            for (int n = 1; n * f0 < 4000.0; ++n) v += std::sin (n * ph) / n;
            const double phrase = std::fmod (t, 2.0) < 1.6 ? std::min (1.0, std::fmod (t, 2.0) * 20.0) : 0.0;
            for (size_t c = 0; c < 2; ++c)
            {
                line.channels[c][i] += static_cast<float> (0.01 * phrase * v);
                whistle.channels[c][i] += static_cast<float> (0.008 * std::sin (2.0 * std::numbers::pi * 3000.0 * t));
            }
        }
        const double lineCut = -measureSpectral (line, {}, probe).resonanceCutDb, whistleCut = -measureSpectral (whistle, {}, probe).resonanceCutDb;
        UNSCOPED_INFO ((with808 ? "with an 808" : "no 808") << ": clean line " << lineCut << " dB, whistle " << whistleCut << " dB");
        CHECK (lineCut < 0.3);
        CHECK (whistleCut > 0.6);
    }
}

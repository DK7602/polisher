// Monitoring-only listening aids: the Listen / Solo hooks inside the modules (what Resonance Tamer cut,
// one Dynamics band, the clipper's clean gain, the limiter's gain), the listen switch (ListenMix) and the
// MONO / PHONE checks (MonitorOutput). The plug-in tests check how they line up in the whole chain.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Signals.h"
#include "polisher/LoudnessTargeting.h"
#include "polisher/Monitoring.h"
#include "polisher/MultibandDynamics.h"
#include "polisher/SoftClipper.h"
#include "polisher/StemEQ.h"
#include "polisher/TruePeakLimiter.h"

#include <cmath>
#include <numbers>
#include <vector>

using namespace polisher;

namespace {

AudioClip ringingMix (double sr, double seconds, double ringHz, unsigned seed = 5)
{
    auto in = testsig::musicLike (sr, seconds, -18.0, seed);
    for (auto& ch : in.channels)
        for (size_t i = 0; i < ch.size(); ++i)
            ch[i] += static_cast<float> (0.05 * std::sin (2.0 * std::numbers::pi * ringHz * static_cast<double> (i) / sr));
    return in;
}

double energy (const std::vector<double>& v, size_t a, size_t b)
{
    double e = 0.0;
    for (size_t i = a; i < b && i < v.size(); ++i) e += v[i] * v[i];
    return e;
}

/** Runs a MonitorOutput over a stereo clip in blocks; per-block callback can flip the checks. */
template <typename OnBlock>
AudioClip runOutput (MonitorOutput& m, AudioClip c, int block, OnBlock onBlock, bool safety = true)
{
    for (int pos = 0, k = 0; pos < c.numSamples(); pos += block, ++k)
    {
        onBlock (k);
        const int n = std::min (block, c.numSamples() - pos);
        float* ch[2] = { c.channels[0].data() + pos, c.channels[1].data() + pos };
        m.process (ch, 2, n, safety);
    }
    return c;
}

double maxStep (const std::vector<float>& v, size_t a, size_t b)
{
    double s = 0.0;
    for (size_t i = std::max<size_t> (a, 1); i < b && i < v.size(); ++i) s = std::max (s, std::abs ((double) v[i] - v[i - 1]));
    return s;
}

} // namespace

TEST_CASE ("PHONE check: a mono phone-speaker band, about 300 Hz to 8 kHz with a small bump", "[monitoring]")
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        MonitorOutput m;
        m.prepare (sr);
        const double ref = m.phoneResponseDb (1000.0);
        const double lo = m.phoneResponseDb (MonitorOutput::kPhoneLowHz) - ref, hi = m.phoneResponseDb (MonitorOutput::kPhoneHighHz) - ref;
        INFO ("sr " << sr << ": 1 kHz " << ref << " dB, 300 Hz " << lo << ", 8 kHz " << hi << ", 2 kHz " << m.phoneResponseDb (2000.0) - ref);
        // -3 dB points (against 1 kHz) at the band edges, within the bump's nudge.
        CHECK (lo < -2.0);  CHECK (lo > -4.0);
        CHECK (hi < -2.0);  CHECK (hi > -4.0);
        // Gentle slopes: an 808 fundamental is far down, its upper harmonics come through.
        CHECK (m.phoneResponseDb (50.0) - ref < -25.0);
        CHECK (m.phoneResponseDb (100.0) - ref < -15.0);
        CHECK (m.phoneResponseDb (600.0) - ref > -1.5);
        CHECK (m.phoneResponseDb (16000.0) - ref < -9.0);
        // The bump: 1-3 kHz is the loudest part, by a few dB at most.
        double peakDb = -100.0, peakHz = 0.0;
        for (double f = 100.0; f < 15000.0; f *= 1.02)
            if (m.phoneResponseDb (f) > peakDb) { peakDb = m.phoneResponseDb (f); peakHz = f; }
        CHECK (peakHz > 1000.0);
        CHECK (peakHz < 3000.0);
        CHECK (peakDb - ref < 4.0);
    }
}

TEST_CASE ("PHONE check is mono and keeps a balanced mix's loudness; MONO sums L and R", "[monitoring]")
{
    const double sr = 48000.0;
    // Pink noise (no kick), different in each channel: a balanced spectrum.
    auto pink = testsig::musicLike (sr, 8.0, -12.0, 3);
    {
        auto other = testsig::musicLike (sr, 8.0, -12.0, 4);
        // musicLike's kick is the same in both channels; remove it by subtracting two seeds' halves.
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = 0; i < pink.channels[c].size(); ++i)
                pink.channels[c][i] = 0.7071f * (pink.channels[c][i] - other.channels[1 - c][i]);
    }
    MonitorOutput mono;
    mono.prepare (sr);
    mono.setMono (true);
    const auto monoOut = runOutput (mono, pink, 512, [] (int) {}, false);
    MonitorOutput phone;
    phone.prepare (sr);
    phone.setPhone (true);
    const auto phoneOut = runOutput (phone, pink, 512, [] (int) {}, false);

    const size_t from = static_cast<size_t> (0.1 * sr);
    double monoErr = 0.0, lr = 0.0;
    for (size_t i = from; i < pink.channels[0].size(); ++i)
    {
        const double expect = 0.5 * ((double) pink.channels[0][i] + pink.channels[1][i]);
        monoErr = std::max ({ monoErr, std::abs (monoOut.channels[0][i] - expect), std::abs (monoOut.channels[1][i] - expect) });
        lr = std::max (lr, std::abs ((double) phoneOut.channels[0][i] - phoneOut.channels[1][i]));
    }
    CHECK (monoErr < 1.0e-6);   // (L + R) / 2 on both sides
    CHECK (lr == 0.0);          // PHONE is mono

    // Level-matched: on a balanced (pink) mix PHONE measures about as loud as MONO, not just quieter.
    const double lMono = measure (monoOut).integratedLufs, lPhone = measure (phoneOut).integratedLufs;
    INFO ("mono " << lMono << " LUFS, phone " << lPhone << " LUFS");
    CHECK (std::abs (lPhone - lMono) < 1.0);

    // Out-of-phase content disappears in MONO (what the check is for).
    auto wide = testsig::sine (sr, 2, 1000.0, -12.0, 1.0);
    for (auto& v : wide.channels[1]) v = -v;
    MonitorOutput m2;
    m2.prepare (sr);
    m2.setMono (true);
    const auto gone = runOutput (m2, wide, 256, [] (int) {}, false);
    double rest = 0.0;
    for (size_t i = static_cast<size_t> (0.05 * sr); i < gone.channels[0].size(); ++i) rest = std::max (rest, std::abs ((double) gone.channels[0][i]));
    CHECK (rest < 1.0e-7);
}

TEST_CASE ("MONO / PHONE switch without clicks, and the safety stage holds 0 dBFS", "[monitoring]")
{
    const double sr = 48000.0;
    auto tone = testsig::sine (sr, 2, 220.0, -6.0, 3.0);
    for (size_t i = 0; i < tone.channels[1].size(); ++i)   // a different right channel, so MONO changes it
        tone.channels[1][i] = static_cast<float> (0.5 * std::sin (2.0 * std::numbers::pi * 330.0 * static_cast<double> (i) / sr));
    MonitorOutput m;
    m.prepare (sr);
    // Flip something every 0.25 s: mono on, phone on, mono off, phone off, both, none...
    const auto out = runOutput (m, tone, 480, [&] (int k)
    {
        const int phase = (k / 25) % 4;
        m.setMono (phase == 1 || phase == 3);
        m.setPhone (phase >= 2);
    });
    // The steepest step of either input tone, with headroom for the phone's makeup and bump.
    const double steadyStep = 2.0 * std::numbers::pi * 330.0 / sr * 0.5;
    const double worst = std::max (maxStep (out.channels[0], 1, out.channels[0].size()), maxStep (out.channels[1], 1, out.channels[1].size()));
    INFO ("largest sample step " << worst << " vs a steady tone's " << steadyStep);
    CHECK (worst < 3.0 * steadyStep);

    // A peak above 0 dBFS never leaves while monitoring.
    auto hot = testsig::sine (sr, 2, 1000.0, 6.0, 0.5);
    MonitorOutput s;
    s.prepare (sr);
    s.setPhone (true);
    const auto safe = runOutput (s, hot, 256, [] (int) {});
    double peak = 0.0;
    for (auto& ch : safe.channels) for (float v : ch) peak = std::max (peak, (double) std::abs (v));
    CHECK (peak <= 1.0);
}

TEST_CASE ("Listen switch: master and monitored signal cross-fade without clicks, arming first", "[monitoring]")
{
    const double sr = 48000.0;
    const int n = static_cast<int> (3.0 * sr), block = 256;
    std::vector<float> l (static_cast<size_t> (n)), r (static_cast<size_t> (n));
    std::vector<double> s0 (static_cast<size_t> (n)), s1 (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        l[static_cast<size_t> (i)] = r[static_cast<size_t> (i)] = static_cast<float> (0.5 * std::sin (2.0 * std::numbers::pi * 200.0 * i / sr));
        s0[static_cast<size_t> (i)] = s1[static_cast<size_t> (i)] = 0.05 * std::sin (2.0 * std::numbers::pi * 600.0 * i / sr + 1.0);
    }
    ListenMix mix;
    mix.prepare (sr);
    mix.setAutoLevel (false);
    const int arm = 300;
    int armedAt = -1, startedCount = 0;
    for (int pos = 0, k = 0; pos < n; pos += block, ++k)
    {
        // off -> sat -> clip -> off -> max -> off, every ~0.5 s
        static constexpr int seq[] = { listen::off, listen::satAdded, listen::clipRemoved, listen::off, listen::maxRemoved, listen::off };
        mix.request (seq[(k / 94) % 6], arm);
        const int m = mix.beginBlock();
        if (mix.startedArming()) { ++startedCount; if (armedAt < 0) armedAt = pos; CHECK (m != listen::off); }
        const int len = std::min (block, n - pos);
        float* ch[2] = { l.data() + pos, r.data() + pos };
        const double* sg[2] = { s0.data() + pos, s1.data() + pos };
        mix.process (ch, 2, sg, len, 1.0);
    }
    CHECK (startedCount == 3);
    // A cross-fade between a 200 Hz tone at 0.5 and a 600 Hz tone at 0.05 never steps further than
    // the tones themselves do, plus the fade's own slope.
    const double tone = 2.0 * std::numbers::pi * 200.0 / sr * 0.5;
    const double worst = maxStep (l, 1, l.size());
    INFO ("largest step " << worst << ", steady tone " << tone);
    CHECK (worst < 1.6 * tone);
    // While a mode arms, the master keeps playing unchanged: the first 300 + 0.1 s after the first request.
    const int quiet = armedAt + arm + static_cast<int> (ListenMix::kMeasureSeconds * sr) - 1;
    double err = 0.0;
    for (int i = armedAt; i < quiet; ++i)
        err = std::max (err, std::abs ((double) l[static_cast<size_t> (i)] - 0.5 * std::sin (2.0 * std::numbers::pi * 200.0 * i / sr)));
    CHECK (err < 1.0e-6);
    // Fully switched: only the monitored signal (at its own level: auto level is off).
    const int mid = static_cast<int> (0.4 * sr);   // inside the satAdded stretch (0.5 s per step, starting at 0.5 s)
    const int at = static_cast<int> (0.5 * sr) + mid;
    double e2 = 0.0;
    for (int i = at; i < at + 1000; ++i)
        e2 = std::max (e2, std::abs ((double) l[static_cast<size_t> (i)] - s0[static_cast<size_t> (i)]));
    CHECK (e2 < 1.0e-6);
}

TEST_CASE ("Listen level: a quiet delta is turned up toward 15 dB under the master, a solo is not", "[monitoring]")
{
    const double sr = 48000.0;
    const int n = static_cast<int> (3.0 * sr), block = 512;
    auto run = [&] (int mode, double sigDb, double& gainDb, double& underDb)
    {
        std::vector<float> l (static_cast<size_t> (n)), r (static_cast<size_t> (n));
        std::vector<double> s (static_cast<size_t> (n));
        const double a = std::pow (10.0, sigDb / 20.0);
        for (int i = 0; i < n; ++i)
        {
            l[static_cast<size_t> (i)] = r[static_cast<size_t> (i)] = static_cast<float> (0.5 * std::sin (2.0 * std::numbers::pi * 997.0 * i / sr));
            s[static_cast<size_t> (i)] = a * std::sin (2.0 * std::numbers::pi * 997.0 * i / sr + 2.0);
        }
        ListenMix mix;
        mix.prepare (sr);
        for (int pos = 0; pos < n; pos += block)
        {
            mix.request (mode, 100);
            mix.beginBlock();
            float* ch[2] = { l.data() + pos, r.data() + pos };
            const double* sg[2] = { s.data() + pos, s.data() + pos };
            mix.process (ch, 2, sg, std::min (block, n - pos), std::pow (10.0, -1.0 / 20.0));
        }
        gainDb = mix.appliedGainDb();
        underDb = mix.levelUnderMasterDb();
        // What came out: the signal times the gain.
        double peak = 0.0;
        for (int i = n - 4800; i < n; ++i) peak = std::max (peak, (double) std::abs (l[static_cast<size_t> (i)]));
        return 20.0 * std::log10 (peak);
    };
    double g = 0.0, under = 0.0;
    // A delta 40 dB under a -6 dBFS tone: up by 25 dB to sit 15 dB under, capped at 24.
    const double outQuiet = run (listen::satAdded, -46.0, g, under);
    INFO ("quiet delta: gain " << g << " dB, under " << under << " dB, out peak " << outQuiet);
    CHECK_THAT (g, Catch::Matchers::WithinAbs (ListenMix::kMaxBoostDb, 0.5));
    CHECK_THAT (under, Catch::Matchers::WithinAbs (-40.0, 1.0));
    // 20 dB under: up by ~5 dB to sit 15 dB under.
    run (listen::clipRemoved, -26.0, g, under);
    CHECK_THAT (g, Catch::Matchers::WithinAbs (5.0, 1.0));
    // A delta louder than that plays as it is; one whose peaks pass the ceiling is turned down to it.
    run (listen::maxRemoved, -10.0, g, under);
    CHECK_THAT (g, Catch::Matchers::WithinAbs (0.0, 0.2));
    const double outHot = run (listen::maxRemoved, 3.0, g, under);
    CHECK (g < -3.5);
    CHECK (outHot < -0.9);
    // A solo plays at its real level.
    run (listen::dynSolo + 1, -30.0, g, under);
    CHECK_THAT (g, Catch::Matchers::WithinAbs (0.0, 0.1));
}

TEST_CASE ("Dynamics solo: the four bands add up to the output; removed + compressed = the plain band", "[monitoring][dynamics]")
{
    const double sr = 48000.0;
    const auto mix = testsig::musicLike (sr, 3.0, -6.0);
    DynamicsParams p;
    for (auto& b : p.bands) { b.ratio = 3.0; b.thresholdDb = -30.0; }
    p.bands[2].gainDb = 2.0;   // makeup on one band: still part of its solo
    auto render = [&] (const DynamicsParams& params, int band, bool removed, std::array<std::vector<double>, 2>& mon)
    {
        MultibandDynamics d;
        d.setParams (params);
        d.setMonitorBand (band, removed);
        d.prepare (sr, 2);
        AudioClip c = mix;
        for (auto& m : mon) m.assign (static_cast<size_t> (c.numSamples()), 0.0);
        for (int pos = 0; pos < c.numSamples(); pos += 256)
        {
            const int n = std::min (256, c.numSamples() - pos);
            float* ch[2] = { c.channels[0].data() + pos, c.channels[1].data() + pos };
            double* mo[2] = { mon[0].data() + pos, mon[1].data() + pos };
            d.process (ch, 2, n, mo);
        }
        return c;
    };
    std::array<std::vector<double>, 2> none, s[4];
    const auto plain = render (p, -1, false, none);
    std::array<std::vector<double>, 2> sum { std::vector<double> (plain.channels[0].size()), std::vector<double> (plain.channels[0].size()) };
    for (int k = 0; k < 4; ++k)
    {
        const auto out = render (p, k, false, s[k]);
        CHECK (out.channels == plain.channels);   // monitoring never changes the module's output
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = 0; i < sum[c].size(); ++i) sum[c][i] += s[k][c][i];
    }
    double err = 0.0;
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = 0; i < sum[c].size(); ++i) err = std::max (err, std::abs (sum[c][i] - plain.channels[c][i]));
    INFO ("four solos vs the output: largest difference " << err);
    CHECK (err < 1.0e-6);   // the output is float

    // Removed (low band) + the low band's solo without makeup = the plain low band of a neutral split.
    DynamicsParams noMakeup = p;
    noMakeup.bands[0].gainDb = 0.0;
    std::array<std::vector<double>, 2> solo, removed, bare;
    render (noMakeup, 0, false, solo);
    render (noMakeup, 0, true, removed);
    const auto neutralOut = render (DynamicsParams {}, 0, false, bare);
    CHECK (neutralOut.channels == mix.channels);   // a solo of a neutral module: the output stays untouched
    double e2 = 0.0, eRemoved = 0.0;
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = 0; i < bare[c].size(); ++i)
        {
            e2 = std::max (e2, std::abs (solo[c][i] + removed[c][i] - bare[c][i]));
            eRemoved += removed[c][i] * removed[c][i];
        }
    CHECK (e2 < 1.0e-9);
    CHECK (eRemoved > 1.0);   // it does take something off the kicks
    // Nothing compressed -> nothing removed.
    std::array<std::vector<double>, 2> idle;
    render (DynamicsParams {}, 0, true, idle);
    CHECK (energy (idle[0], 0, idle[0].size()) == 0.0);
}

TEST_CASE ("Resonance Tamer listen hook: what it cut, lined up with the output; silent when it rests", "[monitoring][spectral]")
{
    // Output + what was cut = the input, delayed, at every rate (the FFT size changes with the rate).
    for (double rate : { 44100.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        const auto mix = ringingMix (rate, 2.0, 2400.0);
        StemEQ s;
        s.prepare (rate, 2);
        ResonanceParams rt; rt.amount = 100.0;
        s.setResonance (rt);
        s.reset();
        s.setTamerDelta (true);
        AudioClip c = mix;
        std::array<std::vector<double>, 2> cut;
        for (auto& d : cut) d.assign (static_cast<size_t> (c.numSamples()), 0.0);
        for (int pos = 0; pos < c.numSamples(); pos += 333)
        {
            const int n = std::min (333, c.numSamples() - pos);
            float* ch[2] = { c.channels[0].data() + pos, c.channels[1].data() + pos };
            double* dl[2] = { cut[0].data() + pos, cut[1].data() + pos };
            s.process (ch, 2, n, dl);
        }
        const auto lat = static_cast<size_t> (s.latencySamples());
        double err = 0.0, ref = 0.0, cutEnergy = 0.0;
        for (size_t ch = 0; ch < 2; ++ch)
            for (size_t i = lat + 2 * static_cast<size_t> (StemEQ::fftSizeFor (rate)); i < c.channels[ch].size(); ++i)
            {
                const double x = mix.channels[ch][i - lat];
                err = std::max (err, std::abs ((double) c.channels[ch][i] + cut[ch][i] - x));
                ref = std::max (ref, std::abs (x));
                cutEnergy += cut[ch][i] * cut[ch][i];
            }
        UNSCOPED_INFO (rate << " Hz: output + cut vs input, largest difference " << err);
        CHECK (err < 1.0e-6 * std::max (1.0, ref));
        CHECK (cutEnergy > 0.0);
    }

    const double sr = 48000.0;
    const auto in = ringingMix (sr, 4.0, 2400.0);
    auto runTamer = [&] (double amount, std::array<std::vector<double>, 2>& delta)
    {
        StemEQ s;
        s.prepare (sr, 2);
        ResonanceParams rt; rt.amount = amount;
        s.setResonance (rt);
        s.reset();
        s.setTamerDelta (true);
        AudioClip c = in;
        for (auto& d : delta) d.assign (static_cast<size_t> (c.numSamples()), 0.0);
        for (int pos = 0; pos < c.numSamples(); pos += 300)
        {
            const int n = std::min (300, c.numSamples() - pos);
            float* ch[2] = { c.channels[0].data() + pos, c.channels[1].data() + pos };
            double* dl[2] = { delta[0].data() + pos, delta[1].data() + pos };
            s.process (ch, 2, n, dl);
        }
        return std::pair { c, s.latencySamples() };
    };
    std::array<std::vector<double>, 2> delta, rest;
    const auto [out, latency] = runTamer (100.0, delta);
    // Output + what was cut = the input, delayed (the only spectral module working is the tamer).
    double err = 0.0, ref = 0.0;
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = static_cast<size_t> (latency) + 2048; i < out.channels[c].size(); ++i)
        {
            const double x = in.channels[c][i - static_cast<size_t> (latency)];
            err = std::max (err, std::abs ((double) out.channels[c][i] + delta[c][i] - x));
            ref = std::max (ref, std::abs (x));
        }
    INFO ("output + cut vs input: largest difference " << err << " (input peak " << ref << ")");
    CHECK (err < 1.0e-6 * std::max (1.0, ref));
    // It is the ring: most of the cut energy is at 2.4 kHz.
    double ringRe = 0.0, ringIm = 0.0, total = 0.0;
    const size_t a = static_cast<size_t> (sr), b = static_cast<size_t> (3.5 * sr);
    for (size_t i = a; i < b; ++i)
    {
        const double ph = 2.0 * std::numbers::pi * 2400.0 * static_cast<double> (i) / sr;
        ringRe += delta[0][i] * std::cos (ph);
        ringIm += delta[0][i] * std::sin (ph);
        total += delta[0][i] * delta[0][i];
    }
    const double ringEnergy = 2.0 * (ringRe * ringRe + ringIm * ringIm) / static_cast<double> (b - a);
    INFO ("ring share of the cut " << ringEnergy / total);
    CHECK (total > 1.0e-3);
    CHECK (ringEnergy / total > 0.5);

    // Amount 0: the stage passes the mix and the hook is exactly silent.
    runTamer (0.0, rest);
    CHECK (energy (rest[0], 0, rest[0].size()) + energy (rest[1], 0, rest[1].size()) == 0.0);
}

TEST_CASE ("Clipper and limiter hooks: their clean gain and applied gain describe the output exactly", "[monitoring]")
{
    const double sr = 48000.0;
    // Clipper: makeup 9 dB, a tone that clips a little. Clean gain x delayed input - output = what was shaved.
    {
        SoftClipper clip;
        clip.prepare (2, sr);
        ClipperParams p; p.enabled = true; p.thresholdDb = -9.0; p.makeupDb = 9.0; p.softness = 1.0;
        clip.setParams (p);
        auto tone = testsig::sine (sr, 2, 1000.0, -11.0, 1.0);
        std::vector<double> gain (static_cast<size_t> (tone.numSamples()));
        AudioClip out = tone;
        float* ch[2] = { out.channels[0].data(), out.channels[1].data() };
        clip.process (ch, 2, out.numSamples(), gain.data());
        const double makeup = std::pow (10.0, 9.0 / 20.0);
        double shaved = 0.0, peakOut = 0.0;
        for (size_t i = 2000; i < gain.size(); ++i)
        {
            CHECK_THAT (gain[i], Catch::Matchers::WithinAbs (makeup, 1.0e-9));
            shaved = std::max (shaved, std::abs (makeup * tone.channels[0][i - SoftClipper::kLatency] - out.channels[0][i]));
            peakOut = std::max (peakOut, (double) std::abs (out.channels[0][i]));
        }
        CHECK (shaved > 0.01);       // the tops are shaved...
        CHECK (peakOut < 0.95);      // ...and nothing reaches the limiter's ceiling (the plug-in test relies on it)
        // Off: clean gain 1 and the output is the delayed input exactly, so nothing is "removed".
        p.enabled = false;
        SoftClipper offClip;
        offClip.prepare (2, sr);
        offClip.setParams (p);
        AudioClip o2 = tone;
        float* c2[2] = { o2.channels[0].data(), o2.channels[1].data() };
        offClip.process (c2, 2, o2.numSamples(), gain.data());
        for (size_t i = static_cast<size_t> (SoftClipper::kLatency); i < gain.size(); ++i)
        {
            REQUIRE (gain[i] == 1.0);
            REQUIRE (o2.channels[0][i] == tone.channels[0][i - SoftClipper::kLatency]);
        }
    }
    // Limiter: output = delayed input x the gain it reports (below its final clamp).
    {
        TruePeakLimiter lim;
        lim.prepare (sr, 2);
        LimiterParams lp; lp.ceilingDbtp = -1.0;
        lim.setParams (lp);
        auto mixIn = testsig::musicLike (sr, 2.0, 0.0);
        AudioClip out = mixIn;
        std::vector<double> g (static_cast<size_t> (out.numSamples()));
        float* ch[2] = { out.channels[0].data(), out.channels[1].data() };
        lim.process (ch, 2, out.numSamples(), g.data());
        const auto d = static_cast<size_t> (lim.latencySamples());
        double err = 0.0, minGain = 1.0;
        for (size_t i = d; i < g.size(); ++i)
        {
            err = std::max (err, std::abs ((double) mixIn.channels[0][i - d] * g[i] - out.channels[0][i]));
            minGain = std::min (minGain, g[i]);
        }
        CHECK (err < 1.0e-6);
        CHECK (minGain < 0.9);   // it was limiting
    }
}

TEST_CASE ("Monitoring safety rides the level: no peak-by-peak limiting on a loud PHONE check", "[monitoring]")
{
    const double sr = 48000.0;
    // A bright, loud master (a steady 2 kHz note at -1.5 dBFS, a little 600 Hz under it): PHONE's bump
    // takes it past 0 dBFS, so the safety stage has to turn it down.
    auto loud = testsig::sine (sr, 2, 2000.0, -1.5, 1.5);
    const auto low = testsig::sine (sr, 2, 600.0, -20.0, 1.5);
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = 0; i < loud.channels[c].size(); ++i) loud.channels[c][i] += low.channels[c][i];
    MonitorOutput unsafeOut, safeOut;
    unsafeOut.prepare (sr);
    safeOut.prepare (sr);
    unsafeOut.setPhone (true);
    safeOut.setPhone (true);
    const auto unsafe = runOutput (unsafeOut, loud, 512, [] (int) {}, false);
    const auto safe = runOutput (safeOut, loud, 512, [] (int) {}, true);
    double unsafePeak = 0.0, safePeak = 0.0;
    for (size_t i = 0; i < safe.channels[0].size(); ++i)
    {
        unsafePeak = std::max (unsafePeak, (double) std::abs (unsafe.channels[0][i]));
        safePeak = std::max (safePeak, (double) std::abs (safe.channels[0][i]));
    }
    INFO ("PHONE alone peaks at " << 20.0 * std::log10 (unsafePeak) << " dBFS");
    REQUIRE (unsafePeak > 1.2);   // the safety stage really has work to do
    CHECK (safePeak <= 1.0);
    // Once it has found the level, it holds it: the output is the PHONE sound times one steady gain
    // (an instant-attack limiter with a short release dips on every wave of the note: distortion).
    double gMin = 10.0, gMax = 0.0;
    for (size_t i = static_cast<size_t> (0.1 * sr); i < safe.channels[0].size(); ++i)
        if (std::abs (unsafe.channels[0][i]) > 0.3)
        {
            const double g = safe.channels[0][i] / unsafe.channels[0][i];
            gMin = std::min (gMin, g);
            gMax = std::max (gMax, g);
        }
    INFO ("safety gain " << 20.0 * std::log10 (gMin) << " .. " << 20.0 * std::log10 (gMax) << " dB");
    CHECK (gMax / gMin < 1.0 + 1.0e-5);
    CHECK (std::abs (20.0 * std::log10 (gMin) + 20.0 * std::log10 (unsafePeak)) < 0.05);   // turned down just enough
}

TEST_CASE ("Monitoring safety lets go when the checks are switched off, and the next check starts level", "[monitoring]")
{
    const double sr = 48000.0;
    auto hot = testsig::sine (sr, 2, 2000.0, -1.0, 2.0);
    const auto quiet = testsig::sine (sr, 2, 1000.0, -12.0, 2.0);
    const int offAt = static_cast<int> (0.5 * sr) / 480 * 480, onAt = static_cast<int> (1.2 * sr) / 480 * 480;
    // Hot PHONE until 0.5 s, then the plain master (quiet), then PHONE again on the quiet master.
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = static_cast<size_t> (offAt); i < hot.channels[c].size(); ++i) hot.channels[c][i] = quiet.channels[c][i];
    MonitorOutput m;
    m.prepare (sr);
    bool activeBeforeOn = true;
    double gainBeforeOn = 0.0;
    const auto out = runOutput (m, hot, 480, [&] (int k)
    {
        if (k * 480 == onAt) { activeBeforeOn = m.isActive(); gainBeforeOn = m.safetyGainNow(); }
        m.setPhone (k * 480 < offAt || k * 480 >= onAt);
    });
    // Off: back to the master exactly within 0.1 s (the 15 ms fade, the safety letting go with it).
    double err = 0.0;
    for (size_t i = static_cast<size_t> (offAt + 0.1 * sr); i < static_cast<size_t> (onAt); ++i)
        for (size_t c = 0; c < 2; ++c) err = std::max (err, std::abs ((double) out.channels[c][i] - hot.channels[c][i]));
    CHECK (err == 0.0);
    CHECK_FALSE (activeBeforeOn);       // nothing left running between the checks
    CHECK (gainBeforeOn == 1.0);
    // The next PHONE check starts at full level: the same as a fresh PHONE on the quiet master.
    MonitorOutput fresh;
    fresh.prepare (sr);
    AudioClip tail = hot;
    for (auto& ch : tail.channels) ch.erase (ch.begin(), ch.begin() + onAt);
    fresh.setPhone (true);
    const auto ref = runOutput (fresh, tail, 480, [] (int) {});
    double e2 = 0.0;
    for (size_t i = 0; i < ref.channels[0].size(); ++i)
        e2 = std::max (e2, std::abs ((double) out.channels[0][i + static_cast<size_t> (onAt)] - ref.channels[0][i]));
    CHECK (e2 < 1.0e-6);
}

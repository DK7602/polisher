#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Signals.h"
#include "SwitchLevel.h"
#include "polisher/MultibandDynamics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

using namespace polisher;
using Catch::Matchers::WithinAbs;

namespace {

/** Level (dB, peak-calibrated) of the tone at hz in channel 0 over [from, to) seconds. */
double toneDb (const AudioClip& c, double hz, double from, double to)
{
    double re = 0.0, im = 0.0;
    const int a = static_cast<int> (from * c.sampleRate), b = static_cast<int> (to * c.sampleRate);
    for (int i = a; i < b; ++i)
    {
        const double w = 2.0 * std::numbers::pi * hz * i / c.sampleRate;
        re += c.channels[0][static_cast<size_t> (i)] * std::cos (w);
        im += c.channels[0][static_cast<size_t> (i)] * std::sin (w);
    }
    return 20.0 * std::log10 (2.0 * std::hypot (re, im) / (b - a) + 1.0e-12);
}

void run (MultibandDynamics& d, AudioClip& c, int block = 256)
{
    for (int start = 0; start < c.numSamples(); start += block)
    {
        const int n = std::min (block, c.numSamples() - start);
        float* ch[2] = { c.channels[0].data() + start, c.channels[1].data() + start };
        d.process (ch, 2, n);
    }
}

AudioClip tones (double sr, double seconds, std::initializer_list<std::pair<double, double>> hzDb)
{
    AudioClip c = testsig::silence (sr, 2, seconds);
    for (int i = 0; i < c.numSamples(); ++i)
    {
        double v = 0.0;
        for (auto [hz, db] : hzDb)
            v += std::pow (10.0, db / 20.0) * std::sin (2.0 * std::numbers::pi * hz * i / sr);
        c.channels[0][static_cast<size_t> (i)] = c.channels[1][static_cast<size_t> (i)] = static_cast<float> (v);
    }
    return c;
}

} // namespace

TEST_CASE ("Dynamics at neutral is an exact pass-through", "[dynamics]")
{
    auto mix = testsig::musicLike (48000.0, 2.0, -6.0);
    const auto original = mix;
    MultibandDynamics d;
    d.prepare (48000.0, 2);
    d.setParams (DynamicsParams {});
    run (d, mix);
    CHECK (mix.channels == original.channels);
    CHECK_FALSE (d.isActive());
}

TEST_CASE ("A band gain too small to hear counts as 0 dB", "[dynamics]")
{
    // The UI shows anything under 0.05 dB as 0; such a gain mustn't switch the crossovers in.
    auto mix = testsig::musicLike (48000.0, 1.0, -6.0);
    const auto original = mix;
    DynamicsParams p;
    p.bands[2].gainDb = 0.005;
    CHECK (MultibandDynamics::isNeutral (p));
    MultibandDynamics d;
    d.prepare (48000.0, 2);
    d.setParams (p);
    d.reset();
    run (d, mix);
    CHECK (mix.channels == original.channels);
    CHECK_FALSE (d.isActive());
}

TEST_CASE ("Stereo detection reads a wide sound the same as a centred one", "[dynamics]")
{
    // Same level in both channels, once identical and once 90 degrees apart (as wide as it gets
    // without cancelling). The louder-channel detector must read both alike.
    const double sr = 48000.0;
    DynamicsParams p;
    for (auto& b : p.bands) { b.ratio = 2.0; b.thresholdDb = 0.0; }   // active, nothing compressing
    for (auto [hz, band] : { std::pair { 60.0, 0 }, std::pair { 400.0, 1 }, std::pair { 2500.0, 2 }, std::pair { 10000.0, 3 } })
    {
        double read[2] {};
        for (int wide = 0; wide < 2; ++wide)
        {
            AudioClip c = testsig::silence (sr, 2, 1.0);
            for (int i = 0; i < c.numSamples(); ++i)
            {
                const double w = 2.0 * std::numbers::pi * hz * i / sr;
                c.channels[0][static_cast<size_t> (i)] = static_cast<float> (0.1 * std::sin (w));
                c.channels[1][static_cast<size_t> (i)] = static_cast<float> (0.1 * (wide ? std::cos (w) : std::sin (w)));
            }
            MultibandDynamics d;
            d.prepare (sr, 2);
            d.setParams (p);
            d.reset();
            run (d, c);
            read[wide] = d.currentLevelDb (band);
        }
        INFO (hz << " Hz: centred " << read[0] << " dB, wide " << read[1] << " dB");
        CHECK_THAT (read[1], WithinAbs (read[0], 0.3));
        CHECK_THAT (read[0], WithinAbs (-20.0, 1.0));
    }
}

TEST_CASE ("The four bands sum flat when nothing is compressing", "[dynamics]")
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        DynamicsParams p;
        for (auto& b : p.bands) { b.ratio = 2.0; b.thresholdDb = 0.0; }   // active, but far above the signal
        for (double hz : { 30.0, 120.0, 400.0, 1000.0, 3000.0, 6000.0, 12000.0 })
        {
            auto c = tones (sr, 1.0, { { hz, -30.0 } });
            MultibandDynamics d;
            d.prepare (sr, 2);
            d.setParams (p);
            d.reset();
            run (d, c);
            INFO ("sr " << sr << " hz " << hz);
            CHECK_THAT (toneDb (c, hz, 0.5, 1.0), WithinAbs (-30.0, 0.02));
        }
    }
}

TEST_CASE ("A band compresses by its ratio and leaves the others alone", "[dynamics]")
{
    const double sr = 48000.0;
    DynamicsParams p;
    p.bands[1].thresholdDb = -20.0;   // low mids
    p.bands[1].ratio = 4.0;
    auto c = tones (sr, 2.0, { { 400.0, -8.0 }, { 50.0, -12.0 }, { 8000.0, -18.0 } });
    MultibandDynamics d;
    d.prepare (sr, 2);
    d.setParams (p);
    d.reset();
    run (d, c);
    // The band's own gain: 12 dB over the threshold at 4:1 -> 9 dB of reduction (the detector
    // sees ~0.2 dB less, because a crossover isn't a brick wall).
    const double gr = d.takeMaxGrDb (1);
    CHECK_THAT (gr, WithinAbs (-9.0, 0.5));
    CHECK (d.takeMaxGrDb (0) == 0.0);
    // At the output a tone is ~97 % in its own band and ~3 % in its neighbours, so it comes out
    // ~0.5 dB above the textbook -17 dB.
    CHECK_THAT (toneDb (c, 400.0, 1.0, 2.0), WithinAbs (-16.5, 0.4));
    // Other bands untouched (apart from their own 3 % share in the compressed band).
    CHECK_THAT (toneDb (c, 50.0, 1.0, 2.0), WithinAbs (-12.0, 0.25));
    CHECK_THAT (toneDb (c, 8000.0, 1.0, 2.0), WithinAbs (-18.0, 0.05));
}

TEST_CASE ("Band gain lifts or lowers just that band", "[dynamics]")
{
    DynamicsParams p;
    p.bands[3].gainDb = 3.0;
    auto c = tones (48000.0, 1.5, { { 12000.0, -20.0 }, { 400.0, -20.0 } });
    MultibandDynamics d;
    d.prepare (48000.0, 2);
    d.setParams (p);
    d.reset();
    run (d, c);
    CHECK_THAT (toneDb (c, 12000.0, 1.0, 1.5), WithinAbs (-17.0, 0.1));
    CHECK_THAT (toneDb (c, 400.0, 1.0, 1.5), WithinAbs (-20.0, 0.05));
}

TEST_CASE ("Attack and release follow their settings", "[dynamics]")
{
    const double sr = 48000.0;
    DynamicsParams p;
    p.bands[1] = { -30.0, 4.0, 10.0, 150.0, 0.0 };
    p.adaptiveRelease = false;
    // 400 Hz: quiet (10 dB under the threshold, clear of the knee), then 28 dB louder for 1 s, then quiet.
    AudioClip c = testsig::silence (sr, 2, 3.0);
    for (int i = 0; i < c.numSamples(); ++i)
    {
        const double t = i / sr;
        const double a = (t >= 1.0 && t < 2.0) ? std::pow (10.0, -12.0 / 20.0) : std::pow (10.0, -40.0 / 20.0);
        c.channels[0][static_cast<size_t> (i)] = c.channels[1][static_cast<size_t> (i)] = static_cast<float> (a * std::sin (2.0 * std::numbers::pi * 400.0 * t));
    }
    MultibandDynamics d;
    d.prepare (sr, 2);
    d.setParams (p);
    d.reset();
    run (d, c, 64);
    const double settled = toneDb (c, 400.0, 1.6, 1.9);                     // ~ -30 + 18/4 = -25.5 (+ crossover share)
    CHECK_THAT (settled, WithinAbs (-24.6, 0.5));
    CHECK (toneDb (c, 400.0, 1.0, 1.005) > settled + 4.0);                   // the first ms still get through
    CHECK_THAT (toneDb (c, 400.0, 1.1, 1.2), WithinAbs (settled, 1.0));      // caught within ~100 ms
    CHECK (toneDb (c, 400.0, 2.0, 2.02) < -46.0);                            // just after: still turned down
    CHECK_THAT (toneDb (c, 400.0, 2.7, 3.0), WithinAbs (-40.0, 0.3));        // released after ~0.7 s
}

TEST_CASE ("Compressing a bass tone stays clean", "[dynamics]")
{
    const double sr = 48000.0;
    DynamicsParams p;
    p.bands[0] = { -20.0, 4.0, 30.0, 200.0, 0.0 };
    auto c = tones (sr, 2.0, { { 60.0, -6.0 } });
    MultibandDynamics d;
    d.prepare (sr, 2);
    d.setParams (p);
    d.reset();
    run (d, c);
    const double fundamental = toneDb (c, 60.0, 1.0, 2.0);
    double harmonics = 0.0;
    for (int h = 2; h <= 6; ++h)
        harmonics += std::pow (10.0, toneDb (c, 60.0 * h, 1.0, 2.0) / 10.0);
    const double thdDb = 10.0 * std::log10 (harmonics + 1.0e-30) - fundamental;
    INFO ("THD " << thdDb << " dB");
    CHECK (thdDb < -50.0);   // under 0.3 %
}

TEST_CASE ("Dynamics never clicks and stays stable", "[dynamics]")
{
    for (double sr : { 44100.0, 96000.0, 192000.0 })
    {
        auto c = tones (sr, 3.0, { { 80.0, -6.0 }, { 900.0, -12.0 } });
        MultibandDynamics d;
        d.prepare (sr, 2);
        DynamicsParams heavy;
        for (auto& b : heavy.bands) { b.thresholdDb = -30.0; b.ratio = 8.0; b.attackMs = 1.0; b.gainDb = 6.0; }
        const int block = 256, n = c.numSamples();
        for (int start = 0; start < n; start += block)
        {
            if (start == static_cast<int> (0.5 * sr) / block * block) d.setParams (heavy);
            if (start == static_cast<int> (1.5 * sr) / block * block) { auto moved = heavy; moved.crossoverHz = { 250.0, 400.0, 3000.0 }; d.setParams (moved); }
            if (start == static_cast<int> (2.2 * sr) / block * block) d.setParams (DynamicsParams {});
            const int len = std::min (block, n - start);
            float* ch[2] = { c.channels[0].data() + start, c.channels[1].data() + start };
            d.process (ch, 2, len);
        }
        double worst = 0.0;
        for (const auto& ch : c.channels)
            for (size_t i = 1; i < ch.size(); ++i)
            {
                REQUIRE (std::isfinite (ch[i]));
                worst = std::max (worst, static_cast<double> (std::abs (ch[i] - ch[i - 1])));
            }
        // Steepest honest step: both tones at full level with +6 dB makeup.
        const double limit = 2.0 * (0.5 * 2.0 * std::numbers::pi * 80.0 + 0.25 * 2.0 * std::numbers::pi * 900.0) / sr * 1.5;
        INFO ("sr " << sr << " worst " << worst << " limit " << limit);
        CHECK (worst < limit);
        CHECK_FALSE (d.isActive());   // back to neutral: pass-through again
    }
}

TEST_CASE ("Dynamics switches on and off without a hole at its crossovers", "[dynamics][switching]")
{
    // The four bands add up to an all-pass. Blending them with the plain signal used to cancel around each
    // crossover: a 1 kHz tone fell 21 dB for ~50 ms when a band gain left 0 dB (even for 0.1 dB) or the
    // module was switched (A / B, a snapshot, undo). Now the short-time level stays within 1 dB, and once
    // through it is exactly the steady setting (or an exact pass-through).
    DynamicsParams air;
    air.bands[3].gainDb = 0.1;
    DynamicsParams glue;   // compressing settings, low split at 40 Hz; the -6 dB tone stays under the threshold
    glue.crossoverHz[0] = 40.0;
    for (auto& b : glue.bands) { b.ratio = 2.0; b.thresholdDb = 0.0; }
    DynamicsParams impact;
    for (auto& b : impact.bands) b.impact = 50.0;
    DynamicsParams glueOff = glue;
    glueOff.enabled = false;
    struct Switch { const char* what; DynamicsParams off, on; };
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const auto& sw : { Switch { "neutral -> Air +0.1 dB", DynamicsParams {}, air }, Switch { "module switch, low split 40 Hz", glueOff, glue },
                                Switch { "neutral -> Impact +50 % on every band", DynamicsParams {}, impact } })
            for (double hz : { 40.0, 120.0, 1000.0, 6000.0, 12000.0 })
            {
                const auto tone = tones (sr, 2.0, { { hz, -6.0 } });
                const int n = tone.numSamples(), at = static_cast<int> (0.5 * sr) / 64 * 64;
                auto render = [&] (const DynamicsParams& from, const DynamicsParams& to, MultibandDynamics& d)
                {
                    AudioClip c = tone;
                    d.prepare (sr, 2);
                    d.setParams (from);
                    d.reset();
                    for (int s = 0; s < n; s += 64)
                    {
                        if (s == at) d.setParams (to);
                        float* ch[2] = { c.channels[0].data() + s, c.channels[1].data() + s };
                        d.process (ch, 2, std::min (64, n - s));
                    }
                    return std::vector<double> (c.channels[0].begin(), c.channels[0].end());
                };
                MultibandDynamics steady;
                const auto steadyOn = render (sw.on, sw.on, steady);
                const std::vector<double> plain (tone.channels[0].begin(), tone.channels[0].end());
                for (bool switchOn : { true, false })
                {
                    MultibandDynamics d;
                    const auto out = render (switchOn ? sw.off : sw.on, switchOn ? sw.on : sw.off, d);
                    const auto r = switchlevel::measure (out, sr, hz, at);
                    INFO ("sr " << sr << ", " << sw.what << " switched " << (switchOn ? "on" : "off") << ", " << hz << " Hz tone: dip " << r.dipDb << " dB, swell +" << r.bumpDb << " dB");
                    CHECK (r.dipDb > -1.0);
                    CHECK (r.bumpDb < 1.0);
                    if (hz <= 1000.0)   // and no click: no sample-to-sample step beyond the tone's own
                        CHECK (switchlevel::stepRatio (out, at) < 1.2);
                    double diff = 0.0;
                    for (int i = static_cast<int> (1.5 * sr); i < n; ++i)
                        diff = std::max (diff, std::abs (out[static_cast<size_t> (i)] - (switchOn ? steadyOn : plain)[static_cast<size_t> (i)]));
                    if (switchOn)
                        CHECK (diff < 1.0e-6);   // (the output is float)
                    else
                    {
                        CHECK (diff == 0.0);
                        CHECK_FALSE (d.isActive());
                    }
                }
            }
}

TEST_CASE ("Dynamics: a soloed band plays on while the module switches", "[dynamics][switching][monitoring]")
{
    // With a band soloed, the solo is what is heard: the crossovers stay in place while the module switches
    // on or off, so the soloed band doesn't drop out or wander.
    const double sr = 48000.0;
    DynamicsParams glue;
    for (auto& b : glue.bands) { b.ratio = 2.0; b.thresholdDb = 0.0; }
    for (bool switchOn : { true, false })
    {
        const auto tone = tones (sr, 2.0, { { 400.0, -6.0 } });   // in the low-mid band (120 Hz - 1 kHz)
        const int n = tone.numSamples(), at = static_cast<int> (0.5 * sr) / 64 * 64;
        MultibandDynamics d;
        d.setParams (switchOn ? DynamicsParams {} : glue);
        d.setMonitorBand (1, false);
        d.prepare (sr, 2);
        AudioClip c = tone;
        std::array<std::vector<double>, 2> mon { std::vector<double> (static_cast<size_t> (n)), std::vector<double> (static_cast<size_t> (n)) };
        for (int s = 0; s < n; s += 64)
        {
            if (s == at) d.setParams (switchOn ? glue : DynamicsParams {});
            float* ch[2] = { c.channels[0].data() + s, c.channels[1].data() + s };
            double* mo[2] = { mon[0].data() + s, mon[1].data() + s };
            d.process (ch, 2, std::min (64, n - s), mo);
        }
        const auto r = switchlevel::measure (mon[0], sr, 400.0, at);
        INFO ("module switched " << (switchOn ? "on" : "off") << " during a low-mid solo: dip " << r.dipDb << " dB, swell +" << r.bumpDb << " dB");
        CHECK (r.dipDb > -0.5);
        CHECK (r.bumpDb < 0.5);
    }
}

namespace {
// Kick-like hits (55 Hz, 2 ms rise, 150 ms decay) every 0.5 s over a steady 1 kHz pad at -20 dB.
AudioClip kicks (double sr, double seconds)
{
    AudioClip c = testsig::silence (sr, 2, seconds);
    const int period = static_cast<int> (0.5 * sr);
    for (int i = 0; i < c.numSamples(); ++i)
    {
        const double t = static_cast<double> (i % period) / sr;
        const double env = std::min (1.0, t / 0.002) * std::exp (-t / 0.15);
        const double v = 0.5 * env * std::sin (2.0 * std::numbers::pi * 55.0 * t)
                       + 0.1 * std::sin (2.0 * std::numbers::pi * 1000.0 * i / sr);
        c.channels[0][static_cast<size_t> (i)] = c.channels[1][static_cast<size_t> (i)] = static_cast<float> (v);
    }
    return c;
}

/** Average over the hits (after the first second) of each hit's peak in its first 40 ms (dB). */
double hitPeakDb (const AudioClip& c)
{
    const int period = static_cast<int> (0.5 * c.sampleRate), win = static_cast<int> (0.04 * c.sampleRate);
    double sum = 0.0;
    int count = 0;
    for (int start = 2 * period; start + period <= c.numSamples(); start += period)
    {
        double p = 0.0;
        for (int i = start; i < start + win; ++i) p = std::max (p, std::abs (static_cast<double> (c.channels[0][static_cast<size_t> (i)])));
        sum += 20.0 * std::log10 (p);
        ++count;
    }
    return sum / count;
}
} // namespace

TEST_CASE ("Impact leaves a steady sound alone", "[dynamics][impact]")
{
    DynamicsParams p;
    for (auto& b : p.bands) b.impact = 100.0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        // (The tone starting is a hit: loudness keeping settles over a few seconds after it.)
        auto c = tones (sr, 12.0, { { 60.0, -10.0 }, { 2000.0, -16.0 }, { 9000.0, -22.0 } });
        MultibandDynamics d;
        d.prepare (sr, 2);
        d.setParams (p);
        d.reset();
        CHECK (d.isActive());
        run (d, c);
        INFO ("sr " << sr);
        CHECK_THAT (toneDb (c, 60.0, 10.0, 12.0), WithinAbs (-10.0, 0.1));
        CHECK_THAT (toneDb (c, 2000.0, 10.0, 12.0), WithinAbs (-16.0, 0.1));
        CHECK_THAT (toneDb (c, 9000.0, 10.0, 12.0), WithinAbs (-22.0, 0.1));
    }
}

TEST_CASE ("Impact makes hits punch harder or softer without changing the loudness much", "[dynamics][impact]")
{
    const double sr = 48000.0;
    const auto in = kicks (sr, 10.0);
    const double inPeak = hitPeakDb (in), inLufs = measure (in).integratedLufs;
    auto render = [&] (double impactLow)
    {
        DynamicsParams p;
        p.bands[0].impact = impactLow;
        AudioClip c = in;
        MultibandDynamics d;
        d.prepare (sr, 2);
        d.setParams (p);
        d.reset();
        run (d, c);
        return c;
    };
    const auto up = render (100.0), half = render (50.0), down = render (-100.0);
    const double upPeak = hitPeakDb (up), halfPeak = hitPeakDb (half), downPeak = hitPeakDb (down);
    INFO ("hit peak: in " << inPeak << ", +100 % " << upPeak << ", +50 % " << halfPeak << ", -100 % " << downPeak);
    // At the same loudness (below), +100 % lifts the hits ~2 dB, -100 % softens them.
    CHECK (upPeak > inPeak + 1.5);
    CHECK (halfPeak > inPeak + 0.7);
    CHECK (halfPeak < upPeak);
    CHECK (downPeak < inPeak - 0.4);
    CHECK (upPeak < inPeak + kImpactMaxDb + 0.5);
    // Loudness is kept (measured after the 2 s it takes to settle).
    auto lufsAfter = [] (const AudioClip& c)
    {
        AudioClip tail = c;
        const auto skip = static_cast<size_t> (3.0 * c.sampleRate);
        for (auto& ch : tail.channels) ch.erase (ch.begin(), ch.begin() + static_cast<std::ptrdiff_t> (skip));
        return measure (tail).integratedLufs;
    };
    const double inTail = lufsAfter (in), upLufs = lufsAfter (up), downLufs = lufsAfter (down);
    INFO ("LUFS: in " << inTail << ", +100 % " << upLufs << ", -100 % " << downLufs << " (whole clip in " << inLufs << ")");
    CHECK (std::abs (upLufs - inTail) < 0.5);
    CHECK (std::abs (downLufs - inTail) < 0.5);
    // The steady pad (another band) is untouched.
    CHECK_THAT (toneDb (up, 1000.0, 1.0, 6.0), WithinAbs (toneDb (in, 1000.0, 1.0, 6.0), 0.15));
    // Only the low band was set: the meter reports its boost, the others stay at 0.
    MultibandDynamics d;
    DynamicsParams p;
    p.bands[0].impact = 100.0;
    d.prepare (sr, 2);
    d.setParams (p);
    d.reset();
    AudioClip c = in;
    run (d, c);
    CHECK (d.takeMaxImpactDb (0) > 2.0);
    CHECK (d.takeMaxImpactDb (2) == 0.0);
}

TEST_CASE ("Impact under 0.5 % counts as off", "[dynamics][impact]")
{
    DynamicsParams p;
    p.bands[1].impact = 0.4;
    CHECK (MultibandDynamics::isNeutral (p));
    p.bands[1].impact = -0.6;
    CHECK_FALSE (MultibandDynamics::isNeutral (p));
}

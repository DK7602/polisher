#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Signals.h"
#include "polisher/MultibandDynamics.h"

#include <cmath>
#include <numbers>

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

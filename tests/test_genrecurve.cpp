// The genre's typical balance on the spectrum display (the dotted "Trap target" line): it must be the
// balance Auto-Master's EQ aims for (genreTargetCurve), drawn where the live display would draw a
// master that has exactly that balance at that loudness.

#include "polisher/AutoMaster.h"
#include "polisher/LoudnessMeter.h"
#include "Signals.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <random>

using namespace polisher;
using Catch::Matchers::WithinAbs;

namespace {

constexpr Genre kAllGenres[] = { Genre::trapHipHop, Genre::rnb, Genre::pop, Genre::edm, Genre::rock, Genre::acoustic };

double displayHz (int i) { return 20.0 * std::pow (1000.0, static_cast<double> (i) / (kDisplayPoints - 1)); }

void fft (std::vector<std::complex<double>>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = (inverse ? 2.0 : -2.0) * std::numbers::pi / static_cast<double> (len);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k, w *= wl)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

double lufsOf (const AudioClip& clip)
{
    LoudnessMeter m;
    m.prepare (clip.sampleRate, clip.numChannels());
    std::vector<const float*> ptrs;
    for (const auto& ch : clip.channels) ptrs.push_back (ch.data());
    m.process (ptrs.data(), clip.numChannels(), clip.numSamples());
    return m.integratedLufs();
}

/** Steady noise with exactly this balance (the same in both channels), at this loudness. */
AudioClip noiseWithBalance (const std::array<double, kEqBands>& bands, double sr, double lufs, unsigned seed = 3)
{
    const BalanceCurve curve (bands);
    constexpr size_t n = size_t (1) << 19;   // ~11 s at 48 kHz
    std::mt19937 rng (seed);
    std::normal_distribution<double> white (0.0, 1.0);
    std::vector<std::complex<double>> buf (n);
    for (auto& v : buf) v = white (rng);
    fft (buf, false);
    for (size_t k = 0; k <= n / 2; ++k)
    {
        const double f = static_cast<double> (k) * sr / static_cast<double> (n);
        const double gain = f > 0.0 && f < std::min (20000.0, 0.5 * sr)
                                ? std::pow (10.0, (curve.db (f) - 4.5 * std::log2 (f / 1000.0)) / 20.0) : 0.0;
        buf[k] *= gain;
        if (k > 0 && k < n / 2) buf[n - k] *= gain;
    }
    fft (buf, true);
    AudioClip clip = testsig::silence (sr, 2, 0.0);
    for (auto& ch : clip.channels)
    {
        ch.resize (n);
        for (size_t i = 0; i < n; ++i) ch[i] = static_cast<float> (buf[i].real());
    }
    const double gain = std::pow (10.0, (lufs - lufsOf (clip)) / 20.0);
    for (auto& ch : clip.channels)
        for (auto& s : ch) s = static_cast<float> (s * gain);
    return clip;
}

} // namespace

TEST_CASE ("Genre curve: measured like Auto-Master measures a mix, it reads exactly the genre's band norms", "[genrecurve]")
{
    for (const auto g : kAllGenres)
    {
        const auto norms = genreTargetCurve (g);
        const BalanceCurve curve (norms);
        for (int b = 0; b < kEqBands; ++b)
        {
            INFO (genreName (g) << " band " << b);
            CHECK_THAT (curve.bandDb (b), WithinAbs (norms[static_cast<size_t> (b)], 1.0e-4));
        }
    }
}

TEST_CASE ("Genre curve: smooth, no bumps the norms don't have, falling below 20 Hz", "[genrecurve]")
{
    // Band centres (Hz), where the curve passes through (close to) each band's norm.
    const double centres[kEqBands] = { 36.06, 108.2, 328.6, 979.8, 2366.4, 5291.5, 12649.1 };
    for (const auto g : kAllGenres)
    {
        INFO (genreName (g));
        const auto norms = genreTargetCurve (g);
        const BalanceCurve curve (norms);
        // Continuous: no 1/48-octave step is steeper than 24 dB/oct (the steepest place is the sub,
        // where Rock and Acoustic fall 12-14 dB between the Bass and Low End centres).
        double worstStep = 0.0;
        for (double t = std::log2 (20.0); t < std::log2 (20000.0); t += 1.0 / 48.0)
            worstStep = std::max (worstStep, std::abs (curve.db (std::exp2 (t + 1.0 / 48.0)) - curve.db (std::exp2 (t))));
        CHECK (worstStep < 0.5);
        // In the mids the curve passes close to each band's norm at the band's centre. Not exactly, and
        // not at all in the lows: a band's mean power per Hz is dominated by its upper end, so where
        // the balance is steep the centre sits off the band's number. Rock's Low End band (20-65 Hz)
        // reads -11 dB mostly from its 50-65 Hz part; the curve is at -15 dB at 36 Hz.
        for (int b = 2; b <= 5; ++b)
        {
            INFO ("band " << b);
            CHECK_THAT (curve.db (centres[b]), WithinAbs (norms[static_cast<size_t> (b)], 0.5));
        }
        // Between two band centres it stays between its values there (a monotone cubic: no
        // overshoot, no ripple).
        for (int b = 0; b + 1 < kEqBands; ++b)
        {
            const double c0 = curve.db (centres[b]), c1 = curve.db (centres[b + 1]);
            for (double t = std::log2 (centres[b]); t <= std::log2 (centres[b + 1]); t += 1.0 / 24.0)
            {
                const double v = curve.db (std::exp2 (t));
                INFO ("between band " << b << " and " << b + 1 << ", at " << std::exp2 (t) << " Hz: " << v);
                CHECK (v >= std::min (c0, c1) - 0.01);
                CHECK (v <= std::max (c0, c1) + 0.01);
            }
        }
        CHECK (curve.db (10.0) < curve.db (20.0) - 20.0);   // the low cut
    }
}

TEST_CASE ("Genre curve: on the display it keeps the norms' shape, and each genre looks different", "[genrecurve]")
{
    std::vector<std::vector<double>> curves;
    for (const auto g : kAllGenres)
    {
        INFO (genreName (g));
        const auto d = genreDisplayCurve (g, 48000.0);
        REQUIRE (d.size() == static_cast<size_t> (kDisplayPoints));
        const BalanceCurve curve (genreTargetCurve (g));
        // The display point nearest 1 kHz is the anchor; elsewhere the dotted line follows the
        // balance curve. Below ~100 Hz the display's own bins (11.7 Hz apart at 48 kHz) and window
        // blur steep slopes, and below ~60 Hz neighbouring points share its few lowest bins: there it
        // blurs and steps for this line exactly as for the live ones.
        const int at1k = 54;   // 20 * 1000^(54/95) = 1.01 kHz
        for (int i = 0; i < kDisplayPoints; ++i)
        {
            const double f = displayHz (i);
            if (f < 60.0 || f > 16000.0) continue;
            INFO (f << " Hz");
            CHECK_THAT (d[static_cast<size_t> (i)] - d[at1k], WithinAbs (curve.db (f) - curve.db (displayHz (at1k)), f < 100.0 ? 1.5 : 0.5));
        }
        // Continuous on screen too: from one point to the next it moves as the balance does (the last
        // point straddles the 20 kHz edge).
        for (int i = 1; i < kDisplayPoints - 1; ++i)
            if (displayHz (i - 1) >= 60.0)
            {
                const double step = d[static_cast<size_t> (i)] - d[static_cast<size_t> (i - 1)];
                CHECK_THAT (step, WithinAbs (curve.db (displayHz (i)) - curve.db (displayHz (i - 1)), 0.8));
            }
        curves.push_back (d);
    }
    // Every pair of genres differs somewhere by more than 1.5 dB (at the same loudness).
    for (size_t a = 0; a < curves.size(); ++a)
        for (size_t b = a + 1; b < curves.size(); ++b)
        {
            double most = 0.0;
            for (size_t i = 0; i < curves[a].size(); ++i) most = std::max (most, std::abs (curves[a][i] - curves[b][i]));
            INFO (genreName (kAllGenres[a]) << " vs " << genreName (kAllGenres[b]));
            CHECK (most > 1.5);
        }
    // Trap carries more sub than Rock or Acoustic (relative to its mids), as the norms say.
    const auto trap = genreDisplayCurve (Genre::trapHipHop, 48000.0), rock = genreDisplayCurve (Genre::rock, 48000.0);
    const auto acoustic = genreDisplayCurve (Genre::acoustic, 48000.0);
    const int atSub = 7;   // 20 * 1000^(7/95) = 33 Hz
    CHECK ((trap[atSub] - trap[54]) > (rock[atSub] - rock[54]) + 4.0);
    CHECK ((trap[atSub] - trap[54]) > (acoustic[atSub] - acoustic[54]) + 6.0);
    // A louder master draws the same line higher by exactly the difference.
    const auto louder = genreDisplayCurve (Genre::trapHipHop, 48000.0, -6.0);
    for (size_t i = 0; i < trap.size(); ++i)
        CHECK_THAT (louder[i] - trap[i], WithinAbs (3.0, 0.11));
}

TEST_CASE ("Genre curve: a steady master with exactly the genre's balance is drawn on the dotted line", "[genrecurve]")
{
    for (const double sr : { 48000.0, 44100.0 })
        for (const auto g : { Genre::trapHipHop, Genre::rock })
        {
            INFO (genreName (g) << " at " << sr << " Hz");
            const auto norms = genreTargetCurve (g);
            const auto noise = noiseWithBalance (norms, sr, -9.0);
            // Its loudness and balance, measured by the meters and by Auto-Master's own analysis.
            CHECK_THAT (lufsOf (noise), WithinAbs (-9.0, 0.01));
            const auto a = analyseMix (noise);
            REQUIRE (a.usable);
            const double nRef = (norms[1] + norms[2] + norms[3] + norms[4] + norms[5]) / 5.0;
            for (int b = 0; b < kEqBands; ++b)
            {
                INFO ("band " << b);
                // (The sub band picks up a little window leakage from the steep slope above it.)
                CHECK_THAT (a.bandDb[static_cast<size_t> (b)], WithinAbs (norms[static_cast<size_t> (b)] - nRef, b == 0 ? 0.5 : 0.3));
            }
            // Its average spectrum, computed exactly as the display (and a reference's curve) does,
            // against the dotted line at the same loudness.
            const auto live = spectrumDisplayCurve (noise);
            const auto dotted = genreDisplayCurve (g, sr, -9.0);
            double sum = 0.0, worst = 0.0;
            int count = 0;
            for (int i = 0; i < kDisplayPoints; ++i)
            {
                const double f = displayHz (i);
                const double diff = live[static_cast<size_t> (i)] - dotted[static_cast<size_t> (i)];
                UNSCOPED_INFO (f << " Hz: live " << live[static_cast<size_t> (i)] << " dotted " << dotted[static_cast<size_t> (i)] << " diff " << diff);
                if (f < 25.0 || f > 18000.0) continue;
                sum += diff;
                worst = std::max (worst, std::abs (diff));
                ++count;
            }
            // The display's fast-rise / slow-fall smoothing reads noise a little above its average
            // power (most where a point averages few bins); otherwise the two agree.
            CHECK (sum / count > 0.0);
            CHECK (sum / count < 1.0);
            CHECK (worst < 1.5);
        }
}


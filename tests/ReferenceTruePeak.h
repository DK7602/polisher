#pragma once

// An independent, much finer true-peak meter for the tests: 16x oversampling with a long
// Kaiser-windowed sinc (128 taps per phase, flat to ~0.47 fs), computed in double precision. The
// plug-in's own meter and limiter use a 4x detector (BS.1770-4); this one checks them from outside,
// so a shared blind spot can't hide an over.

#include "polisher/LoudnessTargeting.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace refpeak {

inline double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 64; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-17 * sum) break;
    }
    return sum;
}

/** Highest absolute value of the band-limited reconstruction of x (linear, not dB). */
inline double truePeak (const std::vector<float>& x)
{
    constexpr int over = 16, half = 64;          // taps reach 64 input samples each side
    constexpr double beta = 10.0;
    static const std::vector<double> kernel = []
    {
        std::vector<double> k (static_cast<size_t> (over * 2 * half + 1));
        const double i0b = besselI0 (beta);
        for (size_t i = 0; i < k.size(); ++i)
        {
            const double t = (static_cast<double> (i) - over * half) / over;     // in input samples
            const double sinc = t == 0.0 ? 1.0 : std::sin (std::numbers::pi * t) / (std::numbers::pi * t);
            const double r = t / half;
            k[i] = sinc * besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
        }
        return k;
    }();

    const int n = static_cast<int> (x.size());
    double peak = 0.0;
    for (int i = 0; i < n; ++i)
        peak = std::max (peak, std::abs ((double) x[static_cast<size_t> (i)]));

    // Only stretches near the loudest samples can hold the true peak: an inter-sample peak is at most
    // a few dB above its neighbours for any real signal, so look between samples within 12 dB of it.
    const double gate = peak * std::pow (10.0, -12.0 / 20.0);
    for (int i = 0; i + 1 < n; ++i)
    {
        if (std::abs (x[static_cast<size_t> (i)]) < gate && std::abs (x[static_cast<size_t> (i + 1)]) < gate) continue;
        for (int ph = 1; ph < over; ++ph)
        {
            double acc = 0.0;
            for (int j = i - half + 1; j <= i + half; ++j)
            {
                if (j < 0 || j >= n) continue;
                acc += x[static_cast<size_t> (j)] * kernel[static_cast<size_t> ((i - j) * over + ph + over * half)];
            }
            peak = std::max (peak, std::abs (acc));
        }
    }
    return peak;
}

inline double truePeakDb (const polisher::AudioClip& clip)
{
    double p = 0.0;
    for (const auto& ch : clip.channels)
        p = std::max (p, truePeak (ch));
    return 20.0 * std::log10 (std::max (p, 1e-12));
}

} // namespace refpeak

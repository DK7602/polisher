#pragma once

// The decisions behind the blind listening test, kept free of JUCE so the unit tests can check them:
// how much to turn each file down so all play at the same loudness without clipping, and the
// shuffled order of the samples.

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <vector>

namespace blindtest {

struct Measured
{
    double integratedLufs = -INFINITY;   // BS.1770 / EBU R128
    double truePeakDbtp = -INFINITY;
};

/** Gain in dB for each file: every file lands on the quietest one's loudness (turned down, never
    up, so no file gains noise or clips from a boost), then all move down together if any would
    peak above `maxPeakDbtp`. Files with no measurable loudness (silence) get 0 dB. */
inline std::vector<double> matchGains (const std::vector<Measured>& files, double maxPeakDbtp = -1.0)
{
    std::vector<double> gains (files.size(), 0.0);
    double quietest = INFINITY;
    for (const auto& f : files)
        if (std::isfinite (f.integratedLufs))
            quietest = std::min (quietest, f.integratedLufs);
    if (! std::isfinite (quietest))
        return gains;

    double highestPeak = -INFINITY;
    for (size_t i = 0; i < files.size(); ++i)
    {
        if (! std::isfinite (files[i].integratedLufs))
            continue;
        gains[i] = quietest - files[i].integratedLufs;                      // <= 0
        highestPeak = std::max (highestPeak, files[i].truePeakDbtp + gains[i]);
    }
    if (std::isfinite (highestPeak) && highestPeak > maxPeakDbtp)
        for (size_t i = 0; i < files.size(); ++i)
            if (std::isfinite (files[i].integratedLufs))
                gains[i] -= highestPeak - maxPeakDbtp;
    return gains;
}

/** A random order of 0..n-1 (sample A is files[order[0]], B is files[order[1]], ...). */
inline std::vector<int> shuffledOrder (int n, unsigned seed)
{
    std::vector<int> order (static_cast<size_t> (n));
    std::iota (order.begin(), order.end(), 0);
    std::mt19937 rng (seed);
    std::shuffle (order.begin(), order.end(), rng);
    return order;
}

} // namespace blindtest

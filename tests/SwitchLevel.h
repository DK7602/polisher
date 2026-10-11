#pragma once

// Short-time level of a steady tone through a switch: what the ear hears as a hole (or a swell), which the
// largest sample-to-sample step never shows (a cross-fade that cancels makes no step at all).

#include <algorithm>
#include <cmath>
#include <vector>

namespace switchlevel {

struct Result
{
    double dipDb = 0.0;    // lowest short-time level below the lower of the steady levels before and after (<= 0)
    double bumpDb = 0.0;   // highest short-time level above the higher of them (>= 0)
};

/** Window for a tone: a whole number of its periods (at least two, and at least 5 ms), so a steady tone
    reads the same wherever the window starts. */
inline int windowFor (double toneHz, double sampleRate)
{
    const double periods = std::max (2.0, std::ceil (0.005 * toneHz));
    return std::max (1, static_cast<int> (std::lround (periods * sampleRate / toneHz)));
}

inline double levelDb (const std::vector<double>& x, int from, int length)
{
    double sum = 0.0;
    for (int i = from; i < from + length; ++i)
        sum += x[static_cast<size_t> (i)] * x[static_cast<size_t> (i)];
    return 10.0 * std::log10 (sum / length + 1.0e-30);
}

/** `x`: the tone after the processor, switched at sample `switchAt`. The steady level before is read over
    the 0.2 s before the switch, the one after over the last 0.2 s of `x` (which must be settled by then);
    the switch is judged from one window before it to there. */
inline Result measure (const std::vector<double>& x, double sampleRate, double toneHz, int switchAt)
{
    const int w = windowFor (toneHz, sampleRate), hop = std::max (1, w / 8);
    const int n = static_cast<int> (x.size()), tail = static_cast<int> (0.2 * sampleRate);
    double before = 1.0e9, after = 1.0e9, beforeMax = -1.0e9, afterMax = -1.0e9;
    for (int s = switchAt - tail; s + w <= switchAt; s += hop)
    {
        const double l = levelDb (x, s, w);
        before = std::min (before, l);
        beforeMax = std::max (beforeMax, l);
    }
    for (int s = n - tail; s + w <= n; s += hop)
    {
        const double l = levelDb (x, s, w);
        after = std::min (after, l);
        afterMax = std::max (afterMax, l);
    }
    Result r;
    for (int s = switchAt - w; s + w <= n - tail; s += hop)
    {
        const double l = levelDb (x, s, w);
        r.dipDb = std::min (r.dipDb, l - std::min (before, after));
        r.bumpDb = std::max (r.bumpDb, l - std::max (beforeMax, afterMax));
    }
    return r;
}

/** Largest sample-to-sample step after `from`, over the largest before it (a click shows as a big ratio). */
inline double stepRatio (const std::vector<double>& x, int from)
{
    double before = 0.0, after = 0.0;
    for (int i = 1; i < static_cast<int> (x.size()); ++i)
    {
        const double d = std::abs (x[static_cast<size_t> (i)] - x[static_cast<size_t> (i - 1)]);
        (i < from ? before : after) = std::max (i < from ? before : after, d);
    }
    return after / (before + 1.0e-30);
}

} // namespace switchlevel

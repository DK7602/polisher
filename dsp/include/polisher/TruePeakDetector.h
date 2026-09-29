#pragma once

#include <array>

namespace polisher {

/** Estimates inter-sample ("true") peaks per ITU-R BS.1770-4 Annex 2 by 4x polyphase
    oversampling with a Kaiser-windowed sinc interpolator.

    Each call to process() consumes one input sample and returns the largest absolute value
    among the four interpolated points covering the interval [n - latency, n - latency + 0.75]. */
class TruePeakDetector
{
public:
    static constexpr int kOversampling = 4;
    static constexpr int kTapsPerPhase = 25;          // 4 * 24 + 1 taps in the prototype
    static constexpr int kLatency      = 12;          // samples; phase 0 lands exactly on x[n - 12]

    TruePeakDetector() noexcept { reset(); }

    void reset() noexcept;

    /** Push one sample; returns max |interpolated value| for this step (linear, not dB). */
    double process (double x) noexcept;

    static constexpr int latencySamples() noexcept { return kLatency; }

private:
    std::array<double, 2 * kTapsPerPhase> history {};
    int writePos = 0;
};

} // namespace polisher

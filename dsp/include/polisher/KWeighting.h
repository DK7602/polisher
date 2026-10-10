#pragma once

#include "Biquad.h"

#include <numbers>

namespace polisher {

/** ITU-R BS.1770-4 "K" frequency weighting: a high-shelf (head effects) followed by a
    high-pass (RLB). Coefficients are derived for any sample rate, matching the published
    48 kHz values exactly at 48 kHz. */
class KWeighting
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept { shelf.reset(); highPass.reset(); }

    inline double process (double x) noexcept { return highPass.process (shelf.process (x)); }

    /** Power gain |H|^2 of the weighting at a frequency (Hz), at the rate given to prepare(). */
    double powerGain (double hz) const noexcept
    {
        const double w = 2.0 * std::numbers::pi * hz / rate;
        return shelf.powerGain (w) * highPass.powerGain (w);
    }

private:
    Biquad shelf, highPass;
    double rate = 48000.0;
};

} // namespace polisher

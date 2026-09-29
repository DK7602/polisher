#pragma once

#include "Biquad.h"

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

private:
    Biquad shelf, highPass;
};

} // namespace polisher

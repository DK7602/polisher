#include "polisher/KWeighting.h"

#include <cmath>
#include <numbers>

namespace polisher {

void KWeighting::prepare (double sampleRate) noexcept
{
    rate = sampleRate;

    // Stage 1: high shelf, +4 dB above ~1.7 kHz (models the acoustic effect of the head).
    {
        const double f0 = 1681.974450955533;
        const double gainDb = 3.999843853973347;
        const double q = 0.7071752369554196;

        const double k  = std::tan (std::numbers::pi * f0 / sampleRate);
        const double vh = std::pow (10.0, gainDb / 20.0);
        const double vb = std::pow (vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;

        shelf.setCoefficients ((vh + vb * k / q + k * k),
                               2.0 * (k * k - vh),
                               (vh - vb * k / q + k * k),
                               a0,
                               2.0 * (k * k - 1.0),
                               (1.0 - k / q + k * k));
    }

    // Stage 2: second-order high-pass at ~38 Hz (revised low-frequency B-curve).
    {
        const double f0 = 38.13547087602444;
        const double q = 0.5003270373238773;
        const double k = std::tan (std::numbers::pi * f0 / sampleRate);
        const double a0 = 1.0 + k / q + k * k;

        highPass.setCoefficients (1.0, -2.0, 1.0,
                                  1.0,
                                  2.0 * (k * k - 1.0) / a0,
                                  (1.0 - k / q + k * k) / a0);
    }

    reset();
}

} // namespace polisher

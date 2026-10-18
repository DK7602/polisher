#pragma once

#include <cmath>

namespace polisher {

/** Double-precision biquad, transposed direct form II. Coefficients are normalised (a0 == 1). */
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    void setCoefficients (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        b0 = nb0 / na0;
        b1 = nb1 / na0;
        b2 = nb2 / na0;
        a1 = na1 / na0;
        a2 = na2 / na0;
    }

    void reset() noexcept { z1 = z2 = 0.0; }

    /** The state, summed (for the modules' NaN / inf guards: a non-finite sample gets into the state and
        never leaves on its own, and a sum of states is non-finite exactly when one of them is). */
    double stateSum() const noexcept { return z1 + z2; }

    /** Power gain |H|^2 at a normalised angular frequency w = 2 pi f / sampleRate. */
    double powerGain (double w) const noexcept
    {
        const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = 1.0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return (nr * nr + ni * ni) / (dr * dr + di * di);
    }

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

} // namespace polisher

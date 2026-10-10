#pragma once

#include "Biquad.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace polisher::design {

/** Normalised biquad coefficients (a0 == 1), RBJ cookbook. */
struct Coeffs { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };

inline void apply (Biquad& f, const Coeffs& c) noexcept
{
    f.b0 = c.b0; f.b1 = c.b1; f.b2 = c.b2; f.a1 = c.a1; f.a2 = c.a2;
}

/** 2nd-order Butterworth low- or high-pass (two in series = Linkwitz-Riley 4th order). */
inline Coeffs butterworth (bool highPass, double f, double sr) noexcept
{
    const double w0 = 2.0 * std::numbers::pi * std::min (f, 0.45 * sr) / sr;
    const double cw = std::cos (w0), alpha = std::sin (w0) / std::numbers::sqrt2;
    const double a0 = 1.0 + alpha;
    const double b0 = highPass ? (1.0 + cw) / 2.0 : (1.0 - cw) / 2.0;
    const double b1 = highPass ? -(1.0 + cw) : (1.0 - cw);
    return { b0 / a0, b1 / a0, b0 / a0, -2.0 * cw / a0, (1.0 - alpha) / a0 };
}

/** The 2nd-order all-pass that equals the sum of a Linkwitz-Riley 4th-order low- and high-pass
    at the same frequency (the Butterworth denominator, mirrored). */
inline Coeffs allpass (double f, double sr) noexcept
{
    const auto lp = butterworth (false, f, sr);
    return { lp.a2, lp.a1, 1.0, lp.a1, lp.a2 };
}

inline Coeffs lowShelf (double f, double gainDb, double sr, double q = 0.707) noexcept
{
    const double w0 = 2.0 * std::numbers::pi * f / sr, cw = std::cos (w0);
    const double A = std::pow (10.0, gainDb / 40.0), alpha = std::sin (w0) / (2.0 * q);
    const double k = 2.0 * std::sqrt (A) * alpha;
    const double a0 = (A + 1.0) + (A - 1.0) * cw + k;
    return { A * ((A + 1.0) - (A - 1.0) * cw + k) / a0, 2.0 * A * ((A - 1.0) - (A + 1.0) * cw) / a0,
             A * ((A + 1.0) - (A - 1.0) * cw - k) / a0, -2.0 * ((A - 1.0) + (A + 1.0) * cw) / a0,
             ((A + 1.0) + (A - 1.0) * cw - k) / a0 };
}

inline Coeffs bell (double f, double gainDb, double q, double sr) noexcept
{
    const double w0 = 2.0 * std::numbers::pi * f / sr, cw = std::cos (w0);
    const double A = std::pow (10.0, gainDb / 40.0), alpha = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + alpha / A;
    return { (1.0 + alpha * A) / a0, -2.0 * cw / a0, (1.0 - alpha * A) / a0, -2.0 * cw / a0, (1.0 - alpha / A) / a0 };
}

} // namespace polisher::design

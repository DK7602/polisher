#include "polisher/TonalEQ.h"

#include <complex>
#include <numbers>

namespace polisher {

namespace {
    double clampFreq (int band, double hz, double sampleRate) noexcept
    {
        const auto& spec = kEqBandSpecs[static_cast<size_t> (band)];
        return std::min (std::clamp (hz, spec.minHz, spec.maxHz), 0.45 * sampleRate);
    }

    /** Power gain |H(j w)|^2 of the RBJ analog prototype at x = w / w0 (A = 10^(gain/40)). */
    double analogPower (EqBandSpec::Shape shape, double A, double q, double x) noexcept
    {
        const double x2 = x * x;
        switch (shape)
        {
            case EqBandSpec::Shape::bell:
            {
                const double d = (1.0 - x2) * (1.0 - x2);
                return (d + (A * x / q) * (A * x / q)) / (d + (x / (A * q)) * (x / (A * q)));
            }
            case EqBandSpec::Shape::lowShelf:
            {
                const double k = std::sqrt (A) * x / q;
                return A * A * ((A - x2) * (A - x2) + k * k) / ((1.0 - A * x2) * (1.0 - A * x2) + k * k);
            }
            case EqBandSpec::Shape::highShelf:
            default:
            {
                const double k = std::sqrt (A) * x / q;
                return A * A * ((1.0 - A * x2) * (1.0 - A * x2) + k * k) / ((A - x2) * (A - x2) + k * k);
            }
        }
    }

    /** Magnitude-matched biquad for a cut (A <= 1), after Vicanek, "Matched Second Order Digital
        Filters" (2016). The poles are the analog poles mapped by impulse invariance. The squared
        magnitude of a biquad is linear in three numerator terms B0..B2 over
        phi1 = sin^2(w/2), phi0 = 1 - phi1, phi2 = 4 phi0 phi1:
            |H|^2 = (B0 phi0 + B1 phi1 + B2 phi2) / (A0 phi0 + A1 phi1 + A2 phi2).
        Vicanek matches the analog magnitude at DC, Nyquist and one more frequency. Here DC and the
        band frequency are matched exactly (so the band hits its set gain), and the one remaining
        degree of freedom is a closed-form weighted least-squares fit to the analog magnitude from
        half the band frequency up to 20 kHz. Nyquist itself is left free: it is above hearing, and
        letting it go is what keeps the high bands within a few tenths of a dB of the analog curve
        right up to 20 kHz at 44.1 kHz. No iteration, no latency. */
    BiquadCoefficients matchedCut (EqBandSpec::Shape shape, double A, double q, double w0, double wTop) noexcept
    {
        // Analog poles: bells have Q x A at w0; shelves have Q at w0 / sqrt(A) (low) or w0 sqrt(A) (high).
        double wp = w0, qp = q;
        if (shape == EqBandSpec::Shape::bell)           qp = q * A;
        else if (shape == EqBandSpec::Shape::lowShelf)  wp = w0 / std::sqrt (A);
        else                                            wp = w0 * std::sqrt (A);
        const double zeta = 0.5 / qp;
        const double r = std::exp (-zeta * wp);
        const double a1 = zeta <= 1.0 ? -2.0 * r * std::cos (std::sqrt (1.0 - zeta * zeta) * wp)
                                      : -2.0 * r * std::cosh (std::sqrt (zeta * zeta - 1.0) * wp);
        const double a2 = r * r;

        const double A0 = (1.0 + a1 + a2) * (1.0 + a1 + a2);
        const double A1 = (1.0 - a1 + a2) * (1.0 - a1 + a2);
        const double A2 = -4.0 * a2;
        struct Phi { double p0, p1, p2; };
        auto phi = [] (double w) { const double s = std::sin (0.5 * w); const double p1 = s * s; return Phi { 1.0 - p1, p1, 4.0 * (1.0 - p1) * p1 }; };
        auto target = [&] (double w, const Phi& p) { return analogPower (shape, A, q, w / w0) * (A0 * p.p0 + A1 * p.p1 + A2 * p.p2); };

        const double B0 = A0 * analogPower (shape, A, q, 0.0);
        const Phi pm = phi (w0);
        const double Tm = target (w0, pm);

        // With B0 fixed and the match at w0 solved for B1, the error at each grid point is c + B2 d.
        constexpr int kGrid = 12;
        const double lo = 0.5 * w0, ratio = std::pow (wTop / lo, 1.0 / (kGrid - 1));
        double num = 0.0, den = 0.0, w = lo;
        for (int k = 0; k < kGrid; ++k, w *= ratio)
        {
            const Phi p = phi (w);
            const double T = target (w, p);
            const double c = B0 * p.p0 + p.p1 * (Tm - B0 * pm.p0) / pm.p1 - T;
            const double d = p.p2 - p.p1 * pm.p2 / pm.p1;
            const double weight = 1.0 / (T * T);   // relative error, i.e. roughly error in dB
            num += weight * c * d;
            den += weight * d * d;
        }
        const double B2 = den > 0.0 ? -num / den : A2 * B0 / A0;
        const double B1 = std::max (0.0, (Tm - B0 * pm.p0 - B2 * pm.p2) / pm.p1);

        // Back to coefficients (Vicanek eq. 13): the minimum-phase numerator with these B's.
        const double s0 = std::sqrt (B0), s1 = std::sqrt (B1);
        const double W = 0.5 * (s0 + s1);
        const double b0 = 0.5 * (W + std::sqrt (std::max (0.0, W * W + B2)));
        const double b1 = 0.5 * (s0 - s1);
        const double b2 = -B2 / (4.0 * b0);
        return { b0, b1, b2, a1, a2 };
    }
}

double BiquadCoefficients::magnitudeDb (double f, double sampleRate) const noexcept
{
    const double w = 2.0 * std::numbers::pi * f / sampleRate;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    const auto num = b0 + b1 * z1 + b2 * z2;
    const auto den = 1.0 + a1 * z1 + a2 * z2;
    return 20.0 * std::log10 (std::abs (num) / std::abs (den));
}

BiquadCoefficients TonalEQ::designBand (int band, double gainDb, double freqHz, double sampleRate) noexcept
{
    const auto& spec = kEqBandSpecs[static_cast<size_t> (band)];
    const double f = clampFreq (band, freqHz, sampleRate);
    const double g = std::clamp (gainDb, -kEqMaxGainDb, kEqMaxGainDb);
    const double w0 = 2.0 * std::numbers::pi * f / sampleRate;
    const double wTop = std::min (2.0 * std::numbers::pi * 20000.0 / sampleRate, std::numbers::pi);

    if (g <= 0.0)
        return matchedCut (spec.shape, std::pow (10.0, g / 40.0), spec.q, w0, wTop);

    // A boost is the exact inverse of the cut by the same amount (the analog prototypes are
    // reciprocal). Designing the cut and swapping zeros and poles keeps the poles well below
    // Nyquist even for a high shelf at 20 kHz, where a boost's own poles would sit above it.
    const auto c = matchedCut (spec.shape, std::pow (10.0, -g / 40.0), spec.q, w0, wTop);
    return { 1.0 / c.b0, c.a1 / c.b0, c.a2 / c.b0, c.b1 / c.b0, c.b2 / c.b0 };
}

double TonalEQ::responseDb (const TonalEQParams& p, double f, double sampleRate) noexcept
{
    if (! p.enabled)
        return 0.0;
    double total = 0.0;
    for (int b = 0; b < kEqBands; ++b)
        if (p.gainDb[static_cast<size_t> (b)] != 0.0)
            total += designBand (b, p.gainDb[static_cast<size_t> (b)], p.freqHz[static_cast<size_t> (b)], sampleRate)
                         .magnitudeDb (f, sampleRate);
    if (subsonicEngaged (p))
        for (const auto& c : designSubsonic (sampleRate))
            total += c.magnitudeDb (f, sampleRate);
    return total;
}

bool TonalEQ::subsonicEngaged (const TonalEQParams& p) noexcept
{
    return p.enabled && p.gainDb[0] > 0.0;
}

std::array<BiquadCoefficients, 2> TonalEQ::designSubsonic (double sampleRate) noexcept
{
    // 4th-order Butterworth high-pass: two 2nd-order sections with Q = 1 / (2 cos(pi/8)) and 1 / (2 cos(3 pi/8)).
    std::array<BiquadCoefficients, 2> out;
    const double w0 = 2.0 * std::numbers::pi * kEqSubsonicHz / sampleRate;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const std::array<double, 2> qs { 0.5 / std::cos (std::numbers::pi / 8.0), 0.5 / std::cos (3.0 * std::numbers::pi / 8.0) };
    for (size_t i = 0; i < 2; ++i)
    {
        const double alpha = sw / (2.0 * qs[i]);
        const double a0 = 1.0 + alpha;
        out[i] = { 0.5 * (1.0 + cw) / a0, -(1.0 + cw) / a0, 0.5 * (1.0 + cw) / a0, -2.0 * cw / a0, (1.0 - alpha) / a0 };
    }
    return out;
}

void TonalEQ::prepare (double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    channelCount = std::clamp (numChannels, 1, kMaxChannels);
    smoothingCoeff = std::exp (-static_cast<double> (kUpdateInterval) / (0.02 * sampleRate));
    const auto hp = designSubsonic (sampleRate);
    for (auto& ch : sub.filters)
        for (size_t i = 0; i < 2; ++i)
        {
            ch[i].b0 = hp[i].b0; ch[i].b1 = hp[i].b1; ch[i].b2 = hp[i].b2; ch[i].a1 = hp[i].a1; ch[i].a2 = hp[i].a2;
        }
    firstParams = true;
    const bool hadParams = hasParams;
    reset();
    setParams (params);    // re-apply the current settings at the new rate, immediately...
    // ...and if nothing was set yet (prepared, then given its settings), let the first setParams() land
    // immediately too (no glide on load). Once settings exist, later changes glide as usual.
    firstParams = ! hadParams;
    hasParams = hadParams;
}

void TonalEQ::reset() noexcept
{
    for (auto& band : bands)
        for (auto& f : band.filters)
            f.reset();
    for (auto& ch : sub.filters)
        for (auto& f : ch)
            f.reset();
    updateCountdown = 0;
}

void TonalEQ::guardState() noexcept
{
    double sum = 0.0;
    for (const auto& band : bands)
        if (band.running)
            for (int c = 0; c < channelCount; ++c)
                sum += band.filters[static_cast<size_t> (c)].stateSum();
    if (sub.running)
        for (int c = 0; c < channelCount; ++c)
            for (const auto& f : sub.filters[static_cast<size_t> (c)])
                sum += f.stateSum();
    if (! std::isfinite (sum))
        reset();
}

void TonalEQ::setParams (const TonalEQParams& p) noexcept
{
    params = p;
    hasParams = true;
    for (int b = 0; b < kEqBands; ++b)
    {
        auto& band = bands[static_cast<size_t> (b)];
        const auto i = static_cast<size_t> (b);
        band.targetGainDb = p.enabled ? std::clamp (p.gainDb[i], -kEqMaxGainDb, kEqMaxGainDb) : 0.0;
        band.targetLogFreq = std::log (clampFreq (b, p.freqHz[i], sampleRate));
        band.targetChannels = p.channels[i];
        if (firstParams)
        {
            band.channels = band.targetChannels;
            band.gainDb = band.targetGainDb;
            band.logFreq = band.targetLogFreq;
            band.running = band.gainDb != 0.0;
            applyCoefficients (b);
        }
    }
    sub.target = subsonicEngaged (p) ? 1.0 : 0.0;
    if (firstParams)
    {
        sub.mix = sub.settled = sub.target;
        sub.step = 0.0;
        sub.running = sub.mix > 0.0;
    }
    firstParams = false;
}

bool TonalEQ::isActive() const noexcept
{
    for (const auto& band : bands)
        if (band.running)
            return true;
    return sub.running;
}

void TonalEQ::applyCoefficients (int b) noexcept
{
    auto& band = bands[static_cast<size_t> (b)];
    const auto c = designBand (b, band.gainDb, std::exp (band.logFreq), sampleRate);
    for (auto& f : band.filters)
    {
        f.b0 = c.b0; f.b1 = c.b1; f.b2 = c.b2; f.a1 = c.a1; f.a2 = c.a2;
    }
}

void TonalEQ::advanceSmoothing() noexcept
{
    const double k = 1.0 - smoothingCoeff;

    // Subsonic filter: fade the high-passed signal in or out, ramped per sample over each interval.
    sub.mix = sub.settled;
    const double dm = sub.target - sub.mix;
    sub.settled = std::abs (dm) < 1.0e-4 ? sub.target : sub.mix + dm * k;
    sub.step = (sub.settled - sub.mix) / static_cast<double> (kUpdateInterval);
    if (sub.mix == 0.0 && sub.settled == 0.0)
    {
        if (sub.running)
        {
            sub.running = false;   // fully dry: out of the path, and starts clean next time
            for (auto& ch : sub.filters)
                for (auto& f : ch)
                    f.reset();
        }
    }
    else
    {
        sub.running = true;
    }
    for (int b = 0; b < kEqBands; ++b)
    {
        auto& band = bands[static_cast<size_t> (b)];
        // A change of channels (stereo / mid / side): glide to 0 dB, switch with clean filters, glide back.
        if (band.channels != band.targetChannels && band.gainDb == 0.0)
        {
            band.channels = band.targetChannels;
            for (auto& f : band.filters)
                f.reset();
        }
        const double goal = band.channels == band.targetChannels ? band.targetGainDb : 0.0;
        const double dg = goal - band.gainDb;
        const double df = band.targetLogFreq - band.logFreq;
        if (dg == 0.0 && df == 0.0)
        {
            // Settled. A band at 0 dB keeps running only until its filter state has rung out,
            // then it is skipped (exact pass-through).
            if (band.running && band.gainDb == 0.0)
            {
                double energy = 0.0;
                for (int c = 0; c < channelCount; ++c)
                {
                    const auto& f = band.filters[static_cast<size_t> (c)];
                    energy += std::abs (f.z1) + std::abs (f.z2);
                }
                if (energy < 1.0e-12)
                {
                    band.running = false;
                    for (auto& f : band.filters)
                        f.reset();
                }
            }
            continue;
        }

        band.gainDb  = std::abs (dg) < 1.0e-3 ? goal : band.gainDb + dg * k;
        band.logFreq = std::abs (df) < 1.0e-5 ? band.targetLogFreq : band.logFreq + df * k;
        band.running = true;
        applyCoefficients (b);
    }
}

} // namespace polisher

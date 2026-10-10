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
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double A = std::pow (10.0, g / 40.0);
    const double alpha = sw / (2.0 * spec.q);

    double b0, b1, b2, a0, a1, a2;
    switch (spec.shape)
    {
        case EqBandSpec::Shape::bell:
            b0 = 1.0 + alpha * A; b1 = -2.0 * cw; b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A; a1 = -2.0 * cw; a2 = 1.0 - alpha / A;
            break;
        case EqBandSpec::Shape::lowShelf:
        {
            const double k = 2.0 * std::sqrt (A) * alpha;
            b0 = A * ((A + 1.0) - (A - 1.0) * cw + k);
            b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw);
            b2 = A * ((A + 1.0) - (A - 1.0) * cw - k);
            a0 = (A + 1.0) + (A - 1.0) * cw + k;
            a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw);
            a2 = (A + 1.0) + (A - 1.0) * cw - k;
            break;
        }
        case EqBandSpec::Shape::highShelf:
        default:
        {
            const double k = 2.0 * std::sqrt (A) * alpha;
            b0 = A * ((A + 1.0) + (A - 1.0) * cw + k);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw);
            b2 = A * ((A + 1.0) + (A - 1.0) * cw - k);
            a0 = (A + 1.0) - (A - 1.0) * cw + k;
            a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw);
            a2 = (A + 1.0) - (A - 1.0) * cw - k;
            break;
        }
    }
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
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
    return total;
}

void TonalEQ::prepare (double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    channelCount = std::clamp (numChannels, 1, kMaxChannels);
    smoothingCoeff = std::exp (-static_cast<double> (kUpdateInterval) / (0.02 * sampleRate));
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
    updateCountdown = 0;
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
        if (firstParams)
        {
            band.gainDb = band.targetGainDb;
            band.logFreq = band.targetLogFreq;
            band.running = band.gainDb != 0.0;
            applyCoefficients (b);
        }
    }
    firstParams = false;
}

bool TonalEQ::isActive() const noexcept
{
    for (const auto& band : bands)
        if (band.running)
            return true;
    return false;
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
    for (int b = 0; b < kEqBands; ++b)
    {
        auto& band = bands[static_cast<size_t> (b)];
        const double dg = band.targetGainDb - band.gainDb;
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

        band.gainDb  = std::abs (dg) < 1.0e-3 ? band.targetGainDb  : band.gainDb + dg * k;
        band.logFreq = std::abs (df) < 1.0e-5 ? band.targetLogFreq : band.logFreq + df * k;
        band.running = true;
        applyCoefficients (b);
    }
}

} // namespace polisher

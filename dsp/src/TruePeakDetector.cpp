#include "polisher/TruePeakDetector.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace polisher {

namespace {

double besselI0 (double x) noexcept
{
    double sum = 1.0, term = 1.0;
    const double halfX = x * 0.5;
    for (int k = 1; k < 64; ++k)
    {
        term *= (halfX / k) * (halfX / k);
        sum += term;
        if (term < 1e-17 * sum)
            break;
    }
    return sum;
}

using Phase = std::array<double, TruePeakDetector::kTapsPerPhase>;
using Table = std::array<Phase, TruePeakDetector::kOversampling>;

Table makeTable() noexcept
{
    constexpr int factor = TruePeakDetector::kOversampling;
    constexpr int length = factor * (TruePeakDetector::kTapsPerPhase - 1) + 1;   // 97
    constexpr int centre = (length - 1) / 2;                                      // 48
    constexpr double cutoff = 0.92;   // fraction of the original Nyquist frequency
    constexpr double beta   = 8.0;    // Kaiser window shape (~80 dB stop-band)

    std::array<double, length> proto {};
    const double i0Beta = besselI0 (beta);

    for (int k = 0; k < length; ++k)
    {
        const double t = static_cast<double> (k - centre) / factor;        // in input samples
        const double x = std::numbers::pi * cutoff * t;
        const double sinc = (k == centre) ? 1.0 : std::sin (x) / x;
        const double r = static_cast<double> (k - centre) / centre;
        const double window = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0Beta;
        proto[static_cast<size_t> (k)] = cutoff * sinc * window;
    }

    Table table {};
    for (int p = 0; p < factor; ++p)
    {
        double sum = 0.0;
        for (int j = 0; j < TruePeakDetector::kTapsPerPhase; ++j)
        {
            const int idx = p + factor * j;
            const double v = idx < length ? proto[static_cast<size_t> (idx)] : 0.0;
            table[static_cast<size_t> (p)][static_cast<size_t> (j)] = v;
            sum += v;
        }
        // Unity DC gain per phase so a constant signal never reads above its level.
        for (auto& v : table[static_cast<size_t> (p)])
            v /= sum;
    }
    return table;
}

const Table& coefficients() noexcept
{
    static const Table table = makeTable();
    return table;
}

} // namespace

void TruePeakDetector::reset() noexcept
{
    history.fill (0.0);
    writePos = 0;
    (void) coefficients();
}

double TruePeakDetector::process (double x) noexcept
{
    writePos = (writePos == 0 ? kTapsPerPhase : writePos) - 1;
    history[static_cast<size_t> (writePos)] = x;
    history[static_cast<size_t> (writePos + kTapsPerPhase)] = x;

    // history[writePos + j] == x[n - j]
    const double* h = history.data() + writePos;
    const auto& table = coefficients();

    // Phase 0 sits exactly on x[n - kLatency]: use the real sample so the true peak can never
    // read below the sample peak (the band-limited filter would slightly soften single-sample spikes).
    double peak = std::abs (h[kLatency]);
    for (size_t p = 1; p < table.size(); ++p)
    {
        const auto& phase = table[p];
        double acc = 0.0;
        for (int j = 0; j < kTapsPerPhase; ++j)
            acc += phase[static_cast<size_t> (j)] * h[j];
        peak = std::max (peak, std::abs (acc));
    }
    return peak;
}

} // namespace polisher

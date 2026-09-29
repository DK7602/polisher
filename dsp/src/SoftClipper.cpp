#include "polisher/SoftClipper.h"

#include <algorithm>
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
        if (term < 1e-17 * sum) break;
    }
    return sum;
}

// Round-trip latency (base-rate samples) of 0..3 cascaded stages with taps {95, 33, 33}:
// (95-1)/2 + (33-1)/4 + (33-1)/8 = 47 + 8 + 4.
constexpr int kStageLatency[4] = { 0, 47, 55, 59 };

} // namespace

// --- HalfbandStage ------------------------------------------------------------------------------

HalfbandStage::HalfbandStage (int numTapsIn)
{
    const int n = numTapsIn | 1;   // odd length, centre tap at (n-1)/2
    const int centre = (n - 1) / 2;
    const double beta = 9.0;       // ~90 dB stop-band
    const double i0Beta = besselI0 (beta);

    taps.resize (static_cast<size_t> (n));
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const int k = i - centre;
        const double x = std::numbers::pi * 0.5 * k;
        const double sinc = (k == 0) ? 1.0 : std::sin (x) / x;
        const double r = static_cast<double> (k) / centre;
        const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0Beta;
        taps[static_cast<size_t> (i)] = 0.5 * sinc * w;
        sum += taps[static_cast<size_t> (i)];
    }
    for (auto& t : taps) t /= sum;   // unity DC gain

    for (int i = 0; i < n; ++i)
        (i % 2 == 0 ? evenTaps : oddTaps).push_back (taps[static_cast<size_t> (i)]);

    upHistory.assign (evenTaps.size() * 2, 0.0);
    downHistory.assign (taps.size() * 2, 0.0);
}

void HalfbandStage::reset() noexcept
{
    std::fill (upHistory.begin(), upHistory.end(), 0.0);
    std::fill (downHistory.begin(), downHistory.end(), 0.0);
    upPos = downPos = 0;
}

void HalfbandStage::upsample (double x, double& out0, double& out1) noexcept
{
    const int len = static_cast<int> (evenTaps.size());
    upPos = (upPos == 0 ? len : upPos) - 1;
    upHistory[static_cast<size_t> (upPos)] = x;
    upHistory[static_cast<size_t> (upPos + len)] = x;
    const double* h = upHistory.data() + upPos;   // h[k] == x[n - k]

    double a = 0.0, b = 0.0;
    for (size_t k = 0; k < evenTaps.size(); ++k) a += evenTaps[k] * h[k];
    for (size_t k = 0; k < oddTaps.size(); ++k)  b += oddTaps[k] * h[k];
    out0 = 2.0 * a;   // x2 compensates for the zero-stuffing
    out1 = 2.0 * b;
}

double HalfbandStage::downsample (double in0, double in1) noexcept
{
    const int len = static_cast<int> (taps.size());
    auto push = [&] (double v)
    {
        downPos = (downPos == 0 ? len : downPos) - 1;
        downHistory[static_cast<size_t> (downPos)] = v;
        downHistory[static_cast<size_t> (downPos + len)] = v;
    };

    // Keep the filter output aligned with the even (first) sample so the round trip is a whole
    // number of samples at the lower rate.
    push (in0);
    const double* h = downHistory.data() + downPos;
    double acc = 0.0;
    for (int t = 0; t < len; ++t) acc += taps[static_cast<size_t> (t)] * h[t];
    push (in1);
    return acc;
}

// --- SoftClipper --------------------------------------------------------------------------------

double SoftClipper::shape (double x, double t, double softness) noexcept
{
    const double ax = std::abs (x);
    const double knee = t * (1.0 - std::clamp (softness, 0.0, 0.999));
    if (ax <= knee)
        return x;
    const double range = t - knee;
    const double y = range > 1e-12 ? knee + range * std::tanh ((ax - knee) / range) : t;
    return x < 0.0 ? -y : y;
}

void SoftClipper::prepare (int numChannels)
{
    channelCount = std::clamp (numChannels, 1, kMaxChannels);
    channelState.assign (static_cast<size_t> (channelCount), Channel {});
    for (auto& ch : channelState)
        ch.delay.assign (64, 0.0);
    setParams (params);
    reset();
}

void SoftClipper::reset() noexcept
{
    for (auto& ch : channelState)
    {
        for (auto& s : ch.stages) s.reset();
        std::fill (ch.delay.begin(), ch.delay.end(), 0.0);
        ch.delayPos = 0;
    }
    maxReductionRatio = 1.0;
}

void SoftClipper::setParams (const ClipperParams& p) noexcept
{
    const int oldStages = activeStages;
    params = p;
    threshold = std::pow (10.0, std::clamp (p.thresholdDb, -24.0, 0.0) / 20.0);
    const int os = p.oversampling;
    activeStages = os >= 8 ? 3 : os >= 4 ? 2 : os >= 2 ? 1 : 0;
    if (activeStages != oldStages)
        for (auto& ch : channelState)
            for (auto& s : ch.stages) s.reset();   // stale filter state from another rate
}

double SoftClipper::clipSample (double x) noexcept
{
    const double y = shape (x, threshold, params.softness);
    const double ax = std::abs (x);
    if (ax > 1e-9 && std::abs (y) < ax)
        maxReductionRatio = std::min (maxReductionRatio, std::abs (y) / ax);
    return y;
}

double SoftClipper::processOversampled (Channel& ch, double x, int stages) noexcept
{
    if (stages == 0)
        return clipSample (x);

    // Recursive cascade: up 2x, process the inner stages on both samples, down 2x.
    auto& stage = ch.stages[static_cast<size_t> (activeStages - stages)];
    double a, b;
    stage.upsample (x, a, b);
    a = processOversampled (ch, a, stages - 1);
    b = processOversampled (ch, b, stages - 1);
    return stage.downsample (a, b);
}

void SoftClipper::processFrame (double* frame, int numChannels) noexcept
{
    const int nch = std::min (numChannels, channelCount);
    const int pad = kLatency - kStageLatency[activeStages];

    for (int c = 0; c < nch; ++c)
    {
        auto& ch = channelState[static_cast<size_t> (c)];
        double y = params.enabled ? processOversampled (ch, frame[c], activeStages) : frame[c];

        // When disabled the signal skips the filters, so pad the full latency instead.
        const int delay = params.enabled ? pad : kLatency;
        const int size = static_cast<int> (ch.delay.size());
        ch.delay[static_cast<size_t> (ch.delayPos)] = y;
        y = ch.delay[static_cast<size_t> ((ch.delayPos - delay + size) % size)];
        ch.delayPos = (ch.delayPos + 1) % size;
        frame[c] = y;
    }
}

} // namespace polisher

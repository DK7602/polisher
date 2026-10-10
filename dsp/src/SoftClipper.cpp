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
    const double knee = kneeFor (t, softness);
    if (ax <= knee)
        return x;
    const double range = t - knee;
    const double y = range > 1e-12 ? knee + range * std::tanh ((ax - knee) / range) : t;
    return x < 0.0 ? -y : y;
}

void SoftClipper::prepare (int numChannels, double newSampleRate)
{
    channelCount = std::clamp (numChannels, 1, kMaxChannels);
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    glideCoeff = 1.0 - std::exp (-1.0 / (0.02 * sampleRate));   // ~20 ms glide for level changes
    fadeStep = 1.0 / (0.015 * sampleRate);                       // 15 ms on/off and oversampling fades
    channelState.assign (static_cast<size_t> (channelCount), Channel {});
    for (auto& ch : channelState)
        for (auto& d : ch.delay)
            d.assign (64, 0.0);
    setParams (params);
    reset();
}

void SoftClipper::reset() noexcept
{
    for (auto& ch : channelState)
    {
        for (auto& c : ch.cascades) resetCascade (c);
        for (auto& d : ch.delay) std::fill (d.begin(), d.end(), 0.0);
        ch.delayPos = 0;
    }
    switching = false;
    snap = true;
    thresholdNow = threshold; makeupNow = makeup; kneeRatioNow = kneeRatio;
    mix = params.enabled ? 1.0 : 0.0;
    maxReductionRatio = 1.0;
}

void SoftClipper::setParams (const ClipperParams& p) noexcept
{
    params = p;
    threshold = std::pow (10.0, std::clamp (p.thresholdDb, -24.0, 6.0) / 20.0);
    makeup = std::pow (10.0, std::clamp (p.makeupDb, 0.0, 24.0) / 20.0);
    kneeRatio = kneeFor (1.0, p.softness);
    const int os = p.oversampling;
    const int stages = os >= 8 ? 3 : os >= 4 ? 2 : os >= 2 ? 1 : 0;

    if (snap)
    {
        // Before any audio: take every setting at once.
        thresholdNow = threshold; makeupNow = makeup; kneeRatioNow = kneeRatio;
        mix = p.enabled ? 1.0 : 0.0;
        if (stages != stagesOf[current])
        {
            stagesOf[current] = stages;
            for (auto& ch : channelState) resetCascade (ch.cascades[static_cast<size_t> (current)]);
        }
        switching = false;
        return;
    }

    const int target = switching ? stagesOf[1 - current] : stagesOf[current];
    if (stages == target)
        return;
    if (stages == stagesOf[current])
    {
        switching = false;   // changed back before the new one faded in
        return;
    }
    // Start the new cascade from silence next to the playing one; it fades in once its filters are full.
    const int next = 1 - current;
    stagesOf[next] = stages;
    for (auto& ch : channelState) resetCascade (ch.cascades[static_cast<size_t> (next)]);
    switching = true;
    warmup = 2 * kLatency;
    switchMix = 0.0;
}

double SoftClipper::clipSample (double x) noexcept
{
    const double ax = std::abs (x);
    const double knee = thresholdNow * kneeRatioNow;
    if (ax <= knee)
        return x;
    const double range = thresholdNow - knee;
    const double y = range > 1e-12 ? knee + range * std::tanh ((ax - knee) / range) : thresholdNow;
    if (params.enabled && ax > 1e-9)
        maxReductionRatio = std::min (maxReductionRatio, y / ax);
    return x < 0.0 ? -y : y;
}

double SoftClipper::processOversampled (Cascade& cascade, double x, int stages, int depth) noexcept
{
    if (depth == stages)
        return clipSample (x);

    // Recursive cascade: up 2x, process the inner stages on both samples, down 2x.
    auto& stage = cascade[static_cast<size_t> (depth)];
    double a, b;
    stage.upsample (x, a, b);
    a = processOversampled (cascade, a, stages, depth + 1);
    b = processOversampled (cascade, b, stages, depth + 1);
    return stage.downsample (a, b);
}

void SoftClipper::processFrame (double* frame, int numChannels) noexcept
{
    const int nch = std::min (numChannels, channelCount);
    snap = false;

    // Glide the levels and the fades once per frame.
    thresholdNow += (threshold - thresholdNow) * glideCoeff;
    makeupNow += (makeup - makeupNow) * glideCoeff;
    kneeRatioNow += (kneeRatio - kneeRatioNow) * glideCoeff;
    mix = params.enabled ? std::min (1.0, mix + fadeStep) : std::max (0.0, mix - fadeStep);
    if (switching)
    {
        if (warmup > 0) --warmup;
        else switchMix = std::min (1.0, switchMix + fadeStep);
    }

    for (int c = 0; c < nch; ++c)
    {
        auto& ch = channelState[static_cast<size_t> (c)];
        const int size = static_cast<int> (ch.delay[0].size());
        const int pos = ch.delayPos;
        auto delayed = [&] (std::vector<double>& line, double v, int d)
        {
            line[static_cast<size_t> (pos)] = v;
            return line[static_cast<size_t> ((pos - d + size) % size)];
        };

        const double x = frame[c];
        const double dry = delayed (ch.delay[2], x, kLatency);
        const int cur = current;
        double wet = delayed (ch.delay[static_cast<size_t> (cur)],
                              processOversampled (ch.cascades[static_cast<size_t> (cur)], x, stagesOf[cur], 0),
                              kLatency - kStageLatency[stagesOf[cur]]);
        if (switching)
        {
            const int nx = 1 - cur;
            const double wetNew = delayed (ch.delay[static_cast<size_t> (nx)],
                                           processOversampled (ch.cascades[static_cast<size_t> (nx)], x, stagesOf[nx], 0),
                                           kLatency - kStageLatency[stagesOf[nx]]);
            wet += (wetNew - wet) * switchMix;
        }
        wet *= makeupNow;
        frame[c] = dry + (wet - dry) * mix;
        ch.delayPos = (pos + 1) % size;
    }

    if (switching && switchMix >= 1.0)
    {
        current = 1 - current;
        switching = false;
    }
}

} // namespace polisher

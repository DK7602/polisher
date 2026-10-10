#include "polisher/TruePeakLimiter.h"

namespace polisher {

namespace {

double timeCoefficient (double ms, double sampleRate) noexcept
{
    const double samples = std::max (1.0, ms * 0.001 * sampleRate);
    return std::exp (-1.0 / samples);
}

} // namespace

// --- SlidingMin -------------------------------------------------------------------------------

void TruePeakLimiter::SlidingMin::prepare (int windowLength)
{
    window = std::max (1, windowLength);
    capacity = window + 2;
    values.assign (static_cast<size_t> (capacity), 1.0);
    stamps.assign (static_cast<size_t> (capacity), 0);
    reset();
}

double TruePeakLimiter::SlidingMin::push (double v) noexcept
{
    // Drop entries from the back that are >= the new value (they can never be the minimum again).
    while (size > 0)
    {
        const int back = (head + size - 1) % capacity;
        if (values[static_cast<size_t> (back)] >= v) --size;
        else break;
    }
    const int slot = (head + size) % capacity;
    values[static_cast<size_t> (slot)] = v;
    stamps[static_cast<size_t> (slot)] = now;
    ++size;

    // Drop entries from the front that have left the window.
    while (stamps[static_cast<size_t> (head)] <= now - window)
    {
        head = (head + 1) % capacity;
        --size;
    }
    ++now;
    return values[static_cast<size_t> (head)];
}

// --- MovingAverage ----------------------------------------------------------------------------

void TruePeakLimiter::MovingAverage::prepare (int len)
{
    length = std::max (1, len);
    ring.assign (static_cast<size_t> (length), 1.0);
    reset (1.0);
}

void TruePeakLimiter::MovingAverage::reset (double fill) noexcept
{
    std::fill (ring.begin(), ring.end(), fill);
    sum = fill * length;
    pos = 0;
    sinceRecompute = 0;
}

double TruePeakLimiter::MovingAverage::push (double v) noexcept
{
    sum += v - ring[static_cast<size_t> (pos)];
    ring[static_cast<size_t> (pos)] = v;
    pos = (pos + 1) % length;

    // Periodically recompute to stop floating-point drift over long sessions.
    if (++sinceRecompute >= 1 << 16)
    {
        sum = 0.0;
        for (double r : ring) sum += r;
        sinceRecompute = 0;
    }
    return sum / length;
}

// --- TruePeakLimiter --------------------------------------------------------------------------

void TruePeakLimiter::prepare (double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate;
    channelCount = std::clamp (numChannels, 1, kMaxChannels);
    lookahead = std::max (1, static_cast<int> (std::lround (kLookaheadMs * 0.001 * sampleRate)));
    delayLength = lookahead + TruePeakDetector::kLatency;

    delayBuffer.assign (static_cast<size_t> (channelCount * delayLength), 0.0);
    slidingMin.prepare (lookahead + 1);
    smoother.prepare (lookahead);

    setParams (params);
    reset();
}

void TruePeakLimiter::reset() noexcept
{
    for (auto& d : detectors) d.reset();
    std::fill (delayBuffer.begin(), delayBuffer.end(), 0.0);
    delayPos = 0;
    slidingMin.reset();
    smoother.reset (1.0);
    gain = 1.0;
    sustain = 1.0;
    minGainSinceRead = 1.0;
    lastGain = 1.0;
}

void TruePeakLimiter::setParams (const LimiterParams& p) noexcept
{
    params = p;
    inputGain = std::pow (10.0, p.driveDb / 20.0);
    ceiling = std::pow (10.0, std::min (p.ceilingDbtp, 0.0) / 20.0);
    ceilingSafe = ceiling * 0.9966;   // ~0.03 dB margin for gain changes between samples
    outputGain = std::pow (10.0, std::min (p.outputTrimDb, 0.0) / 20.0);

    const double base = std::clamp (p.releaseMs, 1.0, 5000.0);
    if (p.adaptiveRelease)
    {
        releaseFastCoef = timeCoefficient (base * 0.3, sampleRate);
        releaseSlowCoef = timeCoefficient (base * 2.5, sampleRate);
    }
    else
    {
        releaseFastCoef = releaseSlowCoef = timeCoefficient (base, sampleRate);
    }
    sustainCoef = timeCoefficient (400.0, sampleRate);
}

void TruePeakLimiter::processFrame (const double* in, double* out, int numChannels) noexcept
{
    const int nch = std::min (numChannels, channelCount);

    // 1. Drive, delay line and oversampled peak detection (linked across channels).
    double peak = 0.0;
    for (int c = 0; c < nch; ++c)
    {
        const double x = params.bypassed ? in[c] : in[c] * inputGain;
        double* line = delayBuffer.data() + c * delayLength;
        out[c] = line[delayPos];        // oldest sample leaves the line
        line[delayPos] = x;
        peak = std::max (peak, detectors[static_cast<size_t> (c)].process (x));
    }
    delayPos = (delayPos + 1) % delayLength;

    if (params.bypassed)
    {
        lastGain = 1.0;
        return;
    }

    // 2. Gain computer.
    const double required = peak > ceilingSafe ? ceilingSafe / peak : 1.0;
    const double windowMin = slidingMin.push (required);
    const double attack = smoother.push (windowMin);

    // 3. Release: program dependent. 'sustain' is a slow average of the gain (~400 ms). A dip
    //    below that average is a transient (a kick, a snare) and recovers fast; once the gain is
    //    back at the program's typical level it moves slowly, so sustained material doesn't pump.
    sustain = sustainCoef * sustain + (1.0 - sustainCoef) * gain;
    const double coef = gain < sustain - 1e-6 ? releaseFastCoef : releaseSlowCoef;

    if (attack < gain)
        gain = attack;
    else
        gain = attack + (gain - attack) * coef;   // approaches 'attack' from below, never above it

    minGainSinceRead = std::min (minGainSinceRead, gain);

    // 4. Apply, trim and a final sample-level safety clamp at the ceiling.
    const double g = gain * outputGain;
    lastGain = g;
    for (int c = 0; c < nch; ++c)
        out[c] = std::clamp (out[c] * g, -ceiling, ceiling);
}

} // namespace polisher

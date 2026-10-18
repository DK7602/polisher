#include "polisher/TruePeakLimiter.h"
#include "polisher/FilterDesign.h"

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
    refBuffer.assign (static_cast<size_t> (channelCount * delayLength), 0.0);
    bassLookahead = std::max (1, static_cast<int> (std::lround (kBassLookaheadMs * 0.001 * sampleRate)));
    for (auto* v : { &bassLine, &lowLine, &highLine })
        v->assign (static_cast<size_t> (channelCount * (bassLookahead + 1)), 0.0);
    bassMin.prepare (bassLookahead + 1);
    bassSmooth.prepare (bassLookahead);
    bassFloor = std::pow (10.0, -kBassMaxCutDb / 20.0);
    bassFadeStep = 1.0 / (ParkGlide::kFadeSeconds * sampleRate);
    slidingMin.prepare (lookahead + 1);
    smoother.prepare (lookahead);
    drainExtra = static_cast<int> (std::lround (kBypassDrainMs * 0.001 * sampleRate));
    glideLength = std::max (1, static_cast<int> (std::lround (kBypassGlideMs * 0.001 * sampleRate)));

    setParams (params);
    reset();
}

void TruePeakLimiter::reset() noexcept
{
    for (auto& d : detectors) d.reset();
    std::fill (delayBuffer.begin(), delayBuffer.end(), 0.0);
    std::fill (refBuffer.begin(), refBuffer.end(), 0.0);
    lastRef = {};
    delayPos = 0;
    for (auto* v : { &bassLine, &lowLine, &highLine })
        std::fill (v->begin(), v->end(), 0.0);
    bassPos = bassCountdown = 0;
    bassMin.reset();
    bassSmooth.reset (1.0);
    bassGain = 1.0;
    minBassSinceRead = 1.0;
    bassSeg.stop();
    // Start in the set state: split at its frequency when Bass First is on (no glide after a reset).
    bassRunning = bassEngaged = ! params.bypassed && params.bassFirst > 0.0;
    bassFade = bassFadeGoal = bassRunning ? 1.0 : 0.0;
    bassLog = bassRunning ? std::log (kBassSplitHz) : ParkGlide::parkLog (sampleRate);
    for (auto& ch : bassLp) for (auto& f : ch) f.reset();
    for (auto& ch : bassHp) for (auto& f : ch) f.reset();
    designBassSplit (std::exp (bassLog));
    slidingMin.reset();
    smoother.reset (1.0);
    gain = 1.0;
    sustain = 1.0;
    minGainSinceRead = 1.0;
    lastGain = 1.0;
    drainArmed = false;
    drainLeft = glideLeft = 0;
    glideFrom = 1.0;
    limitedSinceReset = false;
}

void TruePeakLimiter::guardState() noexcept
{
    double sum = gain + sustain + bassGain + smoother.sum + bassSmooth.sum;
    for (int c = 0; c < channelCount; ++c)
        for (size_t k = 0; k < 2; ++k)
            sum += bassLp[static_cast<size_t> (c)][k].stateSum() + bassHp[static_cast<size_t> (c)][k].stateSum();
    if (! std::isfinite (sum))
        reset();
}

void TruePeakLimiter::setParams (const LimiterParams& p) noexcept
{
    // Into bypass: what is in the delay line was pushed for B, so it is still limited (armDrain). Back
    // out of it: plain limiting at once (the gain computer kept running, so it knows the delay line).
    if (p.bypassed && ! params.bypassed && limitedSinceReset)
        drainArmed = true;
    else if (! p.bypassed)
        drainArmed = false, drainLeft = glideLeft = 0;
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
    bassReleaseCoef = timeCoefficient (std::max (10.0, base * 0.5), sampleRate);
    bassOffCoef = timeCoefficient (10.0, sampleRate);   // A, or Bass First turned off: the low end comes back at once
    drainReleaseCoef = timeCoefficient (kBypassDrainReleaseMs, sampleRate);
}

void TruePeakLimiter::designBassSplit (double hz) noexcept
{
    const auto lp = ParkGlide::butterworth (false, hz, sampleRate), hp = ParkGlide::butterworth (true, hz, sampleRate);
    for (int c = 0; c < channelCount; ++c)
        for (size_t k = 0; k < 2; ++k)
        {
            design::apply (bassLp[static_cast<size_t> (c)][k], lp);
            design::apply (bassHp[static_cast<size_t> (c)][k], hp);
        }
}

void TruePeakLimiter::bassUpdate (bool wanted) noexcept
{
    const double park = ParkGlide::parkLog (sampleRate), target = std::log (kBassSplitHz);
    if (wanted && ! bassRunning)
    {
        // Switch in parked: clean filters just under Nyquist, then fade in (an all-pass up there is
        // practically the plain signal, so the fade can't cut a hole).
        bassRunning = true;
        bassEngaged = false;
        bassSeg.stop();
        bassLog = park;
        for (auto& ch : bassLp) for (auto& f : ch) f.reset();
        for (auto& ch : bassHp) for (auto& f : ch) f.reset();
        designBassSplit (std::exp (bassLog));
        // The lines hold only the plain input while the stage is out: fill the bands as the split at the park
        // gives them (all low), so nothing older leaks into the fade.
        lowLine = bassLine;
        std::fill (highLine.begin(), highLine.end(), 0.0);
    }
    if (! bassRunning || bassSeg.active())
        return;
    const double dt = kUpdateSamples / sampleRate;
    if (wanted)
    {
        bassFadeGoal = 1.0;
        if (bassFade >= 1.0 && bassLog != target)
            bassSeg.start (bassLog, ParkGlide::step (bassLog, target, dt, sampleRate), kUpdateSamples);
        bassEngaged = bassFade >= 1.0 && bassLog == target;
        return;
    }
    // Off: let the cut go first, then glide up to the park, then fade out.
    bassEngaged = false;
    if (bassGain < 1.0)
        return;
    if (bassLog != park)
        bassSeg.start (bassLog, ParkGlide::step (bassLog, park, dt, sampleRate), kUpdateSamples);
    else
    {
        bassFadeGoal = 0.0;
        if (bassFade <= 0.0)
            bassRunning = false;   // out of the path: an exact delay again
    }
}

void TruePeakLimiter::bassFirstStage (const double* in, double* out, double* ref, int nch) noexcept
{
    const int len = bassLookahead + 1;
    const double amount = params.bypassed ? 0.0 : std::clamp (params.bassFirst, 0.0, 1.0);
    if (--bassCountdown <= 0)
    {
        bassUpdate (amount > 0.0);
        bassCountdown = kUpdateSamples;
    }
    if (bassSeg.active())
    {
        bassLog = bassSeg.next();
        designBassSplit (std::exp (bassLog));
    }

    // What the low band must come down to so each channel's sample stays at the ceiling (1 = nothing).
    double need = 1.0;
    for (int c = 0; c < nch; ++c)
    {
        const double x = params.bypassed ? in[c] : in[c] * inputGain;
        const auto at = static_cast<size_t> (c * len + bassPos);
        bassLine[at] = x;
        if (! bassRunning)
            continue;
        auto& lp = bassLp[static_cast<size_t> (c)];
        auto& hp = bassHp[static_cast<size_t> (c)];
        const double low = lp[1].process (lp[0].process (x));
        const double high = hp[1].process (hp[0].process (x));
        lowLine[at] = low;
        highLine[at] = high;
        const double s = low + high;
        if (bassEngaged && amount > 0.0 && std::abs (s) > ceilingSafe && low * s > 0.0)
        {
            // The low band pushes this sample over: the low band alone brings it back to the ceiling.
            const double a = (std::copysign (ceilingSafe, s) - high) / low;
            need = std::min (need, std::clamp (a, bassFloor, 1.0));
        }
    }
    const double wanted = 1.0 - (bassEngaged ? amount : 0.0) * (1.0 - need);
    const double attack = bassSmooth.push (bassMin.push (wanted));
    if (attack < bassGain) bassGain = attack;
    else bassGain = attack + (bassGain - attack) * (bassEngaged && amount > 0.0 ? bassReleaseCoef : bassOffCoef);
    if (bassGain > 0.99995) bassGain = 1.0;
    if (! params.bypassed) minBassSinceRead = std::min (minBassSinceRead, bassGain);
    if (bassRunning)
        ParkGlide::fadeToward (bassFade, bassFadeGoal, bassFadeStep);
    const double w = ParkGlide::fadeWeight (bassFade);

    // The oldest sample leaves the lines: plain, split (low band at bassGain), or a fade between the two.
    const int oldest = (bassPos + 1) % len;
    for (int c = 0; c < nch; ++c)
    {
        const auto at = static_cast<size_t> (c * len + oldest);
        const double plain = bassLine[at];
        if (! bassRunning || w <= 0.0)
        {
            out[c] = ref[c] = plain;
            continue;
        }
        const double whole = lowLine[at] + highLine[at];
        const double split = bassGain * lowLine[at] + highLine[at];
        out[c] = w >= 1.0 ? split : plain + (split - plain) * w;
        ref[c] = w >= 1.0 ? whole : plain + (whole - plain) * w;   // the same without the cut
    }
    bassPos = oldest;
}

void TruePeakLimiter::processFrame (const double* rawIn, double* out, int numChannels) noexcept
{
    const int nch = std::min (numChannels, channelCount);

    // 0. Bass First: the low end takes the limiting first (an exact delay when off).
    std::array<double, kMaxChannels> in {}, ref {};
    bassFirstStage (rawIn, in.data(), ref.data(), nch);

    // 1. Drive (applied in the stage above), delay line and oversampled peak detection (linked across channels).
    double peak = 0.0;
    for (int c = 0; c < nch; ++c)
    {
        const double x = in[static_cast<size_t> (c)];
        double* line = delayBuffer.data() + c * delayLength;
        out[c] = line[delayPos];        // oldest sample leaves the line
        line[delayPos] = x;
        double* refLine = refBuffer.data() + c * delayLength;   // (the same, without Bass First's cut)
        lastRef[static_cast<size_t> (c)] = refLine[delayPos];
        refLine[delayPos] = ref[static_cast<size_t> (c)];
        peak = std::max (peak, detectors[static_cast<size_t> (c)].process (x));
    }
    delayPos = (delayPos + 1) % delayLength;
    armDrain (0);   // a switch to bypass seen here first (processFrame called on its own)

    // 2. Gain computer (in bypass too: it must know what is in the delay line at every switch).
    const double required = peak > ceilingSafe ? ceilingSafe / peak : 1.0;
    const double windowMin = slidingMin.push (required);
    const double attack = smoother.push (windowMin);

    // 3. Release: program dependent. 'sustain' is a slow average of the gain (~400 ms). A dip
    //    below that average is a transient (a kick, a snare) and recovers fast; once the gain is
    //    back at the program's typical level it moves slowly, so sustained material doesn't pump.
    //    Just after a switch to bypass it recovers within a few ms: B's samples are on their way out.
    sustain = sustainCoef * sustain + (1.0 - sustainCoef) * gain;
    const double coef = drainLeft > 0 ? drainReleaseCoef : gain < sustain - 1e-6 ? releaseFastCoef : releaseSlowCoef;

    if (attack < gain)
        gain = attack;
    else
        gain = attack + (gain - attack) * coef;   // approaches 'attack' from below, never above it

    if (params.bypassed)
    {
        double g = 1.0;
        if (drainLeft > 0)
        {
            // Still limiting what was pushed for B (output at or under the ceiling)...
            g = gain * outputGain;
            if (--drainLeft == 0)
            {
                glideFrom = g;
                glideLeft = glideLength;
            }
            for (int c = 0; c < nch; ++c)
                out[c] = std::clamp (out[c] * g, -ceiling, ceiling);
        }
        else if (glideLeft > 0)
        {
            // ...then the plain input fades in from that gain to unity.
            g = 1.0 - (1.0 - glideFrom) * static_cast<double> (glideLeft) / glideLength;
            --glideLeft;
            for (int c = 0; c < nch; ++c)
                out[c] *= g;
        }
        lastGain = g;
        return;
    }

    limitedSinceReset = true;
    minGainSinceRead = std::min (minGainSinceRead, gain);

    // 4. Apply, trim and a final sample-level safety clamp at the ceiling.
    const double g = gain * outputGain;
    lastGain = g;
    for (int c = 0; c < nch; ++c)
        out[c] = std::clamp (out[c] * g, -ceiling, ceiling);
}

} // namespace polisher

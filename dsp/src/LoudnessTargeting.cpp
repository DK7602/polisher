#include "polisher/LoudnessTargeting.h"
#include "polisher/LoudnessMeter.h"

#include <algorithm>
#include <cmath>

namespace polisher {

LoudnessStats measure (const AudioClip& clip)
{
    LoudnessMeter meter;
    meter.prepare (clip.sampleRate, clip.numChannels());

    std::vector<const float*> ptrs;
    for (const auto& ch : clip.channels)
        ptrs.push_back (ch.data());

    meter.process (ptrs.data(), clip.numChannels(), clip.numSamples());

    LoudnessStats s;
    s.integratedLufs = meter.integratedLufs();
    s.truePeakDbtp = meter.truePeakDbtp();
    s.loudnessRangeLu = meter.loudnessRangeLu();
    s.maxShortTermLufs = meter.maxShortTermLufs();
    return s;
}

AudioClip renderLimited (const AudioClip& clip, const LimiterParams& params)
{
    TruePeakLimiter limiter;
    limiter.prepare (clip.sampleRate, clip.numChannels());
    limiter.setParams (params);

    const int nch = clip.numChannels();
    const int n = clip.numSamples();
    const int latency = limiter.latencySamples();

    AudioClip out;
    out.sampleRate = clip.sampleRate;
    out.channels.assign (static_cast<size_t> (nch), std::vector<float> (static_cast<size_t> (n), 0.0f));

    std::vector<double> in (static_cast<size_t> (nch)), res (static_cast<size_t> (nch));
    for (int i = 0; i < n + latency; ++i)
    {
        for (int c = 0; c < nch; ++c)
            in[static_cast<size_t> (c)] = i < n ? clip.channels[static_cast<size_t> (c)][static_cast<size_t> (i)] : 0.0f;

        limiter.processFrame (in.data(), res.data(), nch);

        const int o = i - latency;
        if (o >= 0)
            for (int c = 0; c < nch; ++c)
                out.channels[static_cast<size_t> (c)][static_cast<size_t> (o)] = static_cast<float> (res[static_cast<size_t> (c)]);
    }
    return out;
}

TargetResult findDriveForTarget (const AudioClip& clip, double targetLufs, LimiterParams base,
                                 double toleranceLu, double maxDriveDb)
{
    TargetResult result;
    const LoudnessStats input = measure (clip);
    if (! std::isfinite (input.integratedLufs))
        return result;   // silence: nothing to do

    auto evaluate = [&] (double drive)
    {
        base.driveDb = drive;
        return measure (renderLimited (clip, base));
    };

    // Initial guess: plain gain difference (ignores limiting, so it undershoots on loud targets).
    double d0 = std::clamp (targetLufs - input.integratedLufs, -24.0, maxDriveDb);
    LoudnessStats s0 = evaluate (d0);
    double e0 = s0.integratedLufs - targetLufs;
    result = { d0, s0, 0.0, 1, std::abs (e0) <= toleranceLu };
    if (result.reachedTarget)
        return result;

    // Second point: push further by the remaining error (limiting eats part of every dB added).
    double d1 = std::clamp (d0 - e0 * 1.5, -24.0, maxDriveDb);
    LoudnessStats s1 = evaluate (d1);
    double e1 = s1.integratedLufs - targetLufs;

    for (int it = 2; it <= 12; ++it)
    {
        result = { d1, s1, 0.0, it, std::abs (e1) <= toleranceLu };
        if (result.reachedTarget)
            break;

        const double slope = (e1 - e0) / (d1 - d0);
        double next = (std::abs (slope) > 1e-4) ? d1 - e1 / slope : d1 - e1;
        next = std::clamp (next, -24.0, maxDriveDb);
        if (std::abs (next - d1) < 1e-4)
            break;   // pinned at the drive limit

        d0 = d1; e0 = e1;
        d1 = next;
        s1 = evaluate (d1);
        e1 = s1.integratedLufs - targetLufs;
    }
    if (! result.reachedTarget)
        result = { d1, s1, 0.0, result.iterations, std::abs (e1) <= toleranceLu };

    // Average gain reduction = how much louder the drive made it minus how much louder it got.
    result.averageGainReductionDb = std::min (0.0, (result.output.integratedLufs - input.integratedLufs) - result.driveDb);
    return result;
}

} // namespace polisher

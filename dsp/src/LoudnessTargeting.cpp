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

namespace {
    bool eqIsFlat (const TonalEQParams& p)
    {
        return ! p.enabled || std::all_of (p.gainDb.begin(), p.gainDb.end(), [] (double g) { return g == 0.0; });
    }
}

AudioClip renderEq (const AudioClip& clip, const TonalEQParams& params)
{
    AudioClip out = clip;
    if (eqIsFlat (params) || clip.numChannels() == 0)
        return out;
    TonalEQ eq;
    eq.prepare (clip.sampleRate, clip.numChannels());
    eq.setParams (params);
    std::vector<float*> ptrs;
    for (auto& ch : out.channels)
        ptrs.push_back (ch.data());
    eq.process (ptrs.data(), out.numChannels(), out.numSamples());
    return out;
}

namespace {
    bool bassIsNeutral (const BassParams& p)
    {
        return ! p.enabled || (p.monoHz < kBassMonoOffHz && p.tame <= 0.0 && p.punch <= 0.0);
    }
}

AudioClip renderBass (const AudioClip& clip, const BassParams& params)
{
    AudioClip out = clip;
    if (bassIsNeutral (params) || clip.numChannels() == 0)
        return out;
    BassControl bass;
    bass.prepare (clip.sampleRate, clip.numChannels());
    bass.setParams (params);
    bass.reset();   // start fully in the requested state
    std::vector<float*> ptrs;
    for (auto& ch : out.channels)
        ptrs.push_back (ch.data());
    bass.process (ptrs.data(), out.numChannels(), out.numSamples());
    return out;
}

AudioClip renderPreDrive (const AudioClip& input, const MasterChainParams& params)
{
    AudioClip out = renderBass (renderEq (input, params.eq), params.bass);
    const int nch = out.numChannels(), n = out.numSamples();
    if (nch == 0)
        return out;
    std::vector<float*> ptrs;
    for (auto& ch : out.channels)
        ptrs.push_back (ch.data());

    if (! MultibandDynamics::isNeutral (params.dynamics))
    {
        MultibandDynamics dyn;
        dyn.prepare (out.sampleRate, nch);
        dyn.setParams (params.dynamics);
        dyn.reset();
        dyn.process (ptrs.data(), nch, n);
    }
    const auto& im = params.imager;
    if (im.enabled && (im.lowWidth != 100.0 || im.midWidth != 100.0 || im.highWidth != 100.0) && nch >= 2)
    {
        StereoImager imager;
        imager.prepare (out.sampleRate);
        imager.setParams (im);
        imager.reset();
        imager.process (ptrs.data(), nch, n);
    }
    if (! Saturation::isNeutral (params.saturation))
    {
        // Run the tail through too, then drop the latency so the result lines up with the input.
        Saturation sat;
        sat.prepare (out.sampleRate, nch);
        sat.setParams (params.saturation);
        sat.reset();
        const int lat = Saturation::latencySamples();
        std::vector<double> frame (static_cast<size_t> (nch));
        for (int i = 0; i < n + lat; ++i)
        {
            for (int c = 0; c < nch; ++c)
                frame[static_cast<size_t> (c)] = i < n ? out.channels[static_cast<size_t> (c)][static_cast<size_t> (i)] : 0.0;
            sat.processFrame (frame.data(), nch);
            if (i >= lat)
                for (int c = 0; c < nch; ++c)
                    out.channels[static_cast<size_t> (c)][static_cast<size_t> (i - lat)] = static_cast<float> (frame[static_cast<size_t> (c)]);
        }
    }
    return out;
}

AudioClip renderMaster (const AudioClip& input, const MasterChainParams& params)
{
    const AudioClip clip = renderPreDrive (input, params);
    const int nch = clip.numChannels();
    const int n = clip.numSamples();

    SoftClipper clipper;
    clipper.prepare (nch);
    clipper.setParams (params.clipper);

    TruePeakLimiter limiter;
    limiter.prepare (clip.sampleRate, nch);
    auto lp = params.limiter;
    lp.driveDb = 0.0;
    limiter.setParams (lp);

    const double drive = std::pow (10.0, params.driveDb / 20.0);
    const int latency = SoftClipper::latencySamples() + limiter.latencySamples();

    AudioClip out;
    out.sampleRate = clip.sampleRate;
    out.channels.assign (static_cast<size_t> (nch), std::vector<float> (static_cast<size_t> (n), 0.0f));

    std::vector<double> frame (static_cast<size_t> (nch)), res (static_cast<size_t> (nch));
    for (int i = 0; i < n + latency; ++i)
    {
        for (int c = 0; c < nch; ++c)
            frame[static_cast<size_t> (c)] = i < n ? clip.channels[static_cast<size_t> (c)][static_cast<size_t> (i)] * drive : 0.0;

        clipper.processFrame (frame.data(), nch);
        limiter.processFrame (frame.data(), res.data(), nch);

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
    MasterChainParams chain;
    chain.clipper.enabled = false;
    chain.limiter = base;
    return findDriveForTarget (clip, targetLufs, chain, toleranceLu, maxDriveDb);
}

TargetResult findDriveForTarget (const AudioClip& clip, double targetLufs, MasterChainParams base,
                                 double toleranceLu, double maxDriveDb)
{
    if (! eqIsFlat (base.eq) || ! bassIsNeutral (base.bass))
    {
        // EQ and Bass Control first, once; then search the rest of the chain on that audio.
        const AudioClip shaped = renderBass (renderEq (clip, base.eq), base.bass);
        base.eq = TonalEQParams {};
        base.bass = BassParams {};
        return findDriveForTarget (shaped, targetLufs, base, toleranceLu, maxDriveDb);
    }

    TargetResult result;
    const LoudnessStats input = measure (clip);
    if (! std::isfinite (input.integratedLufs))
        return result;   // silence: nothing to do

    auto evaluate = [&] (double drive)
    {
        base.driveDb = drive;
        return measure (base.clipper.enabled ? renderMaster (clip, base)
                                             : renderLimited (clip, [&] { auto l = base.limiter; l.driveDb = drive; return l; }()));
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

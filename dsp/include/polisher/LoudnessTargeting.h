#pragma once

#include "SoftClipper.h"
#include "TonalEQ.h"
#include "TruePeakLimiter.h"

#include <vector>

namespace polisher {

/** Multichannel audio held in memory for offline analysis (one vector per channel). */
struct AudioClip
{
    double sampleRate = 48000.0;
    std::vector<std::vector<float>> channels;

    int numChannels() const noexcept { return static_cast<int> (channels.size()); }
    int numSamples() const noexcept  { return channels.empty() ? 0 : static_cast<int> (channels.front().size()); }
};

struct LoudnessStats
{
    double integratedLufs = 0.0;
    double truePeakDbtp = 0.0;
    double loudnessRangeLu = 0.0;
    double maxShortTermLufs = 0.0;
};

/** Measures a clip offline (integrated loudness, true peak, LRA). */
LoudnessStats measure (const AudioClip& clip);

/** Renders a clip through the limiter offline, compensating the limiter's latency. */
AudioClip renderLimited (const AudioClip& clip, const LimiterParams& params);

/** Renders a clip through the Tonal EQ offline (zero latency). */
AudioClip renderEq (const AudioClip& clip, const TonalEQParams& params);

/** The mastering chain as the plug-in runs it: Tonal EQ -> drive -> Soft Clipper -> Maximizer. */
struct MasterChainParams
{
    TonalEQParams eq;           // flat by default
    double driveDb = 0.0;       // push into the clipper and limiter
    ClipperParams clipper;      // clipper.enabled = false skips it (latency is kept)
    LimiterParams limiter;      // limiter.driveDb is ignored here; driveDb above is used
};

/** Renders a clip through EQ -> drive -> clipper -> limiter offline, latency compensated. */
AudioClip renderMaster (const AudioClip& clip, const MasterChainParams& params);

struct TargetResult
{
    double driveDb = 0.0;          // drive that hits the target
    LoudnessStats output;          // what the master measures at that drive
    double averageGainReductionDb = 0.0;
    int iterations = 0;
    bool reachedTarget = false;    // false if the target needs more than maxDriveDb
};

/** The loudness step of Auto-Master: finds the limiter drive that lands the master on
    targetLufs (integrated) under the given true-peak ceiling. Uses a secant search on the real
    limiter output, so the answer accounts for exactly how much the limiter squashes this mix. */
TargetResult findDriveForTarget (const AudioClip& clip, double targetLufs, LimiterParams base,
                                 double toleranceLu = 0.05, double maxDriveDb = 24.0);

/** Same search on the full chain (EQ -> drive -> clipper -> limiter). */
TargetResult findDriveForTarget (const AudioClip& clip, double targetLufs, MasterChainParams base,
                                 double toleranceLu = 0.05, double maxDriveDb = 24.0);

} // namespace polisher

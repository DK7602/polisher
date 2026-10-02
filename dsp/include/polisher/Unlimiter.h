#pragma once

#include "Biquad.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace polisher {

enum class UnlimiterFocus { full = 0, kick = 1, snap = 2 };

struct UnlimiterParams
{
    bool enabled = true;
    double amount = 0.0;      // % of the lost punch put back, 0 = off (exact delayed pass-through)
    double lengthMs = 15.0;   // how long each restored hit lasts, 5 ... 50 ms
    UnlimiterFocus focus = UnlimiterFocus::full;   // which hits it listens for
};

/** Unlimiter: puts back the punch a limiter or heavy compressor squashed out of a beat, loop or mix.

    A limiter pulls the whole signal down at every drum hit, so the hits stop standing out from what
    plays around them. The Unlimiter does the opposite: it listens for the start of each hit (a fast
    level follower jumping ahead of a slower one, linked across channels) and lifts the whole signal
    for the first few milliseconds of it, by up to kMaxBoostDb, then lets go at the same speed the
    hit fades. Sustained sound (an 808 note, a pad) is never lifted, so the level between hits stays
    put. It only puts back what was lost: full strength when the audio measures brickwalled (Squash
    kFullSquashDb or less), none on an open, unmastered mix (kOpenSquashDb or more). Focus picks what it listens to: everything, the kick and 808 (below 150 Hz), or snares,
    claps and hats (above 1 kHz); the lift itself is always the whole signal, like the limiter's dip.

    It cannot recreate a waveform a clipper flattened (that information is gone); it restores the
    level contrast between hits and the rest, which is what a squashed beat is missing.

    Constant latency (kLookaheadMs, so the lift starts with the hit, not after it) in every state;
    exact delayed pass-through when off or at Amount 0, cross-faded over 20 ms. Also measures how
    squashed the incoming audio is (Squash: peak vs average level over the last 3 s).

    prepare() allocates; process() does not. */
class Unlimiter
{
public:
    static constexpr int kMaxChannels = 8;
    static constexpr double kLookaheadMs = 1.5;
    static constexpr double kMaxBoostDb = 12.0;
    static constexpr double kFullSquashDb = 8.5;    // Squash at or below this: full strength
    static constexpr double kOpenSquashDb = 12.5;   // at or above this: an open mix, nothing to undo

    /** How much of Amount a Squash reading allows (0 ... 1). */
    static double squashScale (double squashDb) noexcept
    {
        return std::clamp ((kOpenSquashDb - squashDb) / (kOpenSquashDb - kFullSquashDb), 0.0, 1.0);
    }

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setParams (const UnlimiterParams& p) noexcept { params = p; }
    const UnlimiterParams& getParams() const noexcept { return params; }
    static bool isNeutral (const UnlimiterParams& p) noexcept { return ! p.enabled || p.amount < 0.5; }
    int latencySamples() const noexcept { return lookahead; }

    template <typename Sample>
    void process (Sample* const* ch, int numChannels, int numSamples) noexcept
    {
        std::array<double, kMaxChannels> frame {};
        const int nch = std::min (numChannels, channelCount);
        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < nch; ++c) frame[static_cast<size_t> (c)] = ch[c][i];
            processFrame (frame.data(), nch);
            for (int c = 0; c < nch; ++c) ch[c][i] = static_cast<Sample> (frame[static_cast<size_t> (c)]);
        }
    }

    /** One sample per channel, in place (output is delayed by latencySamples()). */
    void processFrame (double* frame, int numChannels) noexcept;

    /** Largest lift (dB, >= 0) since the last call. */
    double takeMaxBoostDb() noexcept { const double v = maxBoost; maxBoost = 0.0; return v; }

    /** How squashed the incoming audio is: sample peak minus RMS (dB) over the last ~3 s of audio
        with signal. About 8 dB or less is brickwalled, 14 dB or more is open (an unmastered mix);
        0 until there is enough audio. */
    double squashDb() const noexcept { return squash; }

    /** The same Squash reading for a whole clip: the median of its 3 s windows (louder ones only). */
    static double measureSquashDb (const std::vector<std::vector<float>>& channels, double sampleRate);

private:
    void updateCoefficients() noexcept;

    UnlimiterParams params;
    double sr = 48000.0;
    int channelCount = 0;
    int lookahead = 0;

    // Delay line (lookahead), one ring per channel
    std::vector<double> delay;
    int delaySize = 0, writePos = 0;

    // Detection
    std::array<Biquad, 2> scFilter[kMaxChannels];
    UnlimiterFocus scFocus = UnlimiterFocus::full;
    double fastEnv = 0.0, slowEnv = 0.0;
    int holdSamples = 0, holdLeft = 0;
    std::vector<double> rmsRing;
    double rmsSum = 0.0;
    int rmsPos = 0;
    double aFastAtk = 0.0, aSlowAtk = 0.0, aRel = 0.0, aGain = 0.0, aLift = 0.0;
    double gainDb = 0.0;
    double amountNow = 0.0, aAmount = 0.0;
    double lengthSet = -1.0;
    double maxBoost = 0.0;

    // Squash meter: 50 ms blocks, the last 60 of them (3 s)
    static constexpr int kSquashBlocks = 60, kMinBlocks = 20;
    std::array<double, kSquashBlocks> blockPeak {}, blockEnergy {};
    int blockLen = 2400, blockFill = 0, blockIndex = 0, blocksSeen = 0;
    double curPeak = 0.0, curEnergy = 0.0;
    double squash = 0.0, squashNow = 0.0, aSquash = 0.0;
};

} // namespace polisher

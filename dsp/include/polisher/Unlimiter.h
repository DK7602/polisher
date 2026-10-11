#pragma once

#include "TruePeakDetector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace polisher {

enum class UnlimiterFocus { full = 0, kick = 1, snap = 2 };

struct UnlimiterParams
{
    bool enabled = true;
    double amount = 0.0;      // how hard it lifts each hit, 0 = off (exact delayed pass-through)
    double lengthMs = 15.0;   // how long each lift lasts, 5 ... 50 ms
    UnlimiterFocus focus = UnlimiterFocus::full;   // which hits it listens for, and what it lifts
    double squashDb = 0.0;    // Squash Auto-Master measured on what it heard; 0 = use the live reading
};

/** Unlimiter: puts back the punch a limiter or heavy compressor squashed out of a beat, loop or mix.

    A limiter pulls the whole signal down at every drum hit, so the hits stop standing out from what
    plays around them. The Unlimiter does the opposite: it listens for the start of each hit (a fast
    level follower jumping ahead of a slower one, linked across channels), lifts it, holds the lift
    for Length and lets go. The lift climbs with how far the hit jumps out (kSlopeDbPerDb, with a soft
    knee so the music's own ripple is left alone) and levels off at a ceiling set by Amount and Squash
    (kMaxBoostDb at Amount 100 on brickwalled audio), so a hit over a quiet bar and the same hit over a
    busy one come back alike. A held sound (an 808 note, a pad) is not lifted once it has started, so
    the level between hits stays put.

    It only puts back what was lost: full strength when the audio measures brickwalled (Squash
    kFullSquashDb or less), none on an open, unmastered mix (kOpenSquashDb or more). The Squash reading
    is the true peak minus the average level, plus a limiter check: on limited audio the loudest peaks
    all press against one ceiling; on an open mix (even one whose loud 808 sits close to its peaks)
    they don't, and it reads open.

    Focus picks what it listens to and what it lifts: everything (the whole signal, like the limiter's
    dip), the kick and 808 (below 150 Hz), or snares, claps and hats (above 1 kHz). With Kick or Snap
    the audio is split into two bands (Linkwitz-Riley, the bands add back up flat) and only the chosen
    band is lifted, so hats over a held 808 don't make the 808 jump, and the other way round.

    It cannot recreate a waveform a clipper flattened (that information is gone); it restores the
    level contrast between hits and the rest, which is what a squashed beat is missing.

    The split's two bands add up to an all-pass (flat level, a phase turn around the split frequency),
    so the plain signal and the split must never be cross-faded there: half way, the two would cancel
    at the split frequency (a 150 Hz or 1 kHz tone dipped 15-17 dB for ~10 ms). The split joins and
    leaves the path only while parked just under half the sample rate (kParkFraction), where its
    all-pass is the plain signal to within a few degrees at 20 kHz, and glides between there and the
    Focus frequency at a rate proportional to the frequency (kGlidePerHz): each frequency above the
    Focus frequency is turned one cycle as the split passes it, slowly enough that its pitch moves
    ~1 %, never down in the 808's range. Park to 150 Hz takes ~0.2 s, to 1 kHz ~0.03 s; Kick <-> Snap
    glides directly. A new lift doesn't start while the band the detector listens to is widening.
    The split is a pair of state-variable filters (they move without clicks): the low band is the
    Linkwitz-Riley low-pass, the high band the all-pass minus it, so the two always add up to the
    all-pass exactly, also while the frequency moves.

    Constant latency (kLookaheadMs, so the lift is already up when the hit arrives) in every state;
    exact delayed pass-through when off or at Amount 0, cross-faded over 20 ms. Strength comes from
    params.squashDb when Auto-Master set it (the same in playback and export), otherwise from the live
    Squash meter (the last ~3 s); a new prepare() or reset() starts from the last live reading, so
    the first bars after a restart are lifted like the rest.

    prepare() allocates; process() does not. */
class Unlimiter
{
public:
    static constexpr int kMaxChannels = 8;
    static constexpr double kLookaheadMs = 5.0;
    static constexpr double kMaxBoostDb = 10.0;     // the most any hit is lifted: Amount 100 on brickwalled audio
    static constexpr double kSlopeDbPerDb = 4.0;    // lift per dB the hit jumps out (past the knee), before it levels off at the ceiling
    static constexpr double kKneeDb = 1.5;          // jumps smaller than about this are the music's own ripple...
    static constexpr double kKneeWidthDb = 2.0;     // ...eased in over this width around it (a soft knee, not a hard cut-off)
    static constexpr double kFullSquashDb = 8.5;    // Squash at or below this: full strength
    static constexpr double kOpenSquashDb = 12.5;   // at or above this: an open mix, nothing to undo
    static constexpr double kSplitKickHz = 150.0, kSplitSnapHz = 1000.0;
    static constexpr double kParkFraction = 0.499;  // where the band split joins and leaves the path (x sample rate)
    static constexpr double kGlidePerHz = 0.03;     // split glide: d ln(f) / dt = kGlidePerHz x f (pitch moves ~1.4 % at most)

    /** How much of Amount a Squash reading allows (0 ... 1). */
    static double squashScale (double squashDb) noexcept
    {
        return std::clamp ((kOpenSquashDb - squashDb) / (kOpenSquashDb - kFullSquashDb), 0.0, 1.0);
    }

    /** The lift (dB) for a hit that jumps riseDb above the level before it, at a strength of 0 ... 1
        (Amount times what Squash allows). The strength sets the ceiling (kMaxBoostDb at 1); the lift
        climbs toward it at kSlopeDbPerDb and levels off, so a clear hit gets close to the ceiling
        however far it happens to jump out of what plays around it. */
    static double liftForRiseDb (double riseDb, double strength) noexcept
    {
        if (strength <= 0.0) return 0.0;
        const double from = kKneeDb - 0.5 * kKneeWidthDb, over = riseDb - from;
        const double eased = over <= 0.0 ? 0.0 : over < kKneeWidthDb ? over * over / (2.0 * kKneeWidthDb) : riseDb - kKneeDb;
        const double ceiling = kMaxBoostDb * strength;
        return ceiling * std::tanh (kSlopeDbPerDb * eased / ceiling);
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

    /** The Squash reading the strength comes from (dB): params.squashDb when Auto-Master set it,
        otherwise the live one (true peak minus RMS over the last ~3 s of audio with signal, read as
        open when the peaks don't press against a ceiling). kFullSquashDb or less is brickwalled,
        kOpenSquashDb or more is open; 0 until there is a reading. */
    double squashDb() const noexcept { return params.squashDb > 0.0 ? params.squashDb : squash > 0.0 ? squash : seedSquash; }

    /** The same Squash reading for a whole clip: the median of its 3 s windows (louder ones only). */
    static double measureSquashDb (const std::vector<std::vector<float>>& channels, double sampleRate);

    /** Squash from 50 ms blocks (true peak, mean square): the crest, read as open (kOpenSquashDb) when
        the loudest blocks don't press against one ceiling the way a limiter or clipper leaves them. */
    static double squashFromBlocks (const double* peaks, const double* energies, int numBlocks) noexcept;

private:
    void updateCoefficients() noexcept;
    void updateSplit() noexcept;
    void setSplitFilters (bool clearState) noexcept;
    bool wantsBandPath() const noexcept { return ! isNeutral (params) && params.focus != UnlimiterFocus::full; }

    UnlimiterParams params;
    double sr = 48000.0;
    int channelCount = 0;
    int lookahead = 0;

    // Delay lines (lookahead), one ring per channel for the audio and the two bands of the split
    std::vector<double> delay, delayLow, delayHigh;
    int delaySize = 0, writePos = 0;

    // Detection and the band split at splitLog (ln Hz), which glides between the park and the Focus
    // frequency (see the class comment): two Butterworth state-variable filters in series per channel.
    struct SvfState { double ic1 = 0.0, ic2 = 0.0; };
    std::array<std::array<SvfState, 2>, kMaxChannels> svf {};
    double svfA1 = 0.0, svfA2 = 0.0, svfA3 = 0.0;
    double split = 0.0, splitTarget = 0.0, splitStep = 0.0;   // 0 = whole-signal path, 1 = band path (linear 20 ms fade)
    double splitLog = 0.0;
    double liftLow = 1.0, liftHigh = 1.0, aWeight = 0.0;      // share of the lift each band gets (Kick 1 / 0, Snap 0 / 1)
    bool splitRunning = false;                                // the band filters run (Focus not Full, or the split still in)
    UnlimiterFocus heard = UnlimiterFocus::full;              // what the detector listens to
    int settleLeft = 0, settleSamples = 0, splitCountdown = 0;
    double fastEnv = 0.0, slowEnv = 0.0;
    int holdSamples = 0, holdLeft = 0;
    std::vector<double> rmsRing;
    double rmsSum = 0.0;
    int rmsPos = 0;
    double aFastAtk = 0.0, aSlowAtk = 0.0, aRel = 0.0, aGain = 0.0, aLift = 0.0;
    double gainDb = 0.0, hitLift = 0.0;
    int liftHoldSamples = 0, liftLeft = 0;
    double amountNow = 0.0, aAmount = 0.0;
    double lengthSet = -1.0;
    double maxBoost = 0.0;

    // Squash meter: 50 ms blocks, the last 60 of them (3 s)
    static constexpr int kSquashBlocks = 60, kMinBlocks = 20;
    std::array<double, kSquashBlocks> blockPeak {}, blockEnergy {};
    std::array<TruePeakDetector, kMaxChannels> truePeak;
    int blockLen = 2400, blockFill = 0, blockIndex = 0, blocksSeen = 0;
    double curPeak = 0.0, curEnergy = 0.0;
    double squash = 0.0, squashNow = 0.0, aSquash = 0.0;
    double seedSquash = 0.0;   // last live reading: kept through reset() and prepare()
};

} // namespace polisher

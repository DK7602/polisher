#pragma once

#include "Biquad.h"
#include "ParkGlide.h"
#include "TruePeakDetector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace polisher {

struct LimiterParams
{
    double driveDb        = 0.0;    // input gain before limiting (how hard we push)
    double ceilingDbtp    = -1.0;   // output true-peak ceiling
    double releaseMs      = 120.0;  // base release time
    bool   adaptiveRelease = true;  // faster on transients, slower on sustained gain reduction
    double outputTrimDb   = 0.0;    // post-limiter trim (<= 0 keeps the ceiling guarantee)
    double bassFirst      = 0.0;    // 0 ... 1: take the limiting from the low end (< 120 Hz) first (see TruePeakLimiter)
    bool   bypassed       = false;  // passes audio through, still delayed by the latency (see TruePeakLimiter)
};

/** Look-ahead brick-wall limiter with 4x oversampled (true-peak) detection. The core of the
    Maximizer module.

    Gain computer (per sample, linked across channels):
      1. required gain r[n] = min(1, ceiling / truePeak(x[n]))
      2. sliding minimum of r over the look-ahead window (L + 1 samples)
      3. moving average of that minimum over L samples (smooth attack that is guaranteed to reach
         r by the time the peak leaves the delay line)
      4. exponential release, program dependent when adaptiveRelease is on
    The audio is delayed by L + TruePeakDetector::kLatency samples, so peaks are caught before they
    happen, with no overshoot.

    Bass First (a stage in front, kBassLookaheadMs of its own look-ahead, always in the path so the
    latency never changes): a Linkwitz-Riley split at kBassSplitHz (low and high add up to an all-pass,
    in phase with each other). When a sample would go over the ceiling and the low band pushes it there
    (an 808 under a vocal), the low band alone is turned down far enough to bring it back (at most
    kBassMaxCutDb), smoothed like the main gain (sliding minimum + moving average, release at half the
    set release). The rest of the mix is not turned down for that peak, so vocals and hats don't duck
    with the 808; the main stage then catches whatever is left (true peaks, highs that peak on their
    own). bassFirst scales how much of that cut is used. Off (0), the stage is an exact delay; it
    switches in by park and glide (see ParkGlide): the split fades in parked under Nyquist, glides down
    to kBassSplitHz, and only then starts cutting (backwards to switch off).

    Bypass (A in the plug-in) passes the delayed input through untouched. The gain computer keeps
    running underneath, so it always knows what is in the delay line. When bypass switches on, the
    samples still in the delay line were pushed for B (and the plug-in ramps its Drive out over that
    first block), so the limiter keeps limiting them: for the delay line, the block that came in with
    the switch and kBypassDrainMs more (while the other modules fade out), with a fast release so it
    lets go as soon as the level drops. Then it glides to unity over kBypassGlideMs. A B -> A switch
    never lets an unlimited peak out: the output stays at or under the ceiling until the glide, and
    the glide only fades the plain input in.

    A NaN or inf input sample would stay in Bass First's split filters for good (the output turned to NaN
    from then on): at the end of a block that put one there, the limiter is reset().

    prepare() allocates; process() does not. */
class TruePeakLimiter
{
public:
    static constexpr int kMaxChannels = 8;
    static constexpr double kLookaheadMs = 3.0;
    static constexpr double kBypassDrainMs = 50.0;
    static constexpr double kBypassDrainReleaseMs = 3.0;
    static constexpr double kBypassGlideMs = 10.0;
    static constexpr double kBassLookaheadMs = 1.5;
    static constexpr double kBassSplitHz = 120.0;
    static constexpr double kBassMaxCutDb = 12.0;
    static constexpr int kUpdateSamples = 16;   // Bass First's switching steps

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setParams (const LimiterParams& p) noexcept;
    const LimiterParams& getParams() const noexcept { return params; }

    int latencySamples() const noexcept { return bassLookahead + lookahead + TruePeakDetector::kLatency; }

    /** Deepest Bass First low-end cut since the last call (dB, <= 0). */
    double takeMaxBassCutDb() noexcept
    {
        const double g = minBassSinceRead;
        minBassSinceRead = 1.0;
        return g > 0.0 ? 20.0 * std::log10 (g) : -120.0;
    }

    /** Gain reduction applied to the most recent sample, in dB (<= 0). */
    double currentGainReductionDb() const noexcept { return gain > 0.0 ? 20.0 * std::log10 (gain) : -120.0; }

    /** Largest gain reduction since the last call (for meters), in dB (<= 0). */
    double takeMaxGainReductionDb() noexcept
    {
        const double g = minGainSinceRead;
        minGainSinceRead = 1.0;
        return g > 0.0 ? 20.0 * std::log10 (g) : -120.0;
    }

    template <typename Sample>
    void process (Sample* const* channels, int numChannels, int numSamples) noexcept
    {
        process (channels, numChannels, numSamples, nullptr);
    }

    /** The same, also writing the gain applied to each output sample (1 = untouched, before the final
        clamp at the ceiling) to gainOut[0 .. numSamples): the plug-in's listen modes give a monitored
        signal the level it has in the master. */
    template <typename Sample>
    void process (Sample* const* channels, int numChannels, int numSamples, double* gainOut) noexcept
    {
        process (channels, numChannels, numSamples, gainOut, nullptr);
    }

    /** The same, also writing (for the first two channels) what the output would be with no limiting at
        all, lined up with it: the input delayed, through Bass First's split when it is in the path. The
        plug-in's "what the Maximizer takes off" is that minus the output. */
    template <typename Sample>
    void process (Sample* const* channels, int numChannels, int numSamples, double* gainOut, double* const* refOut) noexcept
    {
        armDrain (numSamples);   // a switch to bypass: this block (Drive ramping out) is still limited
        const int nch = std::min (numChannels, channelCount);
        for (int i = 0; i < numSamples; ++i)
        {
            std::array<double, kMaxChannels> in {};
            for (int c = 0; c < nch; ++c)
                in[static_cast<size_t> (c)] = static_cast<double> (channels[c][i]);

            std::array<double, kMaxChannels> out {};
            processFrame (in.data(), out.data(), nch);

            for (int c = 0; c < nch; ++c)
                channels[c][i] = static_cast<Sample> (out[static_cast<size_t> (c)]);
            if (refOut != nullptr)
                for (int c = 0; c < std::min (nch, 2); ++c)
                    refOut[c][i] = lastRef[static_cast<size_t> (c)];
            if (gainOut != nullptr)
                gainOut[i] = lastGain;
        }
        guardState();
    }

    /** Processes one multichannel frame. Exposed for offline rendering and tests. */
    void processFrame (const double* in, double* out, int numChannels) noexcept;

private:
    void guardState() noexcept;   // a NaN / inf got into Bass First's split or the gain: reset() (end of each block)

    /** After a switch to bypass, how long the limiter keeps limiting: the delay line, the samples of
        the block that came with the switch, and kBypassDrainMs. */
    void armDrain (int blockSamples) noexcept
    {
        if (! drainArmed)
            return;
        drainArmed = false;
        drainLeft = delayLength + std::max (0, blockSamples) + drainExtra;
        glideLeft = 0;
    }

    // Monotonic-deque sliding-window minimum on a fixed-size ring (no allocation in process()).
    struct SlidingMin
    {
        std::vector<double> values;
        std::vector<long long> stamps;
        int head = 0, size = 0, capacity = 0, window = 1;
        long long now = 0;

        void prepare (int windowLength);
        void reset() noexcept { head = size = 0; now = 0; }
        double push (double v) noexcept;
    };

    struct MovingAverage
    {
        std::vector<double> ring;
        int pos = 0, length = 1;
        double sum = 0.0;
        long long sinceRecompute = 0;

        void prepare (int len);
        void reset (double fill) noexcept;
        double push (double v) noexcept;
    };

    double sampleRate = 48000.0;
    int channelCount = 2;
    int lookahead = 144;
    int delayLength = 156;

    LimiterParams params;
    double inputGain = 1.0, ceiling = 1.0, ceilingSafe = 1.0, outputGain = 1.0;
    double releaseFastCoef = 0.0, releaseSlowCoef = 0.0, sustainCoef = 0.0, drainReleaseCoef = 0.0;

    // Switching to bypass (see the class comment).
    bool drainArmed = false;
    bool limitedSinceReset = false;   // B has played since reset() (else there is nothing to drain)
    int drainLeft = 0, drainExtra = 2400;
    int glideLeft = 0, glideLength = 480;
    double glideFrom = 1.0;

    std::array<TruePeakDetector, kMaxChannels> detectors {};
    std::vector<double> delayBuffer;   // channelCount * delayLength, interleaved per channel block
    int delayPos = 0;

    SlidingMin slidingMin;
    MovingAverage smoother;
    double gain = 1.0;
    double sustain = 0.0;
    double minGainSinceRead = 1.0;
    double lastGain = 1.0;   // gain applied to the latest output sample

    // Bass First (see the class comment).
    void bassFirstStage (const double* in, double* out, double* ref, int nch) noexcept;
    std::vector<double> refBuffer;                         // the stage's output without its cut, through the main delay
    std::array<double, kMaxChannels> lastRef {};
    void bassUpdate (bool wanted) noexcept;
    void designBassSplit (double hz) noexcept;
    int bassLookahead = 72, bassPos = 0, bassCountdown = 0;
    std::vector<double> bassLine, lowLine, highLine;      // delayed input and its two bands, channelCount * (bassLookahead + 1)
    std::array<std::array<Biquad, 2>, kMaxChannels> bassLp {}, bassHp {};
    SlidingMin bassMin;
    MovingAverage bassSmooth;
    double bassGain = 1.0, bassReleaseCoef = 0.0, bassOffCoef = 0.0, bassFloor = 0.25, minBassSinceRead = 1.0;
    bool bassRunning = false, bassEngaged = false;        // split in the path / at its frequency and cutting
    double bassFade = 0.0, bassFadeGoal = 0.0, bassFadeStep = 0.0, bassLog = 0.0;
    ParkGlide::Segment bassSeg;
};

} // namespace polisher

#pragma once

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
    bool   bypassed       = false;  // passes audio through, still delayed by the latency
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

    prepare() allocates; process() does not. */
class TruePeakLimiter
{
public:
    static constexpr int kMaxChannels = 8;
    static constexpr double kLookaheadMs = 3.0;

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setParams (const LimiterParams& p) noexcept;
    const LimiterParams& getParams() const noexcept { return params; }

    int latencySamples() const noexcept { return lookahead + TruePeakDetector::kLatency; }

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
            if (gainOut != nullptr)
                gainOut[i] = lastGain;
        }
    }

    /** Processes one multichannel frame. Exposed for offline rendering and tests. */
    void processFrame (const double* in, double* out, int numChannels) noexcept;

private:
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
    double releaseFastCoef = 0.0, releaseSlowCoef = 0.0, sustainCoef = 0.0;

    std::array<TruePeakDetector, kMaxChannels> detectors {};
    std::vector<double> delayBuffer;   // channelCount * delayLength, interleaved per channel block
    int delayPos = 0;

    SlidingMin slidingMin;
    MovingAverage smoother;
    double gain = 1.0;
    double sustain = 0.0;
    double minGainSinceRead = 1.0;
    double lastGain = 1.0;   // gain applied to the latest output sample
};

} // namespace polisher

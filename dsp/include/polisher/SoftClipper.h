#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace polisher {

struct ClipperParams
{
    bool   enabled       = false;
    double thresholdDb   = -2.0;   // clipping ceiling in dBFS (output never exceeds it before the limiter)
    double softness      = 0.5;    // 0 = hard clip; 1 = the knee starts kKneeDepthDb below the threshold
    double makeupDb      = 0.0;    // gain after clipping (the plug-in uses it so peaks land at the ceiling)
    int    oversampling  = 4;      // 1, 2, 4 or 8
};

/** Linear-phase 2x halfband resampling stage (Kaiser-windowed sinc). Used in cascades of up to
    three stages for 2x / 4x / 8x oversampling. */
class HalfbandStage
{
public:
    explicit HalfbandStage (int numTaps = 33);

    void reset() noexcept;
    int numTaps() const noexcept { return static_cast<int> (taps.size()); }

    /** One sample in at rate r, two samples out at rate 2r. */
    void upsample (double x, double& out0, double& out1) noexcept;

    /** Two samples in at rate 2r, one sample out at rate r. */
    double downsample (double in0, double in1) noexcept;

private:
    std::vector<double> taps;          // full symmetric prototype, odd length, centre at (N-1)/2
    std::vector<double> evenTaps, oddTaps;
    std::vector<double> upHistory;     // input history at rate r, doubled for contiguous reads
    int upPos = 0;
    std::vector<double> downHistory;   // history at rate 2r, doubled
    int downPos = 0;
};

/** Oversampled soft-knee clipper: the stage before the Maximizer that shaves the tallest
    transients so the limiter works less.

    Transfer curve (per channel, in the oversampled domain):
      |x| <= knee                 -> unchanged
      knee < |x|                  -> knee + (T - knee) * tanh((|x| - knee) / (T - knee))
    where T is the threshold and the knee sits softness * kKneeDepthDb below it. The curve is continuous with slope 1 at
    the knee and never exceeds T, so the clipper output never goes above the threshold (before the
    down-sampling filter).

    Latency is constant (kLatency samples) for every oversampling setting and when disabled, so
    changing settings never shifts the audio in time. Every change is click-free: the oversampled
    path always runs, on/off cross-fades it with the time-aligned dry signal, a new oversampling
    setting warms up next to the old one and then cross-fades in, and Clip Level / ceiling moves
    glide over about 20 ms. prepare() allocates; process() does not. */
class SoftClipper
{
public:
    static constexpr int kMaxChannels = 8;
    static constexpr int kLatency = 59;   // samples at the base rate
    static constexpr double kKneeDepthDb = 6.0;   // knee depth at 100 % softness

    /** Where the curve starts to bend for a given threshold and softness (linear). */
    static double kneeFor (double threshold, double softness) noexcept
    {
        return threshold * std::pow (10.0, -std::clamp (softness, 0.0, 1.0) * kKneeDepthDb / 20.0);
    }

    void prepare (int numChannels, double sampleRate = 48000.0);
    void reset() noexcept;
    void setParams (const ClipperParams& p) noexcept;
    const ClipperParams& getParams() const noexcept { return params; }

    static constexpr int latencySamples() noexcept { return kLatency; }

    /** Processes one frame in place. */
    void processFrame (double* frame, int numChannels) noexcept;

    template <typename Sample>
    void process (Sample* const* channels, int numChannels, int numSamples) noexcept
    {
        std::array<double, kMaxChannels> frame {};
        const int nch = numChannels < channelCount ? numChannels : channelCount;
        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < nch; ++c) frame[static_cast<size_t> (c)] = static_cast<double> (channels[c][i]);
            processFrame (frame.data(), nch);
            for (int c = 0; c < nch; ++c) channels[c][i] = static_cast<Sample> (frame[static_cast<size_t> (c)]);
        }
    }

    /** Largest peak reduction applied since the last call, in dB (<= 0). */
    double takeMaxClipDb() noexcept
    {
        const double r = maxReductionRatio;
        maxReductionRatio = 1.0;
        return r < 1.0 ? 20.0 * std::log10 (r) : 0.0;
    }

    /** The static curve (exposed for tests and the UI). */
    static double shape (double x, double threshold, double softness) noexcept;

private:
    struct Channel
    {
        // Stage 0 runs at 2x (steep, 95 taps), stages 1 and 2 at 4x and 8x (gentler, 33 taps).
        std::array<std::array<HalfbandStage, 3>, 2> cascades {{
            { HalfbandStage (95), HalfbandStage (33), HalfbandStage (33) },
            { HalfbandStage (95), HalfbandStage (33), HalfbandStage (33) } }};
        // Delay lines: the dry signal (kLatency) and each cascade's padding up to kLatency.
        std::array<std::vector<double>, 3> delay;
        int delayPos = 0;
    };

    using Cascade = std::array<HalfbandStage, 3>;

    double clipSample (double x) noexcept;
    double processOversampled (Cascade& cascade, double x, int stages, int depth) noexcept;
    static void resetCascade (Cascade& c) noexcept { for (auto& s : c) s.reset(); }

    ClipperParams params;
    int channelCount = 2;
    double sampleRate = 48000.0;
    // Two cascades per channel: [current] plays, [1 - current] warms up after an oversampling change.
    int stagesOf[2] { 2, 2 };         // log2(oversampling) of each cascade
    int current = 0;
    bool switching = false;
    int warmup = 0;                   // samples left before the new cascade starts to fade in
    double switchMix = 0.0;           // 0 = old cascade, 1 = new
    double threshold = 1.0, makeup = 1.0, kneeRatio = 0.5;           // targets
    double thresholdNow = 1.0, makeupNow = 1.0, kneeRatioNow = 0.5;  // gliding values
    double mix = 0.0;                 // 0 = dry, 1 = clipped (glides toward enabled)
    double glideCoeff = 0.001, fadeStep = 0.001;
    bool snap = true;                 // the first setParams after reset() jumps instead of gliding
    std::vector<Channel> channelState;
    double maxReductionRatio = 1.0;
};

} // namespace polisher

#pragma once

#include "SoftClipper.h"   // HalfbandStage

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace polisher {

enum class SaturationMode { tape = 0, tube = 1, clip = 2 };

struct SaturationParams
{
    bool enabled = true;
    SaturationMode mode = SaturationMode::tape;
    double driveDb = 0.0;   // 0 = off (exact pass-through) ... kMaxDriveDb
    double mix = 100.0;     // % of the saturated signal (parallel blend)
};

/** Mastering saturation: adds harmonics that make a master feel denser and help bass translate
    to small speakers.

    - Curves (per channel, 4x oversampled so the new harmonics don't fold back as aliasing):
        tape  y = S tanh(x / S)                          smooth, odd harmonics
        tube  asymmetric tanh (bias 0.35), slope 1 at 0   warm, mostly 2nd harmonic
        clip  y = x / (1 + |x/S|^4)^(1/4)                firmer knee, brighter odd harmonics
      S (where the curve bends) starts 12 dB above full scale at Drive 0 and comes down 1 dB per dB
      of Drive, so quiet material always passes at unity gain and only the louder moments are
      shaped. Level therefore stays put as you add Drive.
    - Only what the curve adds (harmonics and peak rounding) goes through the oversampling filters
      and a 10 Hz high-pass (removes the DC the tube curve creates); it is mixed back onto the dry
      signal, so the clean signal is never filtered and Mix is a true parallel blend.
    - Constant latency (kLatency) in every state; exact pass-through (delayed) when off, at Drive 0
      or Mix 0, with 20 ms cross-fades. Drive glides, so moves never zipper.

    prepare() allocates; process() does not. */
class Saturation
{
public:
    static constexpr int kMaxChannels = 8;
    static constexpr int kLatency = 55;          // 2x (95 taps) + 4x (33 taps) round trip
    static constexpr double kMaxDriveDb = 18.0;
    static constexpr double kHeadroomDb = 12.0;  // S at Drive 0, above full scale
    static constexpr double kTubeBias = 0.35;

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setParams (const SaturationParams& p) noexcept { params = p; }
    const SaturationParams& getParams() const noexcept { return params; }
    static bool isNeutral (const SaturationParams& p) noexcept
    {
        return ! p.enabled || p.driveDb < 0.05 || p.mix < 0.05;
    }
    static constexpr int latencySamples() noexcept { return kLatency; }

    /** The static curve at a given bend point S (exposed for tests and the UI). */
    static double shape (double x, SaturationMode mode, double s) noexcept;

    void processFrame (double* frame, int numChannels) noexcept;

    template <typename Sample>
    void process (Sample* const* channels, int numChannels, int numSamples) noexcept
    {
        std::array<double, kMaxChannels> frame {};
        const int nch = std::min (numChannels, channelCount);
        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < nch; ++c) frame[static_cast<size_t> (c)] = static_cast<double> (channels[c][i]);
            processFrame (frame.data(), nch);
            for (int c = 0; c < nch; ++c) channels[c][i] = static_cast<Sample> (frame[static_cast<size_t> (c)]);
        }
    }

    /** Level of the added harmonics relative to the signal since the last call (dB; -100 when
        nothing was added). */
    double takeHarmonicsDb() noexcept
    {
        const double r = sigEnergy > 1e-20 ? resEnergy / sigEnergy : 0.0;
        resEnergy = sigEnergy = 0.0;
        return r > 1e-10 ? 10.0 * std::log10 (r) : -100.0;
    }

private:
    struct Channel
    {
        std::array<HalfbandStage, 2> stages { HalfbandStage (95), HalfbandStage (33) };
        std::vector<double> dry;   // latency-matched dry path
        int dryPos = 0;
        double hpX = 0.0, hpY = 0.0;
    };

    double curve (double x) const noexcept { return shape (x, params.mode, bend); }

    SaturationParams params;
    int channelCount = 2;
    std::vector<Channel> channelState;
    double bend = 4.0, bendTarget = 4.0, glideCoeff = 0.0;
    double mix = 0.0, mixCoeff = 0.0, hpCoeff = 0.0;
    double resEnergy = 0.0, sigEnergy = 0.0;
    int warmup = 0;
    bool running = false;
};

} // namespace polisher

#pragma once

#include "Biquad.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace polisher {

/** The three classic bus-compressor flavours: Bus (a clean VCA bus compressor: firm knee, quick
    detector), Opto (smooth: soft knee, an RMS detector, never faster than 10 ms, slower to let go)
    and Tube (the softest: a very wide knee, so the ratio grows gently with level, slow and round). */
enum class GlueCharacter { bus = 0, opto = 1, tube = 2 };

inline constexpr double kGlueRatioNeutral = 1.005;   // at or under: no compression
inline constexpr double kGlueGainNeutralDb = 0.05;
inline constexpr double kGlueSidechainOffHz = 20.5;  // Ignore Lows at or under this = off

struct GlueParams
{
    bool enabled = true;
    GlueCharacter character = GlueCharacter::bus;
    double thresholdDb = -20.0;   // -40 ... 0
    double ratio = 1.0;           // 1 (off) ... 10
    double attackMs = 10.0;       // 0.1 ... 30
    double releaseMs = 300.0;     // 50 ... 1200
    bool autoRelease = true;      // quick after short peaks, the Release time on long passages
    double sidechainHz = 90.0;    // the detector hears the lows below this 24 dB/oct quieter (the 808 doesn't pump the mix); <= 20 = off
    double mix = 1.0;             // 0 ... 1: blend with the uncompressed sound (parallel compression)
    double gainDb = 0.0;          // -12 ... +12: level after the Glue (makeup)
};

/** Glue: one compressor for the whole mix, so the parts move together ("glue") instead of band by
    band like Dynamics. Stereo-linked (the louder channel sets the gain for both), feed-forward, with a
    soft knee whose width is set by the character.

    - Detector: each channel through the sidechain high-pass (4th order, 24 dB/oct, Ignore Lows), its mean square
      over the character's RMS time; the level is the louder channel's, read like a sine's peak.
    - Gain: the static curve (threshold, ratio, knee), smoothed in dB with Attack / Release (scaled by
      the character). Auto release lets go quickly (60 ms, or the Release time if that is quicker) while the
      gain is more than 1 dB under its ~1 s average (a short peak) and at the Release time otherwise.
    - Output: (dry * (1 - mix) + compressed * mix) * gain. Mix and gain glide (20 ms).
    - Neutral (ratio 1:1 and gain 0 dB) or off: an exact pass-through. Switching fades the whole effect
      in or out over 20 ms; once faded out the audio is not touched at all.

    Zero latency. process() never allocates. The same gain goes on every channel; the first two are
    detected. */
class GlueCompressor
{
public:
    static constexpr int kMaxChannels = 8;
    static constexpr int kUpdateInterval = 16;
    static constexpr double kFastReleaseMs = 60.0;
    static constexpr double kFadeMs = 20.0;

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setParams (const GlueParams& p) noexcept { params = p; }
    const GlueParams& getParams() const noexcept { return params; }
    static bool isNeutral (const GlueParams& p) noexcept
    {
        return ! p.enabled || (p.ratio <= kGlueRatioNeutral && std::abs (p.gainDb) < kGlueGainNeutralDb);
    }

    /** Deepest gain reduction (dB, <= 0) since the last call, for the meter. */
    double takeMaxReductionDb() noexcept
    {
        const double v = maxReduction;
        maxReduction = 0.0;
        return v;
    }
    double currentReductionDb() const noexcept { return grDb * weight; }

    /** The static curve: gain change (dB, <= 0) for a detected level, for the current settings. */
    double gainComputer (double levelDb) const noexcept;

    /** gainOut (optional): the gain applied to each sample, for Listen signals formed before the Glue that
        must get what the master gets (1.0 while it passes the sound untouched). */
    template <typename Sample>
    void process (Sample* const* ch, int numChannels, int numSamples, double* gainOut = nullptr) noexcept
    {
        const int nch = std::min (numChannels, kMaxChannels);
        const int detected = std::min (nch, 2);
        for (int i = 0; i < numSamples; ++i)
        {
            if (countdown-- <= 0)
            {
                update();
                countdown = kUpdateInterval - 1;
            }
            if (! running)
            {
                if (gainOut != nullptr) gainOut[i] = 1.0;
                continue;
            }

            double ms = 0.0;
            for (int c = 0; c < detected; ++c)
            {
                double v = static_cast<double> (ch[c][i]);
                if (sidechainOn)
                    v = sidechain[static_cast<size_t> (c)][1].process (sidechain[static_cast<size_t> (c)][0].process (v));
                double& m = meanSquare[static_cast<size_t> (c)];
                m += (v * v - m) * rmsCoeff;
                if (m < 1.0e-30) m = 0.0;
                ms = std::max (ms, m);
            }
            const double levelDb = 10.0 * std::log10 (2.0 * ms + 1.0e-30);
            const double target = gainComputer (levelDb);
            const bool attacking = target < grDb;
            double coeff = attacking ? attackCoeff : releaseCoeff;
            if (! attacking && params.autoRelease && grDb < grAverage - 1.0)
                coeff = std::max (coeff, fastReleaseCoeff);
            grDb += (target - grDb) * coeff;
            if (std::abs (grDb) < 1.0e-9) grDb = 0.0;
            grAverage += (grDb - grAverage) * averageCoeff;

            weight = weightGoal > weight ? std::min (weightGoal, weight + fadeStep) : std::max (weightGoal, weight - fadeStep);
            mix += (mixGoal - mix) * glideCoeff;
            gainDb += (gainGoalDb - gainDb) * glideCoeff;
            maxReduction = std::min (maxReduction, grDb * weight);

            const double compressed = std::pow (10.0, grDb * weight / 20.0);
            const double blend = 1.0 + (compressed - 1.0) * mix;
            const double g = blend * std::pow (10.0, gainDb * weight / 20.0);
            for (int c = 0; c < nch; ++c)
                ch[c][i] = static_cast<Sample> (static_cast<double> (ch[c][i]) * g);
            if (gainOut != nullptr) gainOut[i] = g;
        }
    }

private:
    void update() noexcept;
    void setTimes() noexcept;

    GlueParams params;
    double sampleRate = 48000.0;
    int countdown = 0;

    std::array<std::array<Biquad, 2>, 2> sidechain {};   // [channel][stage]
    bool sidechainOn = false;
    double sidechainSetHz = -1.0;
    std::array<double, 2> meanSquare {};

    double grDb = 0.0, grAverage = 0.0, maxReduction = 0.0;
    double rmsCoeff = 1.0, attackCoeff = 1.0, releaseCoeff = 1.0, fastReleaseCoeff = 1.0, averageCoeff = 1.0;
    double kneeDb = 4.0;
    GlueCharacter setCharacter = GlueCharacter::bus;
    double setAttackMs = -1.0, setReleaseMs = -1.0;

    bool running = false;
    double weight = 0.0, weightGoal = 0.0, fadeStep = 1.0;
    double mix = 1.0, mixGoal = 1.0, gainDb = 0.0, gainGoalDb = 0.0, glideCoeff = 1.0;
};

} // namespace polisher

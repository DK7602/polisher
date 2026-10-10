#pragma once

#include "Biquad.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace polisher {

inline constexpr int kDynBands = 4;
inline constexpr double kDynGainNeutralDb = 0.01;   // band gains closer to 0 dB than this count as 0 dB

struct DynBandParams
{
    double thresholdDb = 0.0;   // level (sine-peak calibrated) where compression starts
    double ratio = 1.0;         // 1 = off
    double attackMs = 20.0;
    double releaseMs = 150.0;
    double gainDb = 0.0;        // makeup / band level after compression
};

struct DynamicsParams
{
    bool enabled = true;
    std::array<double, kDynBands - 1> crossoverHz { 120.0, 1000.0, 6000.0 };
    std::array<DynBandParams, kDynBands> bands {
        DynBandParams { 0.0, 1.0, 30.0, 200.0, 0.0 },
        DynBandParams { 0.0, 1.0, 20.0, 150.0, 0.0 },
        DynBandParams { 0.0, 1.0, 10.0, 120.0, 0.0 },
        DynBandParams { 0.0, 1.0, 5.0, 100.0, 0.0 } };
    bool adaptiveRelease = true;
};

/** Crossover ranges (Hz) for the three split points. */
inline constexpr std::array<std::array<double, 2>, kDynBands - 1> kDynCrossoverRange { { { 40.0, 300.0 }, { 300.0, 3000.0 }, { 3000.0, 12000.0 } } };

/** Four-band mastering compressor.

    - Split: Linkwitz-Riley 4th-order crossovers in a tree (mid split first), with 2nd-order all-pass
      compensation on the opposite branch, so the four bands always sum to a flat magnitude response.
      Zero latency.
    - Detection: stereo-linked (each channel's RMS is measured, the louder one drives both, so the
      image never shifts), RMS with a per-band window (30 / 10 / 5 / 3 ms, longest in the lows so
      bass cycles don't ripple the gain), calibrated so a sine reads its peak level.
    - Gain computer: 6 dB soft knee; attack / release smoothing in dB; optional program-dependent
      release (quick after short hits, the set release on sustained compression).
    - Exact pass-through when every band is at 1:1 with 0 dB gain (or the module is off): the split
      path is cross-faded in and out over ~20 ms so switching never clicks. Makeup and crossover
      changes glide.

    process() never allocates. Processes the first two channels (stereo) or one (mono). */
class MultibandDynamics
{
public:
    static constexpr int kUpdateInterval = 16;
    static constexpr double kKneeDb = 6.0;

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;

    void setParams (const DynamicsParams& p) noexcept { params = p; }
    const DynamicsParams& getParams() const noexcept { return params; }
    static bool isNeutral (const DynamicsParams& p) noexcept;

    /** Listen hook (the plug-in's Solo / Listen buttons): band 0..3, or -1 for none. Solo: that band
        after its compression and gain; removed: what its compression takes off it (before the gain).
        While set, the split runs even when the module is neutral or off (a solo is then the plain band;
        the module's own output stays an exact pass-through). */
    void setMonitorBand (int bandIndex, bool removed) noexcept
    {
        monitorBand = bandIndex >= 0 && bandIndex < kDynBands ? bandIndex : -1;
        monitorRemoved = removed;
    }

    template <typename Sample>
    void process (Sample* const* ch, int numChannels, int numSamples) noexcept
    {
        process (ch, numChannels, numSamples, nullptr);
    }

    /** The same, also writing the monitored band (see setMonitorBand) to monitorOut[c][0 .. numSamples)
        for the first two channels (zeros when no band is set). */
    template <typename Sample>
    void process (Sample* const* ch, int numChannels, int numSamples, double* const* monitorOut) noexcept
    {
        const int nch = std::min ({ numChannels, channelCount, 2 });
        for (int i = 0; i < numSamples; ++i)
        {
            if (countdown-- <= 0) { update(); countdown = kUpdateInterval - 1; }
            const bool monitoring = monitorOut != nullptr && monitorBand >= 0;
            if (! clocking)
            {
                if (monitorOut != nullptr)
                    for (int c = 0; c < nch; ++c) monitorOut[c][i] = 0.0;
                continue;
            }

            std::array<std::array<double, kDynBands>, 2> band {};
            std::array<double, 2> dry {};
            for (int c = 0; c < nch; ++c)
            {
                auto& s = chan[static_cast<size_t> (c)];
                const double x = static_cast<double> (ch[c][i]);
                dry[static_cast<size_t> (c)] = x;
                double lo = s.x2lp[1].process (s.x2lp[0].process (x));
                double hi = s.x2hp[1].process (s.x2hp[0].process (x));
                lo = s.apX3.process (lo);
                hi = s.apX1.process (hi);
                auto& b = band[static_cast<size_t> (c)];
                b[0] = s.x1lp[1].process (s.x1lp[0].process (lo));
                b[1] = s.x1hp[1].process (s.x1hp[0].process (lo));
                b[2] = s.x3lp[1].process (s.x3lp[0].process (hi));
                b[3] = s.x3hp[1].process (s.x3hp[0].process (hi));
            }

            if (! running)
            {
                // Neutral or off, split only for a solo: the plain band; the output stays untouched.
                if (monitorOut != nullptr)
                    for (int c = 0; c < nch; ++c)
                        monitorOut[c][i] = monitoring && ! monitorRemoved ? band[static_cast<size_t> (c)][static_cast<size_t> (monitorBand)] : 0.0;
                continue;
            }

            std::array<double, kDynBands> gain {};
            for (size_t k = 0; k < kDynBands; ++k)
            {
                // Each channel's RMS is smoothed on its own, then the louder one drives both. (Taking
                // the louder sample first would read a wide stereo sound ~2 dB too hot.)
                auto& d = det[k];
                d.meanSquare = 0.0;
                for (int c = 0; c < nch; ++c)
                {
                    const double v = band[static_cast<size_t> (c)][k];
                    double& ms = d.channelMeanSquare[static_cast<size_t> (c)];
                    ms += (v * v - ms) * d.rmsCoeff;
                    if (ms < 1.0e-30) ms = 0.0;   // no denormals in silence (offline renders too)
                    d.meanSquare = std::max (d.meanSquare, ms);
                }
                const double levelDb = 10.0 * std::log10 (2.0 * d.meanSquare + 1.0e-30);
                const double target = gainComputer (levelDb, k);
                const bool attacking = target < d.grDb;
                double coeff = attacking ? d.attackCoeff : d.releaseCoeff;
                if (! attacking && params.adaptiveRelease && d.grDb < d.grAverage - 1.0)
                    coeff = d.fastReleaseCoeff;   // a short dip below the running amount: recover quickly
                d.grDb += (target - d.grDb) * coeff;
                d.grAverage += (d.grDb - d.grAverage) * averageCoeff;
                d.maxGr = std::min (d.maxGr, d.grDb);
                d.makeupDb += (d.targetMakeupDb - d.makeupDb) * makeupCoeff;
                gain[k] = std::pow (10.0, (d.grDb + d.makeupDb) / 20.0);
            }

            mix += (mixTarget - mix) * mixCoeff;
            for (int c = 0; c < nch; ++c)
            {
                const auto& b = band[static_cast<size_t> (c)];
                double wet = 0.0;
                for (size_t k = 0; k < kDynBands; ++k)
                    wet += b[k] * gain[k];
                const double x = dry[static_cast<size_t> (c)];
                ch[c][i] = static_cast<Sample> (x + (wet - x) * mix);
            }
            if (monitorOut != nullptr)
            {
                // Solo: the band as it is inside the output (the four solos add up to the output once
                // the module has faded in). Removed: what the gain reduction takes off it.
                const auto k = static_cast<size_t> (monitoring ? monitorBand : 0);
                const double g = ! monitoring ? 0.0
                               : monitorRemoved ? (1.0 - std::pow (10.0, det[k].grDb / 20.0)) * mix
                                                : 1.0 + (gain[k] - 1.0) * mix;
                for (int c = 0; c < nch; ++c)
                    monitorOut[c][i] = band[static_cast<size_t> (c)][k] * g;
            }
        }
    }

    /** Deepest gain reduction in a band since the last call (dB, <= 0). */
    double takeMaxGrDb (int bandIndex) noexcept
    {
        auto& d = det[static_cast<size_t> (bandIndex)];
        const double v = d.maxGr;
        d.maxGr = 0.0;
        return v;
    }

    bool isActive() const noexcept { return running; }

    /** The band's gain reduction right now (dB, <= 0) and its detector level (dB, sine-peak
        calibrated). For offline analysis (Auto-Master); read between process() calls. */
    double currentGrDb (int bandIndex) const noexcept { return det[static_cast<size_t> (bandIndex)].grDb; }
    double currentLevelDb (int bandIndex) const noexcept
    {
        return 10.0 * std::log10 (2.0 * det[static_cast<size_t> (bandIndex)].meanSquare + 1.0e-30);
    }

private:
    struct Channel
    {
        std::array<Biquad, 2> x1lp {}, x1hp {}, x2lp {}, x2hp {}, x3lp {}, x3hp {};
        Biquad apX1, apX3;
    };
    struct Detector
    {
        std::array<double, 2> channelMeanSquare {};
        double meanSquare = 0.0, grDb = 0.0, grAverage = 0.0, maxGr = 0.0;
        double makeupDb = 0.0, targetMakeupDb = 0.0;
        double rmsCoeff = 0.0, attackCoeff = 0.0, releaseCoeff = 0.0, fastReleaseCoeff = 0.0;
        double setAttackMs = -1.0, setReleaseMs = -1.0;
    };

    double gainComputer (double levelDb, size_t k) const noexcept
    {
        const auto& b = params.bands[k];
        const double ratio = std::max (1.0, b.ratio);
        if (ratio <= 1.0)
            return 0.0;
        const double over = levelDb - b.thresholdDb;
        const double slope = 1.0 / ratio - 1.0;
        if (2.0 * over < -kKneeDb)
            return 0.0;
        if (2.0 * std::abs (over) <= kKneeDb)
        {
            const double t = over + 0.5 * kKneeDb;
            return slope * t * t / (2.0 * kKneeDb);
        }
        return slope * over;
    }

    void update() noexcept;
    void applyCrossovers() noexcept;
    void resetFilters() noexcept;
    void resetDetectors() noexcept;

    DynamicsParams params;
    double sampleRate = 48000.0;
    int channelCount = 2;
    int countdown = 0;
    bool running = false;    // compressing (or fading in / out)
    bool clocking = false;   // the split filters run: running, or a band is monitored
    int monitorBand = -1;
    bool monitorRemoved = false;
    double mix = 0.0, mixTarget = 0.0, mixCoeff = 0.0, glideCoeff = 0.0, makeupCoeff = 0.0, averageCoeff = 0.0;
    std::array<double, kDynBands - 1> logFreq {}, targetLogFreq {};
    std::array<Channel, 2> chan {};
    std::array<Detector, kDynBands> det {};
};

} // namespace polisher

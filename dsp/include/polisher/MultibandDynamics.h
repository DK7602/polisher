#pragma once

#include "Biquad.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace polisher {

inline constexpr int kDynBands = 4;

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
    - Detection: stereo-linked (the louder channel drives both, so the image never shifts), RMS with
      a per-band window (30 / 10 / 5 / 3 ms, longest in the lows so bass cycles don't ripple the
      gain), calibrated so a sine reads its peak level.
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

    template <typename Sample>
    void process (Sample* const* ch, int numChannels, int numSamples) noexcept
    {
        const int nch = std::min ({ numChannels, channelCount, 2 });
        for (int i = 0; i < numSamples; ++i)
        {
            if (countdown-- <= 0) { update(); countdown = kUpdateInterval - 1; }
            if (! running)
                continue;

            std::array<std::array<double, kDynBands>, 2> band {};
            std::array<double, kDynBands> energy {};
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
                for (size_t k = 0; k < kDynBands; ++k)
                    energy[k] = std::max (energy[k], b[k] * b[k]);
            }

            std::array<double, kDynBands> gain {};
            for (size_t k = 0; k < kDynBands; ++k)
            {
                auto& d = det[k];
                d.meanSquare += (energy[k] - d.meanSquare) * d.rmsCoeff;
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

    DynamicsParams params;
    double sampleRate = 48000.0;
    int channelCount = 2;
    int countdown = 0;
    bool running = false;
    double mix = 0.0, mixTarget = 0.0, mixCoeff = 0.0, glideCoeff = 0.0, makeupCoeff = 0.0, averageCoeff = 0.0;
    std::array<double, kDynBands - 1> logFreq {}, targetLogFreq {};
    std::array<Channel, 2> chan {};
    std::array<Detector, kDynBands> det {};
};

} // namespace polisher

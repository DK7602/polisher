#pragma once

#include "KWeighting.h"
#include "TruePeakDetector.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace polisher {

/** Converts a K-weighted mean-square energy to LUFS (ITU-R BS.1770-4). Returns -inf for silence. */
inline double energyToLufs (double energy) noexcept
{
    return energy > 0.0 ? -0.691 + 10.0 * std::log10 (energy)
                        : -std::numeric_limits<double>::infinity();
}

inline double lufsToEnergy (double lufs) noexcept
{
    return std::pow (10.0, (lufs + 0.691) / 10.0);
}

inline double gainToDb (double g) noexcept
{
    return g > 0.0 ? 20.0 * std::log10 (g) : -std::numeric_limits<double>::infinity();
}

inline double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }

/** Real-time loudness meter following ITU-R BS.1770-4, EBU R128 and EBU Tech 3341/3342:

    - Momentary (400 ms) and short-term (3 s) loudness, updated every 100 ms
    - Integrated loudness with the -70 LUFS absolute and -10 LU relative gates
    - Loudness range (LRA) with the -70 LUFS absolute and -20 LU relative gates
    - True peak (4x oversampled) and sample peak

    Gated statistics use fine-grained histograms (0.02 LU bins that also keep the exact energy
    sum), so memory is constant and process() never allocates. Safe to call from the audio thread
    after prepare(). Supports up to kMaxChannels; channel weights default to 1.0 (L, R, C) and
    1.41 for channels 4 and 5 (surrounds), with the LFE (channel 3) excluded, per BS.1770. */
class LoudnessMeter
{
public:
    static constexpr int kMaxChannels = 8;

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;

    /** Starts integrated loudness, LRA and the true-peak/max readings over, but keeps the running
        momentary and short-term windows, so those keep reading without a gap. */
    void resetStatistics() noexcept;

    template <typename Sample>
    void process (const Sample* const* channels, int numChannels, int numSamples) noexcept
    {
        const int nch = numChannels < channelCount ? numChannels : channelCount;
        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < nch; ++c)
            {
                const double x = static_cast<double> (channels[c][i]);
                const double k = weighting[static_cast<size_t> (c)].process (x);
                hopSum[static_cast<size_t> (c)] += k * k;

                const double ax = std::abs (x);
                if (ax > samplePeak) samplePeak = ax;
                const double tp = truePeak[static_cast<size_t> (c)].process (x);
                if (tp > truePeakMax) truePeakMax = tp;
                if (tp > truePeakRecent) truePeakRecent = tp;
            }
            if (++hopPos == hopLength)
                finishHop();
        }
    }

    double momentaryLufs() const noexcept   { return energyToLufs (momentaryEnergy); }
    double shortTermLufs() const noexcept   { return energyToLufs (shortTermEnergy); }
    double integratedLufs() const noexcept;
    double loudnessRangeLu() const noexcept;
    double maxMomentaryLufs() const noexcept { return energyToLufs (maxMomentaryEnergy); }
    double maxShortTermLufs() const noexcept { return energyToLufs (maxShortTermEnergy); }
    double truePeakDbtp() const noexcept     { return gainToDb (truePeakMax); }
    double samplePeakDbfs() const noexcept   { return gainToDb (samplePeak); }

    /** Largest true peak since the last call, for peak-hold meter bars. */
    double takeRecentTruePeakDbtp() noexcept
    {
        const double v = truePeakRecent;
        truePeakRecent = 0.0;
        return gainToDb (v);
    }

    /** Seconds of audio measured since the last reset. */
    double measuredSeconds() const noexcept { return static_cast<double> (hopsSeen) * 0.1; }

private:
    struct GatedHistogram
    {
        static constexpr double kMinLufs = -70.0;
        static constexpr double kMaxLufs = 10.0;
        static constexpr double kBinLu   = 0.02;
        static constexpr int    kBins    = 4000;

        std::array<std::uint32_t, kBins> counts {};
        std::array<double, kBins> energySums {};
        std::uint64_t total = 0;
        double totalEnergy = 0.0;

        void clear() noexcept;
        void add (double energy) noexcept;          // ignores blocks at or below -70 LUFS
        static int binFor (double lufs) noexcept;
    };

    void finishHop() noexcept;

    double sampleRate = 48000.0;
    int channelCount = 2;
    int hopLength = 4800;
    int hopPos = 0;
    std::uint64_t hopsSeen = 0;

    std::array<KWeighting, kMaxChannels> weighting {};
    std::array<TruePeakDetector, kMaxChannels> truePeak {};
    std::array<double, kMaxChannels> channelWeight {};
    std::array<double, kMaxChannels> hopSum {};

    static constexpr int kShortTermHops = 30;
    static constexpr int kMomentaryHops = 4;
    std::array<double, kShortTermHops> hopEnergies {};
    int hopIndex = 0;

    double momentaryEnergy = 0.0, shortTermEnergy = 0.0;
    double maxMomentaryEnergy = 0.0, maxShortTermEnergy = 0.0;
    double samplePeak = 0.0, truePeakMax = 0.0, truePeakRecent = 0.0;

    GatedHistogram integratedBlocks, shortTermBlocks;
};

} // namespace polisher

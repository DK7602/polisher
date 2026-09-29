#include "polisher/LoudnessMeter.h"

#include <algorithm>

namespace polisher {

void LoudnessMeter::prepare (double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate;
    channelCount = std::clamp (numChannels, 1, kMaxChannels);
    hopLength = std::max (1, static_cast<int> (std::lround (sampleRate * 0.1)));

    for (int c = 0; c < kMaxChannels; ++c)
    {
        weighting[static_cast<size_t> (c)].prepare (sampleRate);
        // BS.1770 channel weights for the standard 5.1 order L R C LFE Ls Rs.
        double w = 1.0;
        if (channelCount > 2 && c == 3) w = 0.0;             // LFE excluded
        if (channelCount > 4 && (c == 4 || c == 5)) w = 1.41; // surrounds
        channelWeight[static_cast<size_t> (c)] = w;
    }
    reset();
}

void LoudnessMeter::reset() noexcept
{
    for (auto& w : weighting) w.reset();
    for (auto& t : truePeak) t.reset();
    hopSum.fill (0.0);
    hopEnergies.fill (0.0);
    hopPos = 0;
    hopIndex = 0;
    hopsSeen = 0;
    momentaryEnergy = shortTermEnergy = 0.0;
    maxMomentaryEnergy = maxShortTermEnergy = 0.0;
    samplePeak = truePeakMax = truePeakRecent = 0.0;
    integratedBlocks.clear();
    shortTermBlocks.clear();
}

void LoudnessMeter::finishHop() noexcept
{
    double energy = 0.0;
    for (int c = 0; c < channelCount; ++c)
    {
        energy += channelWeight[static_cast<size_t> (c)] * hopSum[static_cast<size_t> (c)] / hopLength;
        hopSum[static_cast<size_t> (c)] = 0.0;
    }
    hopPos = 0;

    hopEnergies[static_cast<size_t> (hopIndex)] = energy;
    hopIndex = (hopIndex + 1) % kShortTermHops;
    ++hopsSeen;

    auto meanOfLast = [this] (int count)
    {
        double sum = 0.0;
        for (int i = 1; i <= count; ++i)
            sum += hopEnergies[static_cast<size_t> ((hopIndex - i + kShortTermHops) % kShortTermHops)];
        return sum / count;
    };

    if (hopsSeen >= kMomentaryHops)
    {
        // 400 ms gating block with 75 % overlap == momentary loudness at this hop.
        momentaryEnergy = meanOfLast (kMomentaryHops);
        maxMomentaryEnergy = std::max (maxMomentaryEnergy, momentaryEnergy);
        integratedBlocks.add (momentaryEnergy);
    }

    if (hopsSeen >= kShortTermHops)
    {
        shortTermEnergy = meanOfLast (kShortTermHops);
        maxShortTermEnergy = std::max (maxShortTermEnergy, shortTermEnergy);
        shortTermBlocks.add (shortTermEnergy);
    }
}

double LoudnessMeter::integratedLufs() const noexcept
{
    const auto& h = integratedBlocks;
    if (h.total == 0)
        return -std::numeric_limits<double>::infinity();

    // Relative gate: 10 LU below the loudness of all blocks above the absolute gate.
    const double relativeGate = energyToLufs (h.totalEnergy / static_cast<double> (h.total)) - 10.0;
    const int firstBin = std::max (0, GatedHistogram::binFor (relativeGate));

    double energy = 0.0;
    std::uint64_t count = 0;
    for (int b = firstBin; b < GatedHistogram::kBins; ++b)
    {
        energy += h.energySums[static_cast<size_t> (b)];
        count  += h.counts[static_cast<size_t> (b)];
    }
    return count > 0 ? energyToLufs (energy / static_cast<double> (count))
                     : -std::numeric_limits<double>::infinity();
}

double LoudnessMeter::loudnessRangeLu() const noexcept
{
    const auto& h = shortTermBlocks;
    if (h.total == 0)
        return 0.0;

    // EBU Tech 3342: relative gate 20 LU below the (absolute-gated) mean, then 10th..95th percentile.
    const double relativeGate = energyToLufs (h.totalEnergy / static_cast<double> (h.total)) - 20.0;
    const int firstBin = std::max (0, GatedHistogram::binFor (relativeGate));

    std::uint64_t count = 0;
    for (int b = firstBin; b < GatedHistogram::kBins; ++b)
        count += h.counts[static_cast<size_t> (b)];
    if (count == 0)
        return 0.0;

    auto percentile = [&] (double p)
    {
        const auto target = static_cast<std::uint64_t> (std::floor (p * static_cast<double> (count - 1)));
        std::uint64_t seen = 0;
        for (int b = firstBin; b < GatedHistogram::kBins; ++b)
        {
            seen += h.counts[static_cast<size_t> (b)];
            if (seen > target)
                return GatedHistogram::kMinLufs + (b + 0.5) * GatedHistogram::kBinLu;
        }
        return GatedHistogram::kMaxLufs;
    };

    return std::max (0.0, percentile (0.95) - percentile (0.10));
}

// --- GatedHistogram --------------------------------------------------------------------------

void LoudnessMeter::GatedHistogram::clear() noexcept
{
    counts.fill (0);
    energySums.fill (0.0);
    total = 0;
    totalEnergy = 0.0;
}

int LoudnessMeter::GatedHistogram::binFor (double lufs) noexcept
{
    if (! (lufs > kMinLufs))
        return 0;
    const auto b = static_cast<int> ((lufs - kMinLufs) / kBinLu);
    return std::min (b, kBins - 1);
}

void LoudnessMeter::GatedHistogram::add (double energy) noexcept
{
    const double lufs = energyToLufs (energy);
    if (! (lufs > kMinLufs))
        return;   // absolute gate

    const auto b = static_cast<size_t> (binFor (lufs));
    ++counts[b];
    energySums[b] += energy;
    ++total;
    totalEnergy += energy;
}

} // namespace polisher

#pragma once

#include <algorithm>
#include <cmath>

namespace polisher {

/** Level-matched A/B (MATCH). Learns how much louder the master (B) is than the original (A) from
    their short-term loudness, then turns whichever side is louder DOWN to the other; it never turns
    anything up, so B can't be pushed past its ceiling. It also watches the original's sample peaks:
    a mix that peaks above 0 dBFS clips the audio interface when you listen to A, which makes A sound
    worse than it is, so both sides are turned down together until A peaks at kPeakTargetDb.

    Framework-free and allocation-free; call update() once per audio block. */
class LevelMatch
{
public:
    static constexpr double kPeakTargetDb = -1.0;   // where A's loudest sample lands
    static constexpr double kSmoothingSeconds = 3.0;
    static constexpr double kMaxGapDb = 24.0;

    void prepare (double sampleRate) noexcept { sr = sampleRate; reset(); }

    void reset() noexcept
    {
        gap = 0.0;
        inputPeakDb = -120.0;
    }

    /** Forget the original's peak level (a new playback pass, or the mix changed). */
    void resetPeak() noexcept { inputPeakDb = -120.0; }

    /** inShort / outShort: short-term loudness of the original and the master (LUFS, before any
        MATCH gain). The gap is learned only while B plays and both readings hold music.
        inputBlockPeak: the original's largest sample magnitude in this block (linear). */
    void update (double inShort, double outShort, double inputBlockPeak, int numSamples, bool listeningToB) noexcept
    {
        if (inputBlockPeak > 0.0)
            inputPeakDb = std::max (inputPeakDb, 20.0 * std::log10 (inputBlockPeak));

        if (listeningToB && std::isfinite (inShort) && std::isfinite (outShort) && inShort > -60.0 && outShort > -60.0)
        {
            const double k = 1.0 - std::exp (-numSamples / (kSmoothingSeconds * sr));
            gap += ((outShort - inShort) - gap) * k;
            gap = std::clamp (gap, -kMaxGapDb, kMaxGapDb);
        }
    }

    /** Master minus original, dB (+ = the master is louder). */
    double gapDb() const noexcept { return gap; }

    /** Turn-down shared by both sides so A doesn't clip the interface (dB, <= 0). */
    double headroomDb() const noexcept
    {
        const double aPeak = inputPeakDb + gainForADbBeforeHeadroom();
        return std::min (0.0, kPeakTargetDb - aPeak);
    }

    /** Gain for B (the master) while MATCH is on (dB, <= 0). */
    double gainForBDb() const noexcept { return -std::max (0.0, gap) + headroomDb(); }

    /** Gain for A (the original) while MATCH is on (dB, <= 0). */
    double gainForADb() const noexcept { return gainForADbBeforeHeadroom() + headroomDb(); }

private:
    double gainForADbBeforeHeadroom() const noexcept { return -std::max (0.0, -gap); }

    double sr = 48000.0;
    double gap = 0.0;
    double inputPeakDb = -120.0;
};

} // namespace polisher

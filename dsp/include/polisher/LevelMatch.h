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
    static constexpr double kSmoothingSeconds = 8.0;   // loudness is averaged as energy over ~8 s
    static constexpr double kMaxGapDb = 24.0;

    void prepare (double sampleRate) noexcept { sr = sampleRate; reset(); }

    void reset() noexcept
    {
        gap = 0.0;
        inEnergy = outEnergy = 0.0;
        inputPeakDb = -120.0;
        holdSamples = 0;
        learnedSamples = 0.0;
        seedGap = false;
    }

    /** True once a gap has been learned (B has played music). */
    bool hasLearned() const noexcept { return inEnergy > 0.0; }

    /** Jump to a gap learned earlier for the same settings (a snapshot heard before), then keep learning
        from it: the original's loudness stays, the master's is set to match. */
    void setGapDb (double gapDb) noexcept
    {
        gap = std::clamp (gapDb, -kMaxGapDb, kMaxGapDb);
        if (inEnergy > 0.0)
            outEnergy = inEnergy * std::pow (10.0, gap / 10.0);
        else
            seedGap = true;   // the first reading sets the original's loudness, the gap is kept
    }

    /** Keep the current gap, without learning, for the next `samples` (the short-term readings still hold
        the settings from before a switch for ~3 s). Restarts learnedSeconds(). */
    void holdLearning (int samples) noexcept { holdSamples = std::max (holdSamples, samples); learnedSamples = 0; }

    /** How long the gap has been learning (B playing music) since the last hold or restart. */
    double learnedSeconds() const noexcept { return learnedSamples / sr; }
    void restartLearnedSeconds() noexcept { learnedSamples = 0; }

    /** Forget the original's peak level (a new playback pass, or the mix changed). */
    void resetPeak() noexcept { inputPeakDb = -120.0; }

    /** inShort / outShort: short-term loudness of the original and the master (LUFS, before any
        MATCH gain). The gap is learned only while B plays and both readings hold music.
        inputBlockPeak: the original's largest sample magnitude in this block (linear). */
    void update (double inShort, double outShort, double inputBlockPeak, int numSamples, bool listeningToB) noexcept
    {
        if (inputBlockPeak > 0.0)
            inputPeakDb = std::max (inputPeakDb, 20.0 * std::log10 (inputBlockPeak));

        if (holdSamples > 0)
        {
            holdSamples -= numSamples;
            return;
        }

        if (listeningToB && std::isfinite (inShort) && std::isfinite (outShort) && inShort > -60.0 && outShort > -60.0)
        {
            const double k = 1.0 - std::exp (-numSamples / (kSmoothingSeconds * sr));
            // Average the two loudnesses as energy (like integrated loudness), not the dB gap: loud
            // and quiet sections are limited differently, and one steady match level is what makes the
            // comparison fair (the gain must not ride the music).
            const double inE = std::pow (10.0, inShort / 10.0), outE = std::pow (10.0, outShort / 10.0);
            if (inEnergy <= 0.0) { inEnergy = inE; outEnergy = seedGap ? inE * std::pow (10.0, gap / 10.0) : outE; seedGap = false; }
            inEnergy += (inE - inEnergy) * k;
            outEnergy += (outE - outEnergy) * k;
            gap = std::clamp (10.0 * std::log10 (outEnergy / inEnergy), -kMaxGapDb, kMaxGapDb);
            learnedSamples = std::min (learnedSamples + static_cast<double> (numSamples), 1.0e12);
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
    double inEnergy = 0.0, outEnergy = 0.0;
    double inputPeakDb = -120.0;
    int holdSamples = 0;
    double learnedSamples = 0.0;
    bool seedGap = false;
};

} // namespace polisher

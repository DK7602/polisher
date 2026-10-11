#pragma once

#include <algorithm>
#include <cmath>

namespace polisher {

/** Level-matched A/B (MATCH). Learns how much louder the master (B) is than the original (A) from
    their short-term loudness, then turns whichever side is louder DOWN to the other; it never turns
    anything up, so B can't be pushed past its ceiling. It also watches the original's sample peaks:
    a mix that peaks above 0 dBFS clips the audio interface when you listen to A, which makes A sound
    worse than it is, so both sides are turned down together until A peaks at kPeakTargetDb.

    The readings it learns from look back 3 s (short-term) and 0.4 s (momentary), so right after a flip
    from A to B (or a settings switch) they still hold what played before: MATCH learns only once the
    window holds B alone (A reads as a 0 dB gap; learning from it pulled the gap toward 0, so B played
    up to ~2 dB too loud while flipping every few seconds). After relearn() (Auto-Master applied, a
    snapshot heard for the first time, a project opened, RESET) it forgets the old level: from half a
    second of B it matches roughly on the momentary readings, and about 3 s in, the first clean
    short-term reading sets the level, which then keeps settling over ~8 s.

    Framework-free and allocation-free; call update() once per audio block. */
class LevelMatch
{
public:
    static constexpr double kPeakTargetDb = -1.0;   // where A's loudest sample lands
    static constexpr double kSmoothingSeconds = 8.0;   // loudness is averaged as energy over ~8 s
    static constexpr double kMaxGapDb = 24.0;
    static constexpr double kWindowSeconds = 3.1;   // short-term readings look back 3 s (+ one 100 ms hop)
    static constexpr double kQuickSeconds = 0.5;    // momentary readings look back 0.4 s (+ one hop)

    void prepare (double sampleRate) noexcept { sr = sampleRate; reset(); }

    void reset() noexcept
    {
        gap = 0.0;
        inEnergy = outEnergy = 0.0;
        inputPeakDb = -120.0;
        holdSamples = 0;
        learnedSamples = 0.0;
        seedGap = false;
        cleanSamples = 0.0;
        quickIn = quickOut = 0.0;
    }

    /** Forget the learned level and learn it afresh from what plays from now on (the settings changed
        a lot: Auto-Master applied, a snapshot without a stored level, a project or preset loaded,
        RESET). The level in use is kept until the first new reading replaces it; A's peak is kept. */
    void relearn() noexcept
    {
        inEnergy = outEnergy = 0.0;
        seedGap = false;
        quickIn = quickOut = 0.0;
        holdSamples = 0;
        cleanSamples = 0.0;
        learnedSamples = 0.0;
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
    void holdLearning (int samples) noexcept
    {
        holdSamples = std::max (holdSamples, samples);
        learnedSamples = 0;
        cleanSamples = 0.0;
    }

    /** How long the gap has been learning (B playing music) since the last hold or restart. */
    double learnedSeconds() const noexcept { return learnedSamples / sr; }
    void restartLearnedSeconds() noexcept { learnedSamples = 0; }

    /** Forget the original's peak level (a new playback pass, or the mix changed). */
    void resetPeak() noexcept { inputPeakDb = -120.0; }

    /** inShort / outShort: short-term loudness of the original and the master (LUFS, before any
        MATCH gain). The gap is learned only while B plays, both readings hold music and their window
        holds B alone. inputBlockPeak: the original's largest sample magnitude in this block (linear).
        inMomentary / outMomentary (optional): momentary loudness, for the first rough match after a
        fresh start or relearn(). */
    void update (double inShort, double outShort, double inputBlockPeak, int numSamples, bool listeningToB,
                 double inMomentary = -100.0, double outMomentary = -100.0) noexcept
    {
        if (inputBlockPeak > 0.0)
            inputPeakDb = std::max (inputPeakDb, 20.0 * std::log10 (inputBlockPeak));

        // How long B has played since the last flip from A (or a switch / relearn): the readings hold B
        // alone once this is longer than they look back.
        cleanSamples = listeningToB ? std::min (cleanSamples + numSamples, 1.0e12) : 0.0;

        if (holdSamples > 0)
        {
            holdSamples -= numSamples;
            return;
        }
        if (! listeningToB)
            return;

        const auto music = [] (double a, double b) { return std::isfinite (a) && std::isfinite (b) && a > -60.0 && b > -60.0; };

        // Nothing learned yet (a fresh start or relearn()): a rough match on the momentary readings as
        // soon as they hold B alone, until the first clean short-term reading takes over below.
        if (inEnergy <= 0.0 && ! seedGap && cleanSamples >= kQuickSeconds * sr && music (inMomentary, outMomentary))
        {
            quickIn += std::pow (10.0, inMomentary / 10.0) * numSamples;
            quickOut += std::pow (10.0, outMomentary / 10.0) * numSamples;
            gap = std::clamp (10.0 * std::log10 (quickOut / quickIn), -kMaxGapDb, kMaxGapDb);
        }

        if (cleanSamples < kWindowSeconds * sr)
            return;   // the short-term readings still hold A or the settings before

        if (music (inShort, outShort))
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
    double cleanSamples = 0.0;               // B played since the last flip from A / switch / relearn
    double quickIn = 0.0, quickOut = 0.0;    // momentary energies summed for the first rough match
};

} // namespace polisher

#pragma once

// Checks of what the suite link says against what Masterology hears itself (framework-free; message thread, from
// the processor's 4 Hz timer). Mixology's hand-off reaches every Masterology in the host process, from whichever
// Mixology publishes it, so it isn't always about the mix this Masterology masters.

#include <cmath>
#include <cstdint>

namespace suitecheck
{
    /** Is Mixology's hand-off about the mix this Masterology hears? Mixology right before Masterology measures the very
        signal Masterology gets: their short-term loudness agrees within a fraction of a LU while music plays. When it
        doesn't for a while, Mixology's numbers describe another signal: Mixology on a group, after Masterology,
        bypassed, or in another open project. */
    class Placement
    {
    public:
        enum Verdict { thisMix = 0, otherSignal = 1, mixologySilent = 2 };

        /** dtMs since the last call. music: Masterology hears music now (the transport isn't stopped and its input is
            above kMusicLufs). ours / theirs: short-term loudness (LUFS) at Masterology's input and as Mixology
            published it (-100 = nothing). */
        void update (std::uint64_t dtMs, bool music, float ours, float theirs) noexcept
        {
            if (! music)
            {
                musicMs = 0;   // stopped or silent: the verdict of the last stretch of music stays
                return;
            }
            musicMs += dtMs;
            if (musicMs < kFillMs)
                return;        // both 3 s windows hold this stretch of music first (after play, a jump, a reset)
            const bool silent = theirs <= kSilentLufs;
            const float gap = silent ? 100.0f : std::abs (theirs - ours);
            if (gap > kDiffersLu)
            {
                agreeMs = 0;
                if ((offMs += dtMs) >= kHoldMs) verdict = silent ? mixologySilent : otherSignal;
            }
            else if (gap < kAgreesLu)
            {
                offMs = 0;
                if ((agreeMs += dtMs) >= kClearMs) verdict = thisMix;
            }
        }
        /** No hand-off any more (or this Masterology stopped): start again. */
        void forget() noexcept { verdict = thisMix; musicMs = offMs = agreeMs = 0; }
        Verdict get() const noexcept { return verdict; }

        static constexpr float kMusicLufs = -45.0f;    // quieter is a gap, a fade or silence: not judged
        static constexpr float kSilentLufs = -70.0f;   // Mixology publishes -100 when it hears nothing
        static constexpr float kDiffersLu = 1.0f, kAgreesLu = 0.7f;
        static constexpr std::uint64_t kFillMs = 3500, kHoldMs = 4000, kClearMs = 2000;

    private:
        Verdict verdict = thisMix;
        std::uint64_t musicMs = 0, offMs = 0, agreeMs = 0;
    };

    /** Does Masterology's own input sound mono (left and right the same: L/R correlation about 1 while music plays)?
        Mixology's "mono mix" flag may come from an Analyze Mix made before the bus was fixed. Not known (false) until
        music has played for kHoldMs. */
    class InputMono
    {
    public:
        void update (std::uint64_t dtMs, bool music, float correlation) noexcept
        {
            if (! music)
                return;
            if (correlation >= kMonoAt)
            {
                stereoMs = 0;
                if ((monoMs += dtMs) >= kHoldMs) verdict = 1;
            }
            else if (correlation < kStereoBelow)
            {
                monoMs = 0;
                if ((stereoMs += dtMs) >= kHoldMs) verdict = 0;
            }
        }
        bool soundsMono() const noexcept { return verdict == 1; }

        static constexpr float kMonoAt = 0.995f;       // the side about 26 dB under the mid, or less
        static constexpr float kStereoBelow = 0.99f;
        static constexpr std::uint64_t kHoldMs = 2000;

    private:
        int verdict = -1;   // -1 not known yet, 0 stereo, 1 mono
        std::uint64_t monoMs = 0, stereoMs = 0;
    };
}

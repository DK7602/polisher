#pragma once

#include "Biquad.h"
#include "ParkGlide.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace polisher {

inline constexpr double kBassMonoOffHz = 30.0;    // Mono Below settings under this are "Off"
inline constexpr double kBassMonoMaxHz = 250.0;
inline constexpr double kBassDetectHz = 120.0;    // the "low end" the detectors listen to
inline constexpr double kBassWidthHz = 150.0;     // bass-width measurement band

struct BassParams
{
    bool enabled = true;
    double monoHz = 0.0;   // side signal high-passed below this (LR4); < kBassMonoOffHz = off
    double tame = 0.0;     // 0..1: evens out low-end level jumps (808 notes that leap out)
    double punch = 0.0;    // 0..1: extra thump on the start of each kick
};

/** Bass Control: three low-end tools that leave the audio bit-for-bit untouched at their neutral
    settings (all off), and never click when moved.

    - Mono Below: high-passes the side (L-R) signal with a 4th-order Linkwitz-Riley filter, so the
      bass below the frequency is mono. The mid (L+R) goes through the matching all-pass (what the
      LR4 low- and high-pass add up to), so above the frequency mid and side stay in phase and a
      sound panned hard left stays on the left. Mid level is unchanged at every frequency. Switching
      on (see ParkGlide): mid and side go through the same all-pass, blended in over 20 ms with it
      parked just under Nyquist, which then glides down to the frequency (~0.15 s at 120 Hz, ~0.5 s at
      30 Hz); only then is the side's bass taken out (20 ms). Switching off runs the other way.
      Blending plain and all-passed at the frequency itself would cut a hole there.
    - Sub Tame: a dynamic low shelf (120 Hz). A detector compares the low band's short-term level
      with its own ~2 s average; when a note leaps above that average, the shelf turns it down
      (up to 12 dB). More Tame = lower threshold and higher ratio. It listens for ~0.75 s of low
      end before it judges anything and starts the average from what it heard, so the first notes
      aren't cut harder than the rest; after 2 s without low end it starts over. A step more than
      12 dB above the average (a drop after a quiet-bass intro) pulls the average up quickly.
    - Punch: a dynamic bell (80 Hz) that boosts the low end for the first few tens of
      milliseconds of each kick, up to +4 dB. It needs the low band jumping (fast vs slow
      envelope) together with a click in the 1-5 kHz band (the attack every kick has) that is
      loud enough next to the low end and not outweighed by hi-hats above 7 kHz. A clean 808 note
      starts with no click, so it isn't boosted.

    Also measures how stereo the incoming bass is (below 150 Hz), for Learn and Auto-Master.
    A NaN or inf input sample would stay in the filters and detectors for good: at the end of a block
    that put one there, the module is reset(), so it plays again from the next block.
    Zero latency. process() never allocates. Up to kMaxChannels channels; Mono Below needs 2. */
class BassControl
{
public:
    static constexpr int kMaxChannels = 8;
    static constexpr int kUpdateInterval = 16;
    static constexpr double kMaxTameDb = 12.0;
    static constexpr double kMaxPunchDb = 4.0;

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;

    void setParams (const BassParams& params) noexcept;
    const BassParams& getParams() const noexcept { return params; }

    template <typename Sample>
    void process (Sample* const* channels, int numChannels, int numSamples) noexcept
    {
        const int nch = std::min (numChannels, channelCount);
        std::array<double, kMaxChannels> x {};
        for (int i = 0; i < numSamples; ++i)
        {
            if (updateCountdown <= 0)
            {
                update();
                updateCountdown = kUpdateInterval;
            }
            --updateCountdown;

            for (int c = 0; c < nch; ++c)
                x[static_cast<size_t> (c)] = static_cast<double> (channels[c][i]);

            measure (x.data(), nch);
            if (! (monoRunning || tameRunning || punchRunning))
                continue;

            if (monoRunning && nch >= 2)
            {
                if (monoSegment.active())
                {
                    monoLogFreq = monoSegment.next();
                    setMonoFilters();
                }
                if (monoFade != monoFadeGoal)
                {
                    ParkGlide::fadeToward (monoFade, monoFadeGoal, fadeStep);
                    monoMix = ParkGlide::fadeWeight (monoFade);
                }
                if (monoCut != monoCutGoal)
                {
                    ParkGlide::fadeToward (monoCut, monoCutGoal, fadeStep);
                    monoCutWeight = ParkGlide::fadeWeight (monoCut);
                }
                const double mid = 0.5 * (x[0] + x[1]);
                const double side = 0.5 * (x[0] - x[1]);
                // The side's LR4 high-pass. While switching, the side goes through the same all-pass as the mid
                // (so a panned sound stays put while the filters glide), minus the LR4 low-pass part as the
                // cut fades in: all-pass - low = high. The filters all run, so either way is ready.
                const double hpSide = monoHp[1].process (monoHp[0].process (side));
                const double lpSide = monoLp[1].process (monoLp[0].process (side));
                const double apSide = monoApSide.process (side);
                const double hp = monoCutWeight >= 1.0 ? hpSide : apSide - monoCutWeight * lpSide;
                const double ap = monoAp.process (mid);
                const double m = mid + monoMix * (ap - mid);
                const double s = side + monoMix * (hp - side);
                x[0] = m + s;
                x[1] = m - s;
            }
            if (tameRunning)
                for (int c = 0; c < nch; ++c)
                    x[static_cast<size_t> (c)] = tameFilter[static_cast<size_t> (c)].process (x[static_cast<size_t> (c)]);
            if (punchRunning)
                for (int c = 0; c < nch; ++c)
                    x[static_cast<size_t> (c)] = punchFilter[static_cast<size_t> (c)].process (x[static_cast<size_t> (c)]);

            for (int c = 0; c < nch; ++c)
                channels[c][i] = static_cast<Sample> (x[static_cast<size_t> (c)]);
        }
        guardState();
    }

    /** Deepest Sub Tame cut since the last call (dB, <= 0). */
    double takeMaxTameDb() noexcept   { const double v = maxTame; maxTame = 0.0; return v; }
    /** Biggest Punch boost since the last call (dB, >= 0). */
    double takeMaxPunchDb() noexcept  { const double v = maxPunch; maxPunch = 0.0; return v; }
    /** How stereo the incoming bass is below 150 Hz: 0 % = mono, 100 % = as wide as uncorrelated. */
    double bassWidthPercent() const noexcept { return widthPercent; }

    bool isActive() const noexcept { return monoRunning || tameRunning || punchRunning; }

private:
    void update() noexcept;
    void setMonoFilters() noexcept;
    void guardState() noexcept;   // a NaN / inf got into a filter or detector: reset() (end of each block)

    inline void measure (const double* x, int nch) noexcept
    {
        // Low band of the mono sum drives Tame and Punch.
        double sum = 0.0;
        for (int c = 0; c < nch; ++c)
            sum += x[c];
        const double low = detectLp[1].process (detectLp[0].process (sum / nch));
        const double e = low * low;
        fastEnergy += (e - fastEnergy) * (e > fastEnergy ? tameAttack : tameRelease);
        const double a = std::abs (low);
        fastPeak += (a - fastPeak) * (a > fastPeak ? punchFastAttack : punchFastRelease);
        slowPeak += (a - slowPeak) * (a > slowPeak ? punchSlowAttack : punchSlowRelease);

        // The kick's click (1-5 kHz) for Punch: a clean 808 note has none.
        const double click = std::abs (clickLp[1].process (clickLp[0].process (clickHp[1].process (clickHp[0].process (sum / nch)))));
        clickFast += (click - clickFast) * (click > clickFast ? punchFastAttack : clickFastRelease);
        clickSlow += (click - clickSlow) * (click > clickSlow ? punchSlowAttack : punchSlowRelease);
        const double air = std::abs (airHp[1].process (airHp[0].process (sum / nch)));   // hats live here
        airFast += (air - airFast) * (air > airFast ? punchFastAttack : clickFastRelease);

        if (nch >= 2)
        {
            const double m = widthLpM[1].process (widthLpM[0].process (0.5 * (x[0] + x[1])));
            const double s = widthLpS[1].process (widthLpS[0].process (0.5 * (x[0] - x[1])));
            midEnergy += (m * m - midEnergy) * widthCoeff;
            sideEnergy += (s * s - sideEnergy) * widthCoeff;
        }

        // In silence the envelopes would decay into denormals, which are very slow on some CPUs
        // (offline renders run without the host's flush-to-zero).
        for (double* v : { &fastEnergy, &fastPeak, &slowPeak, &clickFast, &clickSlow, &airFast, &midEnergy, &sideEnergy })
            if (*v < 1.0e-30) *v = 0.0;
    }

    BassParams params;
    double sampleRate = 48000.0;
    int channelCount = 2;
    int updateCountdown = 0;

    // Mono Below
    std::array<Biquad, 2> monoHp {}, monoLp {};   // the side's LR4 high-pass, and its low-pass (while switching)
    Biquad monoAp;       // mid all-pass matching monoHp's phase
    Biquad monoApSide;   // the same all-pass for the side (while switching)
    double monoLogFreq = std::log (100.0), monoTargetLogFreq = std::log (100.0);
    double monoMix = 0.0, monoTarget = 0.0, glideCoeff = 0.0;
    double monoFade = 0.0, monoFadeGoal = 0.0, fadeStep = 0.0;   // plain -> all-passed blend (at the park): position, weight monoMix
    double monoCut = 0.0, monoCutGoal = 0.0, monoCutWeight = 0.0;   // the side's bass taken out: position, weight
    bool monoRunning = false;
    bool monoEngaged = false;   // arrived at the set frequency after switching on: frequency moves glide as a knob does
    ParkGlide::Segment monoSegment;   // the switching glide, a sample at a time

    // Detectors
    std::array<Biquad, 2> detectLp {}, widthLpM {}, widthLpS {}, clickHp {}, clickLp {}, airHp {};
    double fastEnergy = 0.0, longEnergy = 0.0, tameAttack = 0.0, tameRelease = 0.0, longCoeff = 0.0, catchUpCoeff = 0.0;
    double warmEnergy = 0.0;      // Sub Tame: average level while it warms up
    int warmUpdates = 0, silentUpdates = 0;
    double fastPeak = 0.0, slowPeak = 0.0, clickFast = 0.0, clickSlow = 0.0, clickHold = 0.0, clickLevel = 0.0;
    double airFast = 0.0, airLevel = 0.0;
    int clickAge = 0;
    double punchFastAttack = 0.0, punchFastRelease = 0.0, punchSlowAttack = 0.0, punchSlowRelease = 0.0;
    double clickFastRelease = 0.0, clickHoldDecay = 0.0;
    double midEnergy = 0.0, sideEnergy = 0.0, widthCoeff = 0.0, widthPercent = 0.0;

    // Dynamic filters
    std::array<Biquad, kMaxChannels> tameFilter {}, punchFilter {};
    double tameGainDb = 0.0, punchGainDb = 0.0;
    double gainAttack = 0.0, gainRelease = 0.0, punchGainAttack = 0.0, punchGainRelease = 0.0;
    bool tameRunning = false, punchRunning = false;

    double maxTame = 0.0, maxPunch = 0.0;
};

} // namespace polisher

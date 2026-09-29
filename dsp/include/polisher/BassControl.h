#pragma once

#include "Biquad.h"

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
      bass below the frequency is mono. Mid (L+R) is untouched. Blended in and out over ~20 ms.
    - Sub Tame: a dynamic low shelf (120 Hz). A detector compares the low band's short-term level
      with its own ~2 s average; when a note leaps above that average, the shelf turns it down
      (up to 12 dB). More Tame = lower threshold and higher ratio.
    - Punch: a dynamic bell (80 Hz) that boosts the low end for the first few tens of
      milliseconds of each kick (fast vs slow envelope of the low band), up to +4 dB.

    Also measures how stereo the incoming bass is (below 150 Hz), for Learn and Auto-Master.
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
                monoMix += (monoTarget - monoMix) * mixCoeff;
                if (std::abs (monoTarget - monoMix) < 1.0e-6)
                    monoMix = monoTarget;
                const double mid = 0.5 * (x[0] + x[1]);
                const double side = 0.5 * (x[0] - x[1]);
                const double hp = monoHp[1].process (monoHp[0].process (side));
                const double s = side + monoMix * (hp - side);
                x[0] = mid + s;
                x[1] = mid - s;
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

        if (nch >= 2)
        {
            const double m = widthLpM[1].process (widthLpM[0].process (0.5 * (x[0] + x[1])));
            const double s = widthLpS[1].process (widthLpS[0].process (0.5 * (x[0] - x[1])));
            midEnergy += (m * m - midEnergy) * widthCoeff;
            sideEnergy += (s * s - sideEnergy) * widthCoeff;
        }
    }

    BassParams params;
    double sampleRate = 48000.0;
    int channelCount = 2;
    int updateCountdown = 0;

    // Mono Below
    std::array<Biquad, 2> monoHp {};
    double monoLogFreq = std::log (100.0), monoTargetLogFreq = std::log (100.0);
    double monoMix = 0.0, monoTarget = 0.0, mixCoeff = 0.0, glideCoeff = 0.0;
    bool monoRunning = false;

    // Detectors
    std::array<Biquad, 2> detectLp {}, widthLpM {}, widthLpS {};
    double fastEnergy = 0.0, longEnergy = 0.0, tameAttack = 0.0, tameRelease = 0.0, longCoeff = 0.0;
    double fastPeak = 0.0, slowPeak = 0.0;
    double punchFastAttack = 0.0, punchFastRelease = 0.0, punchSlowAttack = 0.0, punchSlowRelease = 0.0;
    double midEnergy = 0.0, sideEnergy = 0.0, widthCoeff = 0.0, widthPercent = 0.0;

    // Dynamic filters
    std::array<Biquad, kMaxChannels> tameFilter {}, punchFilter {};
    double tameGainDb = 0.0, punchGainDb = 0.0;
    double gainAttack = 0.0, gainRelease = 0.0, punchGainAttack = 0.0, punchGainRelease = 0.0;
    bool tameRunning = false, punchRunning = false;

    double maxTame = 0.0, maxPunch = 0.0;
};

} // namespace polisher

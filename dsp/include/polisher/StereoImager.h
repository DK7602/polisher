#pragma once

#include "Biquad.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace polisher {

struct ImagerParams
{
    bool enabled = true;
    double lowWidth = 100.0;    // % below ~250 Hz (100 = unchanged, 0 = mono, 200 = twice as wide)
    double midWidth = 100.0;    // % 250 Hz - 5 kHz
    double highWidth = 100.0;   // % above ~5 kHz
};

/** Three-band stereo width on the side (L-R) signal only; the mid (L+R) keeps its level.
    The side is split with Linkwitz-Riley 4th-order crossovers at 250 Hz and 5 kHz (the low band
    is passed through the 5 kHz all-pass so the bands sum flat), each band gets its own gain. The
    split sums to an all-pass, so while the imager runs the mid goes through the same all-passes:
    mid and side stay in phase and a hard-panned sound stays on its side.
    Zero latency; exact pass-through at 100 / 100 / 100 (the processed side is cross-faded in and
    out over ~20 ms, so switching never clicks). Also measures the output's L/R correlation
    (+1 mono, 0 wide, below 0 out of phase). */
class StereoImager
{
public:
    static constexpr int kUpdateInterval = 16;

    void prepare (double sampleRate);
    void reset() noexcept;
    void setParams (const ImagerParams& p) noexcept { params = p; }
    const ImagerParams& getParams() const noexcept { return params; }

    template <typename Sample>
    void process (Sample* const* ch, int numChannels, int numSamples) noexcept
    {
        if (numChannels < 2)
            return;
        for (int i = 0; i < numSamples; ++i)
        {
            if (countdown-- <= 0) { update(); countdown = kUpdateInterval - 1; }
            double l = ch[0][i], r = ch[1][i];
            if (running)
            {
                const double m = 0.5 * (l + r), side = 0.5 * (l - r);
                const double low = lp250[1].process (lp250[0].process (side));
                const double rest = hp250[1].process (hp250[0].process (side));
                const double mid = lp5k[1].process (lp5k[0].process (rest));
                const double high = hp5k[1].process (hp5k[0].process (rest));
                const double lowAligned = apLp[1].process (apLp[0].process (low)) + apHp[1].process (apHp[0].process (low));
                const double wet = gLow * lowAligned + gMid * mid + gHigh * high;
                // The mid through the same two all-passes (250 Hz, then 5 kHz) as the side.
                const double m250 = mLp250[1].process (mLp250[0].process (m)) + mHp250[1].process (mHp250[0].process (m));
                const double mWet = mLp5k[1].process (mLp5k[0].process (m250)) + mHp5k[1].process (mHp5k[0].process (m250));
                mix += (mixTarget - mix) * mixCoeff;
                const double s2 = side + (wet - side) * mix;
                const double m2 = m + (mWet - m) * mix;
                l = m2 + s2;
                r = m2 - s2;
                ch[0][i] = static_cast<Sample> (l);
                ch[1][i] = static_cast<Sample> (r);
            }
            lr += (l * r - lr) * corrCoeff;
            ll += (l * l - ll) * corrCoeff;
            rr += (r * r - rr) * corrCoeff;
        }
    }

    double correlation() const noexcept
    {
        const double d = std::sqrt (ll * rr);
        return d > 1.0e-12 ? std::clamp (lr / d, -1.0, 1.0) : 1.0;
    }
    bool isActive() const noexcept { return running; }

private:
    void update() noexcept;

    ImagerParams params;
    double sampleRate = 48000.0, glide = 0.0, corrCoeff = 0.0;
    double gLow = 1.0, gMid = 1.0, gHigh = 1.0;       // current (smoothed) band gains
    double mix = 0.0, mixTarget = 0.0, mixCoeff = 0.0;
    std::array<Biquad, 2> lp250 {}, hp250 {}, lp5k {}, hp5k {}, apLp {}, apHp {};
    std::array<Biquad, 2> mLp250 {}, mHp250 {}, mLp5k {}, mHp5k {};   // the mid's matching all-passes
    bool running = false;
    int countdown = 0;
    double lr = 0.0, ll = 0.0, rr = 0.0;
};

} // namespace polisher

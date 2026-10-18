#pragma once

#include <algorithm>
#include <cmath>

namespace polisher {

/** A gain that glides to a new value in a fixed time, whatever the host's block size (the plug-in's
    Input Trim, Drive and MATCH). Equal dB steps (every sample multiplies by the same ratio), landing exactly
    on the target after the glide time; at rest it is exactly its target, so a gain of 1.0 leaves the audio
    bit for bit untouched. A new target while gliding is reached in the time that is left (MATCH's level
    moves a little every block while it learns: that must not stretch a glide); glideTo (g, true) starts a
    full glide. Gains are > 0: anything else (0, negative, NaN, inf) is ignored. No allocation. */
class GainGlide
{
public:
    void prepare (double sampleRate, double seconds) noexcept
    {
        length = std::max (1, static_cast<int> (std::lround (seconds * sampleRate)));
        jumpTo (target);
    }

    /** At g at once, no glide. */
    void jumpTo (double g) noexcept
    {
        if (valid (g)) target = g;
        value = target;
        left = 0;
        ratio = 1.0;
    }

    /** Glide to g: in the time left of a glide under way (or a full glide), a full glide if `restart`. */
    void glideTo (double g, bool restart = false) noexcept
    {
        if (! valid (g) || g == target)
            return;
        target = g;
        if (left <= 0 || restart)
            left = length;
        ratio = std::pow (target / value, 1.0 / left);
    }

    /** The gain for the next sample. */
    double next() noexcept
    {
        if (left <= 0)
            return value;
        value = --left == 0 ? target : value * ratio;
        return value;
    }

    bool isGliding() const noexcept { return left > 0; }
    double current() const noexcept { return value; }
    double getTarget() const noexcept { return target; }
    int glideSamples() const noexcept { return length; }

private:
    static bool valid (double g) noexcept { return std::isfinite (g) && g > 0.0; }

    double value = 1.0, target = 1.0, ratio = 1.0;
    int left = 0, length = 960;
};

} // namespace polisher

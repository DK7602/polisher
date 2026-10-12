#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace polisher {

/** Dither for exports at a lower bit depth (16-bit WAV / CD). Each sample gets TPDF noise (the sum of
    two uniform random values, +-1 LSB peak) and is rounded onto the target bit depth's grid, so the
    host's conversion to that depth is exact and the quantisation error becomes a steady, signal-free
    hiss (about -98 dBFS at 16 bit) instead of distortion on quiet fades. Plain (flat) TPDF: no noise
    shaping. Off = untouched. Must be the last processing on the master (nothing after it may change
    the level). process() never allocates. */
class Dither
{
public:
    static constexpr int kMaxChannels = 8;

    /** bits: 0 = off, else 16 or 24. */
    void setBits (int bits) noexcept
    {
        depth = bits == 16 || bits == 24 ? bits : 0;
        scale = depth > 0 ? std::ldexp (1.0, depth - 1) : 1.0;
    }
    int bits() const noexcept { return depth; }

    void reset() noexcept
    {
        for (size_t c = 0; c < state.size(); ++c)
            state[c] = 0x9E3779B97F4A7C15ull ^ (0xD1B54A32D192ED03ull * (c + 1));
    }

    template <typename Sample>
    void process (Sample* const* channels, int numChannels, int numSamples) noexcept
    {
        if (depth == 0)
            return;
        const int nch = std::min (numChannels, kMaxChannels);
        const double top = (scale - 1.0) / scale, bottom = -1.0;
        for (int c = 0; c < nch; ++c)
        {
            auto& s = state[static_cast<size_t> (c)];
            for (int i = 0; i < numSamples; ++i)
            {
                const double tpdf = uniform (s) - uniform (s);   // -1 ... +1 LSB, triangular
                const double q = std::floor (static_cast<double> (channels[c][i]) * scale + tpdf + 0.5) / scale;
                channels[c][i] = static_cast<Sample> (std::clamp (q, bottom, top));
            }
        }
    }

private:
    static double uniform (std::uint64_t& s) noexcept
    {
        // xorshift64*: fast, no allocation, independent per channel.
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return static_cast<double> ((s * 0x2545F4914F6CDD1Dull) >> 11) * (1.0 / 9007199254740992.0);
    }

    int depth = 0;
    double scale = 1.0;
    std::array<std::uint64_t, kMaxChannels> state {};
};

} // namespace polisher

#pragma once

// Test-signal generators shared by the unit tests.

#include "polisher/LoudnessTargeting.h"

#include <cmath>
#include <numbers>
#include <random>

namespace testsig {

using polisher::AudioClip;

inline AudioClip silence (double sr, int channels, double seconds)
{
    AudioClip clip;
    clip.sampleRate = sr;
    clip.channels.assign (static_cast<size_t> (channels),
                          std::vector<float> (static_cast<size_t> (std::lround (sr * seconds)), 0.0f));
    return clip;
}

/** Appends a sine of the given peak level (dBFS) to every channel. */
inline void appendSine (AudioClip& clip, double freq, double peakDbfs, double seconds, double phase = 0.0)
{
    const double amp = std::pow (10.0, peakDbfs / 20.0);
    const auto n = static_cast<size_t> (std::lround (clip.sampleRate * seconds));
    for (auto& ch : clip.channels)
    {
        const size_t start = ch.size();
        ch.resize (start + n);
        for (size_t i = 0; i < n; ++i)
            ch[start + i] = static_cast<float> (amp * std::sin (2.0 * std::numbers::pi * freq * static_cast<double> (start + i) / clip.sampleRate + phase));
    }
}

inline AudioClip sine (double sr, int channels, double freq, double peakDbfs, double seconds, double phase = 0.0)
{
    AudioClip clip = silence (sr, channels, 0.0);
    appendSine (clip, freq, peakDbfs, seconds, phase);
    return clip;
}

/** Pink-ish noise (Paul Kellet's filter) with drum-like bursts, a stand-in for a real mix. */
inline AudioClip musicLike (double sr, double seconds, double levelDb, unsigned seed = 7)
{
    std::mt19937 rng (seed);
    std::normal_distribution<double> white (0.0, 1.0);
    AudioClip clip = silence (sr, 2, seconds);
    const double scale = std::pow (10.0, levelDb / 20.0);

    for (auto& ch : clip.channels)
    {
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        double kickPhase = 0.0;
        for (size_t i = 0; i < ch.size(); ++i)
        {
            const double w = white (rng);
            b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759;
            b2 = 0.96900 * b2 + w * 0.1538520; b3 = 0.86650 * b3 + w * 0.3104856;
            b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
            const double pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362) * 0.11;
            b6 = w * 0.115926;

            // A 55 Hz "kick" every half second with a fast decay: big crest factor, like real drums.
            const double t = std::fmod (static_cast<double> (i) / sr, 0.5);
            kickPhase += 2.0 * std::numbers::pi * (55.0 + 90.0 * std::exp (-t * 40.0)) / sr;
            const double kick = 2.2 * std::exp (-t * 9.0) * std::sin (kickPhase);

            ch[i] = static_cast<float> (scale * (pink + kick) * 0.35);
        }
    }
    return clip;
}

inline double truePeakOf (const AudioClip& clip) { return polisher::measure (clip).truePeakDbtp; }

} // namespace testsig

#pragma once

// Test-signal generators shared by the unit tests.

#include "polisher/LoudnessTargeting.h"

#include <algorithm>
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

/** A trap-like beat at 140 BPM: a long sine 808 (F1 / G1 / E1 / A1 notes with a quick pitch drop),
    kick with a click, snare, hats and a vocal-like buzz (160-220 Hz with formants), the 808 loudest,
    peaks at -1 dBFS. */
inline AudioClip trapLike (double sr, double seconds, unsigned seed = 11)
{
    std::mt19937 rng (seed);
    std::normal_distribution<double> white (0.0, 1.0);
    AudioClip clip = silence (sr, 2, seconds);
    const double beat = 60.0 / 140.0, twoPi = 2.0 * std::numbers::pi;
    const double notes[4] = { 43.65, 49.0, 41.2, 55.0 };
    double p808 = 0.0, pKick = 0.0, pVox = 0.0, hatHp[2] = { 0.0, 0.0 }, hatPrev[2] = { 0.0, 0.0 }, snHp = 0.0, snPrev = 0.0;
    double f1[2] = { 0.0, 0.0 }, f2[2] = { 0.0, 0.0 };   // vocal formant resonators (state)
    double f1b[2] = { 0.0, 0.0 }, f2b[2] = { 0.0, 0.0 };
    // Formant resonators, scaled to about unity gain at their centre.
    struct Reson { double a, b, g; };
    const auto reson = [&] (double fc, double bw)
    {
        const double r = std::exp (-std::numbers::pi * bw / sr), w = twoPi * fc / sr;
        return Reson { 2.0 * r * std::cos (w), -r * r, (1.0 - r) * 2.0 * std::sin (w) };
    };
    const Reson r1 = reson (700.0, 150.0), r2 = reson (2500.0, 300.0);
    const int n = clip.numSamples();
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double t808 = std::fmod (t, 2.0 * beat), tBeat = std::fmod (t, beat), tHat = std::fmod (t, beat / 2.0);
        const int note = static_cast<int> (t / (2.0 * beat)) % 4;
        p808 += twoPi * notes[note] * (1.0 + std::exp (-t808 / 0.012)) / sr;
        const double s808 = 0.62 * std::min (1.0, t808 / 0.002) * std::exp (-t808 * 1.2) * std::sin (p808);
        const bool kickBeat = static_cast<int> (t / beat) % 2 == 0;
        pKick += twoPi * (50.0 + 80.0 * std::exp (-tBeat * 35.0)) / sr;
        const double kick = kickBeat ? 0.45 * std::exp (-tBeat * 14.0) * std::sin (pKick) + 0.08 * std::exp (-tBeat * 900.0) * std::sin (twoPi * 3200.0 * tBeat) : 0.0;
        const double w = white (rng);
        snHp = 0.97 * (snHp + w - snPrev);
        snPrev = w;
        const double snare = ! kickBeat ? std::exp (-tBeat * 22.0) * (0.22 * snHp + 0.18 * std::sin (twoPi * 190.0 * tBeat)) : 0.0;
        // Vocal: phrases of 1.5 s on, 0.5 s off; f0 glides 160-220 Hz; a buzz through two formants.
        const double tv = std::fmod (t, 2.0);
        const double f0 = 190.0 + 30.0 * std::sin (twoPi * 0.7 * t) + 3.0 * std::sin (twoPi * 5.5 * t);
        pVox += twoPi * f0 / sr;
        const double buzz = std::fmod (pVox / twoPi, 1.0) * 2.0 - 1.0;
        const double voxGate = tv < 1.5 ? std::min (1.0, tv / 0.03) * std::min (1.0, (1.5 - tv) / 0.03) : 0.0;
        for (int c = 0; c < 2; ++c)
        {
            const double hw = white (rng);
            hatHp[c] = 0.6 * (hatHp[c] + hw - hatPrev[c]);
            hatPrev[c] = hw;
            const double hat = 0.1 * std::exp (-tHat * 70.0) * (hatHp[c] - 0.6 * hatPrev[c]);
            const double y1 = buzz + r1.a * f1[c] + r1.b * f1b[c];
            f1b[c] = f1[c]; f1[c] = y1;
            const double y2 = buzz + r2.a * f2[c] + r2.b * f2b[c];
            f2b[c] = f2[c]; f2[c] = y2;
            const double vox = voxGate * (0.6 * r1.g * y1 + 0.35 * r2.g * y2 + 0.06 * buzz);
            clip.channels[static_cast<size_t> (c)][static_cast<size_t> (i)] = static_cast<float> (s808 + kick + snare + hat + vox);
        }
    }
    double peak = 0.0;
    for (auto& ch : clip.channels) for (float v : ch) peak = std::max (peak, static_cast<double> (std::abs (v)));
    const auto g = static_cast<float> (std::pow (10.0, -1.0 / 20.0) / peak);
    for (auto& ch : clip.channels) for (auto& v : ch) v *= g;
    return clip;
}

inline double truePeakOf (const AudioClip& clip) { return polisher::measure (clip).truePeakDbtp; }

} // namespace testsig

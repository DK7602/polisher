#pragma once

#include "FilterDesign.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace polisher {

/** Switching an all-pass path in and out without cutting a hole in the music ("park and glide").

    Mono Below, the Stereo Imager and the Dynamics run the music through all-pass filters while they work
    (their crossovers add up to an all-pass, which turns the phase and keeps the level). Off, the music
    passes untouched. Cross-fading the plain signal against the all-passed one cancels wherever the
    all-pass has turned the phase by about 180 degrees: half-way through the fade, a tone at a crossover
    frequency drops out (-13 to -20 dB for ~50 ms).

    So each of them switches on in three steps:
    1. cross-fade (kFadeSeconds, raised cosine) to the all-passed path with its filters parked just under
       Nyquist (parkHz), where the all-pass leaves everything up to 20 kHz practically untouched, so the
       fade can't cut a hole;
    2. glide the filters down to where they belong, the module's own effect still at zero (band gains,
       widths and compression at unity; Mono Below's side passes whole, through the same all-pass as the
       mid), so it is only an all-pass on both channels: the stereo image stays put;
    3. fade the effect in.
    Switching off runs the same steps backwards. A gliding all-pass bends the pitch of what passes near it a
    little and moves its level a little; step() keeps the bend under kMaxBend of every note's own pitch
    (measured: within about +-0.45 dB per module). Once there, every module is exactly what it was before: same
    filters, same frequencies, same formulas. */
struct ParkGlide
{
    static constexpr double kParkFraction = 0.495;    // of the sample rate
    static constexpr double kFadeSeconds = 0.02;      // the cross-fade at the park, and the effect's fade in / out
    static constexpr double kMaxBend = 0.04;          // largest pitch bend while gliding, as a fraction of a note's pitch
    static constexpr double kLowestNoteHz = 25.0;     // notes down to here bend no more than that
    static constexpr double kMaxWarpedRate = 150.0;   // natural-log units per second of tan (pi f0 / sr), near Nyquist

    static double parkHz (double sampleRate) noexcept { return kParkFraction * sampleRate; }
    static double parkLog (double sampleRate) noexcept { return std::log (parkHz (sampleRate)); }

    /** Raised-cosine weight for a linear fade position (0..1); exactly 0 and 1 at the ends. */
    static double fadeWeight (double position) noexcept
    {
        return position <= 0.0 ? 0.0 : position >= 1.0 ? 1.0 : 0.5 - 0.5 * std::cos (std::numbers::pi * position);
    }

    /** Moves a linear fade position one sample toward its goal. */
    static void fadeToward (double& position, double goal, double stepPerSample) noexcept
    {
        position = goal > position ? std::min (goal, position + stepPerSample) : std::max (goal, position - stepPerSample);
    }

    /** One glide step of an all-pass frequency (natural log of Hz) toward `goal`, for `seconds` of audio,
        with `moving` all-passes gliding at the same time (their bends add up). Lands exactly on `goal`.
        A tone at f, with the all-pass at f0 moving at R (natural-log units per second), bends by
        R / (pi f0) x S(u) / u, u = f / f0, where S(u) / u = sqrt 2 (1 + u^2) / (1 + u^4) peaks at 1.707
        (u = 0.64): the rate keeps that under kMaxBend for every tone from kLowestNoteHz up. It works on the
        bilinear-warped frequency tan (pi f0 / sr), which the filters really follow; near Nyquist, where
        that runs off to infinity, the speed is capped (kMaxWarpedRate) so the coefficients never jump. */
    static double step (double logHz, double goal, double seconds, double sampleRate, int moving = 1) noexcept
    {
        if (goal == logHz)
            return goal;
        const double k = std::numbers::pi / sampleRate, top = 0.4999 * sampleRate;
        const double warped = std::log (std::tan (k * std::min (std::exp (logHz), top)));
        const double d = std::log (std::tan (k * std::min (std::exp (goal), top))) - warped;
        const double v = kLowestNoteHz / std::exp (logHz);
        const double peak = v <= 0.64 ? 1.707 : std::numbers::sqrt2 * (1.0 + v * v) / (1.0 + v * v * v * v);
        const double rate = std::min (kMaxWarpedRate, kMaxBend * sampleRate * std::exp (warped) / (peak * std::max (1, moving)));
        const double maxStep = rate * seconds;
        if (std::abs (d) <= maxStep)
            return goal;
        return std::log (std::atan (std::exp (warped + std::copysign (maxStep, d))) / k);
    }

    /** One update's glide step, walked a sample at a time: coefficients that jumped once per update made
        the filters ring near Nyquist while they glided down from the park (a sample-to-sample step ten
        times a low note's own). Lands exactly on `to`. */
    struct Segment
    {
        double from = 0.0, to = 0.0;
        int pos = 0, length = 0;
        void start (double fromLog, double toLog, int samples) noexcept { from = fromLog; to = toLog; pos = 0; length = samples; }
        void stop() noexcept { pos = length = 0; }
        bool active() const noexcept { return pos < length; }
        double next() noexcept { ++pos; return pos >= length ? to : from + (to - from) * pos / length; }
    };

    /** design::butterworth / allpass, also above 0.45 x the sample rate (up to the park). Below that,
        exactly the shared designs. */
    static design::Coeffs butterworth (bool highPass, double f, double sampleRate) noexcept
    {
        if (f <= 0.45 * sampleRate)
            return design::butterworth (highPass, f, sampleRate);
        const double w0 = 2.0 * std::numbers::pi * std::min (f, parkHz (sampleRate)) / sampleRate;
        const double cw = std::cos (w0), alpha = std::sin (w0) / std::numbers::sqrt2;
        const double a0 = 1.0 + alpha;
        const double b0 = highPass ? (1.0 + cw) / 2.0 : (1.0 - cw) / 2.0;
        const double b1 = highPass ? -(1.0 + cw) : (1.0 - cw);
        return { b0 / a0, b1 / a0, b0 / a0, -2.0 * cw / a0, (1.0 - alpha) / a0 };
    }
    static design::Coeffs allpass (double f, double sampleRate) noexcept
    {
        const auto lp = butterworth (false, f, sampleRate);
        return { lp.a2, lp.a1, 1.0, lp.a1, lp.a2 };
    }
};

} // namespace polisher

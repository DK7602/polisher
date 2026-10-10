#pragma once

#include "Biquad.h"
#include "SoftClipper.h"   // HalfbandStage

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace polisher {

enum class SaturationMode { tape = 0, tube = 1, clip = 2 };

/** What the curve hears: the whole mix, or only the low end (harmonics of the 808 and kick, so they
    show up on phone and laptop speakers, while vocals and hats stay clean). */
enum class SaturationFocus { full = 0, bass = 1 };

struct SaturationParams
{
    bool enabled = true;
    SaturationMode mode = SaturationMode::tape;
    double driveDb = 0.0;   // 0 = off (exact pass-through) ... kMaxDriveDb
    double mix = 100.0;     // % of the saturated signal (parallel blend)
    SaturationFocus focus = SaturationFocus::full;
};

/** Mastering saturation: adds harmonics that make a master feel denser and help bass translate
    to small speakers.

    - Curves (per channel, 4x oversampled so the new harmonics don't fold back as aliasing):
        tape  y = S tanh(x / S)                          smooth, odd harmonics
        tube  asymmetric tanh (bias 0.35), slope 1 at 0   warm, 2nd-heavy at light Drive (3rd louder at +18)
        clip  y = x / (1 + |x/S|^4)^(1/4)                firmer knee, brighter odd harmonics
      S (where the curve bends) starts 7.5 dB above full scale at Drive 0 and comes down 1 dB per dB
      of Drive, so quiet material always passes at unity gain and only the louder moments are
      shaped; the effect fades in over the first 3 dB of Drive, so leaving 0 never jumps. Level
      stays put as you add Drive. Calibrated on a mix peaking near 0 dBFS: Tape at +6 dB, Mix
      50 % adds harmonics around -35 dB (felt more than heard); +12 dB is clearly audible.
    - Only what the curve adds (harmonics and peak rounding) goes through the oversampling filters
      (and, for tube only, a 1 Hz high-pass that removes the DC its asymmetry creates); it is mixed
      back onto the dry signal, so the clean signal is never filtered and Mix is a true parallel blend.
    - Focus Bass only ("808 to phone"): the curve hears only the low end (a Linkwitz-Riley 4th-order
      low-pass at kBassSplitHz), so the 808 and kick get harmonics and vocals, hats and snares stay
      clean. What it adds is high-passed (2nd order at kBassResidualHz), so it keeps the 2nd, 3rd,
      4th... harmonics (what phone and laptop speakers can play) but adds no sub and leaves the 808's
      fundamental alone. The dry signal is still the plain delay: no crossover on the clean path. The
      low end alone is quieter than the whole mix, so Bass only bends kBassDriveDb earlier at the same
      Drive: on a trap mix the Harmonics meter reads about what Full reads at that Drive.
    - Constant latency (kLatency) in every state; exact pass-through (delayed) when off, at Drive 0
      or Mix 0, with 20 ms cross-fades (after reset() it starts fully in the set state). Drive glides, so moves never zipper.
      Switching Focus warms the other path up next to the one playing (~50 ms, unheard), then
      cross-fades to it over 30 ms (raised cosine): no click, no thump. Full is bit-for-bit what it
      was before Focus.

    prepare() allocates; process() does not. */
class Saturation
{
public:
    static constexpr int kMaxChannels = 8;
    static constexpr int kLatency = 55;          // 2x (95 taps) + 4x (33 taps) round trip
    static constexpr double kMaxDriveDb = 18.0;
    static constexpr double kHeadroomDb = 7.5;   // S at Drive 0, above full scale
    static constexpr double kFadeInDb = 3.0;     // the effect fades in over the first 3 dB of Drive
    static constexpr double kTubeBias = 0.35;
    static constexpr double kBassSplitHz = 150.0;     // Bass only: the curve hears below this (LR4)
    static constexpr double kBassResidualHz = 70.0;   // Bass only: what it adds is high-passed here (2nd order)
    static constexpr double kBassDriveDb = 6.0;       // Bass only: the curve bends this much earlier
    static constexpr double kFocusFadeSeconds = 0.03; // Full <-> Bass only cross-fade

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setParams (const SaturationParams& p) noexcept { params = p; }
    const SaturationParams& getParams() const noexcept { return params; }
    static bool isNeutral (const SaturationParams& p) noexcept
    {
        return ! p.enabled || p.driveDb < 0.05 || p.mix < 0.05;
    }
    /** The blend actually used: Mix, faded in over the first kFadeInDb of Drive (0 when neutral). */
    static double effectiveMix (const SaturationParams& p) noexcept
    {
        return isNeutral (p) ? 0.0 : std::clamp (p.mix, 0.0, 100.0) / 100.0 * std::min (1.0, p.driveDb / kFadeInDb);
    }
    static constexpr int latencySamples() noexcept { return kLatency; }

    /** The static curve at a given bend point S (exposed for tests and the UI). */
    static double shape (double x, SaturationMode mode, double s) noexcept;

    /** Where the curve bends (S) at a Drive, for a Focus. */
    static double bendFor (double driveDb, SaturationFocus focus) noexcept
    {
        return std::pow (10.0, (kHeadroomDb - std::clamp (driveDb, 0.0, kMaxDriveDb) - (focus == SaturationFocus::bass ? kBassDriveDb : 0.0)) / 20.0);
    }

    /** Quick estimate of the harmonics level (dB, at the set Mix) a clip would get, without
        oversampling: for choosing a Drive offline, where speed matters more than the last dB.
        (The sample rate places the Bass only filters.) */
    static double estimateHarmonicsDb (const std::vector<std::vector<float>>& channels, const SaturationParams& p, double sampleRate);

    void processFrame (double* frame, int numChannels) noexcept;

    template <typename Sample>
    void process (Sample* const* channels, int numChannels, int numSamples) noexcept
    {
        std::array<double, kMaxChannels> frame {};
        const int nch = std::min (numChannels, channelCount);
        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < nch; ++c) frame[static_cast<size_t> (c)] = static_cast<double> (channels[c][i]);
            processFrame (frame.data(), nch);
            for (int c = 0; c < nch; ++c) channels[c][i] = static_cast<Sample> (frame[static_cast<size_t> (c)]);
        }
    }

    /** Energy of the added harmonics and of the signal since the last call (for meters that
        average over their own window: average energies, never dB, or quiet gaps drag it down). */
    void takeEnergies (double& residual, double& signal) noexcept
    {
        residual = resEnergy;
        signal = sigEnergy;
        resEnergy = sigEnergy = 0.0;
    }

    /** Level of the added harmonics relative to the signal since the last call (dB; -100 when
        nothing was added). */
    double takeHarmonicsDb() noexcept
    {
        const double r = sigEnergy > 1e-20 ? resEnergy / sigEnergy : 0.0;
        resEnergy = sigEnergy = 0.0;
        return r > 1e-10 ? 10.0 * std::log10 (r) : -100.0;
    }

private:
    struct Channel
    {
        std::array<HalfbandStage, 2> stages { HalfbandStage (95), HalfbandStage (33) };
        std::vector<double> dry;   // latency-matched dry path
        int dryPos = 0;
        double hpX = 0.0, hpY = 0.0;
        double dcSum = 0.0;        // the residual summed over the warm-up's DC window
        // Bass only: its own oversampling, the low-pass in front of the curve, the high-pass after it.
        std::array<HalfbandStage, 2> bassStages { HalfbandStage (95), HalfbandStage (33) };
        std::array<Biquad, 2> split {};
        Biquad bassHp {};
    };
    /** One way of making the residual (Full or Bass only): runs while it is heard or warming up. */
    struct Path
    {
        bool active = false;
        int warm = 0;   // samples left before it may be heard
    };

    double curve (double x) const noexcept { return shape (x, params.mode, bend); }
    void startPath (bool bass, int warmSamples) noexcept;
    void stopPath (bool bass) noexcept;

    SaturationParams params;
    int channelCount = 2;
    std::vector<Channel> channelState;
    double bend = 4.0, bendTarget = 4.0, glideCoeff = 0.0;
    double mix = 0.0, mixCoeff = 0.0, hpCoeff = 0.0;
    double focus = 0.0;   // 0 = the Full path is heard, 1 = Bass only, between = cross-fading
    double focusStep = 0.001, bassBendScale = 0.5;
    double resEnergy = 0.0, sigEnergy = 0.0;
    int dcWindow = 1200, dcCount = 0;
    Path fullPath, bassPath;
    bool running = false;
};

} // namespace polisher

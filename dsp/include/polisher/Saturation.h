#pragma once

#include "Biquad.h"
#include "SoftClipper.h"   // HalfbandStage

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace polisher {

enum class SaturationMode { tape = 0, tube = 1, clip = 2 };

/** What the curve hears: the whole mix; only the low end (harmonics of the 808 and kick, so they
    show up on phone and laptop speakers, while vocals and hats stay clean); only the top (sheen on
    hats and cymbals, vocal body and 808 clean); or the low end and the top at once, each bent on its
    own (the mids, where the vocal lives, stay clean). */
enum class SaturationFocus { full = 0, bass = 1, highs = 2, bassHighs = 3 };

/** Which part of the stereo image gets the harmonics: both channels alike, only the centre (mid,
    (L + R) / 2: vocal, kick, 808) or only the edges (side, (L - R) / 2: width, reverbs, wide synths). */
enum class SaturationChannels { stereo = 0, mid = 1, side = 2 };

struct SaturationParams
{
    bool enabled = true;
    SaturationMode mode = SaturationMode::tape;
    double driveDb = 0.0;   // 0 = off (exact pass-through) ... kMaxDriveDb
    double mix = 100.0;     // % of the saturated signal (parallel blend)
    SaturationFocus focus = SaturationFocus::full;
    SaturationChannels channels = SaturationChannels::stereo;
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
    - Focus Highs only ("sheen"): the same idea at the top: the curve hears only the highs (Linkwitz-Riley
      a Linkwitz-Riley 4th-order high-pass at kHighSplitHz) and bends kHighDriveDb earlier (the top is
      quieter than the mix). Only the new harmonics are added: the part of the curve's output that just
      follows the top (its fundamental, found by a running least-squares fit over ~3 ms) is taken out, so
      the top keeps its level exactly (the band is phase-turned against the dry path, so adding that part
      back would lift or cut the hats). What it adds is high-passed at kHighResidualHz, so the vocal's
      body gets nothing. Bass + Highs runs both, each through its own filters and oversampling.
    - Constant latency (kLatency) in every state; exact pass-through (delayed) when off, at Drive 0
      or Mix 0, with 20 ms cross-fades (after reset() it starts fully in the set state). Drive glides, so moves never zipper.
      Switching Focus or Mode warms a second residual path up next to the one playing (~50 ms,
      unheard; a tube path measures its DC meanwhile), then cross-fades to it over 30 ms (raised
      cosine): no click, no thump. Started by reset() (no warm-up, nothing measured), the tube DC
      filter starts at kDcStartHz and glides down to 1 Hz (kDcGlideSeconds), so a render that starts
      mid-song lets out no thump. Full is bit-for-bit what it was before Focus, apart from that start
      after reset() and the Mode cross-fade.
    - Channels Mid / Side: the curve hears only the centre ((L + R) / 2) or only the edges ((L - R) / 2) and
      what it adds goes back onto that part alone (mid: the same on L and R; side: opposite), so the other
      part passes untouched. Changing Channels cross-fades like Focus and Mode.

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
    static constexpr double kHighSplitHz = 4000.0;    // Highs only: the curve hears above this (LR4)
    static constexpr double kHighResidualHz = 3000.0; // Highs only: what it adds is high-passed here (2nd order)
    static constexpr double kHighDriveDb = 12.0;       // Highs only: the curve bends this much earlier
    static constexpr double kFocusFadeSeconds = 0.03; // cross-fade between residual paths (Focus or Mode change)
    static constexpr double kDcStartHz = 40.0;        // tube DC filter right after reset() ...
    static constexpr double kDcGlideSeconds = 0.05;   // ... gliding down to 1 Hz with this time constant

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
        return std::pow (10.0, (kHeadroomDb - std::clamp (driveDb, 0.0, kMaxDriveDb)
                                - (focus == SaturationFocus::bass ? kBassDriveDb : focus == SaturationFocus::highs ? kHighDriveDb : 0.0)) / 20.0);
        // (Bass + Highs bends each band at its own offset; this returns Full's bend for it.)
    }

    /** Quick estimate of the harmonics level (dB, at the set Mix) a clip would get, without
        oversampling: for choosing a Drive offline, where speed matters more than the last dB.
        (The sample rate places the Bass only / Highs only filters.) */
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
    /** One way of making the residual (a Focus and a Mode): runs while it is heard or warming up. */
    struct Slot
    {
        bool active = false;
        int warm = 0;          // samples left before it may be heard
        SaturationFocus focus = SaturationFocus::full;
        SaturationMode mode = SaturationMode::tape;
        int dcCount = 0;       // Full + tube: samples of the warm-up's DC window measured so far
        double dcHz = 1.0;     // Full + tube: the DC filter's corner (above 1 Hz only right after reset())
        SaturationChannels channels = SaturationChannels::stereo;
        bool is (SaturationFocus f, SaturationMode m, SaturationChannels ch) const noexcept { return active && focus == f && mode == m && channels == ch; }
    };
    /** A slot's filters and state for one channel. */
    struct SlotState
    {
        std::array<HalfbandStage, 2> stages { HalfbandStage (95), HalfbandStage (33) };
        std::array<Biquad, 2> split {};   // Bass only: the low-pass in front of the curve
        Biquad bassHp {};                 // Bass only: the high-pass after it
        std::array<HalfbandStage, 2> hiStages { HalfbandStage (95), HalfbandStage (33) };   // Highs: its own oversampling
        std::array<Biquad, 2> hiSplit {}; // Highs only: the high-pass in front of the curve
        double fitXY = 0.0, fitXX = 0.0;  // Highs only: running fit of the curve's output to its input
        Biquad hiHp {};                   // Highs only: the high-pass after it
        double hpX = 0.0, hpY = 0.0;      // Full: the 1 Hz DC filter (tube)
        double dcSum = 0.0;               // the residual summed over the warm-up's DC window
    };
    struct Channel
    {
        std::vector<double> dry;   // latency-matched dry path
        int dryPos = 0;
        std::array<SlotState, 2> slot;
    };

    void startSlot (int index, SaturationFocus focus, SaturationMode mode, SaturationChannels channels, int warmSamples) noexcept;
    bool wanted (const Slot& s) const noexcept { return s.is (params.focus, params.mode, params.channels); }
    /** One slot's residual for this frame, per channel (mid / side: worked out on the one part, put back on L and R). */
    void slotWetFrame (int index, const double* x, int nch, double* wet, double dcWeight, bool measuringDc, bool seedDc, double dcCoeff) noexcept;
    void stopSlot (int index) noexcept;
    double slotWet (const Slot& slot, SlotState& st, double x, double dcWeight, bool measuringDc, bool seedDc, double dcCoeff) const noexcept;

    SaturationParams params;
    int channelCount = 2;
    double sampleRate = 48000.0;
    std::vector<Channel> channelState;
    double bend = 4.0, bendTarget = 4.0, glideCoeff = 0.0;
    double mix = 0.0, mixCoeff = 0.0, hpCoeff = 0.0, dcGlideCoeff = 0.0;
    std::array<Slot, 2> slots;
    int heard = 0;          // the slot that is heard; the other one warms up, fades in or rests
    double blend = 0.0;     // 0 = the heard slot, 1 = the other one, between = cross-fading
    double blendStep = 0.001, bassBendScale = 0.5, highBendScale = 0.2, fitCoeff = 0.01;
    double lowWet (SaturationMode mode, SlotState& st, double x) const noexcept;
    double highWet (SaturationMode mode, SlotState& st, double x) const noexcept;
    double resEnergy = 0.0, sigEnergy = 0.0;
    int dcWindow = 1200;
    bool running = false;
};

} // namespace polisher

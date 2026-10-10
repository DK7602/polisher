#pragma once

#include "Biquad.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace polisher {

/** One band of the Tonal EQ: its filter shape, default frequency, the range it can be moved in,
    and its width. Bells use a musical, fairly wide Q; the outer bands are shelves. */
struct EqBandSpec
{
    enum class Shape { lowShelf, bell, highShelf };
    Shape shape;
    double defaultHz, minHz, maxHz, q;
};

inline constexpr int kEqBands = 7;
inline constexpr double kEqMaxGainDb = 9.0;
inline constexpr double kEqSubsonicHz = 15.0;   // subsonic high-pass, engaged while Low End is boosted

inline constexpr std::array<EqBandSpec, kEqBands> kEqBandSpecs { {
    { EqBandSpec::Shape::lowShelf,  45.0,    20.0,   120.0,  0.707 },  // Low End (sub)
    { EqBandSpec::Shape::bell,      100.0,   50.0,   250.0,  0.8 },    // Bass
    { EqBandSpec::Shape::bell,      350.0,   150.0,  800.0,  0.9 },    // Low Mid
    { EqBandSpec::Shape::bell,      1000.0,  500.0,  2500.0, 0.8 },    // Mid
    { EqBandSpec::Shape::bell,      2500.0,  1200.0, 5000.0, 0.9 },    // Upper Mid
    { EqBandSpec::Shape::bell,      5000.0,  2500.0, 10000.0, 0.9 },   // Presence
    { EqBandSpec::Shape::highShelf, 12000.0, 6000.0, 20000.0, 0.707 }, // Air
} };

struct TonalEQParams
{
    bool enabled = true;
    std::array<double, kEqBands> gainDb {};
    std::array<double, kEqBands> freqHz { 45.0, 100.0, 350.0, 1000.0, 2500.0, 5000.0, 12000.0 };
};

/** Normalised biquad coefficients (a0 == 1). */
struct BiquadCoefficients
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    /** Magnitude response in dB at frequency f. */
    double magnitudeDb (double f, double sampleRate) const noexcept;
};

/** Seven-band minimum-phase mastering EQ (zero latency).

    - Bells and shelves are the RBJ cookbook's analog prototypes, realised as magnitude-matched
      biquads (Vicanek, "Matched Second Order Digital Filters", 2016): poles by impulse invariance,
      zeros fitted in closed form to the analog magnitude. Each band hits its exact gain at its set
      frequency and keeps its analog shape up to 20 kHz instead of cramping toward Nyquist at
      44.1 / 48 kHz. Frequencies are kept below 0.45 x the sample rate.
    - Subsonic filter: while Low End is boosted, a fixed 4th-order Butterworth high-pass at 15 Hz
      fades in (same glide), so the shelf doesn't also boost inaudible rumble below 20 Hz that only
      eats headroom. It costs < 0.5 dB at 20 Hz and nothing measurable above 30 Hz.
    - Gain and frequency changes glide (about 20 ms, log-frequency), with the coefficients updated
      every 16 samples, so moving a knob or dragging a band never clicks.
    - A band at 0 dB is skipped once it has settled, so a flat or disabled EQ is an exact, bit-for-bit
      pass-through. Turning the EQ off glides every band to 0 dB first.

    process() never allocates. Up to kMaxChannels channels. */
class TonalEQ
{
public:
    static constexpr int kMaxChannels = 8;
    static constexpr int kUpdateInterval = 16;

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;

    /** Sets new targets; the EQ glides to them. The first call after prepare() jumps straight there. */
    void setParams (const TonalEQParams& params) noexcept;
    const TonalEQParams& getParams() const noexcept { return params; }

    template <typename Sample>
    void process (Sample* const* channels, int numChannels, int numSamples) noexcept
    {
        const int nch = numChannels < channelCount ? numChannels : channelCount;
        int done = 0;
        while (done < numSamples)
        {
            if (updateCountdown <= 0)
            {
                advanceSmoothing();
                updateCountdown = kUpdateInterval;
            }
            const int n = std::min (numSamples - done, updateCountdown);
            for (auto& band : bands)
            {
                if (! band.running)
                    continue;
                for (int c = 0; c < nch; ++c)
                {
                    auto& f = band.filters[static_cast<size_t> (c)];
                    Sample* x = channels[c] + done;
                    for (int i = 0; i < n; ++i)
                        x[i] = static_cast<Sample> (f.process (static_cast<double> (x[i])));
                }
            }
            if (sub.running)
            {
                // Dry/high-passed crossfade, ramped per sample while the filter fades in or out.
                const bool full = sub.step == 0.0 && sub.mix == 1.0;
                for (int c = 0; c < nch; ++c)
                {
                    auto& hp = sub.filters[static_cast<size_t> (c)];
                    Sample* x = channels[c] + done;
                    for (int i = 0; i < n; ++i)
                    {
                        const double in = static_cast<double> (x[i]);
                        const double h = hp[1].process (hp[0].process (in));
                        x[i] = static_cast<Sample> (full ? h : in + (sub.mix + sub.step * (i + 1)) * (h - in));
                    }
                }
                sub.mix += sub.step * n;
            }
            done += n;
            updateCountdown -= n;
        }
    }

    /** True if any band is currently filtering (false = exact pass-through). */
    bool isActive() const noexcept;

    /** Coefficients for one band at the given settings (shared by the plug-in, tests and the UI curve). */
    static BiquadCoefficients designBand (int band, double gainDb, double freqHz, double sampleRate) noexcept;

    /** Total response of the EQ at frequency f for the given settings, in dB (subsonic filter included). */
    static double responseDb (const TonalEQParams& params, double f, double sampleRate) noexcept;

    /** True if these settings engage the subsonic high-pass (EQ on and Low End boosted). */
    static bool subsonicEngaged (const TonalEQParams& params) noexcept;

    /** The two biquads of the subsonic high-pass at this sample rate. */
    static std::array<BiquadCoefficients, 2> designSubsonic (double sampleRate) noexcept;

private:
    struct Band
    {
        double gainDb = 0.0, logFreq = 0.0;             // current (smoothed)
        double targetGainDb = 0.0, targetLogFreq = 0.0;
        bool running = false;                            // filtering (or letting its state ring out)
        std::array<Biquad, kMaxChannels> filters;
    };

    struct Subsonic
    {
        double mix = 0.0, target = 0.0;   // 0 = dry, 1 = fully high-passed
        double step = 0.0;                // per-sample ramp of mix until the next update
        double settled = 0.0;             // where mix lands at the next update (exact, no drift)
        bool running = false;
        std::array<std::array<Biquad, 2>, kMaxChannels> filters;
    };

    void advanceSmoothing() noexcept;
    void applyCoefficients (int bandIndex) noexcept;

    TonalEQParams params;
    std::array<Band, kEqBands> bands;
    Subsonic sub;
    double sampleRate = 48000.0;
    double smoothingCoeff = 0.0;
    int channelCount = 2;
    int updateCountdown = 0;
    bool firstParams = true;
    bool hasParams = false;    // setParams() was called at least once
};

} // namespace polisher

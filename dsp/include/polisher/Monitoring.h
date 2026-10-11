#pragma once

#include "Biquad.h"
#include "KWeighting.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace polisher {

/** Monitoring-only listening aids: the Listen / Solo buttons (hear only what a module changes) and the
    MONO / PHONE checks. None of this is a host parameter: the plug-in sets it from its window, forgets it
    when the window closes and never applies it in an offline export, so it can't end up in a bounce. */
namespace listen {
    /** What plays instead of the master. The numbers are shared with the web UI. */
    enum Mode : int
    {
        off = 0,
        satAdded = 1,      // what Saturation adds: its output minus its (latency-aligned) input
        clipRemoved = 2,   // what the Soft Clipper shaves off: aligned input x makeup minus output
        tamerRemoved = 3,  // what Resonance Tamer cuts (formed inside the spectral stage)
        maxRemoved = 4,    // what the Maximizer's limiter takes off: aligned input minus output
        dynSolo = 5,       // 5..8: one Dynamics band, after its compression and gain
        dynRemoved = 9,    // 9..12: what one Dynamics band's compression takes off
        count = 13
    };
    inline constexpr bool isValid (int m) noexcept { return m >= 0 && m < count; }
    inline constexpr bool isSolo (int m) noexcept { return m >= dynSolo && m < dynSolo + 4; }
    inline constexpr bool isDynamics (int m) noexcept { return m >= dynSolo && m < dynRemoved + 4; }
    inline constexpr int dynBand (int m) noexcept { return isSolo (m) ? m - dynSolo : m - dynRemoved; }
    /** A delta (what a module adds or removes) as opposed to a solo (a part of the music itself). */
    inline constexpr bool isDelta (int m) noexcept { return m != off && ! isSolo (m); }
}

/** A stereo delay line of a fixed number of samples (0 = pass straight through). prepare() allocates. */
class MonitorDelay
{
public:
    void prepare (int maxDelay)
    {
        for (auto& l : lines) l.assign (static_cast<size_t> (std::max (1, maxDelay + 1)), 0.0);
        delay = 0;
        clear();
    }
    /** Sets the delay (clamped to what prepare() allowed) and clears the line. */
    void setDelay (int samples) noexcept
    {
        delay = std::clamp (samples, 0, static_cast<int> (lines[0].size()) - 1);
        clear();
    }
    int getDelay() const noexcept { return delay; }
    void clear() noexcept
    {
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0);
        pos = 0;
    }
    /** Pushes one stereo frame in and returns, in place, the frame from `delay` samples ago. */
    void process (double& left, double& right) noexcept
    {
        if (delay == 0) return;
        const auto p = static_cast<size_t> (pos);
        const auto r = static_cast<size_t> ((pos + static_cast<int> (lines[0].size()) - delay) % static_cast<int> (lines[0].size()));
        lines[0][p] = left;
        lines[1][p] = right;
        left = lines[0][r];
        right = lines[1][r];
        pos = pos + 1 == static_cast<int> (lines[0].size()) ? 0 : pos + 1;
    }

private:
    std::array<std::vector<double>, 2> lines;
    int delay = 0, pos = 0;
};

/** The listen switch at the end of the chain: plays the master, or one monitored signal (a module's
    delta or a Dynamics band) in its place.

    - Switching never clicks. Off -> a mode: the master keeps playing while the mode's signal fills
      its delay lines and its level is measured (arm), then a 15 ms cross-fade. A mode -> off: a 15 ms
      cross-fade back. A mode -> another mode: the first fades out, the second arms, then fades in
      (only one monitored signal is ever computed, so a short gap of silence sits between them).
    - Level ("hear it" gain): a delta is turned up toward kTargetUnderMasterDb under the master
      (K-weighted, ~1 s averages), never more than kMaxBoostDb and never down for loudness, because
      added harmonics or shaved peaks often sit 30 dB under the music. Any signal is turned down when its
      peaks would pass the ceiling (slow peak follower: a level, not a limiter). A solo plays at its real
      level. appliedGainDb() and levelUnderMasterDb() say how much, so the UI can be honest about it.
      setAutoLevel(false) plays every signal as it is (tests).

    The caller computes the monitored signal already aligned with the master (same latency) and at the
    level it has in the master. prepare() allocates; nothing else does. */
class ListenMix
{
public:
    static constexpr double kFadeSeconds = 0.015;
    static constexpr double kMeasureSeconds = 0.1;       // level measured before the fade-in
    static constexpr double kTargetUnderMasterDb = 15.0;
    static constexpr double kMaxBoostDb = 24.0;
    static constexpr double kLoudnessSeconds = 1.0;
    static constexpr double kPeakReleaseSeconds = 3.0;

    void prepare (double sampleRate);
    /** Back to the master at once (no fade): sample-rate changes and offline export. */
    void reset() noexcept;
    void setAutoLevel (bool on) noexcept { autoLevel = on; }

    /** The mode wanted now and how many samples its signal needs after it starts being computed before
        it is complete (its delay lines). Takes effect at the next beginBlock(). */
    void request (int mode, int armSamples) noexcept
    {
        wanted = listen::isValid (mode) ? mode : listen::off;
        wantedArm = std::max (0, armSamples);
    }
    int requested() const noexcept { return wanted; }

    /** Call once per block before computing the monitored signal. Returns the mode whose signal must be
        computed for this block (listen::off: none). startedArming() is true when that mode starts
        this block: the caller clears its delay lines and hooks first. */
    int beginBlock() noexcept;
    bool startedArming() const noexcept { return justStarted; }

    /** True while anything but the plain master can come out (a signal is computed, fading or wanted). */
    bool isActive() const noexcept { return computing != listen::off || wanted != listen::off || masterGain < 1.0; }

    /** Mixes in place: `out` holds the master; `signal` the monitored signal (two channels, read only when
        beginBlock() returned a mode). ceilingGain: the master's ceiling (linear), for the peak turn-down. */
    template <typename Sample>
    void process (Sample* const* out, int numChannels, const double* const* signal, int numSamples, double ceilingGain) noexcept
    {
        const int nch = std::clamp (numChannels, 1, 2);
        for (int i = 0; i < numSamples; ++i)
        {
            double m[2] { static_cast<double> (out[0][i]), static_cast<double> (out[nch - 1][i]) };
            trackMaster (m[0], m[1], nch);
            double s[2] { 0.0, 0.0 };
            if (computing != listen::off)
            {
                s[0] = signal[0][i];
                s[1] = nch > 1 ? signal[1][i] : s[0];
                trackSignal (s[0], s[1], nch, ceilingGain);
            }
            step();
            const double gm = smooth (masterGain), gs = smooth (signalGain) * level;
            for (int c = 0; c < nch; ++c)
                out[c][i] = static_cast<Sample> (gm * m[c] + gs * s[c]);
        }
    }

    /** The mode being heard or prepared now (off once back on the master). */
    int playing() const noexcept { return computing; }
    /** The mode whose signal is ready and playing (or fading), off while it still arms. */
    int audible() const noexcept { return armLeft == 0 ? computing : static_cast<int> (listen::off); }
    /** Gain the monitored signal is played with (dB; + = turned up so it can be heard). */
    double appliedGainDb() const noexcept { return 20.0 * std::log10 (std::max (level, 1.0e-6)); }
    /** How far the monitored signal sits under the master, as it is in the master (dB, <= 0 for a
        delta; -100 when there is nothing to hear). */
    double levelUnderMasterDb() const noexcept
    {
        return signalMs > 1.0e-14 && masterMs > 1.0e-14 ? 10.0 * std::log10 (signalMs / masterMs) : -100.0;
    }

private:
    static double smooth (double x) noexcept { return x * x * (3.0 - 2.0 * x); }   // S-curve: s(x) + s(1 - x) = 1
    void trackMaster (double l, double r, int nch) noexcept;
    void trackSignal (double l, double r, int nch, double ceilingGain) noexcept;
    void step() noexcept;
    double targetLevel() const noexcept;

    double sr = 48000.0, fadeStep = 1.0, loudCoeff = 0.0, peakRelease = 0.0, levelDown = 0.0, levelUp = 0.0;
    int measureSamples = 4800;
    bool autoLevel = true;
    int wanted = listen::off, wantedArm = 0;
    int computing = listen::off, armLeft = 0;
    bool justStarted = false;
    double masterGain = 1.0, signalGain = 0.0;     // fade positions 0..1
    double level = 1.0, levelTarget = 1.0;         // "hear it" gain (linear)
    std::array<KWeighting, 2> kwMaster {}, kwSignal {};
    double masterMs = 0.0, signalMs = 0.0, peak = 0.0, ceilingNow = 1.0;
    long tracked = 0;
    int countdown = 0;
};

/** The MONO and PHONE checks at the very end (after MATCH), plus the safety stage of all monitoring.

    - MONO: (L + R) / 2 on both channels. A part that disappears here cancels on mono speakers.
    - PHONE: a phone speaker, roughly: mono, 4th-order Butterworth high-pass at kPhoneLowHz (little below
      300 Hz: 200 Hz ~13 dB down, 100 Hz ~37 dB, an 808's 50 Hz fundamental ~61 dB, so only its upper
      harmonics come through, as on a real phone), 2nd-order low-pass at kPhoneHighHz, a small resonant
      bump at kPhoneBumpHz, and a fixed kPhoneMakeupDb so a balanced mix keeps its loudness (a pink
      spectrum measures the same LUFS through it). Fixed, not automatic: a mix whose weight is all
      sub-bass does get quieter on PHONE, as it does on a real phone.
    - Each switch cross-fades over 15 ms. Safety: while any monitoring is on, nothing leaves above
      0 dBFS. It rides the level rather than limiting each peak: an instant drop when a peak would pass
      0 dBFS (no look-ahead, so nothing can get through), then it holds that level while peaks stay within
      ~1 dB of the top (and kSafetyHoldSeconds after) and lets go over kSafetyReleaseSeconds, so a loud
      bright master on PHONE is turned down a few dB as a whole instead of being limited (distorted) peak
      by peak. It never touches a normal master (which stays at its ceiling). Once every listening aid is
      off it lets go within the 15 ms fade, and it keeps running (isActive) until it is back at exactly
      0 dB, so the next check starts level.
    prepare() allocates nothing; process() never does. */
class MonitorOutput
{
public:
    static constexpr double kPhoneLowHz = 300.0;
    static constexpr double kPhoneHighHz = 8000.0;
    static constexpr double kPhoneBumpHz = 2000.0;
    static constexpr double kPhoneBumpDb = 3.0;
    static constexpr double kPhoneBumpQ = 1.0;
    static constexpr double kPhoneMakeupDb = 1.5;
    static constexpr double kFadeSeconds = 0.015;
    static constexpr double kSafetyHoldSeconds = 0.06;
    static constexpr double kSafetyHoldLevel = 0.89;       // peaks within ~1 dB of 0 dBFS keep the level held
    static constexpr double kSafetyReleaseSeconds = 0.5;
    static constexpr double kSafetyLetGoSeconds = 0.004;   // time constant once nothing is wanted any more

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;   // both checks off at once, filters cleared, safety back at 0 dB
    void setMono (bool on) noexcept { monoWanted = on; }
    void setPhone (bool on) noexcept { phoneWanted = on; }
    /** Whether a Listen / Solo is wanted (the safety stage lets go fast once nothing at all is). */
    void setListenWanted (bool on) noexcept { listenWanted = on; }
    /** True while a check is on or fading, or the safety stage is still letting go. */
    bool isActive() const noexcept { return monoWanted || phoneWanted || monoMix > 0.0 || phoneMix > 0.0 || safetyGain < 1.0; }
    /** The safety stage's gain now (1 = untouched). */
    double safetyGainNow() const noexcept { return safetyGain; }

    /** Applies the checks in place, then (when `safety`) the 0 dBFS safety stage. */
    template <typename Sample>
    void process (Sample* const* ch, int numChannels, int numSamples, bool safety) noexcept
    {
        const int nch = std::clamp (numChannels, 1, 2);
        if (phoneWanted && phoneMix <= 0.0)
            for (auto* f : { &phoneHp, &phoneHp2, &phoneLp, &phoneBump }) f->reset();   // starts clean; the fade hides its start
        const bool wanted = monoWanted || phoneWanted || listenWanted;
        for (int i = 0; i < numSamples; ++i)
        {
            double l = static_cast<double> (ch[0][i]), r = static_cast<double> (ch[nch - 1][i]);
            monoMix = monoWanted ? std::min (1.0, monoMix + fadeStep) : std::max (0.0, monoMix - fadeStep);
            phoneMix = phoneWanted ? std::min (1.0, phoneMix + fadeStep) : std::max (0.0, phoneMix - fadeStep);
            const double mono = 0.5 * (l + r);
            if (monoMix > 0.0)
            {
                const double g = smooth (monoMix);
                l += (mono - l) * g;
                r += (mono - r) * g;
            }
            if (phoneMix > 0.0)
            {
                const double p = phoneBump.process (phoneLp.process (phoneHp2.process (phoneHp.process (mono)))) * phoneMakeup;
                const double g = smooth (phoneMix);
                l += (p - l) * g;
                r += (p - r) * g;
            }
            if (safety)
            {
                const double pk = std::max (std::abs (l), std::abs (r)), level = pk * safetyGain;
                if (level > 1.0)
                {
                    safetyGain = 1.0 / pk;            // instant: nothing passes 0 dBFS
                    safetyHold = wanted ? holdSamples : 0;
                }
                else if (! wanted)
                {
                    safetyHold = 0;                   // every aid is off: let go within the fade back to the master
                    if (safetyGain < 1.0)
                        safetyGain = 1.0 - (1.0 - safetyGain) * safetyLetGo;
                }
                else if (level > kSafetyHoldLevel)
                    safetyHold = holdSamples;         // peaks still near the top: keep the level (no ripple on each wave)
                else if (safetyHold > 0)
                    --safetyHold;
                else if (safetyGain < 1.0)
                    safetyGain = 1.0 - (1.0 - safetyGain) * safetyRelease;
                if (safetyGain > 1.0 - 1.0e-7)
                    safetyGain = 1.0;
                l = std::clamp (l * safetyGain, -1.0, 1.0);
                r = std::clamp (r * safetyGain, -1.0, 1.0);
            }
            else
            {
                safetyGain = 1.0;
                safetyHold = 0;
            }
            ch[0][i] = static_cast<Sample> (l);
            if (nch > 1) ch[1][i] = static_cast<Sample> (r);
        }
    }

    /** The PHONE filter's level at f Hz (dB, makeup included), from its coefficients. */
    double phoneResponseDb (double f) const noexcept;

private:
    static double smooth (double x) noexcept { return x * x * (3.0 - 2.0 * x); }

    double sr = 48000.0, fadeStep = 1.0, safetyRelease = 0.0, safetyLetGo = 0.0, safetyGain = 1.0;
    int holdSamples = 0, safetyHold = 0;
    double phoneMakeup = 1.0;
    bool monoWanted = false, phoneWanted = false, listenWanted = false;
    double monoMix = 0.0, phoneMix = 0.0;
    Biquad phoneHp, phoneHp2, phoneLp, phoneBump;   // the high-pass is two sections: 4th-order Butterworth
};

} // namespace polisher

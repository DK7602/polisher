#pragma once

#include <algorithm>
#include <array>
#include <complex>
#include <vector>

namespace polisher {

inline constexpr int kStems = 4;
enum StemIndex { kStemVocals = 0, kStemDrums = 1, kStemBass = 2, kStemOther = 3 };

/** What Resonance Tamer and Tone Steady did over a stretch of audio (for Auto-Master). */
struct SpectralStats
{
    // Weighted by loudness (A-weighted power), not raw power: an 808 or sub carries most of a trap
    // mix's energy but little of what is heard, and must not hide a ring the ear picks out at once.
    double resonanceCutDb = 0.0;   // loudness-weighted average resonance cut (dB, <= 0)
    double boxinessShare = 0.0;    // share of that cutting in 150-800 Hz (0..1)
    double harshnessShare = 0.0;   // share in 1.5-8 kHz (0..1)
    double steadyMoveDb = 0.0;     // energy-weighted average size of Tone Steady's corrections (dB, >= 0)
};

struct StemEQParams
{
    bool enabled = true;
    std::array<double, kStems> gainDb {};   // vocals, drums, bass, other: -12 ... +12 dB, 0 = untouched
    // Keep loudness: the stage turns its whole output up or down by the loudness change the stem
    // moves cause (K-weighted, averaged over ~2 s), so a move changes the balance, not the level.
    // Off here (the engine's old behaviour); the plug-in's control is on for new instances.
    bool keepLoudness = false;
};

/** Tone Steady's bands: the model's 64 ERB bands (StemNet::kBands). */
inline constexpr int kToneBands = 64;

/** A song's own tone as Tone Steady measures it: per band, its level relative to the whole frame
    (dB), typical for the song. Only the shape matters (adding the same dB to every band changes
    nothing). Learned by Auto-Master while the song plays (SongListener::toneTarget), saved with the
    project. As learned it describes the mix arriving at the plug-in; toneTargetAtStage() turns it
    into what the spectral stage hears after the Tonal EQ. */
struct ToneTarget
{
    static constexpr double kNoLevel = -200.0;
    bool valid = false;
    std::array<float, kToneBands> bandDb {};
    double seconds = 0.0;     // how much music it was learned from
    // The song's level as Tone Steady measures a frame (power mean of the music's frames: the summed
    // bin power of both channels, K-weighted at each band's centre like a loudness meter, over fftSize
    // squared, so it reads the same at every sample rate), dB.
    // Quiet passages are judged against it (more than 15 dB under it: left alone), so that too no
    // longer depends on where playback starts. kNoLevel: not known (a target saved before it): the
    // last ~10 s of music are the reference for that, as when following the music.
    double levelDb = kNoLevel;
    bool hasLevel() const noexcept { return levelDb > kNoLevel + 1.0; }
};

/** How far a tone sits from a target, above the low end (dB): per band above 160 Hz, the difference,
    with the overall offset taken out, as an RMS weighted like Tone Steady weighs the bands (by the
    target's level; bands more than 60 dB under its loudest left out). Below 160 Hz is left out: 808s
    and basses come and go within one song. Measured on synthetic songs: a 15 s listen against the
    whole song's tone 0.1-2.7 dB in steady songs, up to 3.6-4.6 dB for a section with its own part
    (a quiet pad intro, a chorus with a lead); different songs 3-19 dB. 0 when either is invalid. */
double toneDistanceDb (const ToneTarget& target, const ToneTarget& tone, double sampleRate) noexcept;

/** Tone Steady: keeps the tonal balance steady as the song plays. Each band's short-term level is
    compared with the song's own long-term balance; where a passage drifts (muddier, harsher,
    thinner) the band is nudged back, cuts up to Range, boosts up to half of it. Above ~2.5 kHz it
    reacts only to a top end that stays brighter for seconds, never to hats coming and going.

    Two references:
    - Following the music (no target): the song's balance over the last ~10 s. In 30-160 Hz a jump
      of more than 4 dB beyond the rest of the spectrum that lasts about a second (an 808 or bass
      coming in for the chorus) is taken as the arrangement, not a drift: the reference re-learns
      within about a second instead of cutting the new low end for the next ten.
    - A stored target (target.valid): the whole song's own tone, learned by Auto-Master. Nothing is
      learned while playing, so the result no longer depends on where playback or an export starts
      (quiet passages are judged against the song's stored level, target.levelDb, when it has one).
      It compares the music as it enters the stage (before the Stem EQ move and the tamer's cuts,
      which are yours, not a drift) with the target. A band far from the song's tone is a part
      coming or going, not a drift, and is left alone: below 160 Hz both ways beyond 4 dB (fully at
      6 dB: an 808 entering after an intro is not cut, the intro without it not lifted); elsewhere
      only lifts, beyond 6 dB (fully at 9 dB: a part that isn't playing is not faked back). Bands
      left alone don't count in the overall offset either, so a section without the 808 isn't read
      as "everything else too loud". Above 160 Hz a part coming in can't be told from a drift (a
      lead entering in the chorus reads like a harsher passage): it is pulled down, however far it
      drifts, but at most half the Range (the cut a section can get while it plays). */
struct ToneSteadyParams
{
    bool enabled = true;
    double amount = 0.0;      // % of the drift corrected, 0 = off
    double rangeDb = 3.0;     // largest cut (boosts, and cuts above 160 Hz with a target: half), 1 ... 6 dB
    int speed = 1;            // 0 slow (1 s), 1 medium (0.4 s), 2 fast (0.15 s)
    ToneTarget target;        // as the stage hears it (toneTargetAtStage); invalid = follow the music
};

enum class ResonanceFocus { full = 0, boxiness = 1, harshness = 2 };

/** Resonance Tamer: finds peaks standing out of the spectrum around them (ringing boxiness,
    whistly or harsh tones) and cuts them, only while they ring. The cut spans the peak's bin and
    its two neighbours, so it is narrow above ~1 kHz but about 3/4 octave wide below ~800 Hz
    (an FFT bin is ~47 Hz wide). */
struct ResonanceParams
{
    bool enabled = true;
    double amount = 0.0;          // %, 0 = off; 100 = each peak cut down to the threshold
    double sensitivity = 50.0;    // %: higher catches smaller peaks (threshold 12 ... 3 dB above the surroundings)
    double releaseMs = 80.0;      // how fast a cut lets go, 20 ... 300 ms
    ResonanceFocus focus = ResonanceFocus::full;   // full 80 Hz-16 kHz, boxiness 150-800 Hz, harshness 1.5-8 kHz
};

/** The small model behind Stem EQ (tools/stem: conv over 7 frames + GRU + dense, distilled from
    HT-Demucs). For each STFT frame it gives the share of the energy in each of 64 ERB bands that
    belongs to vocals / drums / bass / other. Plain float maths, weights compiled in
    (StemNetWeights.h, written by tools/stem/export.py). The constructor allocates; push() does not. */
class StemNet
{
public:
    static constexpr int kBands = 64, kIn = 2 * kBands, kLookahead = 3;

    StemNet();
    void reset() noexcept;

    /** Feeds one frame of features (kIn: log10 band energy of mid, then of side). Once 2 * kLookahead
        + 1 frames are in, writes the shares ([stem][band], each band sums to 1) of the frame
        kLookahead frames back and returns true. */
    bool push (const float* features, float* shares) noexcept;

private:
    std::vector<float> hist;    // last kTaps normalised feature frames, ring
    std::vector<float> h, x, gi, gh, logits;
    int head = 0, filled = 0;
};
static_assert (StemNet::kBands == kToneBands, "Tone Steady measures in the model's bands");

/** The spectral stage: Stem EQ, Tone Steady and Resonance Tamer share one STFT (so the two later
    modules add no latency). Their gains multiply per bin and frame; Tone Steady and Resonance Tamer
    listen to the spectrum after the Stem EQ move.

    Stem EQ: turn vocals, drums, bass and "other" up or down inside a finished stereo mix.

    The mix is not split into stems. StemNet predicts, per frame and band, how the energy divides
    between the four; the four stem gains are mixed by those shares into one gain per band, spread
    over the FFT bins (triangular ERB bands whose weights sum to 1 in every bin) and applied in an
    STFT (Hann, NFFT 1024 / hop 256; 2048 / 512 from 88.2 kHz, 4096 / 1024 from 176.4 kHz, so
    the bins and the frame rate stay what the model was trained on), overlap-added with a Hann
    synthesis window. With every gain at 0 dB that gain is exactly 1.

    - Constant latency NFFT + 3 hops (1792 samples at 44.1 / 48 kHz, 3584 at 88.2 / 96, 7168 at
      176.4 / 192: 37-41 ms at every rate) in every state: the model looks 3 frames ahead.
    - Exact pass-through (delayed) when off or at 0 / 0 / 0 / 0, with 20 ms cross-fades; gain moves
      glide (30 ms). The model keeps running while neutral, so turning a knob never waits for it.
    - Channels 0 and 1 are processed (a mono input is analysed as L = R); further channels are
      only delayed, so every channel keeps the same latency.
    - Keep loudness (StemEQParams::keepLoudness): per frame, the K-weighted (BS.1770) energy of the
      frame is credited to the stems by the model's shares, as a 4 x 4 matrix of share products
      (sum over bands of E_b * share_s,b * share_t,b) averaged over ~2 s, its weight held to the
      music's level over the last ~0.3 s: after a loud section a quieter one takes over within the
      same ~2 s instead of the loud one's energy ruling for 8-10 s (Bass +6 into a breakdown without
      the 808: -2.7 / -0.5 LU over its first 4 / next 4 s, was -4.0 / -2.3; steady mixes unchanged,
      within 0.4 LU). Near-silence (40 dB under the recent peak) teaches nothing. With the stem
      gains g, the loudness after the move is g' M g against sum(M) before it, so a knob move is
      compensated at once (same frame, same glide), while the music's own changes move the
      estimate within a few seconds. The
      inverse (clamped to +/-12 dB) is applied as a broadband gain inside the same STFT frames: no
      extra latency, and an exact pass-through at 0 / 0 / 0 / 0 as before. Switching it glides
      (0.1 s).

    prepare() allocates; process() does not. */
class StemEQ
{
public:
    static constexpr int kMaxChannels = 8;
    static constexpr double kMaxGainDb = 12.0;
    static constexpr double kMaxKeepDb = 12.0;            // Keep loudness never moves the level more than this
    static constexpr double kKeepAverageSeconds = 2.0;    // how long Keep loudness listens to the music
    static constexpr double kKeepBeatSeconds = 0.3;       // ... and the level of the music now (about a beat)
    static constexpr double kKeepGateDb = 40.0, kKeepPeakFallDbPerSecond = 3.0;   // near-silence teaches nothing
    // Tone Steady with a target: how far (dB) a band may sit from the song's tone before it counts as
    // the arrangement (a part coming or going) and is left alone; below 160 Hz, and the rest.
    static constexpr double kTargetLowPartDb = 4.0, kTargetPartDb = 6.0;
    // ... and when the music playing sits so far from the stored target for so long that it is
    // probably another song's tone (see toneTargetFar). Synthetic songs, live over 20 s: their own
    // tone 0.04-2.1 dB (a 20 s chorus with a lead), another song's 2.9-12 dB (held to one 3-4 dB
    // away, Auto-Master's 40 % / 3 dB moved a band by 0.7-1.0 dB at most; 6-7 dB away, 1.2 dB).
    static constexpr double kTargetFarDb = 4.0, kTargetFarSeconds = 10.0, kTargetLiveSeconds = 20.0;

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setParams (const StemEQParams& p) noexcept { params = p; }
    const StemEQParams& getParams() const noexcept { return params; }
    void setToneSteady (const ToneSteadyParams& p) noexcept { tsParams = p; }
    const ToneSteadyParams& getToneSteady() const noexcept { return tsParams; }
    void setResonance (const ResonanceParams& p) noexcept { rtParams = p; }
    const ResonanceParams& getResonance() const noexcept { return rtParams; }
    static bool isNeutral (const StemEQParams& p) noexcept;
    static bool isNeutral (const ToneSteadyParams& p) noexcept { return ! p.enabled || p.amount < 0.5; }
    static bool isNeutral (const ResonanceParams& p) noexcept { return ! p.enabled || p.amount < 0.5; }
    bool allNeutral() const noexcept { return isNeutral (params) && isNeutral (tsParams) && isNeutral (rtParams); }

    // The model was trained at 44.1 kHz (NFFT 1024, ~187 frames a second): every rate must keep
    // about the same Hz per bin and frames per second, or it hears the music sped up.
    static int fftSizeFor (double sampleRate) noexcept { return sampleRate < 80000.0 ? 1024 : sampleRate < 160000.0 ? 2048 : 4096; }
    static int latencyFor (double sampleRate) noexcept { return fftSizeFor (sampleRate) * 7 / 4; }
    int latencySamples() const noexcept { return latencyFor (sampleRate); }

    /** Centre (Hz) of band b of the model's 64 ERB bands at this sample rate (20 Hz ... 20 kHz, or
        up to half the rate). No allocation. */
    static double bandCentreHz (int band, double sampleRate) noexcept;
    /** The bands as the stage measures them with an FFT of fftSize: each band's centre (Hz), its
        triangular weights over the bins 0 ... fftSize / 2 ([band][bin], every bin's weights sum to
        1), and the first / one-past-last bin it touches. Allocates. */
    static void makeBands (double sampleRate, int fftSize, std::vector<double>& centreHz, std::vector<double>& weights,
                           std::vector<int>& firstBin, std::vector<int>& endBin);
    /** K-weighting (BS.1770, what LUFS meters weigh with) as a power gain at hz. */
    static double kWeightAt (double hz) noexcept;

    void processFrame (double* frame, int numChannels) noexcept;

    template <typename Sample>
    void process (Sample* const* channels, int numChannels, int numSamples) noexcept
    {
        process (channels, numChannels, numSamples, nullptr);
    }

    /** Listen hook (the plug-in's "Listen: removed" on Resonance Tamer). While on, every frame is also
        re-synthesised with X x (Stem EQ, Keep loudness and Tone Steady gains) x (1 - tamer gain): what
        the tamer cut. It is overlap-added like the main output (so it lines up with it exactly, and the
        output plus it is the stage's output without the tamer's cuts) and written by the process()
        overload below. Costs one more inverse FFT per hop while on. Switching it on starts from
        silence; it is complete after about 1.25 frames (fftSize). */
    void setTamerDelta (bool on) noexcept
    {
        if (on && ! tamerDeltaOn)
        {
            for (auto* v : { &tdAcc[0], &tdAcc[1], &tdQueue[0], &tdQueue[1] }) std::fill (v->begin(), v->end(), 0.0);
            tdFrame = { 0.0, 0.0 };
        }
        tamerDeltaOn = on;
    }
    bool isTamerDeltaOn() const noexcept { return tamerDeltaOn; }

    /** The same, also writing what Resonance Tamer cut (setTamerDelta) to tamerDelta[c][0 .. numSamples)
        for channels 0 and 1 (zeros while the hook is off). */
    template <typename Sample>
    void process (Sample* const* channels, int numChannels, int numSamples, double* const* tamerDelta) noexcept
    {
        std::array<double, kMaxChannels> frame {};
        const int nch = numChannels < channelCount ? numChannels : channelCount;
        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < nch; ++c) frame[static_cast<size_t> (c)] = static_cast<double> (channels[c][i]);
            processFrame (frame.data(), nch);
            for (int c = 0; c < nch; ++c) channels[c][i] = static_cast<Sample> (frame[static_cast<size_t> (c)]);
            if (tamerDelta != nullptr)
                for (int c = 0; c < std::min (nch, 2); ++c) tamerDelta[c][i] = tamerDeltaOn ? tdFrame[static_cast<size_t> (c)] : 0.0;
        }
    }

    bool isActive() const noexcept { return running || mix > 0.0; }

    /** Broadband gain Keep loudness applies right now (dB; negative = turning the stage down to make
        up for a boost). 0 when off or neutral. Read from the audio thread. */
    double keepLevelDb() const noexcept { return keepDb; }

    /** Features of one stereo frame of fftSize samples, exactly as the model sees them (window,
        FFT, band energies of mid and side, log10). For tests and offline analysis. */
    void computeFeatures (const double* left, const double* right, float* features) noexcept;

    /** How much of the music's energy the model has credited to each stem since the last call
        (vocals, drums, bass, other; raw power, what Auto-Master's vocal level uses). Not atomic:
        call from the audio thread and hand the numbers on. */
    void takeStemEnergies (std::array<double, kStems>& out) noexcept
    {
        out = stemEnergy;
        stemEnergy.fill (0.0);
    }
    /** The same, weighted like a loudness meter (K-weighting, BS.1770, at each band's centre) for
        the "Model Hears" meter: each stem's share of the loudness of the mix coming in (before the
        stem moves), so a move changes the loudness by about what the share says (Keep loudness
        off; a stem with share s turned up by g dB: 10 log10 (s 10^(g/10) + 1 - s) LU). Raw power
        would credit the sub with more than it adds; A-weighting (-32 dB at 45 Hz against the
        K-weighting's -5 dB) credited an 808 with about a tenth of what a move of it does. Kept
        separately from takeStemEnergies(). */
    void takeStemLoudness (std::array<double, kStems>& out) noexcept
    {
        out = stemLoudness;
        stemLoudness.fill (0.0);
    }

    /** Deepest Resonance Tamer cut (dB, <= 0) and its frequency since the last call. */
    void takeResonanceMeter (double& deepestDb, double& atHz) noexcept
    {
        deepestDb = rtDeepest; atHz = rtDeepestHz;
        rtDeepest = 0.0; rtDeepestHz = 0.0;
    }
    /** Largest Tone Steady correction (dB, signed) and its frequency since the last call. */
    void takeToneSteadyMeter (double& largestDb, double& atHz) noexcept
    {
        largestDb = tsLargest; atHz = tsLargestHz;
        tsLargest = 0.0; tsLargestHz = 0.0;
    }

    /** With a stored tone target: how far the music playing has sat from it over the last ~20 s of
        music at the song's level (toneDistanceDb, as the stage hears the target), dB; -1 without a
        target or before 20 s of such music. Read from the audio thread. */
    double toneTargetDistanceDb() const noexcept { return tsDistance; }
    /** True while the music has sat more than kTargetFarDb from the stored target for kTargetFarSeconds
        (after 20 s of music): the target is probably another song's (a preset, a copied instance). */
    bool toneTargetFar() const noexcept { return tsFarSeconds >= kTargetFarSeconds; }

    /** Averages since the last call (see SpectralStats). */
    SpectralStats takeSpectralStats() noexcept;

    /** Shares ([stem][band]) of the latest frame the model finished, and how many it has done. */
    const std::array<float, kStems * StemNet::kBands>& latestShares() const noexcept { return shares; }
    long framesAnalysed() const noexcept { return analysed; }

private:
    void hop() noexcept;
    void fft (std::complex<double>* data, bool inverse) const noexcept;
    void spectrum (const double* ring, int start, std::complex<double>* out) const noexcept;
    void bandEnergies (const std::complex<double>* a, const std::complex<double>* b, double sign, float* out) const noexcept;
    void resonanceGains (const std::complex<double>* L, const std::complex<double>* R) noexcept;
    void toneSteadyGains (const std::complex<double>* L, const std::complex<double>* R) noexcept;

    StemEQParams params;
    ToneSteadyParams tsParams;
    ResonanceParams rtParams;
    double sampleRate = 48000.0;
    int channelCount = 2, fftSize = 1024, hopSize = 256, latency = 1792;

    // STFT
    std::vector<double> window;                          // periodic Hann
    std::vector<std::complex<double>> twiddle;
    std::vector<int> bitrev;
    std::vector<int> bandLo, bandHi;                     // bins each band touches
    std::vector<double> bandW;                           // [band][bin] (dense, rows only read in [lo, hi))
    std::array<std::vector<double>, 2> inRing, acc, outQueue;
    std::array<std::vector<std::complex<double>>, 2> spectra;   // 4 frames per channel (t-3 ... t)
    std::vector<std::complex<double>> work;
    std::vector<double> binGain, scratch;

    // Resonance Tamer (per bin, dB) and Tone Steady (per band) state
    std::vector<double> power, levelDb, prefix, rtCut, rtLin, rtTarget, rtSmooth, tsBin, bandCf, binLoud;
    std::array<double, 64> tsShort {}, tsLong {}, tsGain {}, tsJump {}, bandLoud {};
    std::array<double, 64> tsLive {};                    // ~20 s average of each band (dB re frame), music at the song's level
    long tsLiveFrames = 0;
    double tsDistance = -1.0, tsFarSeconds = 0.0, frameScaleDb = 0.0;
    long tsRelearn = 0;
    double rtAmount = 0.0, tsAmount = 0.0, framesPerSecond = 187.5, tsLevelShort = 0.0, tsLevelLong = 0.0, tsLoudShort = 0.0;
    long tsFrames = 0;
    double rtDeepest = 0.0, rtDeepestHz = 0.0, tsLargest = 0.0, tsLargestHz = 0.0;
    double statPower = 0.0, statCut = 0.0, statCutLow = 0.0, statCutHigh = 0.0, statSteady = 0.0;
    long statSteadyFrames = 0;
    int inPos = 0, queuePos = 0;
    long frameCount = 0, analysed = 0;

    // model
    StemNet net;
    std::array<float, StemNet::kIn> feat {};
    std::array<float, kStems * StemNet::kBands> shares {};
    std::array<double, kStems> gain { 1.0, 1.0, 1.0, 1.0 };
    std::array<double, kStems> stemEnergy {}, stemLoudness {};
    double gainCoeff = 0.0;

    // Keep loudness
    std::vector<double> binK;                            // K-weighting power gain per bin
    std::array<double, kStems * kStems> keepM {};        // ~2 s average of sum_b E_b share_s share_t, never above keepBeat
    double keepBeat = 0.0, keepPeakDb = -400.0;          // ~0.3 s average of the frames' K-weighted energy; its recent peak (dB)
    double keepMix = 0.0, keepCoeff = 0.0, keepAvg = 0.0, keepBeatAvg = 0.0, keepDb = 0.0;
    void updateKeepLoudness (const std::complex<double>* L, const std::complex<double>* R) noexcept;

    // Listen hook: what Resonance Tamer cut, overlap-added beside the main output
    bool tamerDeltaOn = false;
    std::array<std::vector<double>, 2> tdAcc, tdQueue;
    std::vector<double> tdGain;                          // per bin: the frame's gain without the tamer x (1 - tamer gain)
    std::array<double, 2> tdFrame {};                    // the latest output sample of it

    // dry path and cross-fade
    std::array<std::vector<double>, kMaxChannels> dry;
    int dryPos = 0;
    double mix = 0.0, mixCoeff = 0.0;
    bool running = false;
};

} // namespace polisher

#pragma once

#include <array>
#include <complex>
#include <vector>

namespace polisher {

inline constexpr int kStems = 4;
enum StemIndex { kStemVocals = 0, kStemDrums = 1, kStemBass = 2, kStemOther = 3 };

struct StemEQParams
{
    bool enabled = true;
    std::array<double, kStems> gainDb {};   // vocals, drums, bass, other: -12 ... +12 dB, 0 = untouched
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

/** Stem EQ: turn vocals, drums, bass and "other" up or down inside a finished stereo mix.

    The mix is not split into stems. StemNet predicts, per frame and band, how the energy divides
    between the four; the four stem gains are mixed by those shares into one gain per band, spread
    over the FFT bins (triangular ERB bands whose weights sum to 1 in every bin) and applied in an
    STFT (Hann, NFFT 1024 / hop 256; 2048 / 512 from 88.2 kHz so the bands stay in Hz), overlap-
    added with a Hann synthesis window. With every gain at 0 dB that gain is exactly 1.

    - Constant latency NFFT + 3 hops (1792 samples at 44.1 / 48 kHz, ~40 ms) in every state: the
      model looks 3 frames ahead.
    - Exact pass-through (delayed) when off or at 0 / 0 / 0 / 0, with 20 ms cross-fades; gain moves
      glide (30 ms). The model keeps running while neutral, so turning a knob never waits for it.
    - Channels 0 and 1 are processed (a mono input is analysed as L = R); further channels are
      only delayed, so every channel keeps the same latency.

    prepare() allocates; process() does not. */
class StemEQ
{
public:
    static constexpr int kMaxChannels = 8;
    static constexpr double kMaxGainDb = 12.0;

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setParams (const StemEQParams& p) noexcept { params = p; }
    const StemEQParams& getParams() const noexcept { return params; }
    static bool isNeutral (const StemEQParams& p) noexcept;

    static int fftSizeFor (double sampleRate) noexcept { return sampleRate < 80000.0 ? 1024 : 2048; }
    static int latencyFor (double sampleRate) noexcept { return fftSizeFor (sampleRate) * 7 / 4; }
    int latencySamples() const noexcept { return latencyFor (sampleRate); }

    void processFrame (double* frame, int numChannels) noexcept;

    template <typename Sample>
    void process (Sample* const* channels, int numChannels, int numSamples) noexcept
    {
        std::array<double, kMaxChannels> frame {};
        const int nch = numChannels < channelCount ? numChannels : channelCount;
        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < nch; ++c) frame[static_cast<size_t> (c)] = static_cast<double> (channels[c][i]);
            processFrame (frame.data(), nch);
            for (int c = 0; c < nch; ++c) channels[c][i] = static_cast<Sample> (frame[static_cast<size_t> (c)]);
        }
    }

    bool isActive() const noexcept { return running || mix > 0.0; }

    /** Features of one stereo frame of fftSize samples, exactly as the model sees them (window,
        FFT, band energies of mid and side, log10). For tests and offline analysis. */
    void computeFeatures (const double* left, const double* right, float* features) noexcept;

    /** How much of the music's energy the model has credited to each stem since the last call
        (vocals, drums, bass, other; for the "what the model hears" meter). Not atomic: call from
        the audio thread and hand the numbers on. */
    void takeStemEnergies (std::array<double, kStems>& out) noexcept
    {
        out = stemEnergy;
        stemEnergy.fill (0.0);
    }

    /** Shares ([stem][band]) of the latest frame the model finished, and how many it has done. */
    const std::array<float, kStems * StemNet::kBands>& latestShares() const noexcept { return shares; }
    long framesAnalysed() const noexcept { return analysed; }

private:
    void hop() noexcept;
    void fft (std::complex<double>* data, bool inverse) const noexcept;
    void spectrum (const double* ring, int start, std::complex<double>* out) const noexcept;
    void bandEnergies (const std::complex<double>* a, const std::complex<double>* b, double sign, float* out) const noexcept;

    StemEQParams params;
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
    int inPos = 0, queuePos = 0;
    long frameCount = 0, analysed = 0;

    // model
    StemNet net;
    std::array<float, StemNet::kIn> feat {};
    std::array<float, kStems * StemNet::kBands> shares {};
    std::array<double, kStems> gain { 1.0, 1.0, 1.0, 1.0 };
    std::array<double, kStems> stemEnergy {};
    double gainCoeff = 0.0;

    // dry path and cross-fade
    std::array<std::vector<double>, kMaxChannels> dry;
    int dryPos = 0;
    double mix = 0.0, mixCoeff = 0.0;
    bool running = false;
};

} // namespace polisher

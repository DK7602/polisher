#pragma once

#include "KWeighting.h"
#include "LoudnessTargeting.h"
#include "StemEQ.h"

#include <array>
#include <atomic>
#include <complex>
#include <cstdint>
#include <vector>

namespace polisher {

/** What a whole-song listen measured (for Auto-Master's report). */
struct SongStats
{
    double heardSeconds = 0.0;          // music heard (silence and stops skipped)
    double integratedLufs = -100.0;     // the whole song as it arrived (BS.1770 gating)
    double loudnessRangeLu = 0.0;       // EBU Tech 3342
    double loudestLufs = -100.0;        // the loudest window (mean K-weighted energy over it)
    double loudestStartSeconds = 0.0;   // where it starts: on the DAW's timeline when known, else into what was heard
    double loudestSeconds = 0.0;        // its length
    bool positionFromHost = false;      // true: loudestStartSeconds is a timeline position
};

/** Listens to a whole song as it plays, keeping only what Auto-Master needs, in memory that does not
    grow with the song:
    - the loudest windowSeconds of music (by mean K-weighted energy, BS.1770, at 100 ms steps: the
      same choice LoudestPartFinder makes), as audio, for the analysis;
    - the K-weighted energy of every 100 ms (whole-song integrated loudness, loudness range, where
      the loudest part sits on the DAW's timeline);
    - the song's tone as Tone Steady measures it (per 1 s of music, each of the 64 bands' level
      relative to the whole frame), from which toneTarget() takes the typical value per band.

    The audio is kept in 100 ms pieces from a fixed pool (2 x window + 2 pieces): the last window's
    pieces and the loudest window's pieces; when the last window becomes the loudest, only the list of
    pieces changes hands, nothing is copied. Blocks quieter than kSilencePeak (stopped transport,
    gaps) are skipped, so what is heard is music only.

    prepare() allocates; process() never allocates or locks (audio thread). The progress readings are
    safe from any thread while process() runs; the results (loudestWindow, stats, toneTarget) only
    while process() cannot run. */
class SongListener
{
public:
    static constexpr double kHopSeconds = 0.1;
    static constexpr int kToneBlockHops = 10;           // 1 s of music per tone reading
    static constexpr double kSilencePeak = 0.003;       // about -50 dBFS: a block below it is not music
    static constexpr int kMinToneBlocks = 8;            // a tone needs at least 8 s of music at the song's level

    void prepare (double sampleRate, double windowSeconds, double maxSeconds);
    void reset() noexcept;

    /** Audio thread: one block of the incoming mix (right = left for mono). hostSeconds is the DAW's
        timeline position of the block's first sample (negative: unknown). */
    void process (const float* left, const float* right, int numSamples, double hostSeconds) noexcept;
    void process (const double* left, const double* right, int numSamples, double hostSeconds) noexcept;

    // --- Progress (any thread) ------------------------------------------------------------------
    bool isPrepared() const noexcept               { return hopLen > 0; }
    double getSampleRate() const noexcept          { return sr; }
    double windowSeconds() const noexcept          { return windowHops * kHopSeconds; }
    double heardSeconds() const noexcept           { return sr > 0.0 ? static_cast<double> (heard.load (std::memory_order_acquire)) / sr : 0.0; }
    std::int64_t heardSamples() const noexcept     { return heard.load (std::memory_order_acquire); }
    /** Silent samples in a row since the last music (blocks the host still delivers after the song). */
    std::int64_t silentSamples() const noexcept    { return silent.load (std::memory_order_acquire); }
    /** True once maxSeconds of music were heard: it stops listening. */
    bool isFull() const noexcept                   { return hopsDone.load (std::memory_order_acquire) >= maxHops; }
    /** A whole window of music has been heard (its loudest part exists). */
    bool hasWindow() const noexcept                { return bestStart.load (std::memory_order_acquire) >= 0; }
    /** Loudness of the loudest window so far (LUFS, ungated), -100 before there is one. */
    double loudestLufs() const noexcept;
    /** Where it starts (see SongStats::loudestStartSeconds); -1 before there is one. */
    double loudestStartSeconds() const noexcept;
    bool loudestFromHost() const noexcept;

    // --- Results (only while process() cannot run) ------------------------------------------------
    /** The loudest window heard (all the music, when less than a window was heard), stereo. */
    AudioClip loudestWindow() const;
    SongStats stats() const;
    /** The song's tone as Tone Steady measures it, as the mix arrives: per band, the median over the
        1 s readings of music (those within 15 dB of the song's loudness: K-weighted power mean) of the
        band's level relative to the frame, and that level (ToneTarget::levelDb). Invalid with fewer
        than kMinToneBlocks of them. */
    ToneTarget toneTarget() const;

    /** Bytes held (fixed by prepare(): it does not grow with the song). */
    std::size_t memoryBytes() const noexcept;

private:
    template <typename Sample>
    void processAny (const Sample* left, const Sample* right, int numSamples, double hostSeconds) noexcept;
    void completeHop() noexcept;
    void toneFrame() noexcept;
    void closeToneBlock() noexcept;
    int takeFree() noexcept { return freeList[static_cast<size_t> (--freeCount)]; }
    void giveBack (int piece) noexcept { freeList[static_cast<size_t> (freeCount++)] = piece; }

    double sr = 0.0;
    int hopLen = 0, windowHops = 0, maxHops = 0, pieces = 0;

    // Loudness: K-weighted energy (sum over both channels of the mean square) per 100 ms of music.
    std::array<KWeighting, 2> weighting {};
    std::vector<double> hopEnergy, hopTime;              // [maxHops]; hopTime < 0: timeline position unknown
    double hopSum = 0.0;
    int hopPos = 0;

    // Audio of the last window and of the loudest one, in 100 ms pieces from a fixed pool.
    std::vector<float> pool;                             // pieces x (left hopLen, right hopLen)
    std::vector<int> recent, best, freeList;             // recent: ring of the last windowHops pieces
    std::vector<std::uint8_t> inRecent, inBest;
    int recentHead = 0, recentCount = 0, freeCount = 0, current = 0;
    double bestSum = 0.0;

    // Tone: frames of the stage's FFT (every half frame), 64 band levels relative to the frame.
    int fftSize = 0, framePos = 0, frameFill = 0, sinceFrame = 0;
    std::vector<double> window, bandW, bandCf;
    std::array<double, kToneBands> bandK {};             // K-weighting at each band's centre
    std::vector<int> bandLo, bandHi;
    std::array<std::vector<double>, 2> frameIn;
    std::vector<std::complex<double>> work, twiddle;
    std::vector<int> bitrev;
    std::vector<double> binPow;                          // |L|^2 + |R|^2 per bin of the latest frame
    std::array<double, kToneBands> blockSum {};
    double blockEnergy = 0.0;
    int blockFrames = 0;
    std::vector<float> toneBlocks;                       // [block][band] mean relative dB
    std::vector<float> toneLevel;                        // [block] mean frame level, K-weighted (dB)
    int toneCount = 0, maxBlocks = 0;

    // Progress, published by the audio thread.
    std::atomic<std::int64_t> heard { 0 }, silent { 0 };
    std::atomic<int> hopsDone { 0 }, bestStart { -1 };
    std::atomic<double> bestLufs { -100.0 };
};

/** A tone target as learned (the mix arriving at the plug-in) moved to what the spectral stage hears
    after Input Trim and the Tonal EQ: each band shifted by the EQ's response at its centre, the
    song's level by the trim and by what the EQ does to the song's energy (by its own tone). No
    allocation. */
ToneTarget toneTargetAtStage (const ToneTarget& song, const TonalEQParams& eq, double sampleRate, double inputTrimDb = 0.0) noexcept;

} // namespace polisher

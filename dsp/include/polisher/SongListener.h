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

/** A stretch of the DAW's timeline (seconds). */
struct TimeSpan
{
    double start = 0.0, end = 0.0;
};

/** What a whole-song listen measured (for Auto-Master's report). */
struct SongStats
{
    double heardSeconds = 0.0;          // music heard (silence and stops skipped)
    double integratedLufs = -100.0;     // what it heard as it arrived (BS.1770 gating): the whole song when it was played once through
    double loudnessRangeLu = 0.0;       // EBU Tech 3342, of the same
    double loudestLufs = -100.0;        // the loudest window (mean K-weighted energy over it)
    double loudestStartSeconds = 0.0;   // where it starts: on the DAW's timeline when known, else into what was heard
    double loudestSeconds = 0.0;        // its length (music heard)
    bool positionFromHost = false;      // true: loudestStartSeconds and loudestSpans are timeline positions
    /** Where the loudest window's audio sits on the timeline, in playing order: one span when it was
        played straight through; more when playback jumped or a cycle loop wrapped inside it (its audio
        then joins those places). Empty without timeline positions. Silence skipped inside a span leaves
        it a little longer than loudestSeconds. */
    std::vector<TimeSpan> loudestSpans;
    /** Jumps on the timeline while it listened (a cycle loop wrapping, the playhead moved during
        playback, or playback restarted somewhere else after a stop). With any, integratedLufs and
        loudnessRangeLu describe what was heard (repeats included), not the song played once through. */
    int timelineJumps = 0;
    /** Every part of the timeline that plays at the loudest part's level: each 100 ms of music inside a
        windowSeconds stretch whose loudness is within kHookWithinLu of the loudest one's (the hook or drop
        and its repeats), joined into spans (split where playback jumped or music is missing for more than
        0.5 s). Pieces heard without a timeline position (a reverb tail after Stop) are left out and split
        the span they sit in. Empty without timeline positions. Auto Analyze checks the settings again only
        on a part that plays at this level, so it compares like with like. */
    std::vector<TimeSpan> hookSpans;
    int channels = 2;                   // 1: a mono bus (the loudness above counts one channel, as BS.1770 does)
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
    pieces changes hands, nothing is copied. Each 100 ms piece whose peak stays below kSilencePeak
    (stopped transport, gaps) is skipped as a whole, whatever the host's block size, so what is heard
    is music only. A mono bus (no right channel) is measured as one channel, as BS.1770 does, and the
    loudest window comes back mono. Jumps on the DAW's timeline (cycle loops, the playhead moved while
    playing) are noted, so the report can say where the loudest window really came from.

    prepare() allocates; process() never allocates or locks (audio thread). The progress readings are
    safe from any thread while process() runs; the results (loudestWindow, stats, toneTarget) only
    while process() cannot run. */
class SongListener
{
public:
    static constexpr double kHopSeconds = 0.1;
    static constexpr int kToneBlockHops = 10;           // 1 s of music per tone reading
    static constexpr double kSilencePeak = 0.003;       // about -50 dBFS: a 100 ms piece below it is not music
    static constexpr int kMinToneBlocks = 8;            // a tone needs at least 8 s of music at the song's level
    static constexpr double kJumpSeconds = 0.05;        // a block this far from where the last one ended: the playhead jumped
    static constexpr double kHookWithinLu = 1.0;        // SongStats::hookSpans: windows this close to the loudest one's loudness

    void prepare (double sampleRate, double windowSeconds, double maxSeconds);
    void reset() noexcept;

    /** Audio thread: one block of the incoming mix (right nullptr, or the same pointer as left: a mono
        bus). hostSeconds is the DAW's timeline position of the block's first sample (negative: unknown,
        or the transport is stopped). */
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
    /** Where it ends on the timeline; -1 before there is one, without timeline positions, or when
        playback jumped inside it (then it is no single stretch of the song). */
    double loudestEndSeconds() const noexcept;
    /** Playback jumped inside the loudest window so far (its audio joins two or more places). */
    bool loudestJumped() const noexcept            { return hasWindow() && bestJumps.load (std::memory_order_relaxed) > 0; }
    bool loudestFromHost() const noexcept;

    // --- Results (only while process() cannot run) ------------------------------------------------
    /** The loudest window heard (all the music, when less than a window was heard): stereo, or mono
        when every block heard was mono. */
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
    void skipHop() noexcept;
    void toneFrame() noexcept;
    void closeToneBlock() noexcept;
    int takeFree() noexcept { return freeList[static_cast<size_t> (--freeCount)]; }
    void giveBack (int piece) noexcept { freeList[static_cast<size_t> (freeCount++)] = piece; }

    double sr = 0.0;
    int hopLen = 0, windowHops = 0, maxHops = 0, pieces = 0;

    // Loudness: K-weighted energy (sum over the channels of the mean square) per 100 ms of music.
    std::array<KWeighting, 2> weighting {};
    std::vector<double> hopEnergy, hopTime;              // [maxHops]; hopTime < 0: timeline position unknown
    std::vector<std::uint8_t> hopJump;                   // [maxHops]; 1: the timeline jumped since the hop before (or inside this one)
    double hopSum = 0.0, hopPeak = 0.0;                  // of the piece being filled (judged music or silence when it is full)
    int hopPos = 0;
    double nextHostSeconds = -1.0;                       // where the next block starts if playback runs on (-1: unknown)
    bool jumpPending = false, stereoSeen = false;
    int jumpCount = 0;

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
    std::array<double, kToneBands> blockSum {}, hopToneSum {};   // hopTone*: frames of the piece being filled, kept if it is music
    double blockEnergy = 0.0, hopToneEnergy = 0.0;
    int blockFrames = 0, hopToneFrames = 0;
    std::vector<float> toneBlocks;                       // [block][band] mean relative dB
    std::vector<float> toneLevel;                        // [block] mean frame level, K-weighted (dB)
    int toneCount = 0, maxBlocks = 0;

    // Progress, published by the audio thread.
    std::atomic<std::int64_t> heard { 0 }, silent { 0 };
    std::atomic<int> hopsDone { 0 }, bestStart { -1 }, bestJumps { 0 };
    std::atomic<double> bestLufs { -100.0 };
};

/** A tone target as learned (the mix arriving at the plug-in) moved to what the spectral stage hears
    after Input Trim and the Tonal EQ: each band shifted by the EQ's response at its centre, the
    song's level by the trim and by what the EQ does to the song's energy (by its own tone). No
    allocation. */
ToneTarget toneTargetAtStage (const ToneTarget& song, const TonalEQParams& eq, double sampleRate, double inputTrimDb = 0.0) noexcept;

} // namespace polisher

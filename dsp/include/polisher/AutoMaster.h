#pragma once

#include "KWeighting.h"
#include "LoudnessMeter.h"
#include "LoudnessTargeting.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace polisher {

enum class Genre { trapHipHop, rnb, pop, edm, rock, acoustic };
enum class Intensity { gentle, balanced, punchy };
enum class LoudnessTarget { stream, loud, club };

inline constexpr int kNumGenres = 6;
const char* genreName (Genre) noexcept;
double targetLufs (LoudnessTarget) noexcept;

/** Relative tonal balance (dB per EQ band, in the display's 4.5 dB/oct-tilted view) that
    typical finished masters in a genre sit around. Starting points, to be refined with reference
    analysis ("Match ref"). */
std::array<double, kEqBands> genreTargetCurve (Genre) noexcept;

/** Where the lead vocal usually sits in a genre's masters, as Stem EQ's model hears it (dB relative to
    the whole mix, loudest section). Trap / Hip-Hop and R&B come from the user's own 13 released songs
    (median -2.8 dB, middle 60 % -3.8 ... -2.0); the others are starting points (MUSDB pop / rock
    measures -5.9 dB median). Raw energy shares, not loudness-weighted like the "Model Hears" meter:
    the norms were measured that way and the songs are not at hand to re-measure them weighted. So
    the reading still depends on how loud the 808 is (a louder 808 reads the vocal lower); re-derive
    the norms on the songs before switching measureStemLevels() to StemEQ::takeStemLoudness(). */
double genreVocalLevelDb (Genre) noexcept;

/** Stem EQ vocal move Auto-Master suggests for a measured vocal level (dB as above): 0 when the vocal
    is within 1.5 dB below / 2.5 dB above the genre's usual level, or when there is hardly any vocal
    (below -15 dB: an instrumental); otherwise the move back toward it, at most +/-3 dB, in 0.5 dB steps. */
double suggestVocalMoveDb (double vocalLevelDb, Genre) noexcept;

/** What Auto-Master measured in the audio it listened to. */
struct MixAnalysis
{
    double seconds = 0.0;                       // audio analysed (loud parts only)
    LoudnessStats loudness;                     // of the incoming mix
    double peakToLoudnessDb = 0.0;              // true peak minus integrated (PLR)
    std::array<double, kEqBands> bandDb {};     // tilted band levels, mean of bands 2..6 = 0 (before the user's Stem EQ, as the Tonal EQ hears it)
    double bassWidthPercent = 0.0;              // how stereo the bass is below 150 Hz
    double lowJumpDb = 0.0;                     // how far the loudest bass moments rise above typical ones
    double lowCrestDb = 0.0;                    // peak vs average of the low band (kick punch)
    bool hasLowEnd = false;                     // enough bass/kick energy to judge punch
    double midSideDb = -100.0;                  // side vs mid energy, 250 Hz - 5 kHz (stereo width of the mids)
    double highSideDb = -100.0;                 // side vs mid energy above 5 kHz
    double correlation = 1.0;                   // L/R correlation of the loud parts (+1 mono, 0 wide, < 0 out of phase)
    double subVsBassDb = 0.0;                   // low-end shape: energy at 30-60 Hz vs 60-120 Hz (+ = the deep sub is louder)
    std::array<double, kStems> stemLevelDb {};  // as Stem EQ's model hears it (after the user's Stem EQ; raw energy shares)
    bool usable = false;                        // false: silent or too short
};

MixAnalysis analyseMix (const AudioClip& clip);

/** How much the level of each analysis band changes (dB) with these EQ settings at this sample
    rate, measured the way analyseMix() measures bands: the EQ's power gain averaged over the band
    per Hz, for a typical master's spectrum (falling 4.5 dB/oct). Neighbouring bands overlap, so a
    band's own move only partly lands in its own analysis band and partly spills next door. */
std::array<double, kEqBands> eqBandChangeDb (const TonalEQParams& eq, double sampleRate) noexcept;

/** EQ band gains (default band frequencies) whose combined effect brings each analysis band's
    change as close as possible to `wanted` (regularised least squares on the band-interaction
    matrix, refined on the real response). Only bands with a non-zero wish move, each in the
    direction it was asked to and by at most limitDb[b]; gains are rounded to 0.1 dB and moves
    under 0.4 dB are dropped (and the rest re-solved without them). */
std::array<double, kEqBands> solveEqGains (const std::array<double, kEqBands>& wanted,
                                           const std::array<double, kEqBands>& limitDb, double sampleRate) noexcept;

// =================================================================================================
// Reference matching ("Match ref")
// =================================================================================================

/** What Auto-Master keeps of a reference song: the measurements of its loudest part, never its
    audio. The plug-in saves this with the project. */
struct ReferenceProfile
{
    bool valid = false;
    std::string name;                           // the file name (no folder, no extension), as the report quotes it
    double trackSeconds = 0.0;                  // how much of the song was read (all of it, up to kMaxReferenceMinutes)
    double startSeconds = 0.0;                  // where its loudest part starts
    double partSeconds = 0.0;                   // how long that part is (kReferencePartSeconds, or the whole song if shorter)
    double trackLufs = -100.0;                  // integrated loudness of the whole song
    double lufs = -100.0;                       // integrated loudness of its loudest part: what Auto-Master aims for
    double truePeakDbtp = -100.0;               // of the loudest part
    double peakToLoudnessDb = 0.0;              // of the loudest part
    std::array<double, kEqBands> bandDb {};     // tonal balance, measured like MixAnalysis::bandDb
    double bassWidthPercent = 0.0;
    double midSideDb = -100.0, highSideDb = -100.0, correlation = 1.0;
    double subVsBassDb = 0.0;
    /** The loudest part's average spectrum as the plug-in's spectrum display draws it (dB at
        kDisplayPoints log-spaced frequencies from 20 Hz to 20 kHz, 4.5 dB/oct tilt), at its own
        loudness: shift it by (loudness now - lufs) to compare it with a live curve. */
    std::vector<double> displayDb;
};

inline constexpr double kReferencePartSeconds = 30.0;   // the "loudest part": about a chorus or a drop
inline constexpr double kMaxReferenceMinutes = 15.0;    // longer files: only the first 15 minutes are read
inline constexpr double kRefMinLufs = -16.0, kRefMaxLufs = -6.0;   // loudness targets taken from a reference stay in this range
inline constexpr int kDisplayFftSize = 4096, kDisplayPoints = 96; // the plug-in's spectrum display (PolisherWebEditor)

/** Finds the loudest part of a song streamed through it in blocks, so a whole song never has to be
    held in memory: K-weighted energy per 100 ms (BS.1770, like the loudness meters), plus the whole
    song's integrated loudness. Not for the audio thread (it grows a list of 100 ms readings). */
class LoudestPartFinder
{
public:
    void prepare (double sampleRate, int numChannels);
    void process (const float* const* channels, int numChannels, int numSamples);

    double seconds() const noexcept           { return sampleRate > 0.0 ? static_cast<double> (samples) / sampleRate : 0.0; }
    double integratedLufs() const noexcept    { return meter.integratedLufs(); }
    /** First sample of the loudest stretch of windowSeconds (highest mean K-weighted energy; the
        earliest one when several tie). 0 when the song is shorter than that. */
    std::int64_t loudestStart (double windowSeconds) const;

private:
    double sampleRate = 0.0;
    int channelCount = 0, hopLength = 1, hopPos = 0;
    std::int64_t samples = 0;
    double hopSum = 0.0;
    std::array<KWeighting, 2> weighting {};
    std::vector<double> hopEnergy;              // one per 100 ms
    LoudnessMeter meter;
};

/** The loudest kReferencePartSeconds of a clip (the whole clip when it's shorter); startSeconds is
    set to where it starts. */
AudioClip loudestPart (const AudioClip& song, double& startSeconds);

/** The average spectrum of a clip as the plug-in's spectrum display shows it (mono sum, Hann
    window, fftSize points 30 times a second, each point averaged over +/- 1/12 octave, 4.5 dB/oct
    tilt, the display's fast-rise / slow-fall smoothing), averaged over the clip. */
std::vector<double> spectrumDisplayCurve (const AudioClip& clip, int fftSize = kDisplayFftSize, int points = kDisplayPoints);

/** Measures a reference song's loudest part. part = that part's audio; the rest describes the whole
    song it came from (for the report). */
ReferenceProfile analyseReference (const AudioClip& part, double trackSeconds, double trackLufs, double startSeconds);

/** The same for a whole song held in memory: finds its loudest part first. */
ReferenceProfile analyseReference (const AudioClip& song);

/** One explained decision. module: "ul", "eq", "bass", "dyn", "img", "sat", "clip" or "max". */
struct AutoMasterReason
{
    std::string module, control, value, why;
};

struct AutoMasterSettings
{
    Genre genre = Genre::trapHipHop;
    Intensity intensity = Intensity::balanced;
    LoudnessTarget target = LoudnessTarget::loud;
    StemEQParams stem;                          // the user's Stem EQ: kept, and heard while deciding
    /** When valid, Auto-Master aims for this song instead of the genre norms: its tone, loudness
        (clamped to kRefMinLufs .. kRefMaxLufs), stereo width and low end. The genre still decides
        what a reference can't show (how much clipping, compression and saturation suit the style). */
    ReferenceProfile reference;
};

struct AutoMasterResult
{
    bool ok = false;
    bool notAFullMix = false;                   // one instrument or stem: nothing is applied
    std::string summary;                        // one paragraph for the top of the report
    std::string tipTitle, tip;                  // when !ok: what to do next
    std::vector<std::string> notes;             // extra advice (fix-it-in-the-mix, honesty notes)
    std::vector<AutoMasterReason> reasons;
    MixAnalysis analysis;
    MixAnalysis masterAnalysis;                 // the master, measured the same way (only when matching a reference)
    MasterChainParams chain;                    // the settings to apply
    bool hasStemSuggestion = false;             // a Stem EQ move to offer (never applied automatically)
    StemEQParams stemSuggestion;                // the full Stem EQ setting to offer (the user's, plus the move)
    LoudnessStats output;                       // what the master measures with them
    double targetLufs = 0.0;
    double capturedLufs = -100.0;               // the 12 s it heard, as it arrived (before Input Trim and every module)
    double loudnessCostDb = 0.0;                // loudness given up to peak control (>= 0)
    bool reachedTarget = false;
};

/** The whole Auto-Master: analyse, decide every module, land loudness with the real chain within
    the intensity's limiting budget, and explain each choice in plain language. */
AutoMasterResult autoMaster (const AudioClip& clip, const AutoMasterSettings& settings);

} // namespace polisher

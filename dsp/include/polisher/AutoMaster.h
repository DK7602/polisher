#pragma once

#include "KWeighting.h"
#include "LoudnessMeter.h"
#include "LoudnessTargeting.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
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

/** The most a song style moves its genre's target curve, per EQ band (dB). */
inline constexpr std::array<double, kEqBands> kMaxStyleOffsetDb { 2.0, 1.5, 1.5, 1.0, 1.0, 1.5, 2.0 };

/** How a song style (handoff::kMixStyles, the suite's 75 styles: "hiphop.lofi") moves its genre's target curve:
    its own tonal target (Mixology's bass weight / low-mid scoop / top sheen curve) against the average of the
    styles Auto-Master masters as that genre, averaged over each band, the mean of bands 2..6 taken out, at most
    kMaxStyleOffsetDb. All 0 for an unknown style or one that Auto-Master masters as another genre. */
std::array<double, kEqBands> styleToneOffsetDb (Genre, const std::string& styleKey) noexcept;

/** genreTargetCurve() moved by the song style (styleToneOffsetDb): what Auto-Master's EQ aims for. */
std::array<double, kEqBands> styleTargetCurve (Genre, const std::string& styleKey) noexcept;

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

/** A mix's average power spectrum (its loud parts, like bandDb), folded into kPoints log-spaced points
    per analysis band: each point's share of the power and its power-weighted frequency. With it,
    eqBandChangeDb() predicts how an EQ moves THIS mix's band levels (an 808 sitting at 45-60 Hz takes
    a Low End shelf differently from a typical spectrum's evenly falling sub). */
struct BandSpectrum
{
    static constexpr int kPoints = 24;
    std::array<std::array<double, kPoints>, kEqBands> hz {}, power {};
    bool valid = false;
};

/** What Auto-Master measured in the audio it listened to. */
struct MixAnalysis
{
    double seconds = 0.0;                       // audio analysed (loud parts only)
    LoudnessStats loudness;                     // of the incoming mix
    double peakToLoudnessDb = 0.0;              // true peak minus integrated (PLR)
    std::array<double, kEqBands> bandDb {};     // tilted band levels, mean of bands 2..6 = 0 (before the user's Stem EQ, as the Tonal EQ hears it)
    BandSpectrum spectrum;                      // the spectrum bandDb was measured from (same audio)
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
    rate, measured the way analyseMix() measures bands: the EQ's power gain averaged over the band,
    weighted by the mix's own spectrum when one is given (a band without power in it, or no spectrum:
    a typical master's spectrum, falling 4.5 dB/oct). Neighbouring bands overlap, so a band's own move
    only partly lands in its own analysis band and partly spills next door. */
std::array<double, kEqBands> eqBandChangeDb (const TonalEQParams& eq, double sampleRate, const BandSpectrum* spectrum = nullptr) noexcept;

/** EQ band gains (default band frequencies) whose combined effect brings each analysis band's
    change as close as possible to `wanted` (regularised least squares on the band-interaction
    matrix, refined on the real response; for this spectrum when one is given). Only bands with a
    non-zero wish move, each in the direction it was asked to and by at most limitDb[b]; gains are
    rounded to 0.1 dB and moves under 0.4 dB are dropped (and the rest re-solved without them). */
std::array<double, kEqBands> solveEqGains (const std::array<double, kEqBands>& wanted,
                                           const std::array<double, kEqBands>& limitDb, double sampleRate,
                                           const BandSpectrum* spectrum = nullptr) noexcept;

/** For a band the solver left at 0 dB although wantedDb was wished for: true ("covered") when the
    neighbouring bands' moves change it by movedDb, at least half the wish and the same way; false
    when they don't (the report then says the band was left flat). */
inline bool eqBandCovered (double wantedDb, double movedDb) noexcept
{
    return movedDb * wantedDb > 0.0 && std::abs (movedDb) >= 0.5 * std::abs (wantedDb);
}

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
    double lufs = -100.0;                       // integrated loudness of its loudest part (the loudness its displayDb is at)
    /** Integrated loudness of its loudest kHookSeconds (found over the whole song): what Auto-Master aims
        for, so the reference's hook is compared with the loudest kHookSeconds of your song that the master
        is set from, like for like. -100: a profile saved before it existed (lufs stands in, see
        referenceLoudness()). */
    double hookLufs = -100.0;
    double sampleRate = 0.0;                    // of the file (the display's reading depends on it); 0: unknown (older profile)
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
inline constexpr double kHookSeconds = 12.0;            // the loudest part Auto-Master masters from (the plug-in keeps the loudest 12 s it hears)
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
    /** Integrated loudness (BS.1770 gating) of that stretch (of the whole song when it is shorter),
        as a loudness meter reads that stretch on its own. */
    double loudestLufs (double windowSeconds) const;

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

// =================================================================================================
// A genre's typical balance on the spectrum display (the dotted "Trap target" line)
// =================================================================================================

/** A tonal balance given as band levels (like MixAnalysis::bandDb or genreTargetCurve(): dB per EQ
    band in the 4.5 dB/oct-tilted per-Hz view, any common offset) turned into one smooth curve, dB at
    any frequency in that view. A monotone cubic through the band centres (so it has no bumps the
    bands don't have), carried on past the outer centres with its end slopes, falling 24 dB/oct below
    20 Hz like a master's low cut. Its node values are adjusted so that every band, measured the way
    analyseMix() measures it (mean power per Hz over the band, tilt at the band's centre), reads
    exactly the level it was given. */
class BalanceCurve
{
public:
    explicit BalanceCurve (const std::array<double, kEqBands>& bands) noexcept;

    /** The curve at a frequency (dB, tilted view). */
    double db (double hz) const noexcept;
    /** One band of the curve, measured like analyseMix() measures a mix (dB, tilted view). */
    double bandDb (int band) const noexcept;

private:
    std::array<double, kEqBands> x {}, y {}, slope {};   // band centres (log2 Hz), values (dB), slopes (dB/oct)
    void updateSlopes() noexcept;
};

/** Loudness the genre curve is computed at when no other is asked for (the UI moves it anyway). */
inline constexpr double kGenreCurveLufs = -9.0;

/** A balance as the plug-in's spectrum display draws it (dB at kDisplayPoints log-spaced frequencies
    from 20 Hz to 20 kHz, 4.5 dB/oct tilt, like spectrumDisplayCurve()) for a steady master that has
    exactly this balance in both channels and measures `lufs` integrated: the display's expected
    reading at each point, with its Hann window, bins and +/- 1/12 octave averaging. Nothing above
    20 kHz. Shift it by (loudness now - lufs) to compare it with a live curve, like
    ReferenceProfile::displayDb. */
std::vector<double> balanceDisplayCurve (const std::array<double, kEqBands>& bandDb, double sampleRate,
                                         double lufs = kGenreCurveLufs, int fftSize = kDisplayFftSize, int points = kDisplayPoints);

/** The balance Auto-Master's EQ aims for in a genre (genreTargetCurve()) as the display draws it. */
std::vector<double> genreDisplayCurve (Genre, double sampleRate, double lufs = kGenreCurveLufs,
                                       int fftSize = kDisplayFftSize, int points = kDisplayPoints);

/** Measures a reference song's loudest part. part = that part's audio; the rest describes the whole
    song it came from (for the report). hookLufs: the song's loudest kHookSeconds (LoudestPartFinder::
    loudestLufs); not finite: taken from the loudest kHookSeconds inside part. */
ReferenceProfile analyseReference (const AudioClip& part, double trackSeconds, double trackLufs, double startSeconds,
                                   double hookLufs = std::numeric_limits<double>::quiet_NaN());

/** The loudness Auto-Master aims for with this reference (before the kRefMinLufs .. kRefMaxLufs limits):
    its loudest kHookSeconds, or for a profile saved before that was measured, its loudest part. */
double referenceLoudness (const ReferenceProfile&) noexcept;

/** How much higher (dB) the spectrum display reads the same sound at displayRate than at fileRate: the
    display's FFT has the same number of points at every rate, so each point's bin is wider and holds
    more of the spectrum at a higher rate (10 log10 of the ratio). Checked against balanceDisplayCurve's
    model of the display at both rates (the genre line's): within 0.2 dB from 500 Hz to 16 kHz (0.5 dB
    at 300 Hz). Lower down, at 96 kHz and up, a display point holds only a few bins and any sound's
    reading there depends on where its notes fall (the model swings about +/-2 dB around the shift, a
    live song more), so no finer correction is worth more than the shift. Shift ReferenceProfile::displayDb by
    this to draw it in a project at another rate. 0 when either rate is unknown. */
double displayRateShiftDb (double fileRate, double displayRate) noexcept;

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
    /** The song's own tone (SongListener::toneTarget, as the mix arrives), when it listened to the song:
        Tone Steady holds the song to it (moved by the chosen Tonal EQ), and every render hears it so. */
    ToneTarget songTone;
    /** How much of the song the plug-in listened to before handing over its loudest part (0: only this
        clip). The plug-in's report says that itself, so the summary doesn't repeat it. */
    double songSeconds = 0.0;
    /** The song's style, shared by the suite ("hiphop.lofi"; "" = none). When Auto-Master masters it as this
        genre, the EQ aims for the style's own tone (styleTargetCurve), and its feel (handoff::Feel) scales the
        clipping and saturation a little; a style of another genre (you picked another genre) is left out. */
    std::string styleKey;
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
    StemEQParams stemSuggestion;                // the full Stem EQ setting to offer (the user's, plus the move; 0 dB for the other stems when Stem EQ was off)
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

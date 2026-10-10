#pragma once

#include "LoudnessTargeting.h"

#include <array>
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

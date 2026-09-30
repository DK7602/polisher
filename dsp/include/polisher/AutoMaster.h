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

/** What Auto-Master measured in the audio it listened to. */
struct MixAnalysis
{
    double seconds = 0.0;                       // audio analysed (loud parts only)
    LoudnessStats loudness;                     // of the incoming mix
    double peakToLoudnessDb = 0.0;              // true peak minus integrated (PLR)
    std::array<double, kEqBands> bandDb {};     // tilted band levels, mean of bands 2..6 = 0
    double bassWidthPercent = 0.0;              // how stereo the bass is below 150 Hz
    double lowJumpDb = 0.0;                     // how far the loudest bass moments rise above typical ones
    double lowCrestDb = 0.0;                    // peak vs average of the low band (kick punch)
    bool hasLowEnd = false;                     // enough bass/kick energy to judge punch
    bool usable = false;                        // false: silent or too short
};

MixAnalysis analyseMix (const AudioClip& clip);

/** One explained decision. module: "eq", "bass", "clip" or "max". */
struct AutoMasterReason
{
    std::string module, control, value, why;
};

struct AutoMasterSettings
{
    Genre genre = Genre::trapHipHop;
    Intensity intensity = Intensity::balanced;
    LoudnessTarget target = LoudnessTarget::loud;
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
    LoudnessStats output;                       // what the master measures with them
    double targetLufs = 0.0;
    double loudnessCostDb = 0.0;                // loudness given up to peak control (>= 0)
    bool reachedTarget = false;
};

/** The whole Auto-Master: analyse, decide every module, land loudness with the real chain within
    the intensity's limiting budget, and explain each choice in plain language. */
AutoMasterResult autoMaster (const AudioClip& clip, const AutoMasterSettings& settings);

} // namespace polisher

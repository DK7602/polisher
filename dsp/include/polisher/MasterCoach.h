#pragma once

#include "AutoMaster.h"

#include <string>
#include <vector>

/** The Sonicology Rack's Coach on the master (Beta 29): what Auto-Master would do, as the Rack's COACH page shows it.
    The same shape as Voxology's Coach for a vocal and Mixology's for a track, so the page draws all three alike.
    Everything is measured: the mix as Auto-Master heard it against what it aims for (the genre / style's tonal
    balance and loudness, or your reference), and the master by running the same 12 s through the planned chain
    (renderMaster) and measuring it again. */
namespace polisher::coach
{
    /** What the listen behind the plan heard (from the plug-in's SongListener). */
    struct Heard
    {
        double seconds = 0.0;          // music heard
        double songLufs = -100.0;      // the whole song's loudness (-100: unknown)
        double songLra = 0.0;          // its loudness range (LU)
        bool wholeSong = false;        // it heard the whole song once through
    };

    /** The Rack's module key for an Auto-Master reason's module ("eq" -> "mas.tonaleq"); "" for none. */
    const char* moduleKey (const std::string& code) noexcept;

    /** The Rack's module key for one of Masterology's parameter ids ("eqGain3" -> "mas.tonaleq", "drive" ->
        "mas.max"; "" for bypass). */
    const char* rackKeyOfParam (const std::string& id) noexcept;

    /** The plan as the Rack reads it (a JSON object). `after`: the master measured through the plan (analyseMix of
        renderMaster), nullptr when it wasn't measured. At most maxBytes (reasons shortened, then dropped; always valid
        JSON). `changedIds` (Beta 30): the parameter ids APPLY would change, compared with the settings Masterology has
        now (Auto-Master decides from the mix as it comes in, so after an APPLY the same mix gives the same plan); each
        module then says whether it is already set as planned ("same"), "changes" counts them and "upToDate" is true
        when there are none. nullptr: not compared. */
    std::string planJson (const AutoMasterResult& r, const MixAnalysis* after, const AutoMasterSettings& s, const Heard& heard,
                          std::size_t maxBytes, const std::vector<std::string>* changedIds = nullptr);
}

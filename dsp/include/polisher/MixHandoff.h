#pragma once

#include <string>

/** Mixology's hand-off (the suite link, suite/SuiteLink.h): what Masterology makes of the mix style Mixology mixed
    the song in. Framework-free, unit-tested. */
namespace polisher::handoff
{
    /** The Auto-Master genre (amGenre: 0 Trap / Hip-Hop, 1 R&B, 2 Pop, 3 EDM, 4 Rock, 5 Acoustic) closest to a
        Mixology mix style ("genre.style", e.g. "hiphop.modern"); -1 when there is none or it is unknown. */
    inline int genreForMixStyle (const std::string& styleKey)
    {
        const auto genre = styleKey.substr (0, styleKey.find ('.'));
        if (genre == "hiphop" || genre == "trap") return 0;
        if (genre == "rnb" || genre == "gospel") return 1;
        if (genre == "pop" || genre == "worship" || genre == "latin" || genre == "afro") return 2;
        if (genre == "electronic") return 3;
        if (genre == "rock") return 4;
        if (genre == "folk" || genre == "country" || genre == "jazz" || genre == "cinematic") return 5;
        return -1;
    }

    /** The Auto-Master target's name as Mixology shows it (amTarget: 0 Stream -14, 1 Loud -9, 2 Club -7). */
    inline const char* targetName (int amTarget)
    {
        return amTarget == 0 ? "Streaming" : amTarget == 2 ? "Club" : "Loud release";
    }
}

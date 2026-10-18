#pragma once

#include <string>

/** Mixology's hand-off (the suite link, suite/SuiteLink.h): what Masterology makes of the mix style Mixology mixed
    the song in. Framework-free, unit-tested. */
namespace polisher::handoff
{
    /** How a style's masters treat their peaks, against the rest of its Auto-Master genre: soft styles (Lo-Fi,
        Ambient, Soft Rock, film score) are meant to sound gentle, hard ones (Rage, Metal, Dubstep, Hyperpop) are
        pushed. It scales how much clipping and saturation Auto-Master uses, a little. */
    enum class Feel { soft, natural, normal, hard };

    /** One of the suite's song styles (plugin/ui/mixstyles.js, the same file in Mixology, Voxology and
        Masterology): its key, its full name as Mixology shows it ("Hip Hop - Lo-Fi"), its tonal target (bass
        weight, low-mid scoop, top sheen in dB, Mixology's Tone page) and the Auto-Master genre that masters it
        (amGenre: 0 Trap / Hip-Hop, 1 R&B, 2 Pop, 3 EDM, 4 Rock, 5 Acoustic). The keys, names and tone values must
        match mixstyles.js (tests/test_handoff.cpp reads it); the genre and the feel are Masterology's. */
    struct MixStyle
    {
        const char* key;
        const char* name;
        double bass, scoop, air;
        int genre;
        Feel feel;
    };

    // Mostly by Mixology's genre. Exceptions, by what the style sounds like: Country Modern / Pop and Worship
    // Modern / Live are pop productions (Pop), Outlaw / Southern is a rock band (Rock), Worship Acoustic and Hymns
    // are acoustic (Acoustic), Ambient is soft electronic music (Pop: no club low end, nothing pushed), and Epic
    // Trailer's huge hits and low end master like EDM.
    inline constexpr MixStyle kMixStyles[] = {
        { "hiphop.modern", "Hip Hop - Modern Clean", 4.0, 1.5, 2.0, 0, Feel::normal },
        { "hiphop.boombap", "Hip Hop - Boom Bap", 3.0, 0.0, -1.0, 0, Feel::natural },
        { "hiphop.westcoast", "Hip Hop - West Coast", 4.5, 1.0, 1.5, 0, Feel::normal },
        { "hiphop.gfunk", "Hip Hop - G-Funk", 4.5, 0.5, 1.0, 0, Feel::normal },
        { "hiphop.oldschool", "Hip Hop - Old School", 3.0, 0.5, 0.0, 0, Feel::natural },
        { "hiphop.lofi", "Hip Hop - Lo-Fi", 2.5, 0.0, -2.5, 0, Feel::soft },
        { "hiphop.poprap", "Hip Hop - Pop Rap", 3.0, 1.5, 3.0, 0, Feel::normal },
        { "trap.hard808", "Trap & Drill - Hard 808", 6.0, 2.0, 2.5, 0, Feel::normal },
        { "trap.melodic", "Trap & Drill - Melodic", 5.0, 1.5, 2.5, 0, Feel::normal },
        { "trap.drill", "Trap & Drill - Drill", 5.5, 2.5, 0.5, 0, Feel::normal },
        { "trap.rage", "Trap & Drill - Rage", 6.0, 2.5, 3.0, 0, Feel::hard },
        { "trap.plugg", "Trap & Drill - Plugg", 4.5, 1.5, 2.0, 0, Feel::normal },
        { "rnb.smooth", "R&B & Soul - Smooth R&B", 3.5, 1.0, 1.5, 1, Feel::normal },
        { "rnb.90s", "R&B & Soul - 90s R&B", 3.5, 0.5, 0.5, 1, Feel::normal },
        { "rnb.alt", "R&B & Soul - Alternative R&B", 4.0, 1.5, 2.0, 1, Feel::normal },
        { "rnb.neosoul", "R&B & Soul - Neo-Soul", 3.0, 0.0, 0.5, 1, Feel::natural },
        { "rnb.soul", "R&B & Soul - Classic Soul", 1.5, -0.5, 0.0, 1, Feel::natural },
        { "rnb.funk", "R&B & Soul - Funk", 3.0, 0.5, 1.5, 1, Feel::normal },
        { "pop.modern", "Pop - Modern", 3.0, 1.5, 3.0, 2, Feel::normal },
        { "pop.dance", "Pop - Dance", 4.0, 1.5, 3.0, 2, Feel::normal },
        { "pop.synth", "Pop - Synth / 80s", 2.5, 1.0, 2.5, 2, Feel::normal },
        { "pop.indie", "Pop - Indie", 2.0, 0.5, 1.5, 2, Feel::natural },
        { "pop.acoustic", "Pop - Acoustic", 1.5, 0.5, 2.0, 2, Feel::natural },
        { "pop.ballad", "Pop - Ballad", 2.0, 0.5, 2.0, 2, Feel::natural },
        { "pop.kpop", "Pop - K-Pop", 3.5, 1.5, 3.5, 2, Feel::normal },
        { "pop.hyperpop", "Pop - Hyperpop", 4.0, 2.0, 4.0, 2, Feel::hard },
        { "rock.classic", "Rock - Classic", 2.0, 0.0, 0.5, 4, Feel::natural },
        { "rock.hard", "Rock - Hard", 2.5, 1.0, 2.0, 4, Feel::normal },
        { "rock.alternative", "Rock - Alternative", 2.5, 1.0, 1.5, 4, Feel::normal },
        { "rock.indie", "Rock - Indie", 1.5, 0.5, 1.0, 4, Feel::natural },
        { "rock.punk", "Rock - Punk / Pop Punk", 2.0, 1.5, 2.5, 4, Feel::normal },
        { "rock.grunge", "Rock - Grunge", 2.0, 0.0, 0.5, 4, Feel::normal },
        { "rock.metal", "Rock - Metal", 3.0, 2.5, 2.5, 4, Feel::hard },
        { "rock.modernmetal", "Rock - Modern Metal", 3.5, 3.0, 3.0, 4, Feel::hard },
        { "rock.soft", "Rock - Soft / Light", 1.5, 0.0, 1.0, 4, Feel::soft },
        { "rock.blues", "Rock - Blues Rock", 2.0, -0.5, 0.0, 4, Feel::natural },
        { "country.modern", "Country - Modern", 3.0, 1.0, 2.5, 2, Feel::normal },
        { "country.pop", "Country - Country Pop", 3.0, 1.0, 3.0, 2, Feel::normal },
        { "country.classic", "Country - Classic", 1.5, 0.0, 0.5, 5, Feel::normal },
        { "country.southern", "Country - Outlaw / Southern", 2.5, 0.5, 1.0, 4, Feel::normal },
        { "country.bluegrass", "Country - Bluegrass", 0.5, 0.0, 1.5, 5, Feel::normal },
        { "country.americana", "Country - Americana", 1.5, 0.0, 1.0, 5, Feel::normal },
        { "gospel.contemporary", "Gospel - Contemporary", 3.5, 1.0, 2.0, 1, Feel::normal },
        { "gospel.traditional", "Gospel - Traditional", 2.0, 0.0, 1.0, 1, Feel::natural },
        { "gospel.choir", "Gospel - Mass Choir", 1.5, 0.5, 2.0, 1, Feel::natural },
        { "gospel.urban", "Gospel - Urban Gospel", 4.5, 1.5, 2.0, 1, Feel::normal },
        { "worship.modern", "Worship - Modern", 3.0, 1.0, 2.5, 2, Feel::normal },
        { "worship.live", "Worship - Live / Big Room", 3.5, 1.5, 2.0, 2, Feel::normal },
        { "worship.acoustic", "Worship - Acoustic", 1.5, 0.5, 2.0, 5, Feel::normal },
        { "worship.hymns", "Worship - Hymns", 1.0, 0.0, 1.0, 5, Feel::normal },
        { "electronic.house", "Electronic - House", 4.0, 1.5, 2.5, 3, Feel::normal },
        { "electronic.edm", "Electronic - EDM / Big Room", 4.5, 2.0, 3.5, 3, Feel::normal },
        { "electronic.techno", "Electronic - Techno", 4.0, 1.5, 2.0, 3, Feel::normal },
        { "electronic.dnb", "Electronic - Drum & Bass", 4.5, 1.5, 3.0, 3, Feel::normal },
        { "electronic.dubstep", "Electronic - Dubstep", 5.5, 2.0, 3.0, 3, Feel::hard },
        { "electronic.futurebass", "Electronic - Future Bass", 4.5, 2.0, 3.5, 3, Feel::normal },
        { "electronic.ambient", "Electronic - Ambient", 2.0, 0.5, 1.5, 2, Feel::soft },
        { "latin.reggaeton", "Latin - Reggaeton", 5.0, 1.5, 2.5, 2, Feel::normal },
        { "latin.pop", "Latin - Latin Pop", 3.5, 1.0, 3.0, 2, Feel::normal },
        { "latin.salsa", "Latin - Salsa / Tropical", 2.0, 0.0, 2.0, 2, Feel::natural },
        { "latin.bachata", "Latin - Bachata", 2.5, 0.5, 2.0, 2, Feel::natural },
        { "latin.regional", "Latin - Regional Mexican", 2.0, 0.0, 1.5, 2, Feel::natural },
        { "afro.afrobeats", "Afro & Caribbean - Afrobeats", 4.0, 1.0, 2.5, 2, Feel::normal },
        { "afro.amapiano", "Afro & Caribbean - Amapiano", 5.0, 1.0, 2.0, 2, Feel::normal },
        { "afro.reggae", "Afro & Caribbean - Reggae", 4.0, 0.5, 0.5, 2, Feel::natural },
        { "afro.dancehall", "Afro & Caribbean - Dancehall", 4.5, 1.0, 2.0, 2, Feel::normal },
        { "jazz.jazz", "Jazz & Blues - Jazz", 1.0, 0.0, 0.5, 5, Feel::normal },
        { "jazz.smooth", "Jazz & Blues - Smooth Jazz", 2.0, 0.5, 1.5, 5, Feel::normal },
        { "jazz.blues", "Jazz & Blues - Blues", 1.5, -0.5, 0.0, 5, Feel::normal },
        { "folk.songwriter", "Folk & Acoustic - Singer-Songwriter", 1.0, 0.5, 1.5, 5, Feel::normal },
        { "folk.folk", "Folk & Acoustic - Folk", 0.5, 0.0, 1.0, 5, Feel::normal },
        { "folk.indiefolk", "Folk & Acoustic - Indie Folk", 1.0, 0.5, 1.5, 5, Feel::normal },
        { "cinematic.trailer", "Cinematic - Epic Trailer", 4.0, 1.0, 2.0, 3, Feel::normal },
        { "cinematic.orchestral", "Cinematic - Orchestral", 1.0, 0.0, 1.0, 5, Feel::soft },
        { "cinematic.score", "Cinematic - Soft Score", 1.5, 0.5, 1.0, 5, Feel::soft },
    };

    /** The style with this key ("genre.style"), or nullptr. */
    inline const MixStyle* mixStyle (const std::string& key)
    {
        for (const auto& s : kMixStyles)
            if (key == s.key) return &s;
        return nullptr;
    }

    /** The Auto-Master genre (amGenre: 0 Trap / Hip-Hop, 1 R&B, 2 Pop, 3 EDM, 4 Rock, 5 Acoustic) for a Mixology
        mix style ("genre.style", e.g. "hiphop.modern"): the style's own (kMixStyles); for a style this version
        doesn't know, its genre's usual one; -1 when there is none or the genre is unknown. */
    inline int genreForMixStyle (const std::string& styleKey)
    {
        if (const auto* s = mixStyle (styleKey))
            return s->genre;
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

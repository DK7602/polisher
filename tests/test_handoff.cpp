// The suite link: what Masterology makes of Mixology's hand-off (polisher/MixHandoff.h).

#include "polisher/MixHandoff.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE ("Hand-off: Mixology's mix style picks the closest Auto-Master genre", "[handoff]")
{
    using polisher::handoff::genreForMixStyle;
    CHECK (genreForMixStyle ("hiphop.modern") == 0);   // Trap / Hip-Hop
    CHECK (genreForMixStyle ("trap.drill") == 0);
    CHECK (genreForMixStyle ("rnb.modern") == 1);      // R&B
    CHECK (genreForMixStyle ("gospel.contemporary") == 1);
    CHECK (genreForMixStyle ("pop.dance") == 2);       // Pop
    CHECK (genreForMixStyle ("afro.amapiano") == 2);
    CHECK (genreForMixStyle ("electronic.house") == 3);   // EDM
    CHECK (genreForMixStyle ("rock.indie") == 4);
    CHECK (genreForMixStyle ("folk.acoustic") == 5);   // Acoustic
    CHECK (genreForMixStyle ("cinematic.score") == 5);
    CHECK (genreForMixStyle ("") == -1);               // Mixology's window never opened: no style
    CHECK (genreForMixStyle ("polka.classic") == -1);  // a style this version doesn't know
    CHECK (genreForMixStyle ("hiphop") == 0);          // a genre alone
}

TEST_CASE ("Hand-off: the target's name as Mixology shows it", "[handoff]")
{
    using polisher::handoff::targetName;
    CHECK (std::string (targetName (0)) == "Streaming");
    CHECK (std::string (targetName (1)) == "Loud release");
    CHECK (std::string (targetName (2)) == "Club");
}

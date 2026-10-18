// The suite link: what Masterology makes of Mixology's hand-off (polisher/MixHandoff.h).

#include "polisher/AutoMaster.h"
#include "polisher/MixHandoff.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

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
    // By what the style sounds like, not only by its genre.
    CHECK (genreForMixStyle ("cinematic.trailer") == 3);   // huge hits and low end: EDM, not Acoustic
    CHECK (genreForMixStyle ("cinematic.orchestral") == 5);
    CHECK (genreForMixStyle ("country.pop") == 2);         // "pop low end": Pop
    CHECK (genreForMixStyle ("country.modern") == 2);
    CHECK (genreForMixStyle ("country.southern") == 4);    // a rock band
    CHECK (genreForMixStyle ("country.bluegrass") == 5);
    CHECK (genreForMixStyle ("worship.acoustic") == 5);
    CHECK (genreForMixStyle ("electronic.ambient") == 2);  // soft, no club low end
    CHECK (genreForMixStyle ("hiphop.lofi") == 0);         // Hip-Hop, with its own tone and a soft feel (below)
}

TEST_CASE ("Hand-off: Masterology's copy of the suite's styles is the same as plugin/ui/mixstyles.js", "[handoff]")
{
    std::ifstream file (std::string (POLISHER_SOURCE_DIR) + "/plugin/ui/mixstyles.js");
    REQUIRE (file.good());
    std::stringstream text;
    text << file.rdbuf();
    const auto js = text.str();
    // g("hiphop", "Hip Hop", ...) opens a genre; ["modern", "Modern Clean", 4, 1.5, 2, "..."] is one of its styles.
    const std::regex genreRe (R"re(g\("(\w+)", "([^"]+)")re"), styleRe (R"re(\["(\w+)", "([^"]+)", (-?[\d.]+), (-?[\d.]+), (-?[\d.]+), ")re");
    struct Row { std::string key, name; double bass, scoop, air; };
    std::vector<Row> rows;
    std::string genreId, genreName;
    std::istringstream lines (js);
    for (std::string line; std::getline (lines, line);)
    {
        std::smatch m;
        if (std::regex_search (line, m, genreRe)) { genreId = m[1]; genreName = m[2]; continue; }
        if (std::regex_search (line, m, styleRe) && ! genreId.empty())
            rows.push_back ({ genreId + "." + m[1].str(), genreName + " - " + m[2].str(), std::stod (m[3]), std::stod (m[4]), std::stod (m[5]) });
    }
    using polisher::handoff::kMixStyles;
    REQUIRE (rows.size() == 75);
    REQUIRE (std::size (kMixStyles) == rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
    {
        INFO (rows[i].key);
        CHECK (std::string (kMixStyles[i].key) == rows[i].key);
        CHECK (std::string (kMixStyles[i].name) == rows[i].name);
        CHECK (kMixStyles[i].bass == rows[i].bass);
        CHECK (kMixStyles[i].scoop == rows[i].scoop);
        CHECK (kMixStyles[i].air == rows[i].air);
        CHECK (kMixStyles[i].genre >= 0);
        CHECK (kMixStyles[i].genre < polisher::kNumGenres);
    }
}

TEST_CASE ("Song styles move their genre's target curve, a little", "[handoff][genrecurve]")
{
    using namespace polisher;
    // Every style stays within the bounds, and its genre's styles average out to the genre's curve.
    std::array<std::array<double, kEqBands>, kNumGenres> sum {};
    std::array<int, kNumGenres> count {};
    for (const auto& st : handoff::kMixStyles)
    {
        INFO (st.key);
        const auto g = static_cast<Genre> (st.genre);
        const auto off = styleToneOffsetDb (g, st.key);
        double mid = 0.0;
        for (int b = 0; b < kEqBands; ++b)
        {
            CHECK (std::abs (off[static_cast<size_t> (b)]) <= kMaxStyleOffsetDb[static_cast<size_t> (b)] + 1.0e-12);
            sum[static_cast<size_t> (st.genre)][static_cast<size_t> (b)] += off[static_cast<size_t> (b)];
            if (b >= 1 && b <= 5) mid += off[static_cast<size_t> (b)] / 5.0;
        }
        CHECK (std::abs (mid) < 0.05);   // a tilt and a shape, not a level
        // Another genre than the style's: the genre's own curve.
        const auto other = static_cast<Genre> ((st.genre + 1) % kNumGenres);
        CHECK (styleTargetCurve (other, st.key) == genreTargetCurve (other));
        ++count[static_cast<size_t> (st.genre)];
    }
    for (int g = 0; g < kNumGenres; ++g)
        for (int b = 0; b < kEqBands; ++b)
            CHECK (std::abs (sum[static_cast<size_t> (g)][static_cast<size_t> (b)] / count[static_cast<size_t> (g)]) < 0.05);
    // Unknown: nothing moves.
    CHECK (styleTargetCurve (Genre::trapHipHop, "") == genreTargetCurve (Genre::trapHipHop));
    CHECK (styleTargetCurve (Genre::trapHipHop, "polka.classic") == genreTargetCurve (Genre::trapHipHop));
    // Lo-Fi ("soft, warm and dusty") against Rage ("distorted 808s, aggressive top"): less sub and much less air.
    const auto lofi = styleTargetCurve (Genre::trapHipHop, "hiphop.lofi"), rage = styleTargetCurve (Genre::trapHipHop, "trap.rage");
    CHECK (lofi[0] < rage[0] - 2.0);
    CHECK (lofi[6] < rage[6] - 2.5);
    CHECK (lofi[2] > rage[2] + 1.5);   // Rage scoops the low mids
}


TEST_CASE ("Hand-off: the target's name as Mixology shows it", "[handoff]")
{
    using polisher::handoff::targetName;
    CHECK (std::string (targetName (0)) == "Streaming");
    CHECK (std::string (targetName (1)) == "Loud release");
    CHECK (std::string (targetName (2)) == "Club");
}

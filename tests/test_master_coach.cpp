// The Sonicology Rack's Coach on the master (Beta 29): Auto-Master's plan as the Rack reads it (valid JSON in the
// report's room, every module it turns on with its reasons, the tone against the aim before and through the plan,
// measured by rendering the same 12 s), and the analysis resting while the song plays (ScopedPace) deciding the same.

#include <catch2/catch_test_macros.hpp>

#include "Signals.h"
#include "polisher/AutoAnalyze.h"
#include "polisher/AutoMaster.h"
#include "polisher/LoudnessTargeting.h"
#include "polisher/MasterCoach.h"

#include <cctype>
#include <cstdio>
#include <string>

using namespace polisher;

namespace {
/** A small JSON checker: one value, nothing after it. */
struct Checker
{
    const std::string& s;
    std::size_t i = 0;
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r')) ++i; }
    bool lit (const char* w) { const std::string t (w); if (s.compare (i, t.size(), t) != 0) return false; i += t.size(); return true; }
    bool str()
    {
        if (i >= s.size() || s[i] != '"') return false;
        for (++i; i < s.size(); ++i)
        {
            if (s[i] == '\\') { ++i; continue; }
            if (static_cast<unsigned char> (s[i]) < 0x20) return false;
            if (s[i] == '"') { ++i; return true; }
        }
        return false;
    }
    bool number()
    {
        const std::size_t start = i;
        if (i < s.size() && s[i] == '-') ++i;
        while (i < s.size() && (std::isdigit (static_cast<unsigned char> (s[i])) || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) ++i;
        return i > start;
    }
    bool value()
    {
        ws();
        if (i >= s.size()) return false;
        bool ok = false;
        if (s[i] == '{')
        {
            ++i; ws();
            if (i < s.size() && s[i] == '}') { ++i; ok = true; }
            else
                for (;;)
                {
                    ws();
                    if (! str()) return false;
                    ws();
                    if (i >= s.size() || s[i++] != ':' || ! value()) return false;
                    ws();
                    if (i < s.size() && s[i] == ',') { ++i; continue; }
                    if (i < s.size() && s[i] == '}') { ++i; ok = true; }
                    break;
                }
        }
        else if (s[i] == '[')
        {
            ++i; ws();
            if (i < s.size() && s[i] == ']') { ++i; ok = true; }
            else
                for (;;)
                {
                    if (! value()) return false;
                    ws();
                    if (i < s.size() && s[i] == ',') { ++i; continue; }
                    if (i < s.size() && s[i] == ']') { ++i; ok = true; }
                    break;
                }
        }
        else if (s[i] == '"') ok = str();
        else if (s[i] == 't') ok = lit ("true");
        else if (s[i] == 'f') ok = lit ("false");
        else if (s[i] == 'n') ok = lit ("null");
        else ok = number();
        ws();
        return ok;
    }
};
bool validJson (const std::string& s) { Checker c { s }; return c.value() && c.i == s.size(); }
bool has (const std::string& s, const std::string& part) { return s.find (part) != std::string::npos; }
}

TEST_CASE ("Coach (the master): Auto-Master's plan, measured through the chain, as the Rack reads it", "[coach]")
{
    const auto mix = testsig::musicLike (48000.0, 12.0, -18.0);
    AutoMasterSettings s;
    s.target = LoudnessTarget::loud;
    const auto r = autoMaster (mix, s);
    REQUIRE (r.ok);
    const auto after = analyseMix (renderMaster (mix, r.chain));
    REQUIRE (after.usable);
    coach::Heard h;
    h.seconds = 165.0;
    h.songLufs = -19.5;
    h.songLra = 6.0;
    h.wholeSong = true;

    const auto json = coach::planJson (r, &after, s, h, 7600);
    INFO (json);
    CHECK (validJson (json));
    CHECK (json.size() <= 7600);
    CHECK (has (json, "\"ok\":true"));
    CHECK (has (json, "\"kind\":\"master\""));
    CHECK (has (json, "\"key\":\"mas.max\""));
    CHECK (has (json, "\"key\":\"mas.tonaleq\""));
    CHECK (has (json, "\"target\":["));
    CHECK (has (json, "\"after\":["));
    CHECK (has (json, "Listened to 2:45 of the song (the whole song)"));
    CHECK (has (json, "\"name\":\"Loudness (loudest 12 s)\""));
    // The master is louder than the mix it came from (Loud release), and its peaks stay under the ceiling.
    CHECK (after.loudness.integratedLufs > r.analysis.loudness.integratedLufs + 3.0);
    CHECK (after.loudness.truePeakDbtp <= r.chain.limiter.ceilingDbtp + 0.2);

    for (const std::size_t room : { std::size_t { 3000 }, std::size_t { 1200 }, std::size_t { 400 } })
    {
        INFO (room);
        const auto small = coach::planJson (r, &after, s, h, room);
        CHECK (validJson (small));
        CHECK (small.size() <= room);
    }

    // Silence: why, as the Rack shows it.
    const auto none = autoMaster (testsig::silence (48000.0, 2, 12.0), {});
    const auto fj = coach::planJson (none, nullptr, {}, {}, 7600);
    CHECK (validJson (fj));
    CHECK (has (fj, "\"ok\":false"));

    // Compared with the settings now (Beta 30): nothing to change, or one module that isn't set as planned.
    const std::vector<std::string> unchanged;
    const auto same = coach::planJson (r, &after, s, h, 7600, &unchanged);
    CHECK (validJson (same));
    CHECK (has (same, "\"upToDate\":true"));
    CHECK (has (same, "\"changes\":0"));
    CHECK (has (same, "nothing to change"));
    CHECK_FALSE (has (same, "\"same\":false"));
    const std::vector<std::string> drive { "drive", "ceiling" };
    const auto one = coach::planJson (r, &after, s, h, 7600, &drive);
    CHECK (has (one, "\"upToDate\":false"));
    CHECK (has (one, "\"changes\":2"));
    CHECK (has (one, "\"key\":\"mas.max\",\"name\":\"Maximizer\",\"on\":true,\"caption\":\"Loudness\",\"kept\":false,\"same\":false"));
    CHECK (std::string (coach::rackKeyOfParam ("eqGain3")) == "mas.tonaleq");
    CHECK (std::string (coach::rackKeyOfParam ("dynThr2")) == "mas.dynamics");
    CHECK (std::string (coach::rackKeyOfParam ("inTrim")) == "mas.max");
    CHECK (std::string (coach::rackKeyOfParam ("bypass")).empty());

    CHECK (std::string (coach::moduleKey ("eq")) == "mas.tonaleq");
    CHECK (std::string (coach::moduleKey ("clip")) == "mas.clipper");
    CHECK (std::string (coach::moduleKey ("nothing")).empty());
}

TEST_CASE ("Coach (the master): resting between stretches of work decides exactly the same", "[coach]")
{
    const auto mix = testsig::musicLike (48000.0, 12.0, -16.0, 3);
    AutoMasterSettings s;
    const auto plain = autoMaster (mix, s);
    double rested = 0.0;
    AutoMasterResult paced;
    {
        const ScopedPace gentle (20.0, 8.0);
        paced = autoMaster (mix, s);
        rested = ScopedPace::restedMs();
        INFO ("longest stretch between rests " << ScopedPace::longestStretchMs() << " ms");
    }
    REQUIRE (plain.ok);
    REQUIRE (paced.ok);
    CHECK (paced.summary == plain.summary);
    CHECK (autoanalyze::chainValues (paced.chain) == autoanalyze::chainValues (plain.chain));
    CHECK (paced.output.integratedLufs == plain.output.integratedLufs);
    CHECK (rested > 0.0);
}

TEST_CASE ("Coach (the master): print a plan (manual check)", "[coach][.print]")
{
    const auto mix = testsig::musicLike (48000.0, 12.0, -18.0);
    AutoMasterSettings s;
    s.target = LoudnessTarget::loud;
    const auto r = autoMaster (mix, s);
    const auto after = analyseMix (renderMaster (mix, r.chain));
    coach::Heard h;
    h.seconds = 165.0;
    h.songLufs = -19.5;
    h.songLra = 6.0;
    h.wholeSong = true;
    std::printf ("%s\n", coach::planJson (r, &after, s, h, 7300).c_str());
}

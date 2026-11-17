// The Sonicology Rack link (suite/RackLink.h, the same file in all three repositories): what each plug-in publishes,
// what they do together, and the JSON the Rack view reads. THIS FILE IS THE SAME IN ALL THREE REPOSITORIES.

#include "suite/RackLink.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <clocale>
#include <cmath>
#include <limits>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#if defined(_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <fcntl.h>
 #include <sys/mman.h>
 #include <unistd.h>
#endif
#include <vector>

namespace
{
    struct Memory
    {
        std::unique_ptr<unsigned char[]> bytes { new unsigned char[rack::Link::bytes() + 64] () };
        void* data() { return bytes.get() + (64 - reinterpret_cast<std::uintptr_t> (bytes.get()) % 64) % 64; }
    };

    rack::Module mod (const char* key, rack::State state = rack::State::on)
    {
        rack::Module m;
        m.key = key;
        m.state = state;
        return m;
    }

    rack::Unit unit (rack::Product p, rack::Role r, const std::string& track, std::vector<rack::Module> mods = {})
    {
        rack::Unit u;
        u.product = p;
        u.role = r;
        u.track = track;
        u.hash = track.empty() ? 0u : suite::nameHash (track);
        u.version = suite::packVersion (1, 2, 3);
        u.modules = std::move (mods);
        return u;
    }

    bool has (const std::vector<rack::ProjectLine>& lines, rack::Level level, const std::string& words)
    {
        for (const auto& l : lines)
            if (l.level == level && l.text.find (words) != std::string::npos) return true;
        return false;
    }

    // Well-formed JSON for what rack.js needs: brackets balance outside strings, strings closed, no bare control
    // characters, no "nan" / "inf" numbers.
    bool wellFormed (const std::string& j)
    {
        int depth = 0;
        bool inString = false, escape = false;
        for (std::size_t i = 0; i < j.size(); ++i)
        {
            const char c = j[i];
            if (inString)
            {
                if (static_cast<unsigned char> (c) < 0x20) return false;
                if (escape) escape = false;
                else if (c == '\\') escape = true;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') inString = true;
            else if (c == '{' || c == '[') ++depth;
            else if (c == '}' || c == ']') { if (--depth < 0) return false; }
            else if (c == 'n' && j.compare (i, 3, "nan") == 0) return false;
            else if (c == 'i' && j.compare (i, 3, "inf") == 0) return false;
        }
        return depth == 0 && ! inString;
    }
}

TEST_CASE ("Rack link: a plug-in's unit reaches the Rack whole, from another library", "[rack]")
{
    Memory mem;
    rack::Link vox (mem.data()), view (mem.data());   // two libraries on the same memory
    const int slot = vox.claim (11, 100);
    REQUIRE (slot >= 0);
    CHECK (view.units (100).empty());   // claimed, nothing published yet (version 0)

    auto u = unit (rack::Product::voxology, rack::Role::vocal, "Lead Vox");
    u.inDb = -16.5f;
    u.outDb = -12.0f;
    u.latencyMs = 55.0f;
    u.detailed = true;
    auto eq = mod ("vox.eq");
    eq.summary = "Mud −3 dB · Presence +2.5 dB";
    eq.by = "Auto-Edit";
    eq.values = { { "Mud", "−3.0 dB", 0.33f, true }, { "Presence", "+2.5 dB", 0.64f, true, "eqGain3" },
                  { "Mode", "Natural", 0.5f, false, "ptMode", 3 } };
    eq.switchable = true;
    eq.bands = { { rack::BandType::bell, 320.0f, -3.0f, 1.1f, "Mud", true }, { rack::BandType::highShelf, 10000.0f, 2.0f, 0.7f, "Air", true } };
    auto comp = mod ("vox.comp");
    comp.meterKind = rack::MeterKind::gr;
    comp.meter = -3.5f;
    auto pitch = mod ("vox.pitch", rack::State::rest);
    u.modules = { pitch, eq, comp };
    u.lines = { { rack::Level::ok, "Pitch rests: Honey Tune tunes this track" } };
    u.spectrum.assign (rack::kSpectrumBins, -40.0f);
    u.spectrum[10] = -12.0f;
    u.lock = "Auto-Edit is running";
    CHECK (vox.publish (slot, 11, u, 200));
    CHECK_FALSE (vox.publish (slot, 99, u, 200));   // someone else's instance id: ignored

    const auto seen = view.units (300);
    REQUIRE (seen.size() == 1);
    const auto& s = seen.front();
    CHECK (s.product == rack::Product::voxology);
    CHECK (s.role == rack::Role::vocal);
    CHECK (s.track == "Lead Vox");
    CHECK (s.hash == suite::nameHash ("Lead Vox"));
    CHECK (s.version == suite::packVersion (1, 2, 3));
    CHECK (s.inDb == Catch::Approx (-16.5f));
    CHECK (s.outDb == Catch::Approx (-12.0f));
    CHECK (s.latencyMs == Catch::Approx (55.0f));
    CHECK (s.detailed);
    CHECK (s.slot == slot);
    CHECK (s.generation == 1);
    CHECK (s.instanceId == 11);
    CHECK (s.lock == "Auto-Edit is running");
    REQUIRE (s.modules.size() == 3);
    CHECK (s.modules[0].key == "vox.pitch");
    CHECK (s.modules[0].state == rack::State::rest);
    CHECK (s.modules[1].summary == "Mud −3 dB · Presence +2.5 dB");   // UTF-8 kept
    CHECK (s.modules[1].by == "Auto-Edit");
    REQUIRE (s.modules[1].values.size() == 3);
    CHECK (s.modules[1].switchable);
    CHECK_FALSE (s.modules[0].switchable);
    CHECK (s.modules[1].values[0].key.empty());          // shown only
    CHECK (s.modules[1].values[0].steps == 0);
    CHECK (s.modules[1].values[1].key == "eqGain3");
    CHECK (s.modules[1].values[2].key == "ptMode");
    CHECK (s.modules[1].values[2].steps == 3);
    CHECK (s.modules[1].values[1].name == "Presence");
    CHECK (s.modules[1].values[1].text == "+2.5 dB");
    CHECK (s.modules[1].values[1].norm == Catch::Approx (0.64f));
    CHECK (s.modules[1].values[1].bi);
    REQUIRE (s.modules[1].bands.size() == 2);
    CHECK (s.modules[1].bands[0].type == rack::BandType::bell);
    CHECK (s.modules[1].bands[0].hz == Catch::Approx (320.0f));
    CHECK (s.modules[1].bands[0].db == Catch::Approx (-3.0f));
    CHECK (s.modules[1].bands[0].name == "Mud");
    CHECK (s.modules[1].bands[1].type == rack::BandType::highShelf);
    CHECK (s.modules[2].meterKind == rack::MeterKind::gr);
    CHECK (s.modules[2].meter == Catch::Approx (-3.5f));
    REQUIRE (s.lines.size() == 1);
    CHECK (s.lines[0].text == "Pitch rests: Honey Tune tunes this track");
    REQUIRE (s.spectrum.size() == static_cast<std::size_t> (rack::kSpectrumBins));
    CHECK (s.spectrum[10] == Catch::Approx (-12.0f));

    // A smaller unit after a bigger one: nothing of the bigger one is left over.
    auto fewer = unit (rack::Product::voxology, rack::Role::vocal, "Lead Vox", { mod ("vox.output") });
    CHECK (vox.publish (slot, 11, fewer, 400));
    const auto again = view.units (400);
    REQUIRE (again.size() == 1);
    CHECK (again[0].modules.size() == 1);
    CHECK (again[0].modules[0].values.empty());
    CHECK (again[0].lines.empty());
    CHECK (again[0].spectrum.empty());
}

TEST_CASE ("Rack link: slots come and go like the suite link's", "[rack]")
{
    Memory mem;
    rack::Link link (mem.data());
    const int a = link.claim (1, 100), b = link.claim (2, 100);
    REQUIRE (a >= 0);
    REQUIRE (b >= 0);
    CHECK (a != b);
    REQUIRE (link.publish (a, 1, unit (rack::Product::link, rack::Role::track, "Kick"), 100));
    REQUIRE (link.publish (b, 2, unit (rack::Product::mixology, rack::Role::fullmix, ""), 100));
    CHECK (link.units (200).size() == 2);

    // Released: gone at once.
    link.release (b, 2);
    CHECK (link.units (200).size() == 1);
    link.release (a, 999);   // not its owner: nothing happens
    CHECK (link.units (200).size() == 1);

    // Silent too long (a crashed plug-in): not listed, and its slot can be taken over; the old owner finds out.
    CHECK (link.units (100 + rack::kAliveMs + 1).empty());
    const int c = link.claim (3, 100 + rack::kAliveMs + 1);
    const int d = link.claim (4, 100 + rack::kAliveMs + 1);
    CHECK ((c == a || d == a));
    CHECK_FALSE (link.publish (a, 1, unit (rack::Product::link, rack::Role::track, "Kick"), 3000));

    // Full: -1.
    Memory mem2;
    rack::Link full (mem2.data());
    for (int i = 0; i < rack::kMaxSlots; ++i) REQUIRE (full.claim (static_cast<std::uint64_t> (100 + i), 10) >= 0);
    CHECK (full.claim (9999, 10) == -1);
}

TEST_CASE ("Rack link: limits, cut texts and numbers that aren't finite", "[rack]")
{
    Memory mem;
    rack::Link link (mem.data());
    const int slot = link.claim (7, 10);
    auto u = unit (rack::Product::link, rack::Role::track, std::string (300, 'x'));
    for (int i = 0; i < rack::kMaxModules + 5; ++i)
    {
        auto m = mod ("mix.eq");
        for (int v = 0; v < rack::kMaxValues + 3; ++v)
            m.values.push_back ({ "Gain", "+1 dB", std::numeric_limits<float>::quiet_NaN(), false, std::string (40, 'k'), v == 0 ? 1000 : v == 1 ? 1 : -5 });
        for (int b = 0; b < rack::kMaxBands + 3; ++b) m.bands.push_back ({ rack::BandType::bell, std::numeric_limits<float>::infinity(), 99.0f, 0.0f, "", true });
        m.meter = std::numeric_limits<float>::quiet_NaN();
        u.modules.push_back (m);
    }
    for (int i = 0; i < rack::kMaxLines + 2; ++i) u.lines.push_back ({ rack::Level::info, "line" });
    u.inDb = std::numeric_limits<float>::infinity();
    // The track name cut at a UTF-8 boundary (a 2-byte character across the cut).
    u.track = std::string (rack::kTrackBytes - 2, 'a') + "é";
    REQUIRE (link.publish (slot, 7, u, 10));
    const auto seen = link.units (10);
    REQUIRE (seen.size() == 1);
    const auto& s = seen[0];
    CHECK (s.track == std::string (rack::kTrackBytes - 2, 'a'));
    CHECK (s.modules.size() == static_cast<std::size_t> (rack::kMaxModules));
    CHECK (s.modules[0].values.size() == static_cast<std::size_t> (rack::kMaxValues));
    CHECK (s.modules[0].bands.size() == static_cast<std::size_t> (rack::kMaxBands));
    CHECK (s.lines.size() == static_cast<std::size_t> (rack::kMaxLines));
    CHECK (std::isfinite (s.inDb));
    CHECK (std::isfinite (s.modules[0].meter));
    CHECK (s.modules[0].values[0].norm == Catch::Approx (0.5f));
    CHECK (s.modules[0].values[0].key == std::string (rack::kControlBytes - 1, 'k'));
    CHECK (s.modules[0].values[0].steps == rack::kMaxSteps);   // too many: the most
    CHECK (s.modules[0].values[1].steps == 0);                  // one step isn't a choice
    CHECK (s.modules[0].values[2].steps == 0);
    CHECK (s.modules[0].bands[0].hz == Catch::Approx (1000.0f));
    CHECK (s.modules[0].bands[0].db == Catch::Approx (48.0f));
    CHECK (s.modules[0].bands[0].q == Catch::Approx (0.1f));
}

TEST_CASE ("Rack link: the plug-ins publish details only while a Rack view is open", "[rack]")
{
    Memory mem;
    rack::Link link (mem.data());
    CHECK_FALSE (link.watched (1000));
    link.view (1000);
    CHECK (link.watched (1000));
    CHECK (link.watched (1000 + rack::kViewerMs));
    CHECK_FALSE (link.watched (1000 + rack::kViewerMs + 1));
}

TEST_CASE ("Rack link: a plug-in's publisher (taking part, how often, details while watched)", "[rack]")
{
    using Due = rack::Publisher::Due;
    Memory mem;
    rack::Link link (mem.data());
    std::uint64_t t = 1000;
    {
        rack::Publisher pub (link);
        CHECK (pub.due (t) == Due::none);           // not playing (prepareToPlay not called): not in the Rack
        CHECK (link.units (t).empty());
        pub.start();
        REQUIRE (pub.due (t) == Due::brief);        // joined: published at once, briefly (no Rack open)
        pub.publish (unit (rack::Product::link, rack::Role::track, "Kick", { mod ("mix.eq") }), t);
        auto seen = link.units (t);
        REQUIRE (seen.size() == 1);
        CHECK_FALSE (seen[0].detailed);
        CHECK (pub.due (t + 100) == Due::none);     // not again yet
        CHECK (pub.due (t + rack::Publisher::kBriefMs) == Due::brief);   // the heartbeat
        pub.publish (unit (rack::Product::link, rack::Role::track, "Kick"), t + rack::Publisher::kBriefMs);
        t += rack::Publisher::kBriefMs;

        link.view (t + 10);                         // a Rack opens: full details at once, then about 5 a second
        REQUIRE (pub.due (t + 10) == Due::full);
        pub.publish (unit (rack::Product::link, rack::Role::track, "Kick"), t + 10);
        seen = link.units (t + 10);
        REQUIRE (seen.size() == 1);
        CHECK (seen[0].detailed);
        CHECK (pub.due (t + 100) == Due::none);
        link.view (t + 200);
        CHECK (pub.due (t + 10 + rack::Publisher::kFullMs) == Due::full);
        pub.publish (unit (rack::Product::link, rack::Role::track, "Kick"), t + 10 + rack::Publisher::kFullMs);

        // A due() not followed by publish() publishes nothing; publish() without a due() does nothing.
        t += 10 + rack::Publisher::kFullMs;
        pub.publish (unit (rack::Product::link, rack::Role::track, "Snare"), t + 1);
        CHECK (link.units (t + 1)[0].track == "Kick");

        pub.stop();                                 // the host stopped it (another project plays): out of the Rack
        CHECK (pub.due (t + 2) == Due::none);
        CHECK (link.units (t + 2).empty());
        pub.start();
        CHECK (pub.due (t + 3) != Due::none);       // back
        pub.publish (unit (rack::Product::link, rack::Role::track, "Kick"), t + 3);
        CHECK (link.units (t + 3).size() == 1);
    }
    CHECK (link.units (t + 4).empty());             // destroyed: released

    // Taken over after a long silence: it claims a new slot by itself.
    rack::Publisher a (link);
    a.start();
    REQUIRE (a.due (5000) != Due::none);
    a.publish (unit (rack::Product::link, rack::Role::track, "Bass"), 5000);
    const int first = a.slot();
    rack::Link other (mem.data());
    const int stolen = other.claim (777, 5000 + rack::kAliveMs + 1);
    CHECK (stolen == first);
    REQUIRE (a.due (5000 + rack::kAliveMs + 2) != Due::none);
    a.publish (unit (rack::Product::link, rack::Role::track, "Bass"), 5000 + rack::kAliveMs + 2);
    CHECK (a.slot() == -1);                          // found out
    REQUIRE (a.due (5000 + rack::kAliveMs + 3) != Due::none);
    CHECK (a.slot() >= 0);
    CHECK (a.slot() != first);
}

TEST_CASE ("Rack link: what the plug-ins do together", "[rack]")
{
    using rack::Product;
    using rack::Role;
    using rack::State;
    using rack::Level;

    SECTION ("a vocal: Voxology compresses and adds reverb, the Link adds none")
    {
        auto lines = rack::together ({ unit (Product::voxology, Role::vocal, "Lead Vox", { mod ("vox.comp"), mod ("vox.reverb") }),
                                       unit (Product::link, Role::track, "Lead Vox", { mod ("mix.comp", State::off), mod ("mix.space", State::off) }),
                                       unit (Product::mixology, Role::fullmix, "") });
        CHECK (has (lines, Level::ok, "Voxology compresses this vocal"));
        CHECK (has (lines, Level::ok, "Voxology adds the reverb here"));
        for (const auto& l : lines) CHECK (l.level != Level::warn);
        for (const auto& l : lines) CHECK (l.hash == suite::nameHash ("Lead Vox"));
    }
    SECTION ("two compressors, two reverbs, two delays")
    {
        auto space = mod ("mix.space");
        space.layers = rack::SpaceLayer::hall | rack::SpaceLayer::delay;
        auto lines = rack::together ({ unit (Product::voxology, Role::vocal, "Chorus Vox", { mod ("vox.comp"), mod ("vox.reverb"), mod ("vox.delay") }),
                                       unit (Product::link, Role::track, "Chorus Vox", { mod ("mix.comp"), space }) });
        CHECK (has (lines, Level::warn, "Two compressors"));
        CHECK (has (lines, Level::warn, "Two reverbs"));
        CHECK (has (lines, Level::warn, "(Hall)"));
        CHECK (has (lines, Level::warn, "Two delays"));
    }
    SECTION ("a vocal Mixology can't hear (only when Mixology is in the project)")
    {
        const auto vox = unit (Product::voxology, Role::vocal, "Ad-Libs", { mod ("vox.comp") });
        CHECK_FALSE (has (rack::together ({ vox }), Level::warn, "can't hear"));
        CHECK (has (rack::together ({ vox, unit (Product::mixology, Role::fullmix, "") }), Level::warn, "Mixology can't hear 'Ad-Libs'"));
    }
    SECTION ("Honey Tune and Voxology's Pitch: one tuner, or two")
    {
        const auto ht = unit (Product::honeytune, Role::vocal, "Chorus Vox", { mod ("ht.tune") });
        CHECK (has (rack::together ({ ht, unit (Product::voxology, Role::vocal, "Chorus Vox", { mod ("vox.pitch", State::rest) }) }), Level::ok, "Pitch rests"));
        CHECK (has (rack::together ({ ht, unit (Product::voxology, Role::vocal, "Chorus Vox", { mod ("vox.pitch") }) }), Level::warn, "Two tuners"));
        CHECK_FALSE (has (rack::together ({ ht, unit (Product::voxology, Role::vocal, "Chorus Vox", { mod ("vox.pitch", State::off) }) }), Level::warn, "Two tuners"));
    }
    SECTION ("tracks with the same name: said once, nothing else guessed for them")
    {
        auto lines = rack::together ({ unit (Product::link, Role::track, "Audio 01", { mod ("mix.comp") }),
                                       unit (Product::link, Role::track, "Audio 01", { mod ("mix.comp") }),
                                       unit (Product::mixology, Role::fullmix, "") });
        REQUIRE (lines.size() == 1);
        CHECK (has (lines, Level::warn, "2 tracks are named 'Audio 01'"));
    }
    SECTION ("the Beat")
    {
        CHECK (has (rack::together ({ unit (Product::voxology, Role::beat, "Beat", { mod ("vox.beat") }) }), Level::ok, "Beat mode"));
    }
    SECTION ("the Full Mix and the Master")
    {
        const auto mas = unit (Product::masterology, Role::master, "");
        auto lines = rack::together ({ mas, mas });
        CHECK (has (lines, Level::warn, "2 Masterologys"));
        CHECK (has (lines, Level::info, "No Mixology on the master channel"));
        auto fm = unit (Product::mixology, Role::fullmix, "", { mod ("mix.limit") });
        lines = rack::together ({ fm, mas });
        CHECK (has (lines, Level::warn, "Mixology's Limiter is on the Full Mix"));
        CHECK_FALSE (has (lines, Level::info, "No Mixology on the master channel"));
        CHECK_FALSE (has (rack::together ({ fm }), Level::warn, "Limiter"));   // no Masterology: its own limiter is fine
        bool scoped = false;
        for (const auto& l : rack::together ({ fm, mas }))
            if (l.text.find ("Limiter") != std::string::npos) scoped = l.scope == rack::ProjectLine::Scope::fullmix;
        CHECK (scoped);
        CHECK (has (rack::together ({ unit (Product::link, Role::track, "Kick") }), Level::info, "no Mixology"));
    }
}

TEST_CASE ("Rack link: the JSON the Rack view reads", "[rack]")
{
    auto vox = unit (rack::Product::voxology, rack::Role::vocal, "Lead \"Vox\"\\1\n");
    vox.slot = 4;
    vox.generation = 7;
    vox.lock = "Auto-Edit is running";
    auto eq = mod ("vox.eq");
    eq.summary = "Mud −3 dB";
    eq.values = { { "Mud", "−3.0 dB", 0.33f, true }, { "Mode", "Natural", 0.5f, false, "ptMode", 3 } };
    eq.switchable = true;
    eq.bands = { { rack::BandType::lowCut, 80.0f, 0.0f, 0.7f, "", true } };
    auto space = mod ("mix.space");
    space.layers = rack::SpaceLayer::chorus | rack::SpaceLayer::hall;
    vox.modules = { eq, space, mod ("vox.pitch", rack::State::rest) };
    vox.lines = { { rack::Level::info, "a line" } };
    vox.spectrum.assign (rack::kSpectrumBins, -50.0f);
    vox.spectrum[5] = -20.0f;
    vox.spectrum[6] = std::numeric_limits<float>::quiet_NaN();
    vox.inDb = std::numeric_limits<float>::infinity();

    rack::Overview ov;
    ov.mixKnown = true;
    ov.lufsI = -14.25f;
    ov.lufsS = -13.0f;
    ov.truePeak = -1.04f;
    ov.source = "after Masterology";
    ov.masterKnown = true;
    ov.goal = "Spotify / Apple Music";

    const std::vector<rack::ProjectLine> lines = { { rack::ProjectLine::Scope::track, 77, rack::Level::warn, "w" },
                                                   { rack::ProjectLine::Scope::master, 0, rack::Level::info, "m" },
                                                   { rack::ProjectLine::Scope::project, 0, rack::Level::ok, "p" } };
    // A host locale with a decimal comma must not change the numbers.
    std::setlocale (LC_NUMERIC, "de_DE.UTF-8");
    const auto j = rack::toJson ("voxology", 42, { vox }, lines, ov);
    std::setlocale (LC_NUMERIC, "C");

    CHECK (wellFormed (j));
    CHECK (j.find ("\"host\":\"voxology\"") != std::string::npos);
    CHECK (j.find ("\"hostHash\":42") != std::string::npos);
    CHECK (j.find ("\"id\":\"u4\",\"gen\":7,\"lock\":\"Auto-Edit is running\"") != std::string::npos);
    CHECK (j.find ("\"product\":\"voxology\"") != std::string::npos);
    CHECK (j.find ("\"version\":\"1.2.3\"") != std::string::npos);
    CHECK (j.find ("\"track\":\"Lead \\\"Vox\\\"\\\\1\\n\"") != std::string::npos);   // escaped
    CHECK (j.find ("\"role\":\"vocal\"") != std::string::npos);
    CHECK (j.find ("\"state\":\"rest\"") != std::string::npos);
    CHECK (j.find ("\"summary\":\"Mud −3 dB\"") != std::string::npos);
    CHECK (j.find ("{\"name\":\"Mud\",\"text\":\"−3.0 dB\",\"norm\":0.330,\"bi\":true,\"key\":\"\",\"steps\":0}") != std::string::npos);
    CHECK (j.find ("{\"name\":\"Mode\",\"text\":\"Natural\",\"norm\":0.500,\"bi\":false,\"key\":\"ptMode\",\"steps\":3}") != std::string::npos);
    CHECK (j.find ("\"by\":\"\",\"switchable\":true") != std::string::npos);
    CHECK (j.find ("\"switchable\":false") != std::string::npos);
    CHECK (j.find ("{\"type\":\"lowCut\",\"hz\":80.0,\"db\":0.00,\"q\":0.700,\"name\":\"\",\"on\":true}") != std::string::npos);
    CHECK (j.find ("\"layers\":[\"chorus\",\"hall\"]") != std::string::npos);
    CHECK (j.find ("\"inDb\":0.0") != std::string::npos);                  // infinity: 0
    CHECK (j.find ("\"spectrum\":[") != std::string::npos);               // on the module with bands only
    CHECK (j.find ("3.0") != std::string::npos);                          // the loudest bin at +3 dB
    CHECK (j.find ("\"together\":[{\"level\":\"info\",\"text\":\"a line\"}]") != std::string::npos);
    CHECK (j.find ("{\"hash\":77,\"level\":\"warn\",\"text\":\"w\"}") != std::string::npos);
    CHECK (j.find ("{\"hash\":\"master\",\"level\":\"info\",\"text\":\"m\"}") != std::string::npos);
    CHECK (j.find ("{\"level\":\"ok\",\"text\":\"p\"}") != std::string::npos);
    CHECK (j.find ("\"lufsI\":-14.3") != std::string::npos);             // one decimal, rounded
    CHECK (j.find ("\"tp\":-1.0") != std::string::npos);
    CHECK (j.find ("\"goal\":\"Spotify / Apple Music\",\"lufs\":-14.0,\"ceiling\":-1.0") != std::string::npos);

    // Nothing known: empty objects, still well formed.
    const auto empty = rack::toJson ("mixology", 0, {}, {}, rack::Overview {});
    CHECK (wellFormed (empty));
    CHECK (empty == "{\"host\":\"mixology\",\"hostHash\":0,\"units\":[],\"together\":[],\"mix\":{},\"master\":{},\"otherLayouts\":[]}");
    CHECK (rack::toJson ("mixology", 0, {}, {}, rack::Overview {}, { 2 }).find ("\"otherLayouts\":[2]}") != std::string::npos);
}

TEST_CASE ("Rack link: a view's JSON - the heartbeat, the units, the loudness from the suite link", "[rack]")
{
    auto rackMem = std::make_unique<unsigned char[]> (rack::Link::bytes() + 64);
    auto suiteMem = std::make_unique<unsigned char[]> (suite::Link::bytes() + 64);
    auto align = [] (unsigned char* p) { return static_cast<void*> (p + (64 - reinterpret_cast<std::uintptr_t> (p) % 64) % 64); };
    rack::Link rk (align (rackMem.get()));
    suite::Link sl (align (suiteMem.get()));
    const std::uint64_t t = 1000000;

    rack::Unit vox;
    vox.product = rack::Product::voxology;
    vox.role = rack::Role::vocal;
    vox.track = "Lead Vox";
    vox.version = suite::packVersion (0, 55, 0);
    const int a = rk.claim (7, t);
    REQUIRE (rk.publish (a, 7, vox, t));
    CHECK_FALSE (rk.watched (t));

    auto json = rack::viewJson (rk, sl, "voxology", suite::nameHash ("Lead Vox"), t);
    CHECK (rk.watched (t + 10));   // the view looked: the plug-ins publish their details now
    CHECK (json.find ("\"host\":\"voxology\"") != std::string::npos);
    CHECK (json.find ("\"track\":\"Lead Vox\"") != std::string::npos);
    CHECK (json.find ("\"mix\":{}") != std::string::npos);      // no Mixology, no Masterology: nothing measured
    CHECK (json.find ("\"master\":{}") != std::string::npos);

    suite::MixInfo mix;
    mix.integratedLufs = -12.5f;
    mix.shortTermLufs = -11.0f;
    mix.peakDbtp = -1.2f;
    mix.version = suite::packVersion (0, 26, 0);
    sl.publishMix (mix, t);
    const auto ov = rack::overview (sl, t + 100);
    CHECK (ov.mixKnown);
    CHECK (ov.lufsI == -12.5f);
    CHECK (ov.source == "the mix");
    CHECK_FALSE (ov.masterKnown);
    json = rack::viewJson (rk, sl, "mixology", 0, t + 100);
    CHECK (json.find ("\"lufsI\":-12.5") != std::string::npos);
}

TEST_CASE ("Rack link: a view's JSON carries details only for the track it shows", "[rack]")
{
    auto rackMem = std::make_unique<unsigned char[]> (rack::Link::bytes() + 64);
    auto suiteMem = std::make_unique<unsigned char[]> (suite::Link::bytes() + 64);
    auto align = [] (unsigned char* p) { return static_cast<void*> (p + (64 - reinterpret_cast<std::uintptr_t> (p) % 64) % 64); };
    rack::Link rk (align (rackMem.get()));
    suite::Link sl (align (suiteMem.get()));
    const std::uint64_t t = 2000000;
    auto detailed = [] (const char* track, rack::Role role) {
        rack::Unit u;
        u.product = role == rack::Role::master ? rack::Product::masterology : rack::Product::link;
        u.role = role;
        u.track = track;
        u.version = suite::packVersion (0, 26, 0);
        u.detailed = true;
        rack::Module m;
        m.key = "mix.comp";
        m.state = rack::State::on;
        m.summary = "Ratio 4.0:1";
        m.values = { { "Ratio", "4.0:1", 0.4f, false } };
        u.modules = { m };
        return u;
    };
    const int a = rk.claim (1, t), b = rk.claim (2, t), c = rk.claim (3, t);
    REQUIRE (rk.publish (a, 1, detailed ("Kick", rack::Role::track), t));
    REQUIRE (rk.publish (b, 2, detailed ("Snare", rack::Role::track), t));
    REQUIRE (rk.publish (c, 3, detailed ("", rack::Role::master), t));
    const auto kick = "t" + std::to_string (suite::nameHash ("Kick"));

    auto json = rack::viewJson (rk, sl, "mixology", 0, t + 5, kick);
    CHECK (json.find ("\"Ratio\"") != std::string::npos);                          // Kick's knob
    CHECK (json.find ("\"Ratio\"") == json.rfind ("\"Ratio\""));                   // ... and nobody else's
    CHECK (json.find ("\"summary\":\"Ratio 4.0:1\"") != json.rfind ("\"summary\":\"Ratio 4.0:1\""));   // every summary stays

    json = rack::viewJson (rk, sl, "mixology", 0, t + 5, "master");
    CHECK (json.find ("\"Ratio\"") == json.rfind ("\"Ratio\""));
    json = rack::viewJson (rk, sl, "mixology", 0, t + 5);                          // no track named: everyone's
    CHECK (json.find ("\"Ratio\"") != json.rfind ("\"Ratio\""));
}


// ---- stage 2: requests from a Rack view to the plug-in that owns a module ---------------------------------------

namespace
{
    rack::Request knob (const char* module, const char* control, float value, rack::Gesture g = rack::Gesture::set)
    {
        rack::Request r;
        r.change = rack::Change::value;
        r.module = module;
        r.control = control;
        r.value = value;
        r.gesture = g;
        return r;
    }

    rack::Request power (const char* module, bool on)
    {
        rack::Request r;
        r.change = rack::Change::state;
        r.module = module;
        r.value = on ? 1.0f : 0.0f;
        return r;
    }

    // A plug-in that owns a unit: a publisher, and what its apply() was asked to do.
    struct Owner
    {
        explicit Owner (rack::Link& l, const char* track = "Lead Vox") : pub (l), name (track)
        {
            pub.start();
        }
        bool publish (std::uint64_t now, const std::string& lock = {})
        {
            if (pub.due (now) == rack::Publisher::Due::none) return false;
            auto u = unit (rack::Product::voxology, rack::Role::vocal, name, { mod ("vox.comp"), mod ("vox.pitch") });
            u.lock = lock;
            pub.publish (u, now);
            return true;
        }
        int serve (std::uint64_t now, const std::string& lock = {})
        {
            return pub.serve (now, lock, [this] (const rack::Request& r) -> std::string {
                seen.push_back (r);
                if (r.module != "vox.comp" && r.module != "vox.pitch") return "This plug-in has no " + r.module + ".";
                if (r.change == rack::Change::value && r.control == "speed") return "Pitch has no Speed knob.";
                return {};
            });
        }
        rack::Publisher pub;
        std::string name;
        std::vector<rack::Request> seen;
    };
}

TEST_CASE ("Rack requests: a click in one plug-in's Rack reaches the plug-in that owns the module", "[rack][requests]")
{
    Memory mem;
    rack::Link ownerSide (mem.data()), viewSide (mem.data());   // two libraries on the same memory
    Owner vox (ownerSide);
    REQUIRE (vox.publish (1000));
    const auto units = viewSide.units (1000);
    REQUIRE (units.size() == 1);
    const auto& u = units[0];

    std::string why;
    const auto t1 = viewSide.request (u.slot, u.generation, knob ("vox.comp", "thresh", 0.25f), 1000, why);
    const auto t2 = viewSide.request (u.slot, u.generation, power ("vox.pitch", false), 1000, why);
    CHECK (why.empty());
    REQUIRE (t1 > 0);
    REQUIRE (t2 > t1);
    CHECK (viewSide.answer (u.slot, u.generation, t1).outcome == rack::Outcome::pending);   // not taken yet

    CHECK (vox.serve (1010) == 2);
    REQUIRE (vox.seen.size() == 2);
    CHECK (vox.seen[0].change == rack::Change::value);
    CHECK (vox.seen[0].module == "vox.comp");
    CHECK (vox.seen[0].control == "thresh");
    CHECK (vox.seen[0].value == Catch::Approx (0.25f));
    CHECK (vox.seen[0].gesture == rack::Gesture::set);
    CHECK (vox.seen[0].ticket == t1);
    CHECK (vox.seen[1].change == rack::Change::state);
    CHECK (vox.seen[1].module == "vox.pitch");
    CHECK (vox.seen[1].value == 0.0f);
    CHECK (viewSide.answer (u.slot, u.generation, t1).outcome == rack::Outcome::applied);
    CHECK (viewSide.answer (u.slot, u.generation, t2).outcome == rack::Outcome::applied);
    CHECK (vox.serve (1020) == 0);   // each request once
    CHECK (vox.seen.size() == 2);

    // The plug-in can't do it: refused, with its reason, for the view to show.
    const auto t3 = viewSide.request (u.slot, u.generation, knob ("vox.pitch", "speed", 0.5f), 1030, why);
    const auto t4 = viewSide.request (u.slot, u.generation, power ("mix.space", true), 1030, why);
    const auto t5 = viewSide.request (u.slot, u.generation, knob ("vox.comp", "ratio", 0.5f), 1030, why);
    CHECK (vox.serve (1040) == 1);
    auto a = viewSide.answer (u.slot, u.generation, t3);
    CHECK (a.outcome == rack::Outcome::refused);
    CHECK (a.why == "Pitch has no Speed knob.");
    a = viewSide.answer (u.slot, u.generation, t4);
    CHECK (a.outcome == rack::Outcome::refused);
    CHECK (a.why == "This plug-in has no mix.space.");
    CHECK (viewSide.answer (u.slot, u.generation, t5).outcome == rack::Outcome::applied);

    // Values kept to 0..1 (and a value that isn't a number goes to 0); state requests are on or off; texts cut.
    viewSide.request (u.slot, u.generation, knob ("vox.comp", "thresh", 7.0f), 1050, why);
    viewSide.request (u.slot, u.generation, knob ("vox.comp", "thresh", std::numeric_limits<float>::quiet_NaN()), 1050, why);
    auto odd = power ("vox.comp", true);
    odd.value = 0.7f;
    odd.control = "ignored";
    odd.gesture = rack::Gesture::begin;
    viewSide.request (u.slot, u.generation, odd, 1050, why);
    viewSide.request (u.slot, u.generation, knob ("vox.comp", std::string (100, 'c').c_str(), 0.5f), 1050, why);
    vox.seen.clear();
    vox.serve (1060);
    REQUIRE (vox.seen.size() == 4);
    CHECK (vox.seen[0].value == 1.0f);
    CHECK (vox.seen[1].value == 0.0f);
    CHECK (vox.seen[2].value == 1.0f);
    CHECK (vox.seen[2].control.empty());
    CHECK (vox.seen[2].gesture == rack::Gesture::set);
    CHECK (vox.seen[3].control == std::string (rack::kControlBytes - 1, 'c'));

    // Tickets that were never handed out, and nonsense addresses.
    CHECK (viewSide.answer (u.slot, u.generation, 0).outcome == rack::Outcome::gone);
    CHECK (viewSide.answer (u.slot, u.generation, 9999).outcome == rack::Outcome::gone);
    CHECK (viewSide.answer (-1, u.generation, t1).outcome == rack::Outcome::gone);
    CHECK (viewSide.request (rack::kMaxSlots, 1, power ("vox.comp", true), 1070, why) == 0);
    CHECK_FALSE (why.empty());
}

TEST_CASE ("Rack requests: a plug-in that closed or moved never gets another's requests", "[rack][requests]")
{
    Memory mem;
    rack::Link link (mem.data());
    std::string why;
    auto owner = std::make_unique<Owner> (link);
    REQUIRE (owner->publish (1000));
    const auto u = link.units (1000).at (0);

    // A request waiting when the plug-in closes: gone with it.
    const auto t = link.request (u.slot, u.generation, knob ("vox.comp", "thresh", 0.3f), 1000, why);
    REQUIRE (t > 0);
    owner.reset();
    CHECK (link.answer (u.slot, u.generation, t).outcome == rack::Outcome::gone);
    CHECK (link.request (u.slot, u.generation, knob ("vox.comp", "thresh", 0.3f), 1000, why) == 0);
    CHECK (why.find ("closed") != std::string::npos);

    // Another plug-in takes the same slot: the view's old address (slot + generation) doesn't reach it, and the request
    // that was waiting for the old one isn't served to it.
    Owner next (link, "Bass");
    REQUIRE (next.publish (1100));
    const auto v = link.units (1100).at (0);
    CHECK (v.slot == u.slot);
    CHECK (v.generation != u.generation);
    CHECK (link.request (u.slot, u.generation, knob ("vox.comp", "thresh", 0.3f), 1100, why) == 0);
    CHECK (next.serve (1100) == 0);
    CHECK (next.seen.empty());
    const auto mine = link.request (v.slot, v.generation, knob ("vox.comp", "ratio", 0.6f), 1100, why);
    REQUIRE (mine > 0);
    CHECK (next.serve (1110) == 1);
    REQUIRE (next.seen.size() == 1);
    CHECK (next.seen[0].control == "ratio");

    // A request sent with the old generation in the moment the slot changes hands: taken and dropped, not served.
    Memory mem2;
    rack::Link raw (mem2.data());
    const int slot = raw.claim (5, 100);
    raw.publish (slot, 5, unit (rack::Product::link, rack::Role::track, "Kick"), 100);
    const auto oldGen = raw.units (100).at (0).generation;
    REQUIRE (raw.request (slot, oldGen, knob ("link.comp", "thresh", 0.5f), 100, why) > 0);
    raw.release (slot, 5);
    REQUIRE (raw.claim (6, 200) == slot);
    raw.publish (slot, 6, unit (rack::Product::link, rack::Role::track, "Kick"), 200);
    rack::Request got[4];
    std::uint64_t memo = 0;
    CHECK (raw.take (slot, 6, got, 4, 200, memo) == 0);
    CHECK (raw.take (slot, 5, got, 4, 200, memo) == 0);   // not the owner any more
    // A silent unit (its heartbeat older than kAliveMs) takes no requests.
    CHECK (raw.request (slot, raw.units (200).at (0).generation, knob ("link.comp", "thresh", 0.5f), 200 + rack::kAliveMs + 1, why) == 0);
}

TEST_CASE ("Rack requests: a locked unit (Auto Mix running, padlock) takes none", "[rack][requests]")
{
    Memory mem;
    rack::Link link (mem.data());
    Owner vox (link);
    REQUIRE (vox.publish (1000));
    auto u = link.units (1000).at (0);
    std::string why;

    // Locked when the view asks: refused at once, with the lock's reason.
    vox.pub.due (1000 + rack::Publisher::kBriefMs);
    vox.pub.publish ([&] { auto x = unit (rack::Product::voxology, rack::Role::vocal, "Lead Vox"); x.lock = "Auto-Edit is running."; return x; }(),
                     1000 + rack::Publisher::kBriefMs);
    CHECK (link.request (u.slot, u.generation, knob ("vox.comp", "thresh", 0.3f), 1900, why) == 0);
    CHECK (why == "Auto-Edit is running.");

    // Locked after the view asked (the lock wasn't published yet): refused by the owner, nothing applied.
    vox.pub.due (1000 + 2 * rack::Publisher::kBriefMs);
    vox.pub.publish (unit (rack::Product::voxology, rack::Role::vocal, "Lead Vox"), 1000 + 2 * rack::Publisher::kBriefMs);
    const auto t = link.request (u.slot, u.generation, knob ("vox.comp", "thresh", 0.3f), 2600, why);
    REQUIRE (t > 0);
    CHECK (vox.serve (2610, "Auto Mix is running.") == 0);
    CHECK (vox.seen.empty());
    const auto a = link.answer (u.slot, u.generation, t);
    CHECK (a.outcome == rack::Outcome::refused);
    CHECK (a.why == "Auto Mix is running.");

    // A drag in progress when the lock comes: closed (the host's gesture ends), then refused.
    link.request (u.slot, u.generation, knob ("vox.comp", "thresh", 0.4f, rack::Gesture::begin), 2700, why);
    CHECK (vox.serve (2700) == 1);
    link.request (u.slot, u.generation, knob ("vox.comp", "thresh", 0.5f, rack::Gesture::move), 2710, why);
    vox.seen.clear();
    CHECK (vox.serve (2720, "Auto Mix is running.") == 0);
    REQUIRE (vox.seen.size() == 1);
    CHECK (vox.seen[0].gesture == rack::Gesture::end);
    CHECK (vox.seen[0].ticket == 0);
    CHECK (vox.seen[0].value == Catch::Approx (0.4f));   // where it was, not the refused move
}

TEST_CASE ("Rack requests: knob drags reach the host as one balanced gesture", "[rack][requests]")
{
    using G = rack::Gesture;
    Memory mem;
    rack::Link link (mem.data());
    Owner vox (link);
    REQUIRE (vox.publish (1000));
    const auto u = link.units (1000).at (0);
    std::string why;
    auto send = [&] (const char* control, float v, G g, std::uint64_t now) {
        return link.request (u.slot, u.generation, knob ("vox.comp", control, v, g), now, why);
    };

    // begin, moves, end: as sent. Moves of one knob in one batch: only the newest (the others count as applied).
    send ("thresh", 0.1f, G::begin, 1000);
    vox.serve (1000);
    const auto m1 = send ("thresh", 0.2f, G::move, 1010);
    send ("thresh", 0.3f, G::move, 1010);
    send ("ratio", 0.5f, G::move, 1010);   // another knob at the same time: its own gesture
    send ("thresh", 0.4f, G::move, 1010);
    CHECK (vox.serve (1020) == 4);
    REQUIRE (vox.seen.size() == 3);
    CHECK (vox.seen[0].gesture == G::begin);
    CHECK (vox.seen[1].control == "ratio");
    CHECK (vox.seen[1].gesture == G::begin);   // a move without a begin begins
    CHECK (vox.seen[2].control == "thresh");
    CHECK (vox.seen[2].gesture == G::move);
    CHECK (vox.seen[2].value == Catch::Approx (0.4f));
    CHECK (link.answer (u.slot, u.generation, m1).outcome == rack::Outcome::applied);

    vox.seen.clear();
    send ("thresh", 0.45f, G::begin, 1030);   // a begin while open: a move
    send ("thresh", 0.5f, G::end, 1030);
    send ("ratio", 0.6f, G::set, 1030);        // a set while open: ends it
    send ("ratio", 0.7f, G::end, 1030);        // an end without a begin: a set
    vox.serve (1040);
    REQUIRE (vox.seen.size() == 4);
    CHECK (vox.seen[0].gesture == G::move);
    CHECK (vox.seen[1].gesture == G::end);
    CHECK (vox.seen[2].gesture == G::end);
    CHECK (vox.seen[3].gesture == G::set);

    // The view closed mid-drag: the owner ends the gesture itself after kGestureMs, at the last value.
    vox.seen.clear();
    send ("thresh", 0.6f, G::begin, 2000);
    vox.serve (2000);
    send ("thresh", 0.65f, G::move, 2100);
    vox.serve (2100);
    vox.serve (2100 + rack::kGestureMs - 1);
    CHECK (vox.seen.size() == 2);
    vox.serve (2100 + rack::kGestureMs);
    REQUIRE (vox.seen.size() == 3);
    CHECK (vox.seen[2].gesture == G::end);
    CHECK (vox.seen[2].ticket == 0);
    CHECK (vox.seen[2].value == Catch::Approx (0.65f));
    // ... and a move after that starts a new one.
    send ("thresh", 0.7f, G::move, 4500);
    vox.serve (4500);
    CHECK (vox.seen.back().gesture == G::begin);

    // A refused begin opens nothing: its end comes as a set.
    vox.seen.clear();
    send ("speed", 0.5f, G::begin, 4600);
    vox.serve (4600);
    send ("speed", 0.5f, G::end, 4610);
    vox.serve (4610);
    REQUIRE (vox.seen.size() == 2);
    CHECK (vox.seen[1].gesture == G::set);

    // Stopped by the host (another project plays): open drags still close in time.
    vox.seen.clear();
    vox.pub.stop();
    vox.pub.due (6000);
    vox.serve (4500 + rack::kGestureMs);
    REQUIRE (vox.seen.size() == 1);
    CHECK (vox.seen[0].gesture == G::end);
}

TEST_CASE ("Rack requests: a full queue, and a sender that stopped half-way", "[rack][requests]")
{
    Memory mem;
    rack::Link link (mem.data());
    Owner vox (link);
    REQUIRE (vox.publish (1000));
    const auto u = link.units (1000).at (0);
    std::string why;

    // kMaxRequests waiting: the next is refused as busy, nothing overwritten; served, there's room again.
    std::vector<std::uint64_t> tickets;
    for (int i = 0; i < rack::kMaxRequests; ++i)
        tickets.push_back (link.request (u.slot, u.generation, knob ("vox.comp", "thresh", static_cast<float> (i) / 100.0f), 1000, why));
    for (auto t : tickets) CHECK (t > 0);
    CHECK (link.request (u.slot, u.generation, knob ("vox.comp", "thresh", 0.9f), 1000, why) == 0);
    CHECK (why.find ("busy") != std::string::npos);
    CHECK (vox.serve (1010) == rack::kMaxRequests);
    REQUIRE (vox.seen.size() == static_cast<std::size_t> (rack::kMaxRequests));
    for (int i = 0; i < rack::kMaxRequests; ++i) CHECK (vox.seen[static_cast<std::size_t> (i)].value == Catch::Approx (static_cast<float> (i) / 100.0f));
    CHECK (link.request (u.slot, u.generation, knob ("vox.comp", "thresh", 0.9f), 1020, why) > 0);
    vox.serve (1020);

    // A sender took a ticket and never wrote it (its plug-in stopped in that moment): the owner waits kStuckMs, then
    // skips it and serves the ones after. Its place in the ring was never held, so the next round uses it normally.
    auto* block = static_cast<rack::Block*> (mem.data());
    auto& q = block->slots[static_cast<std::size_t> (u.slot)].queue;
    const auto stuck = q.issued.fetch_add (1) + 1;
    const auto after = link.request (u.slot, u.generation, knob ("vox.comp", "ratio", 0.3f), 2000, why);
    REQUIRE (after == stuck + 1);
    vox.seen.clear();
    CHECK (vox.serve (2000) == 0);
    CHECK (link.answer (u.slot, u.generation, after).outcome == rack::Outcome::pending);
    CHECK (vox.serve (2000 + rack::kStuckMs - 1) == 0);
    CHECK (vox.serve (2000 + rack::kStuckMs) == 1);
    REQUIRE (vox.seen.size() == 1);
    CHECK (vox.seen[0].control == "ratio");
    CHECK (link.answer (u.slot, u.generation, after).outcome == rack::Outcome::applied);

    // A sender that stopped while writing (its place still held): a later sender for the same place gives up rather
    // than mix its request into the other's; the owner skips both in time.
    const auto held = q.issued.fetch_add (1) + 1;
    auto& place = q.ring[static_cast<std::size_t> (held % rack::kMaxRequests)];
    place.stamp.store (2 * held - 1);
    for (int i = 0; i < rack::kMaxRequests - 1; ++i)
        REQUIRE (link.request (u.slot, u.generation, knob ("vox.comp", "thresh", 0.5f), 3000, why) > 0);
    vox.serve (3000);
    vox.serve (3000 + rack::kStuckMs);   // skips `held`, serves the others
    CHECK (link.request (u.slot, u.generation, knob ("vox.comp", "thresh", 0.5f), 4500, why) == 0);   // lands on `held`'s place
    CHECK (why.find ("busy") != std::string::npos);
    vox.serve (4500);
    vox.serve (4500 + rack::kStuckMs);   // skips that one too
    const auto next = link.request (u.slot, u.generation, knob ("vox.comp", "ratio", 0.8f), 6000, why);
    REQUIRE (next > 0);
    vox.seen.clear();
    vox.serve (6000);
    REQUIRE (vox.seen.size() == 1);
    CHECK (vox.seen[0].ticket == next);
}

TEST_CASE ("Rack requests: many views sending at once, the owner serving meanwhile", "[rack][requests]")
{
    // Four senders (Rack views in four plug-ins) and the owner on their own threads: every request that got a ticket
    // is served exactly once, whole (its value says which sender and which request it was), in each sender's order.
    Memory mem;
    rack::Link link (mem.data());
    Owner vox (link);
    REQUIRE (vox.publish (1000));
    const auto u = link.units (1000).at (0);

    constexpr int kSenders = 4, kEach = 3000;
    std::atomic<bool> sending { true };
    std::atomic<int> sent { 0 };
    std::vector<std::vector<std::uint64_t>> tickets (kSenders);
    std::vector<std::thread> senders;
    for (int s = 0; s < kSenders; ++s)
        senders.emplace_back ([&, s] {
            rack::Link mine (mem.data());
            std::string why;
            for (int i = 0; i < kEach; ++i)
            {
                // control "s<sender>", value i / kEach: a torn request would mix them up.
                const std::string control = "s" + std::to_string (s);
                auto r = knob ("vox.comp", control.c_str(), static_cast<float> (i) / kEach);
                r.gesture = rack::Gesture::set;
                std::uint64_t t = 0;
                while ((t = mine.request (u.slot, u.generation, r, 1000, why)) == 0) std::this_thread::yield();
                tickets[static_cast<std::size_t> (s)].push_back (t);
                sent.fetch_add (1);
            }
        });

    std::vector<rack::Request> got;
    std::thread owner ([&] {
        rack::Request batch[rack::kMaxRequests];
        std::uint64_t memo = 0;
        while (sending.load() || static_cast<int> (got.size()) < kSenders * kEach)
        {
            const int n = link.take (u.slot, vox.pub.slot() >= 0 ? link.units (1000).at (0).instanceId : 0, batch, rack::kMaxRequests, 1000, memo);
            for (int i = 0; i < n; ++i) got.push_back (batch[i]);
            link.answered (u.slot, link.units (1000).at (0).instanceId, ~std::uint64_t { 0 });
            if (n == 0) std::this_thread::yield();
        }
    });
    for (auto& t : senders) t.join();
    sending.store (false);
    owner.join();

    REQUIRE (static_cast<int> (got.size()) == kSenders * kEach);
    std::vector<int> nextOf (kSenders, 0);
    std::uint64_t lastTicket = 0;
    for (const auto& r : got)
    {
        CHECK (r.ticket > lastTicket);
        lastTicket = r.ticket;
        REQUIRE (r.module == "vox.comp");
        REQUIRE (r.control.size() == 2);
        const int s = r.control[1] - '0';
        REQUIRE ((s >= 0 && s < kSenders));
        auto& next = nextOf[static_cast<std::size_t> (s)];
        REQUIRE (r.value == Catch::Approx (static_cast<float> (next) / kEach).margin (1e-6));
        ++next;
    }
    for (int s = 0; s < kSenders; ++s)
        for (auto t : tickets[static_cast<std::size_t> (s)])
            CHECK (link.answer (u.slot, u.generation, t).outcome == rack::Outcome::applied);
}

TEST_CASE ("Rack requests: what the view's plug-in hands rack.js (requestJson, answerJson)", "[rack][requests]")
{
    Memory mem;
    rack::Link link (mem.data());
    Owner vox (link);
    REQUIRE (vox.publish (1000));
    const auto u = link.units (1000).at (0);
    const std::string id = "u" + std::to_string (u.slot);

    auto j = rack::requestJson (link, id, u.generation, "value", "vox.comp", "cpThr", 0.4f, "begin", 1000);
    CHECK (wellFormed (j));
    CHECK (j.rfind ("{\"ticket\":1,\"why\":\"\"}", 0) == 0);
    CHECK (rack::answerJson (link, id, u.generation, 1) == "{\"outcome\":\"pending\",\"why\":\"\"}");
    vox.serve (1010);
    REQUIRE (vox.seen.size() == 1);
    CHECK (vox.seen[0].gesture == rack::Gesture::begin);
    CHECK (vox.seen[0].value == Catch::Approx (0.4f));
    CHECK (rack::answerJson (link, id, u.generation, 1) == "{\"outcome\":\"applied\",\"why\":\"\"}");

    j = rack::requestJson (link, id, u.generation, "state", "vox.pitch", "", 0.0f, "set", 1020);
    CHECK (j.rfind ("{\"ticket\":2,", 0) == 0);
    j = rack::requestJson (link, id, u.generation, "value", "vox.pitch", "speed", 0.5f, "end", 1020);
    vox.serve (1030);
    CHECK (vox.seen[1].change == rack::Change::state);
    CHECK (rack::answerJson (link, id, u.generation, 3) == "{\"outcome\":\"refused\",\"why\":\"Pitch has no Speed knob.\"}");

    // Bad addresses and empty requests: a reason, no ticket.
    for (const auto& bad : { std::string ("x1"), std::string ("u"), std::string ("u99999"), std::string ("u-1"), std::string ("") })
    {
        const auto k = rack::requestJson (link, bad, u.generation, "value", "vox.comp", "cpThr", 0.5f, "set", 1040);
        CHECK (k.rfind ("{\"ticket\":0,\"why\":\"", 0) == 0);
        CHECK (k.size() > 22);
        CHECK (rack::answerJson (link, bad, u.generation, 1) == "{\"outcome\":\"gone\",\"why\":\"\"}");
    }
    CHECK (rack::requestJson (link, id, u.generation, "value", "vox.comp", "", 0.5f, "set", 1040) == "{\"ticket\":0,\"why\":\"Nothing to change.\"}");
    CHECK (rack::requestJson (link, id, u.generation + 1, "value", "vox.comp", "cpThr", 0.5f, "set", 1040).rfind ("{\"ticket\":0,", 0) == 0);
}

// ---- Beta 18: what the views show, reset, one-click fixes, other layouts -----------------------------------------

TEST_CASE ("Rack link: plug-ins send details only for the track a Rack view shows", "[rack]")
{
    using rack::Role;
    Memory mem;
    rack::Link link (mem.data());
    const std::uint32_t lead = suite::nameHash ("Lead Vox"), kick = suite::nameHash ("Kick");
    const std::uint64_t t = 100000;
    CHECK_FALSE (link.shows (t, lead, Role::vocal));                 // no view open
    link.view (t, "t" + std::to_string (lead));
    CHECK (link.watched (t));
    CHECK (link.shows (t, lead, Role::vocal));
    CHECK (link.shows (t, lead, Role::track));
    CHECK_FALSE (link.shows (t, kick, Role::track));
    CHECK_FALSE (link.shows (t, 0, Role::fullmix));
    CHECK_FALSE (link.shows (t, 0, Role::master));
    CHECK_FALSE (link.shows (t, lead, Role::master));                // the master isn't a track, whatever its hash

    link.view (t + 10, "master");                                   // a second Rack (another plug-in) on the master
    CHECK (link.shows (t + 10, 0, Role::master));
    CHECK (link.shows (t + 10, lead, Role::vocal));                  // the first still looks
    CHECK_FALSE (link.shows (t + 10, 0, Role::fullmix));
    link.view (t + 20, "fullmix");
    CHECK (link.shows (t + 20, 0, Role::fullmix));

    // A view showing every track's details (the MODULES page, the first look): everything.
    link.view (t + 30, "");
    CHECK (link.shows (t + 30, kick, Role::track));
    // Views that stopped looking: nothing after kViewerMs.
    CHECK_FALSE (link.shows (t + 30 + rack::kViewerMs + 1, lead, Role::vocal));

    // More tracks looked at than places: the one looked at longest ago gives way.
    Memory mem2;
    rack::Link l2 (mem2.data());
    for (int i = 1; i <= rack::kMaxViewers + 1; ++i) l2.view (t + static_cast<std::uint64_t> (i), "t" + std::to_string (i));
    CHECK_FALSE (l2.shows (t + 10, 1, Role::track));
    for (int i = 2; i <= rack::kMaxViewers + 1; ++i) CHECK (l2.shows (t + 10, static_cast<std::uint32_t> (i), Role::track));

    // A publisher: full while its track is shown, brief (and less often) while another is.
    using Due = rack::Publisher::Due;
    Memory mem3;
    rack::Link l3 (mem3.data());
    rack::Publisher pub (l3);
    pub.start();
    REQUIRE (pub.due (t, kick, Role::track) == Due::brief);
    pub.publish (unit (rack::Product::link, Role::track, "Kick"), t);
    l3.view (t + 10, "t" + std::to_string (lead));                    // a Rack shows the vocal
    CHECK (pub.due (t + 20, kick, Role::track) == Due::none);        // the kick: still brief, at the brief rate
    CHECK (pub.due (t + rack::Publisher::kBriefMs, kick, Role::track) == Due::brief);
    pub.publish (unit (rack::Product::link, Role::track, "Kick"), t + rack::Publisher::kBriefMs);
    l3.view (t + rack::Publisher::kBriefMs + 5, "t" + std::to_string (kick));   // now the kick: full at once
    CHECK (pub.due (t + rack::Publisher::kBriefMs + 6, kick, Role::track) == Due::full);
}

TEST_CASE ("Rack requests: after a change the owner publishes at once; reset to default", "[rack][requests]")
{
    using Due = rack::Publisher::Due;
    Memory mem;
    rack::Link link (mem.data());
    Owner vox (link);
    REQUIRE (vox.publish (1000));
    const auto u = link.units (1000).at (0);
    const auto hash = suite::nameHash ("Lead Vox");
    link.view (1000, "t" + std::to_string (hash));
    REQUIRE (vox.pub.due (1000, hash, rack::Role::vocal) == Due::full);
    vox.pub.publish (unit (rack::Product::voxology, rack::Role::vocal, "Lead Vox"), 1000);
    CHECK (vox.pub.due (1010, hash, rack::Role::vocal) == Due::none);   // not yet (every kFullMs)
    std::string why;
    REQUIRE (link.request (u.slot, u.generation, knob ("vox.comp", "cpThr", 0.3f), 1010, why) > 0);
    CHECK (vox.serve (1010) == 1);
    CHECK (vox.pub.due (1011, hash, rack::Role::vocal) == Due::full);   // at once: the Rack sees the new value
    vox.pub.publish (unit (rack::Product::voxology, rack::Role::vocal, "Lead Vox"), 1011);
    CHECK (vox.pub.due (1012, hash, rack::Role::vocal) == Due::none);
    CHECK (vox.serve (1013) == 0);                                       // nothing applied: no extra publish
    CHECK (vox.pub.due (1014, hash, rack::Role::vocal) == Due::none);

    // Reset: a set of its own, after closing a drag of the same knob that was still open.
    vox.seen.clear();
    link.request (u.slot, u.generation, knob ("vox.comp", "cpThr", 0.6f, rack::Gesture::begin), 1100, why);
    vox.serve (1100);
    const auto j = rack::requestJson (link, "u" + std::to_string (u.slot), u.generation, "reset", "vox.comp", "cpThr", 0.9f, "move", 1110);
    CHECK (j.rfind ("{\"ticket\":", 0) == 0);
    CHECK (j.find ("\"ticket\":0") == std::string::npos);
    vox.serve (1120);
    REQUIRE (vox.seen.size() == 3);
    CHECK (vox.seen[0].gesture == rack::Gesture::begin);
    CHECK (vox.seen[1].gesture == rack::Gesture::end);       // the open drag closed
    CHECK (vox.seen[1].ticket == 0);
    CHECK (vox.seen[2].change == rack::Change::reset);
    CHECK (vox.seen[2].control == "cpThr");
    CHECK (vox.seen[2].gesture == rack::Gesture::set);
    // A reset needs a knob.
    CHECK (rack::requestJson (link, "u" + std::to_string (u.slot), u.generation, "reset", "vox.comp", "", 0.0f, "set", 1130)
           == "{\"ticket\":0,\"why\":\"Nothing to change.\"}");
}

TEST_CASE ("Rack link: warnings carry one-click fixes for the units in the block", "[rack]")
{
    using rack::Product;
    using rack::Role;
    using rack::State;
    Memory mem;
    rack::Link link (mem.data());
    auto put = [&] (std::uint64_t id, const rack::Unit& u) {
        const int slot = link.claim (id, 1000);
        REQUIRE (slot >= 0);
        REQUIRE (link.publish (slot, id, u, 1000));
    };
    auto space = mod ("mix.space");
    space.layers = rack::SpaceLayer::hall | rack::SpaceLayer::delay;
    put (1, unit (Product::voxology, Role::vocal, "Lead Vox", { mod ("vox.comp"), mod ("vox.reverb"), mod ("vox.delay") }));
    put (2, unit (Product::link, Role::track, "Lead Vox", { mod ("mix.comp"), space }));
    put (3, unit (Product::mixology, Role::fullmix, "", { mod ("mix.limit") }));
    put (4, unit (Product::masterology, Role::master, ""));
    const auto units = link.units (1000);
    const auto lines = rack::together (units);

    auto lineWith = [&] (const std::string& words) -> const rack::ProjectLine* {
        for (const auto& l : lines)
            if (l.text.find (words) != std::string::npos) return &l;
        return nullptr;
    };
    const auto* comp = lineWith ("Two compressors");
    REQUIRE (comp != nullptr);
    REQUIRE (comp->fixes.size() == 2);
    CHECK (comp->fixes[0].module == "mix.comp");
    CHECK (comp->fixes[0].label == "Turn the Link's Compressor off");
    CHECK_FALSE (comp->fixes[0].on);
    CHECK (comp->fixes[1].module == "vox.comp");
    const auto* reverb = lineWith ("Two reverbs");
    REQUIRE (reverb != nullptr);
    REQUIRE (reverb->fixes.size() == 2);
    CHECK (reverb->fixes[0].module == "mix.space");
    CHECK (reverb->fixes[0].label.find ("Hall") != std::string::npos);   // Space plays more than the reverb: the button says so
    CHECK (reverb->fixes[0].label.find ("Delay") != std::string::npos);
    const auto* limiter = lineWith ("Mixology's Limiter");
    REQUIRE (limiter != nullptr);
    REQUIRE (limiter->fixes.size() == 1);
    CHECK (limiter->fixes[0].module == "mix.limit");
    // Each fix names its unit as the block holds it.
    for (const auto& u : units)
        if (u.product == Product::link) { CHECK (comp->fixes[0].slot == u.slot); CHECK (comp->fixes[0].generation == u.generation); }

    const auto j = rack::toJson ("mixology", 0, units, lines, rack::Overview {});
    CHECK (wellFormed (j));
    CHECK (j.find ("\"fixes\":[{\"unit\":\"u") != std::string::npos);
    CHECK (j.find ("\"module\":\"mix.limit\",\"on\":false,\"label\":\"Turn Mixology's Limiter off\"}") != std::string::npos);

    // Units that aren't in the block (no slot): the warning stays, without a button.
    const auto loose = rack::together ({ unit (Product::voxology, Role::vocal, "Lead Vox", { mod ("vox.comp") }),
                                         unit (Product::link, Role::track, "Lead Vox", { mod ("mix.comp") }) });
    REQUIRE_FALSE (loose.empty());
    for (const auto& l : loose) CHECK (l.fixes.empty());
}

TEST_CASE ("Rack link: an older beta's plug-in in the same project is noticed", "[rack]")
{
    // A Rack block of layout 2 (Beta 16 / 17) in this process, with a live unit in it.
    const std::uint32_t old = 2;
    const std::size_t bytes = 64 + 23936 * static_cast<std::size_t> (rack::kMaxSlots);
    const auto now = suite::nowMs() + 10 * 1000 * 1000;   // well past anything cached
#if defined(_WIN32)
    const std::string name = "Local\\SonicologyRack.v2." + std::to_string (GetCurrentProcessId());
    HANDLE h = CreateFileMappingA (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD> (bytes), name.c_str());
    REQUIRE (h != nullptr);
    const auto found = rack::Link::otherLayouts (now);
    CHECK (std::find (found.begin(), found.end(), old) != found.end());
    CloseHandle (h);
    CHECK (rack::Link::otherLayouts (now + 2000).empty());   // gone with the plug-in that had it
#else
    const std::string name = "/SonicologyRack.v2." + std::to_string (getpid());
    const int fd = shm_open (name.c_str(), O_RDWR | O_CREAT, 0600);
    REQUIRE (fd >= 0);
    REQUIRE (ftruncate (fd, static_cast<off_t> (bytes)) == 0);
    auto* p = static_cast<unsigned char*> (mmap (nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    close (fd);
    REQUIRE (p != MAP_FAILED);
    std::memset (p, 0, bytes);
    CHECK (rack::Link::otherLayouts (now).empty());          // there, but nobody in it (left from a plug-in that went)
    auto* slot = p + 64 + 23936 * 5;
    const std::uint32_t claimed = 1;
    std::memcpy (slot, &claimed, sizeof claimed);
    std::memcpy (slot + 16, &now, sizeof now);
    const auto found = rack::Link::otherLayouts (now + 2000);
    CHECK (found == std::vector<std::uint32_t> { old });
    CHECK (rack::Link::otherLayouts (now + 2000 + rack::kAliveMs + 1000).empty());   // silent too long: gone
    munmap (p, bytes);
    shm_unlink (name.c_str());
#endif
}

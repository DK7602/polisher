// The Sonicology Rack link (suite/RackLink.h, the same file in all three repositories): what each plug-in publishes,
// what they do together, and the JSON the Rack view reads. THIS FILE IS THE SAME IN ALL THREE REPOSITORIES.

#include "suite/RackLink.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <clocale>
#include <cmath>
#include <limits>
#include <memory>
#include <string>

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
    eq.values = { { "Mud", "−3.0 dB", 0.33f, true }, { "Presence", "+2.5 dB", 0.64f, true } };
    eq.bands = { { rack::BandType::bell, 320.0f, -3.0f, 1.1f, "Mud", true }, { rack::BandType::highShelf, 10000.0f, 2.0f, 0.7f, "Air", true } };
    auto comp = mod ("vox.comp");
    comp.meterKind = rack::MeterKind::gr;
    comp.meter = -3.5f;
    auto pitch = mod ("vox.pitch", rack::State::rest);
    u.modules = { pitch, eq, comp };
    u.lines = { { rack::Level::ok, "Pitch rests: Honey Tune tunes this track" } };
    u.spectrum.assign (rack::kSpectrumBins, -40.0f);
    u.spectrum[10] = -12.0f;
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
    CHECK (s.instanceId == 11);
    REQUIRE (s.modules.size() == 3);
    CHECK (s.modules[0].key == "vox.pitch");
    CHECK (s.modules[0].state == rack::State::rest);
    CHECK (s.modules[1].summary == "Mud −3 dB · Presence +2.5 dB");   // UTF-8 kept
    CHECK (s.modules[1].by == "Auto-Edit");
    REQUIRE (s.modules[1].values.size() == 2);
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
        for (int v = 0; v < rack::kMaxValues + 3; ++v) m.values.push_back ({ "Gain", "+1 dB", std::numeric_limits<float>::quiet_NaN(), false });
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
    auto eq = mod ("vox.eq");
    eq.summary = "Mud −3 dB";
    eq.values = { { "Mud", "−3.0 dB", 0.33f, true } };
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
    CHECK (j.find ("\"id\":\"u4\"") != std::string::npos);
    CHECK (j.find ("\"product\":\"voxology\"") != std::string::npos);
    CHECK (j.find ("\"version\":\"1.2.3\"") != std::string::npos);
    CHECK (j.find ("\"track\":\"Lead \\\"Vox\\\"\\\\1\\n\"") != std::string::npos);   // escaped
    CHECK (j.find ("\"role\":\"vocal\"") != std::string::npos);
    CHECK (j.find ("\"state\":\"rest\"") != std::string::npos);
    CHECK (j.find ("\"summary\":\"Mud −3 dB\"") != std::string::npos);
    CHECK (j.find ("{\"name\":\"Mud\",\"text\":\"−3.0 dB\",\"norm\":0.330,\"bi\":true}") != std::string::npos);
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
    CHECK (empty == "{\"host\":\"mixology\",\"hostHash\":0,\"units\":[],\"together\":[],\"mix\":{},\"master\":{}}");
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


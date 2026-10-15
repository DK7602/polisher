// The suite link between Voxology, Mixology and Masterology (suite/SuiteLink.h, the same file in all three repos;
// these tests are the same in all three too).

#include "suite/SuiteLink.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <thread>

namespace
{
    struct Memory
    {
        std::unique_ptr<unsigned char[]> bytes { new unsigned char[suite::Link::bytes() + 64] () };
        void* data() { return bytes.get() + (64 - reinterpret_cast<std::uintptr_t> (bytes.get()) % 64) % 64; }
    };
}

TEST_CASE ("Suite: Voxology's vocals reach Mixology, with the same name hash as the bridge", "[suite]")
{
    Memory mem;
    suite::Link vox (mem.data()), mix (mem.data());   // two libraries on the same memory
    const int a = vox.claimVocal (11, 100), b = vox.claimVocal (22, 100);
    REQUIRE (a >= 0);
    REQUIRE (b >= 0);
    CHECK (a != b);
    CHECK (mix.vocals (100).empty());   // claimed, nothing published yet

    suite::VocalInfo v;
    v.trackName = "Lead Vox";
    v.flags = suite::VocalInfo::processing | suite::VocalInfo::compressor | suite::VocalInfo::reverb;
    v.style = suite::VocalInfo::Style::trapLead;
    v.compressionDb = 4.5f;
    v.reverbMix = 0.18f;
    v.version = suite::packVersion (0, 47, 0);
    CHECK (vox.publishVocal (a, 11, v, 200));
    CHECK_FALSE (vox.publishVocal (a, 99, v, 200));   // someone else's instance id: ignored

    const auto seen = mix.vocals (300);
    REQUIRE (seen.size() >= 1);
    const suite::VocalInfo* lead = nullptr;
    for (const auto& x : seen) if (x.trackName == "Lead Vox") lead = &x;
    REQUIRE (lead != nullptr);
    CHECK (lead->nameHash == suite::nameHash ("Lead Vox"));
    CHECK ((lead->flags & suite::VocalInfo::compressor) != 0);
    CHECK (lead->style == suite::VocalInfo::Style::trapLead);
    CHECK (lead->compressionDb == Catch::Approx (4.5f));
    CHECK (lead->reverbMix == Catch::Approx (0.18f));
    CHECK (lead->version == suite::packVersion (0, 47, 0));

    // Gone quiet (plug-in removed without releasing, or its window... the heartbeat stopped): not listed.
    CHECK (mix.vocals (300 + suite::kAliveMs + 200).empty());
    // Released: the slot is free again.
    vox.releaseVocal (a, 11);
    vox.releaseVocal (b, 22);
    CHECK (vox.claimVocal (33, 5000) >= 0);
}

TEST_CASE ("Suite: Mixology's hand-off reaches Masterology; Masterology's goal reaches Mixology", "[suite]")
{
    Memory mem;
    suite::Link mix (mem.data()), master (mem.data());
    suite::MixInfo got;
    CHECK_FALSE (master.readMix (got, 100));   // nothing yet

    suite::MixInfo m;
    m.styleKey = "hiphop.modern";
    m.styleName = "Hip Hop - Modern Clean";
    m.flags = suite::MixInfo::analysed | suite::MixInfo::monoMix;
    m.peakDbtp = -5.8f;
    m.integratedLufs = -16.2f;
    m.airDb = -3.1f;
    m.version = suite::packVersion (0, 8, 0);
    mix.publishMix (m, 1000);
    REQUIRE (master.readMix (got, 1500));
    CHECK (got.styleKey == "hiphop.modern");
    CHECK (got.styleName == "Hip Hop - Modern Clean");
    CHECK ((got.flags & suite::MixInfo::monoMix) != 0);
    CHECK (got.peakDbtp == Catch::Approx (-5.8f));
    CHECK (got.airDb == Catch::Approx (-3.1f));
    CHECK (got.whenMs == 1000);
    CHECK_FALSE (master.readMix (got, 1000 + suite::kAliveMs + 1));   // Mixology closed

    const int s = master.claimMaster (7, 2000);
    REQUIRE (s >= 0);
    suite::MasterInfo g;
    g.goalName = "Spotify / Apple Music";
    g.genreName = "Hip-Hop / Trap";
    g.targetLufs = -9.0f;
    g.ceilingDbtp = -1.0f;
    g.version = suite::packVersion (0, 29, 0);
    master.publishMaster (s, 7, g, 2100);
    const auto ms = mix.masters (2200);
    REQUIRE (ms.size() == 1);
    CHECK (ms[0].goalName == "Spotify / Apple Music");
    CHECK (ms[0].targetLufs == Catch::Approx (-9.0f));
    CHECK (ms[0].ceilingDbtp == Catch::Approx (-1.0f));
    master.releaseMaster (s, 7);
    CHECK (mix.masters (2200).empty());
}

TEST_CASE ("Suite: long names are cut on a UTF-8 boundary; a crashed owner's slot is taken over", "[suite]")
{
    Memory mem;
    suite::Link link (mem.data());
    const int s = link.claimVocal (1, 100);
    suite::VocalInfo v;
    v.version = 1;
    v.trackName = std::string (126, 'a') + "\xC3\xA9\xC3\xA9";   // 126 + two 2-byte characters
    link.publishVocal (s, 1, v, 100);
    const auto seen = link.vocals (100);
    REQUIRE (seen.size() == 1);
    CHECK (seen[0].trackName.size() == 126);   // "é" would not fit whole: cut before it
    CHECK (seen[0].nameHash == suite::nameHash (v.trackName));   // the hash covers the whole name

    // Fill every slot; the owners stop heart-beating (crashed); new ones can still claim.
    for (int i = 0; i < suite::kMaxVocals; ++i) link.claimVocal (1000 + static_cast<std::uint64_t> (i), 100);
    CHECK (link.claimVocal (5000, 100) == -1);                          // full, all alive
    const int taken = link.claimVocal (5001, 100 + suite::kAliveMs + 10);   // stale ones are taken over
    REQUIRE (taken >= 0);
    CHECK (taken == s);   // the first stale slot: instance 1's
    // Instance 1 comes back: its slot is gone (publish says so), so it would claim a new one.
    CHECK_FALSE (link.publishVocal (taken, 1, v, 100 + suite::kAliveMs + 20));
    CHECK (link.publishVocal (taken, 5001, v, 100 + suite::kAliveMs + 20));
}

TEST_CASE ("Suite: a reader never sees half a write", "[suite]")
{
    Memory mem;
    suite::Link writer (mem.data()), reader (mem.data());
    const int s = writer.claimVocal (5, 1);
    std::atomic<bool> stop { false };
    std::thread t ([&] {
        suite::VocalInfo a, b;
        a.trackName = "AAAAAAAAAAAAAAAAAAAA";
        a.compressionDb = 1.0f;
        a.version = 1;
        b.trackName = "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";
        b.compressionDb = 2.0f;
        b.version = 1;
        for (int i = 0; ! stop.load(); ++i) writer.publishVocal (s, 5, (i & 1) ? a : b, 10);
    });
    bool consistent = true;
    for (int i = 0; i < 20000; ++i)
        for (const auto& v : reader.vocals (10))
        {
            if (v.trackName.empty()) continue;
            const bool isA = v.trackName[0] == 'A';
            consistent = consistent && ((isA && v.trackName.size() == 20 && v.compressionDb == 1.0f)
                                        || (! isA && v.trackName.size() == 38 && v.compressionDb == 2.0f));
        }
    stop = true;
    t.join();
    CHECK (consistent);
}

TEST_CASE ("Suite: this process's block is shared across libraries", "[suite]")
{
    CHECK (&suite::Link::process() == &suite::Link::process());
    CHECK (suite::Link::processShared());
    CHECK (suite::nowMs() > 0);
}

TEST_CASE ("Suite: the song's style - the newest choice wins, in any plug-in", "[suite]")
{
    Memory mem;
    suite::Link a (mem.data()), b (mem.data()), c (mem.data());
    suite::SongStyle mix (suite::StyleInfo::From::mixology), master (suite::StyleInfo::From::masterology), vox (suite::StyleInfo::From::voxology);
    suite::StyleInfo got;
    CHECK_FALSE (a.readStyle (got, 100));

    // Never picked anywhere: nothing is shared, everyone keeps their own.
    master.setUnpicked ("pop.modern", "Pop - Modern");
    CHECK_FALSE (master.sync (b, 2, 100));
    CHECK_FALSE (b.readStyle (got, 100));

    // Picked in Mixology: Masterology and Voxology follow.
    mix.pick ("trap.drill", "Trap & Drill - Drill");
    CHECK_FALSE (mix.sync (a, 1, 200));
    REQUIRE (b.readStyle (got, 300));
    CHECK (got.key == "trap.drill");
    CHECK (got.from == suite::StyleInfo::From::mixology);
    CHECK (master.sync (b, 2, 300));
    CHECK (master.style().key == "trap.drill");
    CHECK (vox.sync (c, 3, 300));
    CHECK (vox.style().name == "Trap & Drill - Drill");
    CHECK_FALSE (vox.sync (c, 3, 400));   // nothing new

    // Then picked in Voxology (later): Mixology follows.
    vox.pick ("rnb.smooth", "R&B & Soul - Smooth R&B");
    CHECK_FALSE (vox.sync (c, 3, 500));
    CHECK (mix.sync (a, 1, 600));
    CHECK (mix.style().key == "rnb.smooth");

    // An older saved choice (a project reopened) doesn't beat the newer one.
    suite::SongStyle old (suite::StyleInfo::From::masterology);
    REQUIRE (old.restore ("pop.kpop|Pop - K-Pop|5|3"));
    CHECK (old.sync (b, 4, 700));
    CHECK (old.style().key == "rnb.smooth");

    // Saved text round trip; older projects (no text) restore nothing.
    suite::SongStyle again (suite::StyleInfo::From::mixology);
    REQUIRE (again.restore (mix.save()));
    CHECK (again.style() == mix.style());
    CHECK (again.style().from == suite::StyleInfo::From::voxology);
    CHECK_FALSE (again.restore (""));
    CHECK_FALSE (again.restore ("hiphop.modern"));

    // Everyone that held it closes: it lapses at once, so the next project starts from its own saved choices.
    mix.leave (a, 1); vox.leave (c, 3); master.leave (b, 2); old.leave (b, 4);
    CHECK_FALSE (a.readStyle (got, 700));
    suite::SongStyle next (suite::StyleInfo::From::masterology);
    REQUIRE (next.restore ("pop.kpop|Pop - K-Pop|5|3"));
    CHECK_FALSE (next.sync (b, 5, 800));
    CHECK (next.style().key == "pop.kpop");

    // And when nobody refreshes it, it lapses after kAliveMs.
    CHECK (a.readStyle (got, 800 + suite::kAliveMs));
    CHECK_FALSE (a.readStyle (got, 800 + suite::kAliveMs + 1));
}

TEST_CASE ("Suite: the DAW is a second shared choice, apart from the style; Voxology's recording warnings reach Mixology", "[suite]")
{
    Memory mem;
    suite::Link a (mem.data()), b (mem.data());
    suite::SongStyle style (suite::StyleInfo::From::mixology);
    suite::SharedChoice dawMix (suite::StyleInfo::From::mixology, suite::Choice::daw);
    suite::SharedChoice dawVox (suite::StyleInfo::From::voxology, suite::Choice::daw);

    style.pick ("trap.drill", "Trap & Drill - Drill");
    style.sync (a, 1, 100);
    dawVox.setUnpicked ("cubase", "Cubase");        // detected from the host, never picked: not shared
    CHECK_FALSE (dawVox.sync (b, 2, 100));
    suite::StyleInfo got;
    CHECK_FALSE (a.readChoice (suite::Choice::daw, got, 100));
    REQUIRE (a.readChoice (suite::Choice::style, got, 100));
    CHECK (got.key == "trap.drill");

    dawMix.pick ("studioone", "Studio One");      // picked in Mixology: Voxology follows, the style stays
    dawMix.sync (a, 1, 200);
    CHECK (dawVox.sync (b, 2, 300));
    CHECK (dawVox.style().key == "studioone");
    REQUIRE (b.readChoice (suite::Choice::style, got, 300));
    CHECK (got.key == "trap.drill");
    dawMix.leave (a, 1); dawVox.leave (b, 2);
    CHECK_FALSE (a.readChoice (suite::Choice::daw, got, 300));
    CHECK (a.readChoice (suite::Choice::style, got, 300));   // leaving the DAW leaves the style alone

    // A vocal recorded too hot: the last Auto-Edit's findings go with the vocal's slot.
    const int slot = b.claimVocal (9, 400);
    REQUIRE (slot >= 0);
    suite::VocalInfo v;
    v.trackName = "Lead Vox 1 - Verse";
    v.flags = suite::VocalInfo::processing | suite::VocalInfo::recordedHot | suite::VocalInfo::recordedClipped;
    v.recordedPeakDb = 0.2f;
    v.clippedSpots = 29;
    v.version = 1;
    b.publishVocal (slot, 9, v, 400);
    const auto vocals = a.vocals (500);
    REQUIRE (vocals.size() == 1);
    CHECK ((vocals[0].flags & suite::VocalInfo::recordedClipped) != 0);
    CHECK (vocals[0].recordedPeakDb == Catch::Approx (0.2f));
    CHECK (vocals[0].clippedSpots == 29);
}

TEST_CASE ("Suite: the DAW is found from the host program", "[suite]")
{
    CHECK (suite::dawFromHostPath ("C:\\Program Files\\Steinberg\\Cubase 14\\Cubase14.exe") == "cubase");
    CHECK (suite::dawFromHostPath ("C:\\Program Files\\Steinberg\\Nuendo 13\\Nuendo13.exe") == "cubase");
    CHECK (suite::dawFromHostPath ("C:\\Program Files\\PreSonus\\Studio One 7\\Studio One.exe") == "studioone");
    CHECK (suite::dawFromHostPath ("C:\\Program Files\\Image-Line\\FL Studio 2024\\FL64.exe") == "fl");
    CHECK (suite::dawFromHostPath ("C:\\ProgramData\\Ableton\\Live 12 Suite\\Program\\Ableton Live 12 Suite.exe") == "ableton");
    CHECK (suite::dawFromHostPath ("C:\\Program Files\\REAPER (x64)\\reaper.exe") == "reaper");
    CHECK (suite::dawFromHostPath ("C:\\Program Files\\Bitwig Studio\\Bitwig Studio.exe") == "bitwig");
    CHECK (suite::dawFromHostPath ("/Applications/Logic Pro.app/Contents/MacOS/Logic Pro") == "logic");
    CHECK (suite::dawFromHostPath ("C:\\Program Files\\Cakewalk\\Cakewalk Core\\Cakewalk.exe") == "cakewalk");
    CHECK (suite::dawFromHostPath ("C:\\Program Files\\Cakewalk\\Sonar\\Sonar.exe") == "cakewalk");
    CHECK (suite::dawFromHostPath ("C:\\Tools\\pluginval.exe") == "other");
    CHECK (suite::dawFromHostPath ("") == "other");
    CHECK (std::string (suite::dawName ("studioone")) == "Studio One");
    CHECK (std::string (suite::dawName ("nope")) == "Other DAW");
}

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
    g.version = suite::packVersion (0, 26, 0);
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

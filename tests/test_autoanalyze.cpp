#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Signals.h"
#include "polisher/AutoAnalyze.h"
#include "polisher/AutoMaster.h"
#include "polisher/SongListener.h"

#include <random>

using namespace polisher;
using namespace polisher::autoanalyze;
using Catch::Matchers::WithinAbs;

namespace {

const Edit* find (const std::vector<Edit>& edits, const std::string& module)
{
    for (const auto& e : edits)
        if (e.module == module) return &e;
    return nullptr;
}

const Change* change (const Edit* e, const std::string& id)
{
    if (e != nullptr)
        for (const auto& c : e->changes)
            if (c.id == id) return &c;
    return nullptr;
}

void append (AudioClip& song, const AudioClip& part)
{
    if (song.channels.empty()) { song = part; return; }
    for (size_t c = 0; c < song.channels.size(); ++c)
        song.channels[c].insert (song.channels[c].end(), part.channels[c].begin(), part.channels[c].end());
}

void listen (SongListener& l, const AudioClip& clip, double startSeconds)
{
    std::mt19937 rng (3);
    std::uniform_int_distribution<int> size (64, 4096);
    const int n = clip.numSamples();
    for (int pos = 0; pos < n;)
    {
        const int len = std::min (size (rng), n - pos);
        l.process (clip.channels[0].data() + pos, clip.channels[1].data() + pos, len, startSeconds < 0.0 ? -1.0 : startSeconds + pos / clip.sampleRate);
        pos += len;
    }
}

} // namespace

TEST_CASE ("Auto Analyze: what each control belongs to, and what it never moves", "[autoanalyze]")
{
    CHECK (moduleOf ("eqGain3") == "eq");
    CHECK (moduleOf ("dynThr2") == "dyn");
    CHECK (moduleOf ("dynX1") == "dyn");
    CHECK (moduleOf ("clipOn") == "clip");
    CHECK (moduleOf ("clipLevel") == "clip");
    CHECK (moduleOf ("drive") == "max");
    CHECK (moduleOf ("inTrim") == "max");
    CHECK (moduleOf ("stemVoc") == "stem");
    CHECK (moduleOf ("ulSquashSet") == "ul");
    // The switches are yours, and what Auto-Master always sets the same way stays where you put it.
    for (const char* id : { "bypass", "eqOn", "ulOn", "dynOn", "satOn", "eqFreq3", "eqMode2", "glueRatio", "glueGain",
                            "dynImpact1", "dynSustain4", "dynChannels", "satChannels", "maxBassFirst", "stemOn", "stemKeep" })
        CHECK (moduleOf (id).empty());

    // chainValues holds every control the plug-in sets from a chain.
    MasterChainParams c;
    c.clipper.enabled = true;
    c.clipper.thresholdDb = -2.5;
    c.limiter.ceilingDbtp = -1.5;
    const auto v = chainValues (c);
    CHECK_THAT (v.at ("clipLevel"), WithinAbs (-1.0, 1.0e-9));
    CHECK (v.count ("eqGain6") == 1);
    CHECK (v.count ("dynRel4") == 1);
    CHECK (v.count ("ulSquashSet") == 1);
    c.clipper.enabled = false;
    CHECK (chainValues (c).count ("clipLevel") == 0);   // the clipper's knobs only when it's on
}

TEST_CASE ("Auto Analyze moves a control by what Auto-Master changed, keeping the user's own offset", "[autoanalyze]")
{
    const Values planned { { "eqGain1", 1.0 }, { "eqGain2", 0.0 }, { "drive", 6.0 }, { "satMode", 0.0 }, { "dynX1", 120.0 },
                           { "dynThr1", -20.0 } };
    SECTION ("Nothing changed: nothing offered, even where the user moved a knob")
    {
        Values current = planned;
        current["eqGain1"] = 2.5;
        current["drive"] = 4.0;
        CHECK (planEdits (planned, planned, current).empty());
    }
    SECTION ("A knob moves by the change, from where it is now")
    {
        Values fresh = planned, current = planned;
        fresh["eqGain1"] = 3.0;    // Auto-Master would now boost 2 dB more
        current["eqGain1"] = 2.0;  // the user had added 1 dB
        fresh["eqGain2"] = 0.3;    // too small on its own, goes with the EQ edit
        const auto edits = planEdits (planned, fresh, current);
        REQUIRE (edits.size() == 1);
        const auto* g1 = change (find (edits, "eq"), "eqGain1");
        REQUIRE (g1 != nullptr);
        CHECK_THAT (g1->from, WithinAbs (2.0, 1.0e-9));
        CHECK_THAT (g1->to, WithinAbs (4.0, 1.0e-9));
        CHECK (g1->shown);
        const auto* g2 = change (find (edits, "eq"), "eqGain2");
        REQUIRE (g2 != nullptr);
        CHECK_FALSE (g2->shown);
    }
    SECTION ("Small changes alone aren't offered")
    {
        Values fresh = planned;
        fresh["eqGain1"] = 1.5;
        fresh["drive"] = 6.5;
        fresh["dynThr1"] = -19.0;
        CHECK (planEdits (planned, fresh, planned).empty());
    }
    SECTION ("Frequencies and times move by the ratio; a choice goes to the new choice")
    {
        Values fresh = planned, current = planned;
        fresh["dynX1"] = 180.0;
        current["dynX1"] = 100.0;
        fresh["satMode"] = 1.0;
        const auto edits = planEdits (planned, fresh, current);
        REQUIRE (change (find (edits, "dyn"), "dynX1") != nullptr);
        CHECK_THAT (change (find (edits, "dyn"), "dynX1")->to, WithinAbs (150.0, 1.0e-9));
        REQUIRE (change (find (edits, "sat"), "satMode") != nullptr);
        CHECK_THAT (change (find (edits, "sat"), "satMode")->to, WithinAbs (1.0, 1.0e-9));
        // Already on the new choice: nothing to do there.
        current["satMode"] = 1.0;
        CHECK (find (planEdits (planned, fresh, current), "sat") == nullptr);
        // Auto-Master didn't change its mind: the user's own choice stays.
        CHECK (find (planEdits (planned, planned, current), "sat") == nullptr);
    }
    SECTION ("Edits come in signal-chain order")
    {
        Values fresh = planned;
        fresh["drive"] = 4.0;
        fresh["eqGain2"] = -1.0;
        fresh["satMode"] = 2.0;
        const auto edits = planEdits (planned, fresh, planned);
        REQUIRE (edits.size() == 3);
        CHECK (edits[0].module == "eq");
        CHECK (edits[1].module == "sat");
        CHECK (edits[2].module == "max");
        CHECK_THAT (change (&edits[2], "drive")->to, WithinAbs (4.0, 1.0e-9));
    }
}

TEST_CASE ("Auto Analyze: the clipper switching on, Mono Below on/off, and the Unlimiter's hidden setting", "[autoanalyze]")
{
    const Values planned { { "clipOn", 0.0 }, { "bassMono", 0.0 }, { "ulAmount", 0.0 } };
    Values current = planned, fresh = planned;
    current["clipLevel"] = 0.0;
    current["ulSquashSet"] = 0.0;
    current["bassTame"] = 20.0;
    fresh["clipOn"] = 1.0;
    fresh["clipLevel"] = -1.2;   // not in planned (the clipper was off): goes to Auto-Master's new value
    fresh["bassMono"] = 120.0;
    fresh["ulSquashSet"] = 11.0; // hidden: alone it is no reason for an edit
    const auto edits = planEdits (planned, fresh, current);
    REQUIRE (find (edits, "clip") != nullptr);
    CHECK_THAT (change (find (edits, "clip"), "clipLevel")->to, WithinAbs (-1.2, 1.0e-9));
    REQUIRE (change (find (edits, "bass"), "bassMono") != nullptr);
    CHECK_THAT (change (find (edits, "bass"), "bassMono")->to, WithinAbs (120.0, 1.0e-9));
    CHECK (find (edits, "ul") == nullptr);

    fresh["ulAmount"] = 50.0;
    const auto withUl = planEdits (planned, fresh, current);
    REQUIRE (change (find (withUl, "ul"), "ulSquashSet") != nullptr);
    CHECK_FALSE (change (find (withUl, "ul"), "ulSquashSet")->shown);
}

TEST_CASE ("Auto Analyze: steps up and down count alike, and a step read back from a float still counts", "[autoanalyze]")
{
    // Mono Below 80 -> 100 Hz and 100 -> 80 Hz are the same size of step.
    for (auto [from, to] : { std::pair { 80.0, 100.0 }, std::pair { 100.0, 80.0 }, std::pair { 100.0, 120.0 }, std::pair { 120.0, 100.0 } })
    {
        const Values planned { { "bassMono", from } };
        CHECK (find (planEdits (planned, { { "bassMono", to } }, planned), "bass") != nullptr);
    }
    // A plan stored as a float (-20.3 reads -20.2999992) and a change of exactly the worth.
    const Values planned { { "eqGain1", static_cast<double> (0.35f) }, { "dynThr1", static_cast<double> (-20.3f) } };
    CHECK (find (planEdits (planned, { { "eqGain1", 1.1 }, { "dynThr1", -20.3 } }, planned), "eq") != nullptr);
    CHECK (find (planEdits (planned, { { "eqGain1", 0.35 }, { "dynThr1", -18.8 } }, planned), "dyn") != nullptr);
    // Mono Below switched off by you stays off while Auto-Master keeps it on; Auto-Master switching it off is offered.
    CHECK (planEdits ({ { "bassMono", 80.0 } }, { { "bassMono", 120.0 } }, { { "bassMono", 0.0 } }).empty());
    const auto off = planEdits ({ { "bassMono", 80.0 } }, { { "bassMono", 0.0 } }, { { "bassMono", 80.0 } });
    REQUIRE (change (find (off, "bass"), "bassMono") != nullptr);
    CHECK (change (find (off, "bass"), "bassMono")->to == 0.0);
    // A stretch heard twice counts once.
    CHECK_THAT (overlapSeconds ({ { 10.0, 13.0 }, { 10.0, 13.0 } }, { { 0.0, 60.0 } }), WithinAbs (3.0, 1.0e-9));
}

TEST_CASE ("Auto Analyze changes the cards only when the advice really changed", "[autoanalyze]")
{
    const Values planned { { "eqGain1", 0.0 }, { "drive", 6.0 } };
    Values a = planned, b = planned;
    a["eqGain1"] = -2.0;
    b["eqGain1"] = -2.2;   // the same advice, a hair different
    CHECK (sameAdvice (planEdits (planned, a, planned), planEdits (planned, b, planned)));
    b["eqGain1"] = -3.0;
    CHECK_FALSE (sameAdvice (planEdits (planned, a, planned), planEdits (planned, b, planned)));
    b = a;
    b["drive"] = 4.0;
    CHECK_FALSE (sameAdvice (planEdits (planned, a, planned), planEdits (planned, b, planned)));
    CHECK (sameAdvice ({}, {}));
}

TEST_CASE ("Timeline spans: how much two parts share", "[autoanalyze]")
{
    CHECK_THAT (overlapSeconds ({ { 10.0, 22.0 } }, { { 15.0, 40.0 } }), WithinAbs (7.0, 1.0e-9));
    CHECK_THAT (overlapSeconds ({ { 10.0, 22.0 } }, { { 30.0, 40.0 } }), WithinAbs (0.0, 1.0e-9));
    CHECK_THAT (overlapSeconds ({ { 0.0, 5.0 }, { 60.0, 67.0 } }, { { 3.0, 62.0 } }), WithinAbs (4.0, 1.0e-9));
}

TEST_CASE ("Song listener marks the parts that play at the hook's level, repeats included", "[autoanalyze][songlistener]")
{
    const double sr = 44100.0;
    AudioClip song;
    append (song, testsig::musicLike (sr, 30.0, -20.0, 1));   //   0-30  verse
    append (song, testsig::musicLike (sr, 20.0, -12.0, 2));   //  30-50  chorus
    append (song, testsig::musicLike (sr, 30.0, -20.0, 3));   //  50-80  verse
    append (song, testsig::musicLike (sr, 20.0, -12.4, 4));   //  80-100 chorus 2, a little quieter
    append (song, testsig::musicLike (sr, 10.0, -26.0, 5));   // 100-110 outro
    SongListener l;
    l.prepare (sr, 12.0, 600.0);
    listen (l, song, 5.0);   // the song starts at 0:05 on the timeline
    const auto st = l.stats();
    REQUIRE (st.positionFromHost);
    REQUIRE (st.hookSpans.size() == 2);
    // Both choruses .
    // (timeline = song + 5 s: the choruses play at 0:35-0:55 and 1:25-1:45; a window reaching a few seconds
    // into the verse next to a chorus is still within 1 LU of it)
    CHECK (st.hookSpans[0].start >= 31.0);
    CHECK (st.hookSpans[0].start <= 35.5);
    CHECK (st.hookSpans[0].end >= 54.5);
    CHECK (st.hookSpans[0].end <= 59.0);
    CHECK (st.hookSpans[1].start >= 82.0);
    CHECK (st.hookSpans[1].start <= 88.0);
    CHECK (st.hookSpans[1].end >= 102.0);
    CHECK (st.hookSpans[1].end <= 107.0);
    CHECK (overlapSeconds (st.loudestSpans, st.hookSpans) >= 11.9);   // the loudest part is one of them

    // Without timeline positions: none.
    SongListener n;
    n.prepare (sr, 12.0, 600.0);
    listen (n, song, -1.0);
    CHECK (n.stats().hookSpans.empty());

    // A reverb tail heard after Stop (no timeline position) doesn't erase them: the second chorus still counts.
    SongListener t;
    t.prepare (sr, 12.0, 600.0);
    listen (t, song, 5.0);
    listen (t, testsig::musicLike (sr, 0.4, -26.0, 9), -1.0);
    const auto tail = t.stats();
    REQUIRE (tail.hookSpans.size() == 2);
    for (size_t i = 0; i < 2; ++i)
    {
        CHECK_THAT (tail.hookSpans[i].start, WithinAbs (st.hookSpans[i].start, 1.0e-9));
        CHECK_THAT (tail.hookSpans[i].end, WithinAbs (st.hookSpans[i].end, 1.0e-9));
    }

    // A piece without a position inside the second chorus splits its span there; the rest keeps its place.
    SongListener m;
    m.prepare (sr, 12.0, 600.0);
    AudioClip first, second;
    first.sampleRate = second.sampleRate = sr;
    const auto cut = static_cast<size_t> (90.0 * sr), gap = static_cast<size_t> (0.3 * sr);
    for (const auto& ch : song.channels)
    {
        first.channels.emplace_back (ch.begin(), ch.begin() + static_cast<std::ptrdiff_t> (cut));
        second.channels.emplace_back (ch.begin() + static_cast<std::ptrdiff_t> (cut + gap), ch.end());
    }
    AudioClip hole;
    hole.sampleRate = sr;
    for (const auto& ch : song.channels)
        hole.channels.emplace_back (ch.begin() + static_cast<std::ptrdiff_t> (cut), ch.begin() + static_cast<std::ptrdiff_t> (cut + gap));
    listen (m, first, 5.0);
    listen (m, hole, -1.0);
    listen (m, second, 5.0 + (cut + gap) / sr);
    const auto split = m.stats();
    REQUIRE (split.hookSpans.size() == 3);
    CHECK_THAT (split.hookSpans[0].start, WithinAbs (st.hookSpans[0].start, 0.2));
    CHECK_THAT (split.hookSpans[1].start, WithinAbs (st.hookSpans[1].start, 0.2));
    CHECK (split.hookSpans[1].end <= 95.0 + 1.0e-6);
    CHECK (split.hookSpans[2].start >= 95.3 - 1.0e-6);
}

TEST_CASE ("Auto Analyze offers nothing for the same mix and follows a mix that got louder or brighter", "[autoanalyze][automaster]")
{
    const double sr = 44100.0;
    const auto mix = testsig::musicLike (sr, 12.0, -6.0, 11);
    AutoMasterSettings s;
    s.target = LoudnessTarget::loud;
    const auto first = autoMaster (mix, s);
    REQUIRE (first.ok);
    const auto planned = chainValues (first.chain);

    // Heard again, unchanged: the same decisions, nothing to offer.
    const auto again = autoMaster (mix, s);
    CHECK (planEdits (planned, chainValues (again.chain), planned).empty());

    // The mix arrives 3 dB louder: Drive (or Input Trim) comes down by about that, Dynamics follows.
    const auto louder = autoMaster (withGain (mix, 3.0), s);
    REQUIRE (louder.ok);
    const auto edits = planEdits (planned, chainValues (louder.chain), planned);
    const auto* max = find (edits, "max");
    for (const auto& e : edits) for (const auto& c : e.changes) UNSCOPED_INFO (e.module << " " << c.id << " " << c.from << " -> " << c.to);
    UNSCOPED_INFO (first.summary << " || " << louder.summary);
    const auto* drive = change (max, "drive");
    const auto* trim = change (max, "inTrim");
    const double moved = (drive ? drive->to - drive->from : 0.0) + (trim ? trim->to - trim->from : 0.0);
    CHECK (moved < -2.0);
    CHECK (moved > -4.0);

    // The mix arrives brighter (Presence +4 dB): the EQ's Presence band comes down.
    TonalEQParams air;
    air.gainDb[5] = 4.0;
    const auto bright = autoMaster (renderEq (mix, air), s);
    REQUIRE (bright.ok);
    const auto toneEdits = planEdits (planned, chainValues (bright.chain), planned);
    for (const auto& e : toneEdits) for (const auto& c : e.changes) UNSCOPED_INFO (e.module << " " << c.id << " " << c.from << " -> " << c.to);
    const auto* g6 = change (find (toneEdits, "eq"), "eqGain6");
    REQUIRE (g6 != nullptr);
    CHECK (g6->to < g6->from - 0.7);
}

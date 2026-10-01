#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "polisher/LevelMatch.h"

#include <cmath>

using namespace polisher;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 512;

/** Feeds `seconds` of steady readings. peakDb: the original's sample peak. */
void run (LevelMatch& m, double inLufs, double outLufs, double peakDb, bool onB, double seconds)
{
    const double peak = std::pow (10.0, peakDb / 20.0);
    for (int i = 0; i < static_cast<int> (seconds * kSr / kBlock); ++i)
        m.update (inLufs, outLufs, peak, kBlock, onB);
}

} // namespace

TEST_CASE ("MATCH turns a louder master down to the original", "[levelmatch]")
{
    LevelMatch m;
    m.prepare (kSr);
    run (m, -14.0, -9.0, -3.0, true, 20.0);
    CHECK_THAT (m.gapDb(), WithinAbs (5.0, 0.01));
    CHECK_THAT (m.gainForBDb(), WithinAbs (-5.0, 0.01));
    CHECK_THAT (m.gainForADb(), WithinAbs (0.0, 1e-9));   // A peaks at -3 dBFS: untouched
}

TEST_CASE ("MATCH never boosts: a quieter master turns the original down", "[levelmatch]")
{
    // Stream -14 on a hot -10 LUFS mix: the old MATCH boosted B by 4 dB, past its ceiling.
    LevelMatch m;
    m.prepare (kSr);
    run (m, -10.0, -14.0, -2.0, true, 20.0);
    CHECK (m.gainForBDb() <= 0.0);
    CHECK_THAT (m.gainForBDb(), WithinAbs (0.0, 1e-9));
    CHECK_THAT (m.gainForADb(), WithinAbs (-4.0, 0.01));
}

TEST_CASE ("MATCH gives a clipping original headroom, and B the same turn-down", "[levelmatch]")
{
    // The v0.8 test: the mix reached Stereo Out at about +6 dBFS and 1.1 LU quieter than the master.
    LevelMatch m;
    m.prepare (kSr);
    run (m, -10.4, -9.3, 6.1, true, 20.0);
    const double a = m.gainForADb(), b = m.gainForBDb();
    CHECK_THAT (6.1 + a, WithinAbs (LevelMatch::kPeakTargetDb, 0.01));    // A's loudest sample lands at -1 dBFS
    CHECK_THAT ((-10.4 + a) - (-9.3 + b), WithinAbs (0.0, 0.01));        // still the same loudness
    CHECK (b <= -1.1);

    // The headroom is remembered: a quiet passage doesn't bring A's level back up...
    run (m, -10.4, -9.3, -12.0, true, 5.0);
    CHECK_THAT (m.gainForADb(), WithinAbs (a, 0.01));
    // ...until RESET.
    m.resetPeak();
    run (m, -10.4, -9.3, -3.0, true, 1.0);
    CHECK_THAT (m.gainForADb(), WithinAbs (0.0, 1e-9));
}

TEST_CASE ("MATCH learns only while B plays, and holds one steady level", "[levelmatch]")
{
    LevelMatch m;
    m.prepare (kSr);
    run (m, -14.0, -14.0, -3.0, false, 10.0);    // on A the output is the input: nothing to learn
    run (m, -14.0, -8.0, -3.0, false, 10.0);
    CHECK_THAT (m.gapDb(), WithinAbs (0.0, 1e-9));

    run (m, -14.0, -8.0, -3.0, true, 30.0);      // steady music on B: settles on the gap
    CHECK_THAT (m.gapDb(), WithinAbs (6.0, 0.05));

    run (m, -80.0, -80.0, -3.0, true, 10.0);     // silence (stopped transport) keeps what was learned
    CHECK_THAT (m.gapDb(), WithinAbs (6.0, 0.05));
    run (m, NAN, -8.0, -3.0, true, 1.0);
    CHECK (std::isfinite (m.gainForBDb()));

    // Verse / chorus: the limiter works harder on the loud chorus, so the instant gap swings 4 dB.
    // MATCH must not ride the music: after settling, B's gain moves by well under that.
    LevelMatch v;
    v.prepare (kSr);
    double lo = 0.0, hi = -100.0;
    for (int section = 0; section < 16; ++section)
    {
        const bool chorus = section % 2 == 0;
        for (int i = 0; i < static_cast<int> (4.0 * kSr / kBlock); ++i)
        {
            v.update (chorus ? -10.0 : -20.0, chorus ? -6.0 : -12.0, 0.5, kBlock, true);
            if (section >= 8) { lo = std::min (lo, v.gainForBDb()); hi = std::max (hi, v.gainForBDb()); }
        }
    }
    INFO ("B gain range " << lo << " .. " << hi);
    CHECK (hi - lo < 1.5);
}

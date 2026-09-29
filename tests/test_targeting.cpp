#include "Signals.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using namespace polisher;

TEST_CASE ("Auto-Master loudness step lands on the target under the ceiling", "[targeting]")
{
    const auto mix = testsig::musicLike (44100.0, 30.0, -6.0);
    const double inputLufs = measure (mix).integratedLufs;
    INFO ("input " << inputLufs << " LUFS");

    for (double target : { -14.0, -11.0, -9.5 })
    {
        LimiterParams p;
        p.ceilingDbtp = -1.0;
        const auto r = findDriveForTarget (mix, target, p);
        INFO ("target " << target << ": drive " << r.driveDb << " dB -> " << r.output.integratedLufs
              << " LUFS, TP " << r.output.truePeakDbtp << ", GR " << r.averageGainReductionDb
              << " dB in " << r.iterations << " passes");
        CHECK (r.reachedTarget);
        CHECK_THAT (r.output.integratedLufs, WithinAbs (target, 0.1));
        CHECK (r.output.truePeakDbtp <= -0.95);
        CHECK (r.iterations <= 8);
    }
}

TEST_CASE ("An unreachable target is reported honestly, not faked", "[targeting]")
{
    // A limiter alone can't make this very dynamic mix hit -6 LUFS at -1 dBTP; that needs the
    // clipper and saturation stages. The search must say so and still respect the ceiling.
    const auto mix = testsig::musicLike (44100.0, 20.0, -6.0);
    LimiterParams p;
    p.ceilingDbtp = -1.0;
    const auto r = findDriveForTarget (mix, -6.0, p);
    CHECK_FALSE (r.reachedTarget);
    CHECK (r.output.integratedLufs < -6.5);
    CHECK (r.output.truePeakDbtp <= -0.95);
}

TEST_CASE ("Silence is left alone", "[targeting]")
{
    const auto r = findDriveForTarget (testsig::silence (48000.0, 2, 3.0), -14.0, {});
    CHECK_FALSE (r.reachedTarget);
    CHECK (r.driveDb == 0.0);
}

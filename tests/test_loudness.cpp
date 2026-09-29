// Loudness meter checks modelled on the EBU Tech 3341 / 3342 minimum-requirement test cases.

#include "Signals.h"
#include "polisher/LoudnessMeter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using namespace polisher;

TEST_CASE ("K-weighted stereo 1 kHz sine reads its peak level in LUFS (EBU 3341 case 1)", "[loudness]")
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const auto s = measure (testsig::sine (sr, 2, 1000.0, -23.0, 20.0));
        INFO ("sample rate " << sr);
        CHECK_THAT (s.integratedLufs, WithinAbs (-23.0, 0.1));
    }
}

TEST_CASE ("Level changes track 1:1 (EBU 3341 case 2)", "[loudness]")
{
    const auto s = measure (testsig::sine (48000.0, 2, 1000.0, -33.0, 20.0));
    CHECK_THAT (s.integratedLufs, WithinAbs (-33.0, 0.1));
}

TEST_CASE ("Relative gate ignores the quiet intro and outro (EBU 3341 case 3)", "[loudness]")
{
    auto clip = testsig::silence (48000.0, 2, 0.0);
    testsig::appendSine (clip, 1000.0, -36.0, 10.0);
    testsig::appendSine (clip, 1000.0, -23.0, 60.0);
    testsig::appendSine (clip, 1000.0, -36.0, 10.0);
    CHECK_THAT (measure (clip).integratedLufs, WithinAbs (-23.0, 0.1));
}

TEST_CASE ("Absolute gate ignores material below -70 LUFS (EBU 3341 case 4)", "[loudness]")
{
    auto clip = testsig::silence (48000.0, 2, 0.0);
    testsig::appendSine (clip, 1000.0, -72.0, 10.0);
    testsig::appendSine (clip, 1000.0, -26.0, 60.0);
    testsig::appendSine (clip, 1000.0, -20.0, 60.0);
    testsig::appendSine (clip, 1000.0, -26.0, 60.0);
    testsig::appendSine (clip, 1000.0, -72.0, 10.0);
    CHECK_THAT (measure (clip).integratedLufs, WithinAbs (-23.0, 0.1));
}

TEST_CASE ("Momentary and short-term follow a steady tone", "[loudness]")
{
    LoudnessMeter m;
    m.prepare (48000.0, 2);
    const auto clip = testsig::sine (48000.0, 2, 1000.0, -18.0, 5.0);
    const float* ptrs[] = { clip.channels[0].data(), clip.channels[1].data() };
    m.process (ptrs, 2, clip.numSamples());
    CHECK_THAT (m.momentaryLufs(), WithinAbs (-18.0, 0.1));
    CHECK_THAT (m.shortTermLufs(), WithinAbs (-18.0, 0.1));
}

TEST_CASE ("Silence reads -inf and never crashes", "[loudness]")
{
    const auto s = measure (testsig::silence (48000.0, 2, 5.0));
    CHECK (std::isinf (s.integratedLufs));
    CHECK (s.loudnessRangeLu == 0.0);
}

TEST_CASE ("Loudness range of a 10 LU step (EBU 3342 case 1)", "[loudness][lra]")
{
    auto clip = testsig::silence (48000.0, 2, 0.0);
    testsig::appendSine (clip, 1000.0, -20.0, 20.0);
    testsig::appendSine (clip, 1000.0, -30.0, 20.0);
    CHECK_THAT (measure (clip).loudnessRangeLu, WithinAbs (10.0, 1.0));
}

TEST_CASE ("Loudness range of a 5 LU step (EBU 3342 case 2)", "[loudness][lra]")
{
    auto clip = testsig::silence (48000.0, 2, 0.0);
    testsig::appendSine (clip, 1000.0, -20.0, 20.0);
    testsig::appendSine (clip, 1000.0, -15.0, 20.0);
    CHECK_THAT (measure (clip).loudnessRangeLu, WithinAbs (5.0, 1.0));
}

TEST_CASE ("Loudness range ignores material more than 20 LU down (relative gate)", "[loudness][lra]")
{
    auto clip = testsig::silence (48000.0, 2, 0.0);
    testsig::appendSine (clip, 1000.0, -50.0, 20.0);   // far below the -20 LU relative gate
    testsig::appendSine (clip, 1000.0, -20.0, 20.0);
    testsig::appendSine (clip, 1000.0, -30.0, 20.0);
    testsig::appendSine (clip, 1000.0, -50.0, 20.0);
    CHECK_THAT (measure (clip).loudnessRangeLu, WithinAbs (10.0, 1.0));
}

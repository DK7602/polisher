#include <catch2/catch_test_macros.hpp>

#include "polisher/GainGlide.h"

#include <cmath>
#include <limits>

using polisher::GainGlide;

TEST_CASE ("GainGlide: a new gain arrives in the set time, in equal dB steps, exactly on target", "[glide]")
{
    GainGlide g;
    g.prepare (48000.0, 0.02);
    REQUIRE (g.glideSamples() == 960);
    CHECK (g.current() == 1.0);
    CHECK (g.next() == 1.0);   // at rest: exactly the gain, nothing moves

    g.glideTo (4.0);   // +12 dB
    double before = 1.0, steepest = 0.0, gentlest = 1.0e9;
    for (int i = 0; i < 960; ++i)
    {
        const double v = g.next();
        const double step = 20.0 * std::log10 (v / before);
        steepest = std::max (steepest, step);
        gentlest = std::min (gentlest, step);
        before = v;
        if (i < 959) CHECK (g.isGliding());
    }
    CHECK_FALSE (g.isGliding());
    CHECK (g.current() == 4.0);   // exactly, so a gain back at 1.0 later is bit for bit untouched
    CHECK (steepest - gentlest < 1.0e-9);
    CHECK (std::abs (steepest - 12.0412 / 960.0) < 1.0e-4);

    g.glideTo (1.0);
    for (int i = 0; i < 960; ++i) g.next();
    CHECK (g.current() == 1.0);
}

TEST_CASE ("GainGlide: a target moved while gliding is reached in the time left; a restart takes the full time", "[glide]")
{
    GainGlide g;
    g.prepare (48000.0, 0.02);
    g.glideTo (0.5);
    for (int i = 0; i < 480; ++i) g.next();
    g.glideTo (0.4);   // MATCH learning a little more: must not stretch the glide
    int n = 0;
    while (g.isGliding()) { g.next(); ++n; }
    CHECK (n == 480);
    CHECK (g.current() == 0.4);

    g.glideTo (1.0);
    for (int i = 0; i < 100; ++i) g.next();
    g.glideTo (0.25, true);   // a new A / B side: a whole glide
    n = 0;
    while (g.isGliding()) { g.next(); ++n; }
    CHECK (n == 960);
    CHECK (g.current() == 0.25);

    g.jumpTo (2.0);
    CHECK_FALSE (g.isGliding());
    CHECK (g.next() == 2.0);
}

TEST_CASE ("GainGlide: a gain that isn't a positive number is ignored", "[glide][nonfinite]")
{
    GainGlide g;
    g.prepare (44100.0, 0.02);
    g.jumpTo (0.5);
    for (double bad : { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), 0.0, -1.0 })
    {
        g.glideTo (bad);
        g.jumpTo (bad);
        CHECK_FALSE (g.isGliding());
        CHECK (g.next() == 0.5);
    }
}

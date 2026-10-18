// One NaN or inf sample (a broken plug-in before Masterology, a corrupt region) must never leave a module
// playing NaN, or silently not working, from then on. Every module with memory recovers by itself within a
// block; the plug-in also cleans its input and resets the chain if anything non-finite still comes out
// (tests/plugin).

#include <catch2/catch_test_macros.hpp>

#include "Signals.h"
#include "polisher/BassControl.h"
#include "polisher/GlueCompressor.h"
#include "polisher/MultibandDynamics.h"
#include "polisher/Saturation.h"
#include "polisher/SoftClipper.h"
#include "polisher/StemEQ.h"
#include "polisher/StereoImager.h"
#include "polisher/TonalEQ.h"
#include "polisher/TruePeakLimiter.h"
#include "polisher/Unlimiter.h"

#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace polisher;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr double kBadAt = 1.0;        // s: where the bad sample sits
constexpr double kRecovered = 1.15;   // s: from here on every sample must be finite (latency + a block)

using Process = std::function<void (float* const*, int)>;
/** A module set up to work, and something to run once the bad sample has gone through (or nothing). */
struct Case
{
    std::string name;
    std::function<Process()> make;
};

struct Run
{
    long badSamples = 0;   // non-finite output samples from kRecovered on
    double tailDb = 0.0;   // level of the last second
};

Run run (const Process& process, const AudioClip& clip, float bad, bool bothChannels)
{
    AudioClip c = clip;
    const auto at = static_cast<size_t> (kBadAt * kRate);
    c.channels[0][at] = bad;
    if (bothChannels) c.channels[1][at] = bad;
    const int n = c.numSamples();
    for (int s = 0; s < n; s += kBlock)
    {
        float* ch[2] = { c.channels[0].data() + s, c.channels[1].data() + s };
        process (ch, std::min (kBlock, n - s));
    }
    Run r;
    double e = 0.0;
    const int tail = n - static_cast<int> (kRate);
    for (int i = static_cast<int> (kRecovered * kRate); i < n; ++i)
        for (const auto& ch : c.channels)
        {
            const float v = ch[static_cast<size_t> (i)];
            if (! std::isfinite (v)) ++r.badSamples;
            else if (i >= tail) e += static_cast<double> (v) * v;
        }
    r.tailDb = 10.0 * std::log10 (e / (2.0 * kRate) + 1.0e-30);
    return r;
}

std::vector<Case> cases()
{
    std::vector<Case> v;
    v.push_back ({ "Unlimiter amount 50, kick", []
    {
        auto u = std::make_shared<Unlimiter>();
        UnlimiterParams p; p.amount = 50.0; p.focus = UnlimiterFocus::kick;
        u->setParams (p); u->prepare (kRate, 2);
        return Process ([u] (float* const* ch, int n) { u->process (ch, 2, n); });
    } });
    v.push_back ({ "Tonal EQ Low End +2 (subsonic in), Presence +1.5 on the mid", []
    {
        auto e = std::make_shared<TonalEQ>();
        e->prepare (kRate, 2);
        TonalEQParams p; p.gainDb[0] = 2.0; p.gainDb[5] = 1.5; p.channels[5] = EqChannels::mid;
        e->setParams (p);
        return Process ([e] (float* const* ch, int n) { e->process (ch, 2, n); });
    } });
    v.push_back ({ "Stem EQ vocals +2, Keep loudness", []
    {
        auto s = std::make_shared<StemEQ>();
        s->prepare (kRate, 2);
        StemEQParams p; p.gainDb[kStemVocals] = 2.0; p.keepLoudness = true;
        s->setParams (p);
        return Process ([s] (float* const* ch, int n) { s->process (ch, 2, n); });
    } });
    v.push_back ({ "Stem EQ off while the bad sample passes, switched on 0.5 s later", []
    {
        // The model runs while the stage is neutral (its output is the plain delay): a poisoned model
        // used to show only once Stem EQ was switched on, as NaN for good.
        auto s = std::make_shared<StemEQ>();
        s->prepare (kRate, 2);
        auto at = std::make_shared<long> (0);
        return Process ([s, at] (float* const* ch, int n)
        {
            if (*at >= static_cast<long> ((kBadAt + 0.5) * kRate) && StemEQ::isNeutral (s->getParams()))
            {
                StemEQParams p; p.gainDb[kStemBass] = 3.0;
                s->setParams (p);
            }
            s->process (ch, 2, n);
            *at += n;
        });
    } });
    v.push_back ({ "Tone Steady 50 %", []
    {
        auto s = std::make_shared<StemEQ>();
        s->prepare (kRate, 2);
        ToneSteadyParams t; t.amount = 50.0;
        s->setToneSteady (t);
        return Process ([s] (float* const* ch, int n) { s->process (ch, 2, n); });
    } });
    v.push_back ({ "Resonance Tamer 50 %", []
    {
        auto s = std::make_shared<StemEQ>();
        s->prepare (kRate, 2);
        ResonanceParams t; t.amount = 50.0;
        s->setResonance (t);
        return Process ([s] (float* const* ch, int n) { s->process (ch, 2, n); });
    } });
    v.push_back ({ "Bass Control: Mono Below 100, Sub Tame 30 %, Punch 30 %", []
    {
        auto b = std::make_shared<BassControl>();
        BassParams p; p.monoHz = 100.0; p.tame = 0.3; p.punch = 0.3;
        b->setParams (p); b->prepare (kRate, 2);
        return Process ([b] (float* const* ch, int n) { b->process (ch, 2, n); });
    } });
    v.push_back ({ "Glue 2:1 at -20 dB, Ignore Lows on", []
    {
        auto g = std::make_shared<GlueCompressor>();
        GlueParams p; p.ratio = 2.0; p.thresholdDb = -20.0; p.sidechainHz = 90.0;
        g->setParams (p); g->prepare (kRate, 2);
        return Process ([g] (float* const* ch, int n) { g->process (ch, 2, n); });
    } });
    v.push_back ({ "Dynamics 2:1 at -20 dB, every band, Impact and Sustain", []
    {
        auto d = std::make_shared<MultibandDynamics>();
        DynamicsParams p;
        for (auto& b : p.bands) { b.ratio = 2.0; b.thresholdDb = -20.0; }
        p.bands[0].impact = 40.0; p.bands[1].sustain = 40.0;
        d->setParams (p); d->prepare (kRate, 2);
        return Process ([d] (float* const* ch, int n) { d->process (ch, 2, n); });
    } });
    v.push_back ({ "Dynamics on the side only", []
    {
        auto d = std::make_shared<MultibandDynamics>();
        DynamicsParams p;
        for (auto& b : p.bands) { b.ratio = 3.0; b.thresholdDb = -30.0; }
        p.channels = DynChannels::side;
        d->setParams (p); d->prepare (kRate, 2);
        return Process ([d] (float* const* ch, int n) { d->process (ch, 2, n); });
    } });
    v.push_back ({ "Imager 90 / 110 / 120 %", []
    {
        auto im = std::make_shared<StereoImager>();
        ImagerParams p; p.lowWidth = 90.0; p.midWidth = 110.0; p.highWidth = 120.0;
        im->setParams (p); im->prepare (kRate);
        return Process ([im] (float* const* ch, int n) { im->process (ch, 2, n); });
    } });
    v.push_back ({ "Saturation tube +6 dB, Mix 50 %", []
    {
        auto s = std::make_shared<Saturation>();
        SaturationParams p; p.mode = SaturationMode::tube; p.driveDb = 6.0; p.mix = 50.0;
        s->setParams (p); s->prepare (kRate, 2);
        return Process ([s] (float* const* ch, int n) { s->process (ch, 2, n); });
    } });
    v.push_back ({ "Saturation tape, Bass + Highs", []
    {
        auto s = std::make_shared<Saturation>();
        SaturationParams p; p.mode = SaturationMode::tape; p.driveDb = 6.0; p.mix = 50.0; p.focus = SaturationFocus::bassHighs;
        s->setParams (p); s->prepare (kRate, 2);
        return Process ([s] (float* const* ch, int n) { s->process (ch, 2, n); });
    } });
    v.push_back ({ "Saturation clip, Highs only, Side", []
    {
        auto s = std::make_shared<Saturation>();
        SaturationParams p; p.mode = SaturationMode::clip; p.driveDb = 9.0; p.mix = 60.0; p.focus = SaturationFocus::highs;
        p.channels = SaturationChannels::side;
        s->setParams (p); s->prepare (kRate, 2);
        return Process ([s] (float* const* ch, int n) { s->process (ch, 2, n); });
    } });
    v.push_back ({ "Soft Clipper at -2 dB", []
    {
        auto k = std::make_shared<SoftClipper>();
        ClipperParams p; p.enabled = true; p.thresholdDb = -2.0;
        k->prepare (2, kRate); k->setParams (p);
        return Process ([k] (float* const* ch, int n) { k->process (ch, 2, n); });
    } });
    v.push_back ({ "Limiter at -1 dBTP, Bass First 50 %", []
    {
        auto l = std::make_shared<TruePeakLimiter>();
        LimiterParams p; p.ceilingDbtp = -1.0; p.driveDb = 6.0; p.bassFirst = 0.5;
        l->setParams (p); l->prepare (kRate, 2);
        return Process ([l] (float* const* ch, int n) { l->process (ch, 2, n); });
    } });
    return v;
}

} // namespace

TEST_CASE ("One NaN or inf sample never leaves a module playing NaN: each recovers within a block", "[nonfinite]")
{
    const auto clip = testsig::trapLike (kRate, 4.0);
    const float values[] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                             -std::numeric_limits<float>::infinity() };
    const char* names[] = { "NaN", "+inf", "-inf" };
    for (const auto& c : cases())
    {
        const Run clean = run (c.make(), clip, clip.channels[0][static_cast<size_t> (kBadAt * kRate)], false);
        REQUIRE (clean.badSamples == 0);
        for (int v = 0; v < 3; ++v)
            for (bool both : { false, true })
            {
                const Run r = run (c.make(), clip, values[v], both);
                INFO (c.name << ", one " << names[v] << (both ? " on both channels" : " on the left") << ": " << r.badSamples
                      << " non-finite samples after " << kRecovered << " s; last second " << r.tailDb << " dB (clean run "
                      << clean.tailDb << " dB)");
                CHECK (r.badSamples == 0);
                CHECK (std::abs (r.tailDb - clean.tailDb) < 0.5);   // working again, not silent or stuck
            }
    }
}

TEST_CASE ("After a NaN or inf on both channels, Glue and Dynamics still compress", "[nonfinite]")
{
    // With both channels hit, Glue's output stayed finite but its detector read silence for good: it never
    // compressed again (0.00 dB of gain reduction where a fresh Glue does -4.5 dB).
    const auto clip = testsig::trapLike (kRate, 4.0);
    for (float bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
    {
        GlueParams gp; gp.ratio = 2.0; gp.thresholdDb = -20.0;
        GlueCompressor hit, fresh;
        hit.setParams (gp); hit.prepare (kRate, 2);
        fresh.setParams (gp); fresh.prepare (kRate, 2);
        DynamicsParams dp;
        for (auto& b : dp.bands) { b.ratio = 2.0; b.thresholdDb = -30.0; }
        MultibandDynamics dynHit, dynFresh;
        dynHit.setParams (dp); dynHit.prepare (kRate, 2);
        dynFresh.setParams (dp); dynFresh.prepare (kRate, 2);

        AudioClip a = clip, b = clip, c = clip, d = clip;
        const auto at = static_cast<size_t> (kBadAt * kRate);
        a.channels[0][at] = a.channels[1][at] = bad;
        c.channels[0][at] = c.channels[1][at] = bad;
        double grHit = 0.0, grFresh = 0.0, dynGrHit = 0.0, dynGrFresh = 0.0;
        for (int s = 0; s < clip.numSamples(); s += kBlock)
        {
            const int n = std::min (kBlock, clip.numSamples() - s);
            float* pa[2] = { a.channels[0].data() + s, a.channels[1].data() + s };
            float* pb[2] = { b.channels[0].data() + s, b.channels[1].data() + s };
            float* pc[2] = { c.channels[0].data() + s, c.channels[1].data() + s };
            float* pd[2] = { d.channels[0].data() + s, d.channels[1].data() + s };
            hit.process (pa, 2, n);
            fresh.process (pb, 2, n);
            dynHit.process (pc, 2, n);
            dynFresh.process (pd, 2, n);
            const bool late = s >= static_cast<int> (2.0 * kRate);   // judged a second after the bad sample
            const double g1 = hit.takeMaxReductionDb(), g2 = fresh.takeMaxReductionDb();
            double d1 = 0.0, d2 = 0.0;
            for (int k = 0; k < kDynBands; ++k) { d1 = std::min (d1, dynHit.takeMaxGrDb (k)); d2 = std::min (d2, dynFresh.takeMaxGrDb (k)); }
            if (late)
            {
                grHit = std::min (grHit, g1); grFresh = std::min (grFresh, g2);
                dynGrHit = std::min (dynGrHit, d1); dynGrFresh = std::min (dynGrFresh, d2);
            }
        }
        INFO ("deepest gain reduction from 2 s on: Glue " << grHit << " dB (fresh " << grFresh << "), Dynamics " << dynGrHit
              << " dB (fresh " << dynGrFresh << ")");
        CHECK (grFresh < -2.0);
        CHECK (std::abs (grHit - grFresh) < 0.25);
        CHECK (dynGrFresh < -2.0);
        CHECK (std::abs (dynGrHit - dynGrFresh) < 0.25);
    }
}

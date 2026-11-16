#pragma once

#include "polisher/BassControl.h"
#include "polisher/GlueCompressor.h"
#include "polisher/LoudnessTargeting.h"
#include "polisher/MultibandDynamics.h"
#include "polisher/Saturation.h"
#include "polisher/StemEQ.h"
#include "polisher/StereoImager.h"
#include "polisher/TonalEQ.h"
#include "polisher/TruePeakLimiter.h"
#include "polisher/Unlimiter.h"
#include "suite/RackLink.h"

#include <array>
#include <string>
#include <vector>

/** Sonicology Rack (Mixology's docs/rack/RACK-PLAN.md): what Masterology tells the Rack view about its modules.
    Framework-free and unit-tested; the plug-in only fills a State from its parameters and meters.

    The 12 modules in signal order: Unlimiter, Tonal EQ, Stem EQ, Tone Steady, Resonance Tamer, Bass Control, Glue,
    Dynamics, Stereo Imager, Saturation, Soft Clipper, Maximizer (with Input Trim, Drive, the ceiling and the
    loudness it aims for). Each with its state (on; idle when switched on but set to do nothing; off), a one-line
    summary, its knobs (named as Masterology's window names them), the Tonal EQ's bands and its meter (gain reduction
    where it has one). */
namespace polisher::rackunit
{
    /** Deepest since the Rack last read them (dB; cuts <= 0), from the plug-in's own copies, never the window's. */
    struct Meters
    {
        float resonance = 0.0f, bassTame = 0.0f, glue = 0.0f, clipper = 0.0f, maximizer = 0.0f;
        std::array<float, kDynBands> dynamics {};
    };

    struct State
    {
        UnlimiterParams unlimiter;
        TonalEQParams eq;
        StemEQParams stem;
        ToneSteadyParams steady;
        bool steadyHasTarget = false;     // Auto-Master stored the song's tone: Tone Steady holds it
        ResonanceParams resonance;
        BassParams bass;                  // tame / punch 0..1
        GlueParams glue;                  // mix 0..1
        DynamicsParams dynamics;
        ImagerParams imager;
        SaturationParams saturation;
        bool clipOn = false;
        double clipLevelDb = 0.0;         // the clip level against the ceiling (-9 .. +3 dB)
        double clipSoftness = 50.0;       // %
        int clipOversampling = 2;         // 1x, 2x, 4x, 8x
        double inputTrimDb = 0.0, driveDb = 0.0;
        LimiterParams limiter;            // ceiling, release, bass first (0..1)
        std::string goal;                 // "Loud release", "Matching My Ref" ...
        float targetLufs = -9.0f;

        bool hasMeters = false;           // audio came through lately
        Meters meters;
    };

    /** The modules (brief: keys, states and summaries only; full: values, bands and meters too). */
    std::vector<rack::Module> modules (const State&, bool full);

    /** Stage 2: a module's on switch (its parameter id: "mas.glue" -> "glueOn"), nullptr for none (the Maximizer). */
    const char* switchOf (const std::string& moduleKey);
    /** Where a Rack knob (0..1, as modules() published it) puts its parameter (by its id), in the parameter's units
        (Sub Tame, Glue Mix, Bass First in %), the same range and curve as the host's; a choice to a whole option.
        False: not a knob the Rack can turn. */
    bool fromKnob (const std::string& key, float knob, double& value);
}

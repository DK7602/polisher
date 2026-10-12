#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "polisher/LoudnessTargeting.h"
#include "polisher/MultibandDynamics.h"
#include "polisher/Saturation.h"
#include "polisher/StemEQ.h"
#include "polisher/StereoImager.h"
#include "polisher/Unlimiter.h"

#include <array>

/** Parameters of the Input Trim and the Unlimiter, Stem EQ, Dynamics and Saturation modules: IDs, layout and a reader that turns
    the current values into engine params. Shared by the processor, Auto-Master and the web editor. */
namespace ModuleParams
{
    inline juce::String band (const char* prefix, int b) { return juce::String (prefix) + juce::String (b + 1); }

    /** Stem EQ gain IDs, in polisher::StemIndex order (vocals, drums, bass, other). */
    inline const char* const stemIds[polisher::kStems] = { "stemVoc", "stemDrums", "stemBass", "stemOther" };

    /** Every slider / toggle / choice ID, for the web editor's relays. */
    inline juce::StringArray sliderIds()
    {
        juce::StringArray ids;
        for (auto* id : stemIds) ids.add (id);
        for (const char* id : { "tsAmount", "tsRange", "rtAmount", "rtSens", "rtRelease", "ulAmount", "ulLength", "inTrim" }) ids.add (id);
        for (int x = 0; x < polisher::kDynBands - 1; ++x) ids.add (band ("dynX", x));
        for (const char* p : { "dynThr", "dynRatio", "dynAtk", "dynRel", "dynGain", "dynImpact", "dynSustain" })
            for (int b = 0; b < polisher::kDynBands; ++b) ids.add (band (p, b));
        ids.add ("maxBassFirst");   // (defined with the Maximizer in PluginProcessor)
        ids.add ("satDrive");
        ids.add ("satMix");
        return ids;
    }
    inline juce::StringArray toggleIds() { return { "ulOn", "stemOn", "stemKeep", "tsOn", "rtOn", "dynOn", "dynAdaptive", "satOn" }; }
    inline juce::StringArray comboIds()
    {
        juce::StringArray ids { "satMode", "satFocus", "tsSpeed", "rtFocus", "ulFocus", "maxDither" };
        for (int b = 1; b <= polisher::kEqBands; ++b) ids.add ("eqMode" + juce::String (b));   // EQ band channels (defined with the EQ)
        return ids;
    }

    inline void addTo (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
    {
        using namespace juce;
        auto dbText = [] (float v, int) { return String (v, 1) + " dB"; };
        auto msText = [] (float v, int) { return v < 10.0f ? String (v, 1) + " ms" : String (roundToInt (v)) + " ms"; };
        auto hzText = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (roundToInt (v)) + " Hz"; };

        // Input Trim (before the whole chain): turns a too-hot mix down; 0 dB = untouched. Version hint 4.
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "inTrim", 4 }, "Input Trim",
                                                           NormalisableRange<float> (static_cast<float> (polisher::kMinTrimDb), 0.0f, 0.1f), 0.0f,
                                                           AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));

        // Unlimiter (first in the chain): Amount 0 = untouched. Version hint 3.
        auto pctText0 = [] (float v, int) { return String (roundToInt (v)) + " %"; };
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "ulOn", 3 }, "Unlimiter On", true));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "ulAmount", 3 }, "Unlimiter Amount", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                                                           AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (pctText0)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "ulLength", 3 }, "Unlimiter Length", NormalisableRange<float> (5.0f, 50.0f, 0.1f, 0.6f), 15.0f,
                                                           AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msText)));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "ulFocus", 3 }, "Unlimiter Focus", StringArray { "Full", "Kick", "Snap" }, 0));
        // The Squash Auto-Master measured on what it heard (0 = none: the live meter decides). Set by
        // Auto-Master, not a control: it makes the strength the same in playback and export, from the
        // first bar. Version hint 5.
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "ulSquashSet", 5 }, "Unlimiter Squash (Auto-Master)", NormalisableRange<float> (0.0f, 30.0f, 0.01f), 0.0f,
                                                           AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText).withAutomatable (false)));

        // Stem EQ (after Tonal EQ): level of each stem inside the mix, 0 dB = untouched. Version hint 2:
        // added after the first builds went out.
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "stemOn", 2 }, "Stem EQ On", true));
        for (auto [id, name] : { std::pair { stemIds[0], "Stem EQ Vocals" }, std::pair { stemIds[1], "Stem EQ Drums" },
                                 std::pair { stemIds[2], "Stem EQ Bass" }, std::pair { stemIds[3], "Stem EQ Other" } })
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 2 }, name,
                                                               NormalisableRange<float> (static_cast<float> (-polisher::StemEQ::kMaxGainDb),
                                                                                         static_cast<float> (polisher::StemEQ::kMaxGainDb), 0.1f), 0.0f,
                                                               AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));
        // Keep loudness: Stem EQ moves change the balance, not the level. On for a new instance; a state
        // saved before it existed loads with it off (setStateInformation adds it), so old songs sound
        // as they did. Version hint 6.
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "stemKeep", 6 }, "Stem EQ Keep Loudness", true));

        // Tone Steady and Resonance Tamer (run with Stem EQ): Amount 0 = untouched. Version hint 2.
        auto pctText = [] (float v, int) { return String (roundToInt (v)) + " %"; };
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "tsOn", 2 }, "Tone Steady On", true));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "tsAmount", 2 }, "Tone Steady Amount", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                                                           AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (pctText)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "tsRange", 2 }, "Tone Steady Range", NormalisableRange<float> (1.0f, 6.0f, 0.1f), 3.0f,
                                                           AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "tsSpeed", 2 }, "Tone Steady Speed", StringArray { "Slow", "Medium", "Fast" }, 1));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "rtOn", 2 }, "Resonance Tamer On", true));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "rtAmount", 2 }, "Resonance Tamer Amount", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                                                           AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (pctText)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "rtSens", 2 }, "Resonance Tamer Sensitivity", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
                                                           AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (pctText)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "rtRelease", 2 }, "Resonance Tamer Release", NormalisableRange<float> (20.0f, 300.0f, 1.0f, 0.5f), 80.0f,
                                                           AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msText)));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "rtFocus", 2 }, "Resonance Tamer Focus", StringArray { "Full", "Boxiness", "Harshness" }, 0));

        // Dynamics (after Bass Control): four bands, 1:1 = untouched.
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "dynOn", 1 }, "Dynamics On", true));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "dynAdaptive", 1 }, "Dynamics Adaptive Release", true));
        const polisher::DynamicsParams def;
        for (int x = 0; x < polisher::kDynBands - 1; ++x)
        {
            const auto [lo, hi] = polisher::kDynCrossoverRange[static_cast<size_t> (x)];
            NormalisableRange<float> r (static_cast<float> (lo), static_cast<float> (hi), 1.0f);
            r.setSkewForCentre (static_cast<float> (std::sqrt (lo * hi)));
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { band ("dynX", x), 1 }, "Dynamics Split " + String (x + 1), r,
                                                               static_cast<float> (def.crossoverHz[static_cast<size_t> (x)]),
                                                               AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (hzText)));
        }
        for (int b = 0; b < polisher::kDynBands; ++b)
        {
            const auto& d = def.bands[static_cast<size_t> (b)];
            const String n = " " + String (b + 1);
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { band ("dynThr", b), 1 }, "Dynamics Threshold" + n,
                                                               NormalisableRange<float> (-48.0f, 0.0f, 0.1f), -20.0f,
                                                               AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { band ("dynRatio", b), 1 }, "Dynamics Ratio" + n,
                                                               NormalisableRange<float> (1.0f, 10.0f, 0.01f, 0.5f), 1.0f,
                                                               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + ":1"; })));
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { band ("dynAtk", b), 1 }, "Dynamics Attack" + n,
                                                               NormalisableRange<float> (0.5f, 100.0f, 0.1f, 0.4f), static_cast<float> (d.attackMs),
                                                               AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msText)));
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { band ("dynRel", b), 1 }, "Dynamics Release" + n,
                                                               NormalisableRange<float> (20.0f, 1000.0f, 1.0f, 0.4f), static_cast<float> (d.releaseMs),
                                                               AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msText)));
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { band ("dynGain", b), 1 }, "Dynamics Gain" + n,
                                                               NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                                               AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));
            // Impact (version hint 8): 0 = off, which is also what a state saved before it existed loads as.
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { band ("dynImpact", b), 8 }, "Dynamics Impact" + n,
                                                               NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
                                                               AudioParameterFloatAttributes().withLabel ("%")
                                                                   .withStringFromValueFunction ([] (float v, int) { return (v > 0.5f ? "+" : "") + String (roundToInt (v)) + " %"; })));
            // Sustain (version hint 9): 0 = off, as a state saved before it existed loads.
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { band ("dynSustain", b), 9 }, "Dynamics Sustain" + n,
                                                               NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
                                                               AudioParameterFloatAttributes().withLabel ("%")
                                                                   .withStringFromValueFunction ([] (float v, int) { return (v > 0.5f ? "+" : "") + String (roundToInt (v)) + " %"; })));
        }

        // Saturation (after the Stereo Imager, before Drive). Drive 0 = untouched.
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "satOn", 1 }, "Saturation On", true));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "satMode", 1 }, "Saturation Mode", StringArray { "Tape", "Tube", "Clip" }, 0));
        // Focus: the whole mix, or the low end only (harmonics of the 808 for phone speakers). Version hint 7.
        // The default, Full, is also what a state saved before Focus existed loads as (JUCE gives a control
        // missing from a state its default), which is exactly how it sounded then.
        // Highs only and Bass + Highs were added after it (v0.21): appended, so saved indexes keep their meaning.
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "satFocus", 7 }, "Saturation Focus", StringArray { "Full", "Bass only", "Highs only", "Bass + Highs" }, 0));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "satDrive", 1 }, "Saturation Drive",
                                                           NormalisableRange<float> (0.0f, static_cast<float> (polisher::Saturation::kMaxDriveDb), 0.1f), 0.0f,
                                                           AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "satMix", 1 }, "Saturation Mix",
                                                           NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
                                                           AudioParameterFloatAttributes().withLabel ("%")
                                                               .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; })));
    }

    /** Reads the current values (lock-free; safe on the audio thread once attached). */
    class Reader
    {
    public:
        void attach (juce::AudioProcessorValueTreeState& s)
        {
            auto get = [&s] (const juce::String& id) { auto* p = s.getRawParameterValue (id); jassert (p != nullptr); return p; };
            ulOn = get ("ulOn"); ulAmount = get ("ulAmount"); ulLength = get ("ulLength"); ulFocus = get ("ulFocus"); ulSquashSet = get ("ulSquashSet");
            stemOn = get ("stemOn");
            stemKeep = get ("stemKeep");
            tsOn = get ("tsOn"); tsAmount = get ("tsAmount"); tsRange = get ("tsRange"); tsSpeed = get ("tsSpeed");
            rtOn = get ("rtOn"); rtAmount = get ("rtAmount"); rtSens = get ("rtSens"); rtRelease = get ("rtRelease"); rtFocus = get ("rtFocus");
            for (int k = 0; k < polisher::kStems; ++k) stemGain[static_cast<size_t> (k)] = get (stemIds[k]);
            dynOn = get ("dynOn");
            dynAdaptive = get ("dynAdaptive");
            for (int x = 0; x < polisher::kDynBands - 1; ++x) dynX[static_cast<size_t> (x)] = get (band ("dynX", x));
            for (int b = 0; b < polisher::kDynBands; ++b)
            {
                const auto i = static_cast<size_t> (b);
                thr[i] = get (band ("dynThr", b));
                ratio[i] = get (band ("dynRatio", b));
                atk[i] = get (band ("dynAtk", b));
                rel[i] = get (band ("dynRel", b));
                gain[i] = get (band ("dynGain", b));
                impact[i] = get (band ("dynImpact", b));
                sustain[i] = get (band ("dynSustain", b));
            }
            satOn = get ("satOn");
            satMode = get ("satMode");
            satFocus = get ("satFocus");
            satDrive = get ("satDrive");
            satMix = get ("satMix");
            imgOn = get ("imgOn");
            imgLow = get ("imgLow");
            imgMid = get ("imgMid");
            imgHigh = get ("imgHigh");
        }

        polisher::UnlimiterParams unlimiter (bool bypassed) const noexcept
        {
            polisher::UnlimiterParams p;
            p.enabled = ulOn->load() > 0.5f && ! bypassed;
            p.amount = ulAmount->load();
            p.lengthMs = ulLength->load();
            p.focus = static_cast<polisher::UnlimiterFocus> (juce::jlimit (0, 2, juce::roundToInt (ulFocus->load())));
            p.squashDb = ulSquashSet->load();
            return p;
        }

        polisher::StemEQParams stem (bool bypassed) const noexcept
        {
            polisher::StemEQParams p;
            p.enabled = stemOn->load() > 0.5f && ! bypassed;
            p.keepLoudness = stemKeep->load() > 0.5f;
            for (size_t k = 0; k < stemGain.size(); ++k) p.gainDb[k] = stemGain[k]->load();
            return p;
        }

        polisher::ToneSteadyParams steady (bool bypassed) const noexcept
        {
            polisher::ToneSteadyParams p;
            p.enabled = tsOn->load() > 0.5f && ! bypassed;
            p.amount = tsAmount->load();
            p.rangeDb = tsRange->load();
            p.speed = juce::jlimit (0, 2, juce::roundToInt (tsSpeed->load()));
            return p;
        }

        polisher::ResonanceParams resonance (bool bypassed) const noexcept
        {
            polisher::ResonanceParams p;
            p.enabled = rtOn->load() > 0.5f && ! bypassed;
            p.amount = rtAmount->load();
            p.sensitivity = rtSens->load();
            p.releaseMs = rtRelease->load();
            p.focus = static_cast<polisher::ResonanceFocus> (juce::jlimit (0, 2, juce::roundToInt (rtFocus->load())));
            return p;
        }

        polisher::DynamicsParams dynamics (bool bypassed) const noexcept
        {
            polisher::DynamicsParams p;
            p.enabled = dynOn->load() > 0.5f && ! bypassed;
            p.adaptiveRelease = dynAdaptive->load() > 0.5f;
            for (size_t x = 0; x < dynX.size(); ++x) p.crossoverHz[x] = dynX[x]->load();
            for (size_t b = 0; b < thr.size(); ++b)
                p.bands[b] = { thr[b]->load(), ratio[b]->load(), atk[b]->load(), rel[b]->load(), gain[b]->load(), impact[b]->load(), sustain[b]->load() };
            return p;
        }

        polisher::SaturationParams saturation (bool bypassed) const noexcept
        {
            polisher::SaturationParams p;
            p.enabled = satOn->load() > 0.5f && ! bypassed;
            p.mode = static_cast<polisher::SaturationMode> (juce::jlimit (0, 2, juce::roundToInt (satMode->load())));
            p.driveDb = satDrive->load();
            p.mix = satMix->load();
            p.focus = static_cast<polisher::SaturationFocus> (juce::jlimit (0, 3, juce::roundToInt (satFocus->load())));
            return p;
        }

        polisher::ImagerParams imager (bool bypassed) const noexcept
        {
            polisher::ImagerParams p;
            p.enabled = imgOn->load() > 0.5f && ! bypassed;
            p.lowWidth = imgLow->load();
            p.midWidth = imgMid->load();
            p.highWidth = imgHigh->load();
            return p;
        }

    private:
        std::atomic<float>* ulOn = nullptr; std::atomic<float>* ulAmount = nullptr; std::atomic<float>* ulLength = nullptr; std::atomic<float>* ulFocus = nullptr; std::atomic<float>* ulSquashSet = nullptr;
        std::atomic<float>* stemOn = nullptr;
        std::atomic<float>* stemKeep = nullptr;
        std::atomic<float>* tsOn = nullptr; std::atomic<float>* tsAmount = nullptr; std::atomic<float>* tsRange = nullptr; std::atomic<float>* tsSpeed = nullptr;
        std::atomic<float>* rtOn = nullptr; std::atomic<float>* rtAmount = nullptr; std::atomic<float>* rtSens = nullptr;
        std::atomic<float>* rtRelease = nullptr; std::atomic<float>* rtFocus = nullptr;
        std::array<std::atomic<float>*, polisher::kStems> stemGain {};
        std::atomic<float>* dynOn = nullptr;
        std::atomic<float>* dynAdaptive = nullptr;
        std::array<std::atomic<float>*, polisher::kDynBands - 1> dynX {};
        std::array<std::atomic<float>*, polisher::kDynBands> thr {}, ratio {}, atk {}, rel {}, gain {}, impact {}, sustain {};
        std::atomic<float>* satOn = nullptr;
        std::atomic<float>* satMode = nullptr;
        std::atomic<float>* satFocus = nullptr;
        std::atomic<float>* satDrive = nullptr;
        std::atomic<float>* satMix = nullptr;
        std::atomic<float>* imgOn = nullptr;
        std::atomic<float>* imgLow = nullptr;
        std::atomic<float>* imgMid = nullptr;
        std::atomic<float>* imgHigh = nullptr;
    };
}

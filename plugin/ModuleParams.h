#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "polisher/MultibandDynamics.h"
#include "polisher/Saturation.h"
#include "polisher/StemEQ.h"
#include "polisher/StereoImager.h"

#include <array>

/** Parameters of the Stem EQ, Dynamics and Saturation modules: IDs, layout and a reader that turns
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
        for (const char* id : { "tsAmount", "tsRange", "rtAmount", "rtSens", "rtRelease" }) ids.add (id);
        for (int x = 0; x < polisher::kDynBands - 1; ++x) ids.add (band ("dynX", x));
        for (const char* p : { "dynThr", "dynRatio", "dynAtk", "dynRel", "dynGain" })
            for (int b = 0; b < polisher::kDynBands; ++b) ids.add (band (p, b));
        ids.add ("satDrive");
        ids.add ("satMix");
        return ids;
    }
    inline juce::StringArray toggleIds() { return { "stemOn", "tsOn", "rtOn", "dynOn", "dynAdaptive", "satOn" }; }
    inline juce::StringArray comboIds()  { return { "satMode", "tsSpeed", "rtFocus" }; }

    inline void addTo (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
    {
        using namespace juce;
        auto dbText = [] (float v, int) { return String (v, 1) + " dB"; };
        auto msText = [] (float v, int) { return v < 10.0f ? String (v, 1) + " ms" : String (roundToInt (v)) + " ms"; };
        auto hzText = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (roundToInt (v)) + " Hz"; };

        // Stem EQ (after Tonal EQ): level of each stem inside the mix, 0 dB = untouched. Version hint 2:
        // added after the first builds went out.
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "stemOn", 2 }, "Stem EQ On", true));
        for (auto [id, name] : { std::pair { stemIds[0], "Stem EQ Vocals" }, std::pair { stemIds[1], "Stem EQ Drums" },
                                 std::pair { stemIds[2], "Stem EQ Bass" }, std::pair { stemIds[3], "Stem EQ Other" } })
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 2 }, name,
                                                               NormalisableRange<float> (static_cast<float> (-polisher::StemEQ::kMaxGainDb),
                                                                                         static_cast<float> (polisher::StemEQ::kMaxGainDb), 0.1f), 0.0f,
                                                               AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));

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
        }

        // Saturation (after the Stereo Imager, before Drive). Drive 0 = untouched.
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "satOn", 1 }, "Saturation On", true));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "satMode", 1 }, "Saturation Mode", StringArray { "Tape", "Tube", "Clip" }, 0));
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
            stemOn = get ("stemOn");
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
            }
            satOn = get ("satOn");
            satMode = get ("satMode");
            satDrive = get ("satDrive");
            satMix = get ("satMix");
            imgOn = get ("imgOn");
            imgLow = get ("imgLow");
            imgMid = get ("imgMid");
            imgHigh = get ("imgHigh");
        }

        polisher::StemEQParams stem (bool bypassed) const noexcept
        {
            polisher::StemEQParams p;
            p.enabled = stemOn->load() > 0.5f && ! bypassed;
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
                p.bands[b] = { thr[b]->load(), ratio[b]->load(), atk[b]->load(), rel[b]->load(), gain[b]->load() };
            return p;
        }

        polisher::SaturationParams saturation (bool bypassed) const noexcept
        {
            polisher::SaturationParams p;
            p.enabled = satOn->load() > 0.5f && ! bypassed;
            p.mode = static_cast<polisher::SaturationMode> (juce::jlimit (0, 2, juce::roundToInt (satMode->load())));
            p.driveDb = satDrive->load();
            p.mix = satMix->load();
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
        std::atomic<float>* stemOn = nullptr;
        std::atomic<float>* tsOn = nullptr; std::atomic<float>* tsAmount = nullptr; std::atomic<float>* tsRange = nullptr; std::atomic<float>* tsSpeed = nullptr;
        std::atomic<float>* rtOn = nullptr; std::atomic<float>* rtAmount = nullptr; std::atomic<float>* rtSens = nullptr;
        std::atomic<float>* rtRelease = nullptr; std::atomic<float>* rtFocus = nullptr;
        std::array<std::atomic<float>*, polisher::kStems> stemGain {};
        std::atomic<float>* dynOn = nullptr;
        std::atomic<float>* dynAdaptive = nullptr;
        std::array<std::atomic<float>*, polisher::kDynBands - 1> dynX {};
        std::array<std::atomic<float>*, polisher::kDynBands> thr {}, ratio {}, atk {}, rel {}, gain {};
        std::atomic<float>* satOn = nullptr;
        std::atomic<float>* satMode = nullptr;
        std::atomic<float>* satDrive = nullptr;
        std::atomic<float>* satMix = nullptr;
        std::atomic<float>* imgOn = nullptr;
        std::atomic<float>* imgLow = nullptr;
        std::atomic<float>* imgMid = nullptr;
        std::atomic<float>* imgHigh = nullptr;
    };
}

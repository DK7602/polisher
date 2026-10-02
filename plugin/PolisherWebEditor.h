#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>
#include <vector>
#include <optional>

/** The Polisher liquid-glass interface: an HTML/CSS/JS front end (plugin/ui) embedded in the
    binary and shown in the platform web view (WKWebView on macOS, WebView2 on Windows).

    C++ -> JS: parameters through JUCE web relays, plus a "polisherMeters" event at 30 Hz with
               loudness readings and two spectra (mix and master).
    JS -> C++: parameter changes through the relays, and the native function "resetMeter". */
class PolisherWebEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer
{
public:
    explicit PolisherWebEditor (PolisherAudioProcessor&);
    ~PolisherWebEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** False when the platform can't host the web UI; the processor then uses the basic editor. */
    static bool isSupported();

    static constexpr int kDesignWidth = 1600;
    static constexpr int kDesignHeight = 900;

private:
    static constexpr int kFftOrder = 12;
    static constexpr int kFftSize = 1 << kFftOrder;
    static constexpr int kSpectrumPoints = 96;

    struct Analyser
    {
        std::array<float, kSpectrumPoints> smoothed {};
        bool primed = false;
    };

    static std::vector<std::unique_ptr<juce::WebSliderRelay>> makeSliderRelays (const juce::String& prefix);

    template <typename Relay>
    static std::vector<std::unique_ptr<Relay>> makeRelays (const juce::StringArray& ids)
    {
        std::vector<std::unique_ptr<Relay>> relays;
        for (const auto& id : ids)
            relays.push_back (std::make_unique<Relay> (id));
        return relays;
    }

    void timerCallback() override;
    juce::var makeSpectrum (const SpectrumTap& tap, Analyser& state);
    static juce::WebBrowserComponent::Options makeBaseOptions();
    juce::WebBrowserComponent::Options makeEditorOptions();
    static std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    PolisherAudioProcessor& audioProcessor;

    // Relays must be constructed before the browser, which takes options from them.
    juce::WebSliderRelay driveRelay { "drive" }, ceilingRelay { "ceiling" }, releaseRelay { "release" };
    juce::WebToggleButtonRelay adaptiveRelay { "adaptive" }, bypassRelay { "bypass" }, autoResetRelay { "autoreset" };
    juce::WebToggleButtonRelay clipOnRelay { "clipOn" }, resetOnChangeRelay { "resetOnChange" }, levelMatchRelay { "levelMatch" };
    juce::WebSliderRelay clipLevelRelay { "clipLevel" }, clipSoftnessRelay { "clipSoftness" };
    juce::WebComboBoxRelay clipOversamplingRelay { "clipOversampling" };
    juce::WebToggleButtonRelay eqOnRelay { "eqOn" }, bassOnRelay { "bassOn" }, imgOnRelay { "imgOn" };
    juce::WebSliderRelay imgLowRelay { "imgLow" }, imgMidRelay { "imgMid" }, imgHighRelay { "imgHigh" };
    juce::WebSliderRelay bassMonoRelay { "bassMono" }, bassTameRelay { "bassTame" }, bassPunchRelay { "bassPunch" };
    juce::WebComboBoxRelay amGenreRelay { "amGenre" }, amIntensityRelay { "amIntensity" }, amTargetRelay { "amTarget" };
    std::vector<std::unique_ptr<juce::WebSliderRelay>> eqGainRelays = makeSliderRelays ("eqGain"),
                                                       eqFreqRelays = makeSliderRelays ("eqFreq");
    // Dynamics and Saturation (see ModuleParams.h).
    std::vector<std::unique_ptr<juce::WebSliderRelay>> moduleSliderRelays = makeRelays<juce::WebSliderRelay> (ModuleParams::sliderIds());
    std::vector<std::unique_ptr<juce::WebToggleButtonRelay>> moduleToggleRelays = makeRelays<juce::WebToggleButtonRelay> (ModuleParams::toggleIds());
    std::vector<std::unique_ptr<juce::WebComboBoxRelay>> moduleComboRelays = makeRelays<juce::WebComboBoxRelay> (ModuleParams::comboIds());

    juce::WebBrowserComponent webView;

    juce::WebSliderParameterAttachment driveAttachment, ceilingAttachment, releaseAttachment;
    juce::WebToggleButtonParameterAttachment adaptiveAttachment, bypassAttachment, autoResetAttachment;
    juce::WebToggleButtonParameterAttachment clipOnAttachment, resetOnChangeAttachment, levelMatchAttachment;
    juce::WebSliderParameterAttachment clipLevelAttachment, clipSoftnessAttachment;
    juce::WebComboBoxParameterAttachment clipOversamplingAttachment;
    juce::WebToggleButtonParameterAttachment eqOnAttachment, bassOnAttachment, imgOnAttachment;
    juce::WebSliderParameterAttachment imgLowAttachment, imgMidAttachment, imgHighAttachment;
    juce::WebSliderParameterAttachment bassMonoAttachment, bassTameAttachment, bassPunchAttachment;
    juce::WebComboBoxParameterAttachment amGenreAttachment, amIntensityAttachment, amTargetAttachment;
    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> eqAttachments;
    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> moduleSliderAttachments;
    std::vector<std::unique_ptr<juce::WebToggleButtonParameterAttachment>> moduleToggleAttachments;
    std::vector<std::unique_ptr<juce::WebComboBoxParameterAttachment>> moduleComboAttachments;

    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window { static_cast<size_t> (kFftSize), juce::dsp::WindowingFunction<float>::hann, false };
    std::array<float, 2 * kFftSize> fftBuffer {};
    Analyser mixAnalyser, masterAnalyser;
    float grHold = 0.0f, clipHold = 0.0f, tameHold = 0.0f, punchHold = 0.0f;
    std::array<float, polisher::kDynBands> dynHold {};
    double satResHold = 0.0, satSigHold = 0.0;
    std::array<double, polisher::kStems> stemHold {};
    float rtHold = 0.0f, rtHzHold = 0.0f, tsHold = 0.0f, tsHzHold = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolisherWebEditor)
};

#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>

/** Basic fallback editor, shown only when no web view is available for the full screen (on Windows:
    the Microsoft Edge WebView2 Runtime is missing), or inside the full screen's window when its web view
    didn't start after all. A notice says what is missing and how to get it, and that the whole chain
    keeps working with its saved settings; the Maximizer controls and the loudness readouts stay usable. */
class PolisherAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    /** Why the full screen isn't there: no web view on this system, or one that failed to start. */
    enum class Reason { noWebView, webViewFailed };
    explicit PolisherAudioProcessorEditor (PolisherAudioProcessor&, Reason = Reason::noWebView);
    ~PolisherAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Readout
    {
        juce::String label;
        juce::String value;
        juce::Rectangle<int> bounds;
    };

    PolisherAudioProcessor& audioProcessor;

    juce::Slider driveKnob, ceilingKnob, releaseKnob;
    juce::Label driveLabel, ceilingLabel, releaseLabel;
    juce::ToggleButton adaptiveButton { "Adaptive release" }, bypassButton { "Bypass" },
                       autoResetButton { "Reset on play" };
    juce::TextButton resetButton { "Reset meter" };
    juce::TextButton getWebViewButton { "GET WEBVIEW2" };
    juce::Rectangle<int> noticeArea;
    const char* noticeText = nullptr;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> driveAttachment, ceilingAttachment, releaseAttachment;
    std::unique_ptr<ButtonAttachment> adaptiveAttachment, bypassAttachment, autoResetAttachment;

    std::array<Readout, 6> readouts;
    float grDisplay = 0.0f, peakDisplay = -100.0f;

    juce::LookAndFeel_V4 lookAndFeel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolisherAudioProcessorEditor)
};

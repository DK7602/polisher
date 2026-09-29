#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>

/** Engineering editor for v0.1: working controls and live loudness readouts in the Polisher
    colours. The liquid-glass UI replaces this once the WebView front end lands. */
class PolisherAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit PolisherAudioProcessorEditor (PolisherAudioProcessor&);
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

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> driveAttachment, ceilingAttachment, releaseAttachment;
    std::unique_ptr<ButtonAttachment> adaptiveAttachment, bypassAttachment, autoResetAttachment;

    std::array<Readout, 6> readouts;
    float grDisplay = 0.0f, peakDisplay = -100.0f;

    juce::LookAndFeel_V4 lookAndFeel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolisherAudioProcessorEditor)
};

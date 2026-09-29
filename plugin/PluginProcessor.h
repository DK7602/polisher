#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "polisher/LoudnessMeter.h"
#include "polisher/TruePeakLimiter.h"
#include "polisher/SoftClipper.h"
#include "polisher/TonalEQ.h"
#include "polisher/BassControl.h"
#include "SpectrumTap.h"

#include <array>
#include <atomic>

/** Polisher: Tonal EQ -> Bass Control -> Drive -> Soft Clipper -> Maximizer (true-peak limiter), plus an EBU R128 output meter
    and spectrum taps. The other modules and Auto-Master plug into this chain in later milestones. */
class PolisherAudioProcessor final : public juce::AudioProcessor
{
public:
    PolisherAudioProcessor();
    ~PolisherAudioProcessor() override = default;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Shared with the editor
    juce::AudioProcessorValueTreeState parameters;

    struct Meters
    {
        std::atomic<float> momentary { -100.0f };
        std::atomic<float> shortTerm { -100.0f };
        std::atomic<float> integrated { -100.0f };
        std::atomic<float> loudnessRange { 0.0f };
        std::atomic<float> truePeakMax { -100.0f };
        std::atomic<float> truePeakRecent { -100.0f };
        std::atomic<float> gainReduction { 0.0f };
        std::atomic<float> peakLeft { -100.0f };
        std::atomic<float> peakRight { -100.0f };
        std::atomic<float> clipReduction { 0.0f };   // largest clipper peak reduction since read (dB, <= 0)
        std::atomic<float> inputShortTerm { -100.0f };  // your mix, before Drive (LUFS)
        std::atomic<float> inputIntegrated { -100.0f };
        std::atomic<float> bassTame { 0.0f };        // deepest Sub Tame cut since read (dB, <= 0)
        std::atomic<float> bassPunch { 0.0f };       // biggest Punch boost since read (dB, >= 0)
        std::atomic<float> bassWidth { 0.0f };       // how stereo the incoming bass is (%)
    };
    Meters meters;

    /** Full reset (RESET button). */
    void requestMeterReset() noexcept { meterResetRequested.store (true); }
    /** Restart Integrated / range / true peak only; Momentary and Short-term keep running
        (used when a control changes, so the readings you watch while adjusting never blank). */
    void requestStatisticsReset() noexcept { statsResetRequested.store (true); }

    /** Recent audio before (mix) and after (master) processing, for the spectrum display. */
    SpectrumTap inputTap, outputTap;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    template <typename Sample>
    void processAnyPrecision (juce::AudioBuffer<Sample>& buffer);

    void updateLimiterParams();
    double targetDriveGain() const;
    void publishMeters();

    polisher::TonalEQ eq;
    polisher::BassControl bass;
    polisher::SoftClipper clipper;
    polisher::TruePeakLimiter limiter;
    double currentDriveGain = 1.0;
    polisher::LoudnessMeter outputMeter;
    polisher::LoudnessMeter inputMeter;   // the incoming mix, so Learn can say how much louder the master is
    std::atomic<bool> meterResetRequested { false }, statsResetRequested { false };

    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* ceilingParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* adaptiveParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* autoResetParam = nullptr;
    std::atomic<float>* clipOnParam = nullptr;
    std::atomic<float>* clipLevelParam = nullptr;
    std::atomic<float>* clipSoftnessParam = nullptr;
    std::atomic<float>* clipOversamplingParam = nullptr;
    std::atomic<float>* eqOnParam = nullptr;
    std::array<std::atomic<float>*, polisher::kEqBands> eqGainParams {}, eqFreqParams {};
    std::atomic<float>* bassOnParam = nullptr;
    std::atomic<float>* bassMonoParam = nullptr;
    std::atomic<float>* bassTameParam = nullptr;
    std::atomic<float>* bassPunchParam = nullptr;
    bool wasPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolisherAudioProcessor)
};

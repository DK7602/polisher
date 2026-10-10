#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "polisher/LoudnessMeter.h"
#include "polisher/TruePeakLimiter.h"
#include "polisher/SoftClipper.h"
#include "polisher/TonalEQ.h"
#include "polisher/BassControl.h"
#include "polisher/StereoImager.h"
#include "polisher/LevelMatch.h"
#include "polisher/MultibandDynamics.h"
#include "polisher/Saturation.h"
#include "polisher/StemEQ.h"
#include "polisher/Unlimiter.h"
#include "polisher/Monitoring.h"
#include "ModuleParams.h"
#include "SpectrumTap.h"
#include "AutoMasterController.h"
#include "ReferenceTrack.h"

#include <array>
#include <atomic>

/** Polisher: Input Trim -> Unlimiter -> Tonal EQ -> Stem EQ -> Bass Control -> Dynamics -> Stereo Imager -> Saturation -> Drive -> Soft Clipper
    -> Maximizer (true-peak limiter), plus EBU R128
    meters, spectrum taps and Auto-Master (which listens to the incoming mix and sets the chain). After the
    meters, for listening only: Listen / Solo (setListenMode), MATCH, then the MONO / PHONE checks. */
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
    // ~45 ms of audio still sits in the look-ahead buffers when the input stops, plus the limiter's
    // release: a host that suspends silent plug-ins must keep calling us until it has come out.
    double getTailLengthSeconds() const override { return tailSeconds; }
    // The host's bypass button is Polisher's own A: latency-matched (no ~45 ms jump) and MATCH-aware.
    juce::AudioProcessorParameter* getBypassParameter() const override { return parameters.getParameter ("bypass"); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }   // VST3 validator: every program needs a name
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
        std::atomic<float> matchDb { 0.0f };          // MATCH gain for the side playing now, A or B (dB, <= 0)
        std::atomic<float> correlation { 1.0f };      // output L/R correlation (+1 mono ... -1 out of phase)
        std::array<std::atomic<float>, polisher::kDynBands> dynGr {};   // deepest Dynamics gain reduction per band since read (dB, <= 0)
        std::atomic<float> satResidual { 0.0f }, satSignal { 0.0f };   // Saturation energies since read (harmonics, signal)
        std::array<std::atomic<float>, polisher::kStems> stemEnergy {};   // loudness-weighted energy the Stem EQ model credited to each stem since read
        std::atomic<float> stemKeep { 0.0f };              // broadband gain Stem EQ Keep loudness applies now (dB)
        std::atomic<float> rtCut { 0.0f }, rtHz { 0.0f };   // deepest Resonance Tamer cut since read (dB, <= 0) and where
        std::atomic<float> ulBoost { 0.0f };               // largest Unlimiter lift since read (dB, >= 0)
        std::atomic<float> ulSquash { 0.0f };              // how squashed the incoming audio is (dB, 0 = not measured yet)
        std::atomic<float> tsDb { 0.0f }, tsHz { 0.0f };    // largest Tone Steady correction since read (dB, signed) and where
        std::atomic<int> listenPlaying { 0 };              // listen mode being heard (polisher::listen::Mode; 0 while it prepares)
        std::atomic<float> listenGainDb { 0.0f };          // gain it is played with (dB, + = turned up to be heard)
        std::atomic<float> listenUnderDb { -100.0f };      // how far it sits under the master, as it is in the master (dB)
    };
    Meters meters;

    // --- Monitoring: Listen / Solo and the MONO / PHONE checks ---------------------------------------
    // Not parameters, so a host never saves or automates them: the editor sets them, they go back to
    // off when it closes (resetMonitoring), and an offline export (isNonRealtime) never contains them.
    /** What plays instead of the master (polisher::listen::Mode; 0 = the master). */
    void setListenMode (int mode) noexcept { listenRequest.store (polisher::listen::isValid (mode) ? mode : 0); }
    void setMonoCheck (bool on) noexcept { monoRequest.store (on); }
    void setPhoneCheck (bool on) noexcept { phoneRequest.store (on); }
    int getListenMode() const noexcept { return listenRequest.load(); }
    bool getMonoCheck() const noexcept { return monoRequest.load(); }
    bool getPhoneCheck() const noexcept { return phoneRequest.load(); }
    /** Everything back to the plain master (the editor closing). */
    void resetMonitoring() noexcept { setListenMode (0); setMonoCheck (false); setPhoneCheck (false); }
    /** Tests: play a listen signal exactly at its level in the master (no "hear it" gain or peak turn-down). */
    void setListenAutoLevel (bool on) noexcept { listenAutoLevel.store (on); }

    /** Adds to a meter accumulator the editor drains with exchange(0); a CAS loop, so neither
        side's update is lost (atomic<float>::fetch_add is not available everywhere yet). */
    static void addTo (std::atomic<float>& a, float v) noexcept
    {
        float cur = a.load();
        while (! a.compare_exchange_weak (cur, cur + v)) {}
    }

    /** Full reset (RESET button). */
    void requestMeterReset() noexcept { meterResetRequested.store (true); }
    /** Restart Integrated / range / true peak only; Momentary and Short-term keep running
        (used when a control changes, so the readings you watch while adjusting never blank). */
    void requestStatisticsReset() noexcept { statsResetRequested.store (true); }

    /** Recent audio before (mix) and after (master) processing, for the spectrum display. */
    SpectrumTap inputTap, outputTap;

    /** The reference song for Match ref (its measurements only), saved with the state. */
    ReferenceTrack reference;

    /** Listens to the mix, decides every module's settings and explains them. */
    AutoMasterController autoMaster { parameters, reference };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    template <typename Sample>
    void processInChunks (juce::AudioBuffer<Sample>& buffer);
    template <typename Sample>
    void processAnyPrecision (juce::AudioBuffer<Sample>& buffer);

    int listenArmSamples (int mode) const noexcept;
    void startListenMode (int mode) noexcept;

    void updateLimiterParams();
    double targetDriveGain() const;
    double targetTrimGain() const;
    void publishMeters();

    polisher::Unlimiter unlimiter;
    polisher::TonalEQ eq;
    polisher::StemEQ stemEq;
    polisher::BassControl bass;
    polisher::MultibandDynamics dynamics;
    polisher::StereoImager imager;
    polisher::Saturation saturation;
    ModuleParams::Reader moduleParams;
    polisher::SoftClipper clipper;
    polisher::TruePeakLimiter limiter;
    double currentDriveGain = 1.0, currentTrimGain = 1.0;
    polisher::LoudnessMeter outputMeter;
    polisher::LoudnessMeter inputMeter;   // the incoming mix, so Learn can say how much louder the master is
    std::atomic<bool> meterResetRequested { false }, statsResetRequested { false };

    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* trimParam = nullptr;
    std::atomic<float>* ceilingParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* adaptiveParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    double tailSeconds = 0.25;
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
    std::atomic<float>* levelMatchParam = nullptr;
    std::atomic<float>* imgOnParam = nullptr;
    std::atomic<float>* imgLowParam = nullptr;
    std::atomic<float>* imgMidParam = nullptr;
    std::atomic<float>* imgHighParam = nullptr;
    polisher::LevelMatch levelMatch;
    double currentMatchGain = 1.0;

    // Monitoring (see setListenMode). The monitored signal is formed where its module sits, carried to
    // the output by monOut (so it lines up with the master to the sample) and given the Maximizer's gain
    // there; monForm lines a module's input up with its output. Buffers hold monCapacity samples (larger
    // host blocks are processed in parts).
    std::atomic<int> listenRequest { 0 };
    std::atomic<bool> monoRequest { false }, phoneRequest { false }, listenAutoLevel { true };
    polisher::ListenMix listenMix;
    polisher::MonitorOutput monitorOut;
    polisher::MonitorDelay monForm, monOut;
    std::array<std::vector<double>, 2> monPre, monSig, monAux;
    std::vector<double> monLimGain, monClipGain;
    int monCapacity = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolisherAudioProcessor)
};

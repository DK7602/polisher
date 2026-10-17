#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "polisher/LoudnessMeter.h"
#include "polisher/TruePeakLimiter.h"
#include "polisher/SoftClipper.h"
#include "polisher/TonalEQ.h"
#include "polisher/BassControl.h"
#include "polisher/GlueCompressor.h"
#include "polisher/StereoImager.h"
#include "polisher/LevelMatch.h"
#include "polisher/MultibandDynamics.h"
#include "polisher/Saturation.h"
#include "polisher/StemEQ.h"
#include "polisher/Unlimiter.h"
#include "polisher/Dither.h"
#include "polisher/Monitoring.h"
#include "ModuleParams.h"
#include "SpectrumTap.h"
#include "AutoMasterController.h"
#include "ReferenceTrack.h"
#include "ToneTargetStore.h"
#include "SettingsHistory.h"
#include "suite/SuiteLink.h"

#include <array>
#include <atomic>

/** Masterology: Input Trim -> Unlimiter -> Tonal EQ -> Stem EQ -> Bass Control -> Glue -> Dynamics -> Stereo Imager -> Saturation -> Drive -> Soft Clipper
    -> Maximizer (true-peak limiter), plus EBU R128
    meters, spectrum taps and Auto-Master (which listens to the incoming mix and sets the chain). After the
    meters, for listening only: Listen / Solo (setListenMode), MATCH, then the MONO / PHONE checks. */
class PolisherAudioProcessor final : public juce::AudioProcessor,
                                     private juce::Timer
{
public:
    PolisherAudioProcessor();
    ~PolisherAudioProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
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
    // The host's bypass button is Masterology's own A: latency-matched (no ~45 ms jump) and MATCH-aware.
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
        std::atomic<float> bassFirstCut { 0.0f };    // deepest Bass First low-end cut since read (dB, <= 0)
        std::atomic<float> peakLeft { -100.0f };
        std::atomic<float> peakRight { -100.0f };
        std::atomic<float> clipReduction { 0.0f };   // largest clipper peak reduction since read (dB, <= 0)
        std::atomic<float> inputShortTerm { -100.0f };  // your mix, before Drive (LUFS)
        std::atomic<float> inputIntegrated { -100.0f };
        std::atomic<float> inputTruePeak { -100.0f };   // the incoming mix's highest true peak (dBTP), for Mixology
        std::atomic<float> bassTame { 0.0f };        // deepest Sub Tame cut since read (dB, <= 0)
        std::atomic<float> bassPunch { 0.0f };       // biggest Punch boost since read (dB, >= 0)
        std::atomic<float> bassWidth { 0.0f };       // how stereo the incoming bass is (%)
        std::atomic<float> matchDb { 0.0f };          // MATCH gain for the side playing now, A or B (dB, <= 0)
        std::atomic<float> correlation { 1.0f };      // output L/R correlation (+1 mono ... -1 out of phase)
        std::array<std::atomic<float>, polisher::kDynBands> dynImpact {};   // largest Impact move per band since read (dB, signed)
        std::array<std::atomic<float>, polisher::kDynBands> dynSustain {};  // largest Sustain move per band since read (dB, signed)
        std::atomic<float> glueGr { 0.0f };          // deepest Glue gain reduction since read (dB, <= 0)
        std::array<std::atomic<float>, polisher::kDynBands> dynGr {};   // deepest Dynamics gain reduction per band since read (dB, <= 0)
        std::atomic<float> satResidual { 0.0f }, satSignal { 0.0f };   // Saturation energies since read (harmonics, signal)
        std::array<std::atomic<float>, polisher::kStems> stemEnergy {};   // K-weighted energy the Stem EQ model credited to each stem since read
        std::atomic<float> stemKeep { 0.0f };              // broadband gain Stem EQ Keep loudness applies now (dB)
        std::atomic<float> rtCut { 0.0f }, rtHz { 0.0f };   // deepest Resonance Tamer cut since read (dB, <= 0) and where
        std::atomic<float> ulBoost { 0.0f };               // largest Unlimiter lift since read (dB, >= 0)
        std::atomic<float> ulSquash { 0.0f };              // how squashed the incoming audio is (dB, 0 = not measured yet)
        std::atomic<float> tsDb { 0.0f }, tsHz { 0.0f };    // largest Tone Steady correction since read (dB, signed) and where
        std::atomic<float> tsDistance { -1.0f };           // how far the music sits from Tone Steady's stored tone (dB, -1: not known)
        std::atomic<bool> tsFar { false };                 // ... far for long: probably another song's tone
        std::atomic<int> listenPlaying { 0 };              // listen mode being heard (polisher::listen::Mode; 0 while it prepares)
        std::atomic<float> listenGainDb { 0.0f };          // gain it is played with (dB, + = turned up to be heard)
        std::atomic<float> listenUnderDb { -100.0f };      // how far it sits under the master, as it is in the master (dB)
        std::atomic<int> transport { -1 };                 // host transport: 1 playing, 0 stopped, -1 the host doesn't say (standalone)
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

    // --- The suite link (Voxology, Mixology, Masterology) --------------------------------------------
    /** Tells Mixology what this Masterology aims for (target loudness, ceiling, genre) and reads Mixology's
        hand-off for the song. Message thread; the plug-in's timer calls it four times a second. Only while the
        host runs this Masterology (prepareToPlay .. releaseResources, see suite::Presence): in a project that
        is open but not active, or switched off, it neither shows itself nor follows the others. */
    void updateSuite();
    /** Mixology's hand-off (the style it mixed in, the mix's peaks and loudness, its tone), when a Mixology
        is in this project. Message thread. */
    bool mixologyHandoff (suite::MixInfo& out) const { if (! hasHandoff) return false; out = handoff; return true; }

    /** The song's style (Mixology's list of 75, the same in Voxology and Mixology): you picked it here. Sets the
        Auto-Master genre it belongs to; the other plug-ins in the project follow. Message thread. */
    void pickStyle (const std::string& key, const std::string& name);
    /** The style ("" = none chosen yet) and a counter that goes up whenever it changed (here or in another plug-in). */
    suite::StyleInfo songStyleNow() const { const juce::ScopedLock sl (styleLock); return songStyle.style(); }
    std::uint32_t songStyleVersion() const { return styleVersion.load(); }

    /** The DAW you work in, shared the same way: found from the host, or picked here / in Mixology / in Voxology.
        Every tip then says how to do things in it. "" = back to the one found. Message thread. */
    void pickDaw (const std::string& key);
    suite::StyleInfo dawNow() const { const juce::ScopedLock sl (styleLock); return daw.style(); }
    std::string dawDetected() const { return detectedDaw; }

    /** The reference song for Match ref (its measurements only), saved with the state. */
    ReferenceTrack reference;
    /** Match ref from the window: read and measure a song / forget it. Both mark the project as changed
        (the reference is saved with it, not a parameter). Message thread. */
    void loadReference (const juce::File& file);
    void clearReference();
    /** Message thread, regularly (the editor's timer): when a load started here has ended, marks the
        project changed again (the host may have saved while it was still being measured). */
    void pollReference();

    /** Tells the host the project changed although no parameter did (a snapshot stored, a reference
        loaded or cleared, Tone Steady's target learned or cleared), so it asks to save it. A VST3 host
        marks a project changed only for parameter edits or setDirty, and Logic (AU) only for parameter
        edits: this calls setDirty and moves the hidden State Revision parameter (kStateRevisionId).
        Message thread (from another thread the parameter moves at the next timer tick). */
    void markStateChanged();
    static constexpr const char* kStateRevisionId = "stateRev";
    static constexpr int kStateRevisions = 1000;

    /** Auto Analyze on / off (see AutoMasterController), saved with the project: marks it changed. Message thread. */
    void setAutoAnalyze (bool on);

    /** Tone Steady's stored target (the song's own tone, learned by Auto-Master), saved with the state. */
    ToneTargetStore toneTarget;
    /** Tone Steady's CLEAR: back to following the music, as one undo step (message thread). */
    void clearToneTarget();

    /** Listens to the mix, decides every module's settings and explains them. */
    AutoMasterController autoMaster { parameters, reference, toneTarget };

    // --- Undo / redo and snapshots (message thread; see SettingsHistory.h) ----------------------------
    /** Every change made in Masterology's window, Auto-Master run, Stem EQ Apply and snapshot recall, as
        undo / redo steps. Kept while the window closes and opens; not saved (a project load clears it). */
    SettingsHistory history { *this };
    /** Snapshot slots S1..S4 (the whole sound), saved with the project. A recall is one undo step, and
        MATCH remembers the level it learned for each slot (see processAnyPrecision). */
    SettingsSnapshots snapshots { *this, history };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** The processor's own work, four times a second on the message thread (its timer; tests call it directly):
        what other threads left for the message thread, then the suite link. */
    void tick()
    {
        // A project opened on another thread: Auto Analyze's saved switch is applied here, on the message thread.
        if (const int pending = pendingAutoAnalyze.exchange (-1); pending >= 0)
            autoMaster.setAutoAnalyze (pending != 0);
        if (stateRevisionPending.exchange (false))
            bumpStateRevision();
        updateSuite();
    }

private:
    void timerCallback() override { tick(); }
    std::atomic<int> pendingAutoAnalyze { -1 };   // -1: nothing waiting
    suite::Presence suitePresence;                // taking part in the suite link (prepareToPlay .. releaseResources)
    void leaveSuite();                            // stopped or closing: off the link at once
    juce::RangedAudioParameter* stateRevisionParam = nullptr;
    std::atomic<bool> stateRevisionPending { false };   // markStateChanged ran off the message thread
    void bumpStateRevision();
    int suiteSlot = -1;
    std::uint64_t suiteId = 0;
    suite::MixInfo handoff;
    bool hasHandoff = false;
    mutable juce::CriticalSection styleLock;
    suite::SongStyle songStyle { suite::StyleInfo::From::masterology };   // under styleLock
    std::atomic<std::uint32_t> styleVersion { 0 };
    void applyStyleGenre();   // the Auto-Master genre the song's style belongs to
    const std::string detectedDaw = suite::dawFromHostPath (juce::PluginHostType::getHostPath().toStdString());
    suite::SharedChoice daw { suite::StyleInfo::From::masterology, suite::Choice::daw };   // under styleLock
    int suiteRefVersion = -1;
    polisher::ReferenceProfile suiteRef;

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
    polisher::GlueCompressor glue;
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
    std::atomic<float>* bassFirstParam = nullptr;
    std::atomic<float>* ditherParam = nullptr;
    polisher::Dither dither;
    std::atomic<float>* bypassParam = nullptr;
    double tailSeconds = 0.25;
    std::atomic<float>* autoResetParam = nullptr;
    std::atomic<float>* clipOnParam = nullptr;
    std::atomic<float>* clipLevelParam = nullptr;
    std::atomic<float>* clipSoftnessParam = nullptr;
    std::atomic<float>* clipOversamplingParam = nullptr;
    std::atomic<float>* eqOnParam = nullptr;
    std::array<std::atomic<float>*, polisher::kEqBands> eqGainParams {}, eqFreqParams {}, eqModeParams {};
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
    // Tone Steady's target on the audio thread: as stored (the mix arriving), and moved by the Tonal EQ
    // in front of the stage (recomputed when either changes).
    polisher::ToneTarget songTone, stageTone;
    int songToneVersion = -1;
    polisher::TonalEQParams stageToneEq;
    double stageToneRate = 0.0, stageToneTrimDb = 0.0;
    double currentMatchGain = 1.0;

    // MATCH per snapshot: the gap learned while a slot played unchanged, so flipping back to it is
    // level-matched at once. The audio thread owns matchMemory; recall / store post requests.
    void handleMatchRequests() noexcept;
    // What undo steps and snapshots carry besides the controls: the Auto-Master report and Tone Steady's
    // stored target, as one JSON string (an older snapshot's plain report still restores the report).
    juce::String captureExtraState() const;
    void restoreExtraState (const juce::String& extra);
    struct MatchMemory { bool valid = false; double gapDb = 0.0; };
    std::array<MatchMemory, SettingsSnapshots::kSlots> matchMemory {};
    std::atomic<int> matchSwitchRequest { 0 };   // 0x100 | (leaving + 1) | (entering + 1) << 4
    std::atomic<int> matchForgetRequest { 0 };   // one bit per slot
    bool referenceLoadPending = false;           // a Match ref load started from the window is still running
    // The settings changed a lot, MATCH learns the level afresh: 1 = Auto-Master (the level learned until
    // then still belongs to the snapshot that was playing), 2 = another project or preset.
    std::atomic<int> matchRelearnRequest { 0 };
    MatchMemory relearnedLevel {};   // the level before an Auto-Master relearn, for the snapshot it leaves
    static constexpr double kMatchHoldSeconds = 3.0;    // after a switch: the short-term readings' window
    static constexpr double kMatchLearnSeconds = 10.0;  // a slot's level is kept once learned this long

    // Monitoring (see setListenMode). The monitored signal is formed where its module sits, carried to
    // the output by monOut (so it lines up with the master to the sample) and given the Maximizer's gain
    // there; monForm lines a module's input up with its output. Buffers hold monCapacity samples (larger
    // host blocks are processed in parts).
    std::atomic<int> listenRequest { 0 };
    std::atomic<bool> monoRequest { false }, phoneRequest { false }, listenAutoLevel { true };
    polisher::ListenMix listenMix;
    polisher::MonitorOutput monitorOut;
    polisher::MonitorDelay monForm, monOut;
    polisher::MonitorDelay monClipDelay;   // the Soft Clipper's clean gain, carried on past the limiter's look-ahead
    std::array<std::vector<double>, 2> monPre, monSig, monAux;
    std::vector<double> monLimGain, monClipGain, monGlueGain;
    int monCapacity = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolisherAudioProcessor)
};

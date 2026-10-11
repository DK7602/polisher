#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "polisher/AutoMaster.h"
#include "polisher/SongListener.h"
#include "ReferenceTrack.h"
#include "ToneTargetStore.h"

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

/** Runs Auto-Master inside the plug-in.

    1. start(): the audio thread feeds the incoming mix (before any processing) to a
       polisher::SongListener while you play the song. It skips silence and keeps, in fixed memory,
       the loudest kWindowSeconds it has heard, the loudness of every 100 ms and the song's tone.
    2. It keeps listening until the song ends (kSongEndSilenceSeconds without music after at least
       kAutoFinishSeconds of it), or finish() ("use what you've heard", from kMinListenSeconds on),
       or kMaxListenSeconds.
    3. A background thread runs polisher::autoMaster() on the loudest part (analysis, decisions,
       loudness search), with the whole song's tone for Tone Steady.
    4. Back on the message thread the chosen values are set as normal host parameter changes (so the
       DAW sees them, and they can be automated), the song's tone goes to Tone Steady (toneTarget),
       all of it as one step of the processor's undo history (see onApplyBegin / onApplyEnd), and the
       report is saved with the plug-in state so it's still there when the project is reopened.

    start (toneOnly) listens the same way but only learns the song's tone for Tone Steady.
    A short Auto-Master listen (under kAutoFinishSeconds) keeps a stored tone learned from more of
    the song only when what it heard matches it (kSameSongToneDb); otherwise the new tone replaces it.

    The listener's memory is allocated by start() for the current sample rate (on the message thread,
    never while the audio thread may be using it), so nothing reallocates while the audio thread
    writes. Each run has a session number; a result from an older session is thrown away. Everything
    except capture() runs on the message thread (prepare() may run on any thread and only touches
    atomics). */
class AutoMasterController final : private juce::Timer
{
public:
    static constexpr double kWindowSeconds = 12.0;          // the part it decides from: the loudest it heard
    static constexpr double kMinListenSeconds = 12.0;       // finish() works from here on
    static constexpr double kAutoFinishSeconds = 30.0;      // music heard before the song's end finishes it by itself
    static constexpr double kSongEndSilenceSeconds = 4.0;   // ...this long without music (or without audio at all; 2 bars at 120 BPM)
    static constexpr double kMaxListenSeconds = 15.0 * 60.0;
    // A short listen keeps a tone learned earlier from more of the song only when what it heard sits
    // within this of it (polisher::toneDistanceDb): a preset, a copied instance or an older project
    // can carry another song's tone. Synthetic songs: a 15 s listen reads 0.1-2.7 dB from its own
    // song's tone in steady songs, other songs 3-19 dB (see toneDistanceDb).
    static constexpr double kSameSongToneDb = 3.0;
    enum State { idle = 0, listening = 1, analysing = 2 };
    enum Mode { fullMaster = 0, toneOnly = 1 };

    AutoMasterController (juce::AudioProcessorValueTreeState& state, const ReferenceTrack& reference, ToneTargetStore& toneTarget);
    ~AutoMasterController() override;

    /** Remembers the sample rate. A listen in progress starts over if it changed; otherwise it carries
        on (hosts often re-prepare on transport start). */
    void prepare (double sampleRate);

    /** Audio thread: hands the mix to the listener while listening. hostSeconds: the DAW's timeline
        position of the block (negative: unknown). Never allocates or locks. */
    template <typename Sample>
    void capture (const juce::AudioBuffer<Sample>& buffer, double hostSeconds) noexcept
    {
        // Busy flag first, then the state (both sequentially consistent): whoever stops the listening
        // either sees this block in progress and waits for it, or this block sees the new state.
        captureBusy.store (true);
        if (stateFlag.load() == listening && listener.getSampleRate() == sampleRate.load (std::memory_order_relaxed))
        {
            const int nch = buffer.getNumChannels(), n = buffer.getNumSamples();
            if (nch > 0 && n > 0)
                listener.process (buffer.getReadPointer (0), buffer.getReadPointer (std::min (1, nch - 1)), n, hostSeconds);
        }
        captureBusy.store (false, std::memory_order_release);
    }

    /** False if the plug-in isn't ready yet (no sample rate) or an analysis is still running. */
    bool start (Mode mode = fullMaster);
    /** "Use what you've heard": stops listening and decides from it. False before kMinListenSeconds. */
    bool finish();
    void cancel();

    /** What the timer does 15 times a second while listening or analysing (tests call it directly). */
    void tick();

    /** Around applying a result's settings, and around a tone-only listen storing the song's tone
        (message thread): the processor makes each one undo step, labelled e.g. "Auto-Master (Trap /
        Hip-Hop, Balanced)" or "Tone Steady: learn the song's tone". */
    std::function<void (const juce::String& label)> onApplyBegin;
    std::function<void()> onApplyEnd;

    /** Sets every module from a finished analysis and stores its report (message thread). Called by the
        timer when an analysis ends; public for the plug-in tests. */
    void apply (const polisher::AutoMasterResult& result, const polisher::AutoMasterSettings& settings);

    /** After the host loads a preset or project state. */
    void onStateRestored (const juce::String& savedReport);
    /** Undo / redo / snapshot recall: the report that explains the settings now playing. */
    void restoreReport (const juce::String& json) { setReport (json); }
    /** The report to save with the plug-in state (safe from any thread). */
    juce::String getReportForSaving() const;

    State getState() const noexcept           { return static_cast<State> (stateFlag.load()); }
    Mode getMode() const noexcept             { return static_cast<Mode> (mode.load()); }
    float getProgress() const noexcept;        // 0..1: music heard of kMinListenSeconds, while listening
    bool isHearingAudio() const noexcept       { return hearingAudio; }
    double getHeardSeconds() const noexcept;   // music heard so far (this or the last listen)
    bool canFinish() const noexcept            { return getState() == listening && getHeardSeconds() >= kMinListenSeconds; }
    double getLoudestStartSeconds() const noexcept;   // where the loudest part so far starts (-1: none yet)
    double getLoudestLufs() const noexcept;           // its loudness (-100: none yet)
    /** Whether the last listen ended by itself because the song ended (or the listen limit). */
    bool finishedByItself() const noexcept     { return autoFinished; }
    /** How many tone-only listens heard too little music at the song's level to learn its tone. */
    int getToneLearnFailures() const noexcept  { return toneLearnFailures; }
    /** Memory the listen holds (fixed when it starts; it doesn't grow with the song). */
    std::size_t getListenMemoryBytes() const noexcept { return listener.isPrepared() ? listener.memoryBytes() : 0; }
    int getReportVersion() const noexcept      { return reportVersion.load(); }
    juce::String getReportJson() const;        // "" when there's no report

private:
    void timerCallback() override { tick(); }
    /** Sets a new state and waits until the audio thread is out of capture(), so the listener is ours. */
    void takeListener (State newState);
    void launchAnalysis();
    void setParam (const juce::String& id, float value);
    void setReport (const juce::String& json);

    juce::AudioProcessorValueTreeState& state;
    const ReferenceTrack& reference;              // Match ref: when loaded, Auto-Master aims for it
    ToneTargetStore& toneTarget;                  // Tone Steady's stored target (the song's tone)

    polisher::SongListener listener;              // prepared by start() for the current sample rate
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<int> stateFlag { idle }, mode { fullMaster };
    std::atomic<bool> captureBusy { false };
    bool hearingAudio = false, autoFinished = false;
    std::int64_t lastHeard = 0;                   // message thread: music heard at the last tick
    double lastMusicMs = 0.0;                     // when it last grew
    int session = 0;                              // message thread
    int toneLearnFailures = 0;                    // message thread
    polisher::SongStats pendingStats;             // the listen behind the analysis running now

    juce::CriticalSection resultLock;
    std::unique_ptr<polisher::AutoMasterResult> pendingResult;
    int pendingSession = -1;
    polisher::AutoMasterSettings pendingSettings;
    std::atomic<bool> resultReady { false };

    std::atomic<int> reportVersion { 0 };
    mutable juce::CriticalSection reportLock;
    juce::String report;

    juce::ThreadPool pool { 1 };                  // last: destroyed (and joined) first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoMasterController)
};

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "polisher/AutoAnalyze.h"
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
    atomics).

    Auto Analyze (setAutoAnalyze): after a successful Auto-Master, while the window is open and the
    controller has nothing else to do, the same listener hears the mix as it arrives (one stretch of
    playback at a time: it starts again after a stop, when Mixology's hand-off changes, after
    kWatchMaxSeconds, or for a new report). When the loudest part it heard is over and lies almost wholly
    inside the hook spans of the song Auto-Master heard (the report's hook spans), it runs Auto-Master again
    on that part in the background and offers the difference as edits (polisher::autoanalyze::planEdits). It never changes
    anything by itself: applyAutoEdit / keepAutoEdit, one undo step each. */
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
    // Auto Analyze
    static constexpr double kWatchEnoughSeconds = polisher::autoanalyze::kEnoughSeconds;   // music heard before the first check
    static constexpr double kWatchRefreshSeconds = 15.0;    // then at most every 15 s (when the loudest part heard changed)
    static constexpr double kWatchMaxSeconds = 90.0;        // a stretch of playback longer than this starts again (the mix as it is now)
    static constexpr double kWatchStopMs = 4000.0;          // this long without music: the song stopped (the next play starts afresh; as Auto-Master's song end)
    // Like with like (polisher::autoanalyze::loudestSettled / atHook): the loudest part heard is judged once it is
    // over (kSettledSeconds of music after it) and lies at least kHookInsideSeconds of its 12 s inside the song's hook spans.
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
        if ((stateFlag.load() == listening || watchFlag.load()) && listener.getSampleRate() == sampleRate.load (std::memory_order_relaxed))
        {
            const int nch = buffer.getNumChannels(), n = buffer.getNumSamples();
            if (nch > 0 && n > 0)   // a mono bus: no right channel, so it is measured as one channel
                listener.process (buffer.getReadPointer (0), nch > 1 ? buffer.getReadPointer (1) : nullptr, n, hostSeconds);
        }
        captureBusy.store (false, std::memory_order_release);
    }

    /** False if the plug-in isn't ready yet (no sample rate) or an analysis is still running. */
    bool start (Mode mode = fullMaster);
    /** "Use what you've heard": stops listening and decides from it. False before kMinListenSeconds. */
    bool finish();
    /** Stops listening, or stops an analysis that is running (nothing is changed either way). */
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
    double getLoudestEndSeconds() const noexcept;     // where it ends on the timeline (-1: none yet, no positions, or playback jumped inside it)
    bool loudestPartJumped() const noexcept           { return listener.isPrepared() && listener.loudestJumped(); }
    double getLoudestLufs() const noexcept;           // its loudness (-100: none yet)
    /** Whether the last listen ended by itself because the song ended (or the listen limit). */
    bool finishedByItself() const noexcept     { return autoFinished; }
    /** How many tone-only listens heard too little music at the song's level to learn its tone. */
    int getToneLearnFailures() const noexcept  { return toneLearnFailures; }
    /** Memory the listen holds (fixed when it starts; it doesn't grow with the song). */
    std::size_t getListenMemoryBytes() const noexcept { return listener.isPrepared() ? listener.memoryBytes() : 0; }
    int getReportVersion() const noexcept      { return reportVersion.load(); }
    juce::String getReportJson() const;        // "" when there's no report

    // ---- Auto Analyze (message thread) ---------------------------------------------------------------
    void setAutoAnalyze (bool on);
    bool getAutoAnalyze() const noexcept       { return aaOn.load(); }   // any thread (the host saves state on its own)
    /** The editor tells whether its window is open: Auto Analyze listens only then. */
    void setEditorOpen (bool open);
    /** Mixology's hand-off (a hash of what it says about the mix; 0: none). A change starts the listen again. */
    void setMixKey (std::uint64_t key)         { mixKey = key; }
    /** Why it isn't checking right now: "off", "noReport", "busy", "closed", "settings", "stopped", "listening",
        "hook" (the part heard isn't at the hook's level), "checking", "cantJudge", or "" (checked; see the edits). */
    juce::String getAutoAnalyzeStatus() const  { return aaPause; }
    double getAutoAnalyzeHeard() const noexcept;   // music heard in this stretch of playback
    int getAutoAnalyzeVersion() const noexcept { return aaVersion; }
    int getAutoEditCount() const noexcept      { return editsLive() ? static_cast<int> (aaEdits.size()) : 0; }
    /** Status, the edits on offer (module, controls now -> new with their texts, Auto-Master's reasons) and
        what changed in the mix, for Learn. */
    juce::String getAutoAnalyzeJson() const;
    /** Applies one module's edit ("all": every edit) as one undo step. False when there is none. */
    bool applyAutoEdit (const juce::String& module);
    /** Keeps the settings as they are for that edit: Auto-Master's plan is moved to the new one without
        changing a control, so the same advice doesn't come back. One undo step. */
    bool keepAutoEdit (const juce::String& module);

private:
    void timerCallback() override { tick(); }
    void updateTimer();
    polisher::AutoMasterSettings currentSettings() const;
    void autoTick (double nowMs);
    void startWatching (bool fresh);
    void stopWatching();
    void restartWatching();
    void launchCheck (polisher::AudioClip clip);
    void takeFresh (const polisher::AutoMasterResult& result);
    void recomputeEdits (bool fromCheck);
    bool reportSettingsMatch() const;
    polisher::autoanalyze::Values plannedValues() const;
    float paramValue (const juce::String& id) const;
    bool changeReport (const juce::String& module, bool applyParams);
    /** The edits still apply: not while Auto-Master's plan is for other choices, or there is no plan. */
    bool editsLive() const { return aaPause != "settings" && aaPause != "noReport" && aaPause != "busy"; }
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
    std::shared_ptr<std::atomic<bool>> analysisCancel;   // the running analysis's stop flag (message thread)

    juce::CriticalSection resultLock;
    std::unique_ptr<polisher::AutoMasterResult> pendingResult;
    int pendingSession = -1;
    polisher::AutoMasterSettings pendingSettings;
    std::atomic<bool> resultReady { false };

    std::atomic<int> reportVersion { 0 };
    mutable juce::CriticalSection reportLock;
    juce::String report;

    // Auto Analyze (message thread, except watchFlag)
    std::atomic<bool> watchFlag { false };        // the audio thread feeds the listener for Auto Analyze
    std::atomic<bool> aaOn { false };
    bool editorOpen = false, watching = false, checkRunning = false, hasFresh = false;
    std::uint64_t mixKey = 0, watchMixKey = 0;
    double watchMusicMs = 0.0;                    // when the music last grew
    std::int64_t watchHeard = 0;
    double checkedLoudStart = -1.0, checkedLoudLufs = -100.0, checkedAtHeard = 0.0;   // the loudest part last judged
    int aaSeenReport = -1, aaSession = 0, aaVersion = 0;
    juce::var aaReport;                           // the report parsed (the one the advice is about)
    // Auto Analyze's own copy of Auto-Master's plan, kept in the report as "aaPlan": it moves as edits are applied
    // or kept, while "values" (and "reasons") stay what Auto-Master itself chose, for Learn's module pages.
    juce::String aaBaseline;                      // its time stamp: a new Auto-Master run starts the advice over
    juce::String aaPause { "off" };
    polisher::autoanalyze::Values fresh;          // what Auto-Master chooses for the mix heard last
    std::vector<polisher::autoanalyze::Edit> aaEdits;
    juce::var freshInfo;                          // the last check: mix loudness / tone, notes, reasons
    juce::String aaCheckedAt, aaUpdatedAt;
    std::shared_ptr<std::atomic<bool>> checkCancel;
    juce::CriticalSection checkLock;
    std::unique_ptr<polisher::AutoMasterResult> pendingCheck;
    int pendingCheckSession = -1;
    std::atomic<bool> checkReady { false };

    juce::ThreadPool pool { 1 };                  // last: destroyed (and joined) first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoMasterController)
};

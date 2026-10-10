#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "polisher/AutoMaster.h"
#include "ReferenceTrack.h"

#include <atomic>
#include <memory>
#include <vector>

/** Runs Auto-Master inside the plug-in.

    1. start(): the audio thread copies the incoming mix (before any processing) into a buffer,
       skipping silence, until it has kListenSeconds of music.
    2. A background thread runs polisher::autoMaster() on it (analysis, decisions, loudness search).
    3. Back on the message thread the chosen values are set as normal host parameter changes (so the
       DAW sees them, and they can be automated or undone), a snapshot is kept for Undo, and the
       report is saved with the plug-in state so it's still there when the project is reopened.

    The capture buffer is allocated once, for the highest supported sample rate, so nothing ever
    reallocates while the audio thread may be writing. Each run has a session number; a result from
    an older session is thrown away. Everything except capture() runs on the message thread
    (prepare() may run on any thread and only touches atomics). */
class AutoMasterController final : private juce::Timer
{
public:
    static constexpr double kListenSeconds = 12.0;
    static constexpr double kMaxSampleRate = 192000.0;
    enum State { idle = 0, listening = 1, analysing = 2 };

    AutoMasterController (juce::AudioProcessorValueTreeState& state, const ReferenceTrack& reference);
    ~AutoMasterController() override;

    /** Sets the capture length for the sample rate. A capture in progress starts over if the rate
        changed; otherwise it carries on (hosts often re-prepare on transport start). */
    void prepare (double sampleRate);

    /** Audio thread: records the mix while listening. Never allocates or locks. */
    template <typename Sample>
    void capture (const juce::AudioBuffer<Sample>& buffer) noexcept
    {
        if (stateFlag.load (std::memory_order_acquire) != listening)
            return;
        const int nch = buffer.getNumChannels(), n = buffer.getNumSamples();
        if (nch == 0 || n == 0)
            return;
        Sample peak = 0;
        for (int c = 0; c < nch; ++c)
            peak = std::max (peak, buffer.getMagnitude (c, 0, n));
        if (peak < static_cast<Sample> (0.003))   // about -50 dBFS: not playing
            return;
        heardAudio.store (true, std::memory_order_relaxed);

        int w = written.load (std::memory_order_acquire);
        const int cap = capacity.load (std::memory_order_acquire);
        const int todo = std::min (n, cap - w);
        if (todo <= 0)
            return;
        const Sample* l = buffer.getReadPointer (0);
        const Sample* r = buffer.getReadPointer (std::min (1, nch - 1));
        for (int i = 0; i < todo; ++i)
        {
            captureL[static_cast<size_t> (w + i)] = static_cast<float> (l[i]);
            captureR[static_cast<size_t> (w + i)] = static_cast<float> (r[i]);
        }
        // If start() reset the count meanwhile, drop this block rather than restore stale progress.
        written.compare_exchange_strong (w, w + todo, std::memory_order_acq_rel);
    }

    /** False if the plug-in isn't ready yet (no sample rate) or an analysis is still running. */
    bool start();
    void cancel();
    bool undo();

    /** After the host loads a preset or project state: the old undo no longer applies. */
    void onStateRestored (const juce::String& savedReport);
    /** The report to save with the plug-in state (safe from any thread). */
    juce::String getReportForSaving() const;

    State getState() const noexcept           { return static_cast<State> (stateFlag.load()); }
    float getProgress() const noexcept;        // 0..1 while listening
    bool isHearingAudio() const noexcept       { return hearingAudio; }
    bool canUndo() const noexcept              { return undoValid.load() && ! undoSnapshot.empty(); }
    int getReportVersion() const noexcept      { return reportVersion.load(); }
    juce::String getReportJson() const;        // "" when there's no report

private:
    void timerCallback() override;
    void launchAnalysis();
    void apply (const polisher::AutoMasterResult& result, const polisher::AutoMasterSettings& settings);
    void setParam (const juce::String& id, float value);
    void setReport (const juce::String& json);

    juce::AudioProcessorValueTreeState& state;
    const ReferenceTrack& reference;              // Match ref: when loaded, Auto-Master aims for it

    std::vector<float> captureL, captureR;       // sized once, for kMaxSampleRate
    std::atomic<int> capacity { 0 };
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<int> written { 0 };
    std::atomic<int> stateFlag { idle };
    std::atomic<bool> heardAudio { false };
    bool hearingAudio = false;
    int quietTicks = 0;
    int session = 0;                              // message thread

    juce::CriticalSection resultLock;
    std::unique_ptr<polisher::AutoMasterResult> pendingResult;
    int pendingSession = -1;
    polisher::AutoMasterSettings pendingSettings;
    std::atomic<bool> resultReady { false };

    std::vector<std::pair<juce::RangedAudioParameter*, float>> undoSnapshot;   // message thread only
    std::atomic<bool> undoValid { false };        // cleared from any thread when state is restored
    std::atomic<int> reportVersion { 0 };
    mutable juce::CriticalSection reportLock;
    juce::String report;

    juce::ThreadPool pool { 1 };                  // last: destroyed (and joined) first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoMasterController)
};

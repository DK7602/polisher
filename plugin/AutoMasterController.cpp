#include "AutoMasterController.h"
#include "ModuleParams.h"

#include <map>
#include <thread>

namespace
{
    juce::var reasonsToVar (const std::vector<polisher::AutoMasterReason>& reasons)
    {
        juce::Array<juce::var> list;
        for (const auto& r : reasons)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("module",  juce::String (r.module));
            o->setProperty ("control", juce::String::fromUTF8 (r.control.c_str()));
            o->setProperty ("value",   juce::String::fromUTF8 (r.value.c_str()));
            o->setProperty ("why",     juce::String::fromUTF8 (r.why.c_str()));
            list.add (juce::var (o));
        }
        return list;
    }

    /** The measurements Learn compares with the reference (tone, low end, width). */
    juce::var statsToVar (const polisher::MixAnalysis& a)
    {
        auto* o = new juce::DynamicObject();
        juce::Array<juce::var> bands;
        for (double v : a.bandDb) bands.add (v);
        o->setProperty ("bandDb", bands);
        o->setProperty ("bassWidth", a.bassWidthPercent);
        o->setProperty ("midSide", a.midSideDb);
        o->setProperty ("highSide", a.highSideDb);
        o->setProperty ("subVsBass", a.subVsBassDb);
        return juce::var (o);
    }

    juce::String mmss (double seconds)
    {
        const int t = static_cast<int> (std::lround (std::max (0.0, seconds)));
        return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
    }

    /** One line for the top of the report: how much of the song it heard and which part set the levels.
        When playback jumped (a cycle loop, the playhead moved while playing) it says so, and names the
        places the loudest part's audio came from instead of one made-up stretch. */
    juce::String listenedText (const polisher::SongStats& st, double windowSeconds)
    {
        const auto dash = juce::String::fromUTF8 ("\xE2\x80\x93");
        juce::String text = "Listened to " + mmss (st.heardSeconds) + " of your song";
        if (st.heardSeconds < windowSeconds + 2.0)
            text += " and set the levels from it.";
        else if (! st.positionFromHost)
            text += "; the loudest part (" + mmss (st.loudestStartSeconds) + dash + mmss (st.loudestStartSeconds + st.loudestSeconds)
                  + " into what I heard, usually the hook or drop) set the levels.";
        else if (st.loudestSpans.size() == 1)
            text += "; the loudest part (" + mmss (st.loudestSpans[0].start) + dash + mmss (st.loudestSpans[0].end)
                  + ", usually the hook or drop) set the levels.";
        else
        {
            text += "; the loudest " + juce::String (juce::roundToInt (st.loudestSeconds)) + " seconds it heard set the levels. Playback jumped "
                    "inside them, so they aren't one stretch of your song: they join ";
            const auto& sp = st.loudestSpans;
            if (sp.size() > 3)
                text += "pieces from " + juce::String (static_cast<int> (sp.size())) + " places on your timeline";
            else
                for (size_t i = 0; i < sp.size(); ++i)
                    text += (i == 0 ? "" : i + 1 == sp.size() ? " and " : ", ") + mmss (sp[i].start) + dash + mmss (sp[i].end);
            text += ".";
        }
        if (st.timelineJumps > 0)
            text += " Playback jumped (or a cycle loop repeated) while I listened, so the whole-song figures count what I heard, "
                    "repeats included, not the song played once through.";
        return text;
    }

    const char* const kIntensityNames[] = { "Gentle", "Balanced", "Punchy" };
    const char* const kTargetNames[] = { "Stream \xE2\x88\x92" "14", "Loud \xE2\x88\x92" "9", "Club \xE2\x88\x92" "7" };
}

AutoMasterController::AutoMasterController (juce::AudioProcessorValueTreeState& s, const ReferenceTrack& ref, ToneTargetStore& tone)
    : state (s), reference (ref), toneTarget (tone)
{
}

AutoMasterController::~AutoMasterController()
{
    stopTimer();
    stateFlag.store (idle);
    // An analysis still running stops at its next check (well under a second at any sample rate), so
    // closing the project or removing the plug-in doesn't wait for it to finish.
    watchFlag.store (false);
    if (analysisCancel != nullptr)
        analysisCancel->store (true);
    if (checkCancel != nullptr)
        checkCancel->store (true);
    pool.removeAllJobs (true, 10000);
}

void AutoMasterController::prepare (double newSampleRate)
{
    if (newSampleRate > 0.0)
        sampleRate.store (newSampleRate);   // a listen at another rate starts over (tick)
}

void AutoMasterController::takeListener (State newState)
{
    stateFlag.store (newState);
    while (captureBusy.load())
        std::this_thread::yield();
}

bool AutoMasterController::start (Mode m)
{
    const double rate = sampleRate.load();
    if (getState() == analysing || rate <= 0.0)
        return false;
    stopWatching();   // Auto Analyze gives the listener back (and a check still running stops)
    if (checkCancel != nullptr)
        checkCancel->store (true);
    takeListener (idle);
    if (! listener.isPrepared() || listener.getSampleRate() != rate)
        listener.prepare (rate, kWindowSeconds, kMaxListenSeconds);   // allocates: the audio thread is out of it
    else
        listener.reset();
    ++session;
    mode.store (m);
    hearingAudio = false;
    autoFinished = false;
    lastHeard = 0;
    lastMusicMs = juce::Time::getMillisecondCounterHiRes();
    stateFlag.store (listening);
    updateTimer();
    return true;
}

bool AutoMasterController::finish()
{
    if (! canFinish())
        return false;
    autoFinished = false;
    launchAnalysis();
    return true;
}

void AutoMasterController::cancel()
{
    if (getState() == listening)
    {
        stateFlag.store (idle);
        updateTimer();
    }
    else if (getState() == analysing)
    {
        // The analysis stops at its next check and hands back nothing; any result already on its way
        // is from a session no longer analysing, so tick() drops it.
        if (analysisCancel != nullptr)
            analysisCancel->store (true);
        stateFlag.store (idle);
        updateTimer();
    }
}

double AutoMasterController::getHeardSeconds() const noexcept
{
    return listener.isPrepared() ? listener.heardSeconds() : 0.0;
}

double AutoMasterController::getLoudestStartSeconds() const noexcept
{
    return listener.isPrepared() ? listener.loudestStartSeconds() : -1.0;
}

double AutoMasterController::getLoudestEndSeconds() const noexcept
{
    return listener.isPrepared() ? listener.loudestEndSeconds() : -1.0;
}

double AutoMasterController::getLoudestLufs() const noexcept
{
    return listener.isPrepared() ? listener.loudestLufs() : -100.0;
}

float AutoMasterController::getProgress() const noexcept
{
    return getState() == listening ? static_cast<float> (juce::jmin (1.0, getHeardSeconds() / kMinListenSeconds)) : 0.0f;
}

void AutoMasterController::tick()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (getState() == listening)
    {
        // The host changed the sample rate: start the listening over at the new rate.
        const double rate = sampleRate.load();
        if (rate > 0.0 && listener.getSampleRate() != rate)
        {
            takeListener (idle);
            listener.prepare (rate, kWindowSeconds, kMaxListenSeconds);
            lastHeard = 0;
            lastMusicMs = now;
            stateFlag.store (listening);
        }
        const auto heard = listener.heardSamples();
        if (heard != lastHeard) { lastHeard = heard; lastMusicMs = now; hearingAudio = true; }
        else if (now - lastMusicMs > 1300.0) hearingAudio = false;   // ~1.3 s without music

        // The song ended: enough music, then a few seconds without it (silent blocks, or no blocks at all
        // when the host stops processing with the transport). Or it heard as much as it may.
        const bool songEnded = listener.heardSeconds() >= kAutoFinishSeconds
                            && (static_cast<double> (listener.silentSamples()) >= kSongEndSilenceSeconds * listener.getSampleRate()
                                || now - lastMusicMs >= kSongEndSilenceSeconds * 1000.0);
        if (songEnded || listener.isFull())
        {
            autoFinished = true;
            launchAnalysis();
        }
    }

    if (resultReady.exchange (false))
    {
        std::unique_ptr<polisher::AutoMasterResult> result;
        int resultSession = -1;
        {
            const juce::ScopedLock sl (resultLock);
            result = std::move (pendingResult);
            resultSession = pendingSession;
        }
        if (resultSession == session && getState() == analysing)
        {
            if (result != nullptr)
                apply (*result, pendingSettings);
            stateFlag.store (idle);
            updateTimer();
        }
        // Otherwise it's from an older run: ignore it and keep going.
    }

    autoTick (now);
}

void AutoMasterController::launchAnalysis()
{
    takeListener (analysing);   // the audio thread is out of the listener from here on

    auto tone = listener.toneTarget();   // the song's tone for Tone Steady
    pendingStats = listener.stats();

    if (getMode() == toneOnly)
    {
        // Learn only: Tone Steady gets the song's tone; nothing else changes. The host is told the
        // song changed (it's not a parameter), so the project asks to be saved.
        if (tone.valid)
        {
            const bool grouped = onApplyBegin && onApplyEnd;   // its own undo step
            if (grouped) onApplyBegin ("Tone Steady: learn the song's tone");
            toneTarget.set (tone);
            if (grouped) onApplyEnd();
            state.processor.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));
        }
        else
            ++toneLearnFailures;
        stateFlag.store (idle);
        updateTimer();
        return;
    }

    // A short Auto-Master listen (finished early) doesn't replace a tone learned from more of the song,
    // as long as that tone is this song's: a preset, a copied Masterology or an older project can carry
    // another song's. It is kept only when what was heard now sits close to it; a tone it can't check
    // (too little music heard at the song's level) isn't kept either.
    const auto stored = toneTarget.get();
    if (stored.valid && tone.valid && tone.seconds < kAutoFinishSeconds && tone.seconds < stored.seconds
        && polisher::toneDistanceDb (stored, tone, listener.getSampleRate()) <= kSameSongToneDb)
        tone = stored;

    polisher::AudioClip clip = listener.loudestWindow();

    auto settings = currentSettings();
    settings.songTone = tone;                       // Tone Steady holds the song to its own tone
    settings.songSeconds = pendingStats.heardSeconds;   // the report says itself how much it heard
    pendingSettings = settings;

    const int thisSession = session;
    analysisCancel = std::make_shared<std::atomic<bool>> (false);
    pool.addJob ([this, clip = std::move (clip), settings, thisSession, stop = analysisCancel]
    {
        std::unique_ptr<polisher::AutoMasterResult> result;
        try
        {
            const polisher::ScopedCancelFlag cancellable (stop.get());
            result = std::make_unique<polisher::AutoMasterResult> (polisher::autoMaster (clip, settings));
        }
        catch (const polisher::AnalysisCancelled&)
        {
            return;   // cancelled, or the plug-in is closing: nothing to hand back
        }
        catch (...)
        {
            result = std::make_unique<polisher::AutoMasterResult>();
            result->summary = "Something went wrong while analysing. Please try Auto-Master again.";
        }
        {
            const juce::ScopedLock sl (resultLock);
            pendingResult = std::move (result);
            pendingSession = thisSession;
        }
        resultReady.store (true);
    });
}

polisher::AutoMasterSettings AutoMasterController::currentSettings() const
{
    auto choice = [this] (const char* id) { return juce::roundToInt (state.getRawParameterValue (id)->load()); };
    polisher::AutoMasterSettings settings;
    settings.genre = static_cast<polisher::Genre> (juce::jlimit (0, polisher::kNumGenres - 1, choice ("amGenre")));
    settings.intensity = static_cast<polisher::Intensity> (juce::jlimit (0, 2, choice ("amIntensity")));
    settings.target = static_cast<polisher::LoudnessTarget> (juce::jlimit (0, 2, choice ("amTarget")));
    // Stem EQ is the user's: Auto-Master hears the mix with it and leaves it as it is.
    settings.stem.enabled = state.getRawParameterValue ("stemOn")->load() > 0.5f;
    settings.stem.keepLoudness = state.getRawParameterValue ("stemKeep")->load() > 0.5f;   // its loudness search hears the compensated stage
    for (int k = 0; k < polisher::kStems; ++k)
        settings.stem.gainDb[static_cast<size_t> (k)] = state.getRawParameterValue (ModuleParams::stemIds[k])->load();
    settings.reference = reference.getProfile();   // Match ref (invalid = none: genre targets)
    return settings;
}

void AutoMasterController::setParam (const juce::String& id, float value)
{
    if (auto* p = state.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

void AutoMasterController::apply (const polisher::AutoMasterResult& r, const polisher::AutoMasterSettings& s)
{
    auto* doc = new juce::DynamicObject();
    doc->setProperty ("ok", r.ok);
    doc->setProperty ("summary", juce::String::fromUTF8 (r.summary.c_str()));
    doc->setProperty ("notAFullMix", r.notAFullMix);
    doc->setProperty ("tipTitle", juce::String::fromUTF8 (r.tipTitle.c_str()));
    doc->setProperty ("tip", juce::String::fromUTF8 (r.tip.c_str()));
    doc->setProperty ("genre", juce::String (polisher::genreName (s.genre)));
    doc->setProperty ("intensity", juce::String (kIntensityNames[static_cast<int> (s.intensity)]));
    doc->setProperty ("target", juce::String::fromUTF8 (kTargetNames[static_cast<int> (s.target)]));
    doc->setProperty ("time", juce::Time::getCurrentTime().toISO8601 (true));

    // What it listened to: how much of the song, the whole song's loudness, where the loudest part is.
    if (pendingStats.heardSeconds > 0.0)
    {
        const auto& st = pendingStats;
        auto* listen = new juce::DynamicObject();
        listen->setProperty ("heard", st.heardSeconds);
        listen->setProperty ("songLufs", st.integratedLufs);
        listen->setProperty ("songLra", st.loudnessRangeLu);
        listen->setProperty ("loudStart", st.loudestStartSeconds);
        listen->setProperty ("loudEnd", st.positionFromHost && st.loudestSpans.size() == 1 ? st.loudestSpans[0].end
                                                                                           : st.loudestStartSeconds + st.loudestSeconds);
        listen->setProperty ("loudLufs", st.loudestLufs);
        listen->setProperty ("fromHost", st.positionFromHost);
        listen->setProperty ("loudPieces", static_cast<int> (st.loudestSpans.size()));   // > 1: playback jumped inside the loudest part
        listen->setProperty ("jumps", st.timelineJumps);   // > 0: the whole-song figures count what was heard, repeats included
        listen->setProperty ("byItself", autoFinished);   // the song ended (not the Finish button)
        juce::Array<juce::var> hook;   // where the song plays at the hook's level (Auto Analyze checks there)
        for (const auto& sp : st.hookSpans)
            hook.add (juce::Array<juce::var> { sp.start, sp.end });
        listen->setProperty ("hookSpans", hook);
        doc->setProperty ("listen", juce::var (listen));
        doc->setProperty ("listened", listenedText (st, kWindowSeconds));
    }

    // Everything below (the settings, Tone Steady's target and the new report) is one undo step.
    const bool grouped = r.ok && onApplyBegin && onApplyEnd;
    if (grouped)
        onApplyBegin ("Auto-Master (" + (s.reference.valid ? juce::String ("Match ref") : juce::String (polisher::genreName (s.genre)))
                      + ", " + kIntensityNames[static_cast<int> (s.intensity)] + ")");

    if (r.ok)
    {
        // Tone Steady's target: the song's own tone, as the analysis heard it (undo puts the old one back).
        toneTarget.set (s.songTone);
        if (s.songTone.valid)
            doc->setProperty ("toneTarget", s.songTone.seconds);

        // Every control Auto-Master decides (polisher::autoanalyze::chainValues: the same list Auto Analyze
        // compares with); the report keeps them, except the hidden ulSquashSet.
        auto* values = new juce::DynamicObject();
        for (const auto& [id, v] : polisher::autoanalyze::chainValues (r.chain))
        {
            const juce::String pid (id);
            setParam (pid, static_cast<float> (v));
            if (pid != "ulSquashSet")
                values->setProperty (pid, static_cast<float> (v));
        }

        juce::Array<juce::var> notes;
        for (const auto& n : r.notes)
            notes.add (juce::String::fromUTF8 (n.c_str()));
        doc->setProperty ("notes", notes);
        doc->setProperty ("reasons", reasonsToVar (r.reasons));
        // Stem EQ is only ever suggested: the editor offers an Apply button with these values.
        if (r.hasStemSuggestion)
        {
            juce::Array<juce::var> sug;
            for (double g : r.stemSuggestion.gainDb) sug.add (g);
            doc->setProperty ("stemSuggest", sug);
        }
        doc->setProperty ("values", juce::var (values));
        doc->setProperty ("outputLufs", r.output.integratedLufs);
        doc->setProperty ("outputTruePeak", r.output.truePeakDbtp);
        doc->setProperty ("targetLufs", r.targetLufs);
        doc->setProperty ("reachedTarget", r.reachedTarget);
        doc->setProperty ("inputLufs", r.analysis.loudness.integratedLufs);
        doc->setProperty ("capturedLufs", r.capturedLufs);
        doc->setProperty ("bassWidth", r.analysis.bassWidthPercent);
        doc->setProperty ("mixStats", statsToVar (r.analysis));   // Auto Analyze compares the mix's tone with it
        // The choices it was made for: Auto Analyze stops when they change (run Auto-Master again instead).
        auto* chosen = new juce::DynamicObject();
        chosen->setProperty ("genre", static_cast<int> (s.genre));
        chosen->setProperty ("intensity", static_cast<int> (s.intensity));
        chosen->setProperty ("target", static_cast<int> (s.target));
        chosen->setProperty ("reference", s.reference.valid ? juce::String::fromUTF8 (s.reference.name.c_str()) : juce::String());
        doc->setProperty ("settings", juce::var (chosen));
        // Match ref: what Learn needs for its reference / your mix / master comparison.
        if (s.reference.valid)
        {
            auto ref = ReferenceTrack::profileToVar (s.reference);
            if (auto* o = ref.getDynamicObject())
                o->removeProperty ("displayDb");   // the spectrum curve comes from the reference itself
            doc->setProperty ("reference", ref);
            doc->setProperty ("target", "Ref " + juce::String (r.targetLufs, 1).replace ("-", juce::String::fromUTF8 ("\xE2\x88\x92")) + " LUFS");
            doc->setProperty ("masterStats", statsToVar (r.masterAnalysis));
        }
    }

    // Saved with the plug-in state (see getReportForSaving), so it survives reopening the project.
    setReport (juce::JSON::toString (juce::var (doc), true));
    if (grouped)
        onApplyEnd();
}

void AutoMasterController::setReport (const juce::String& json)
{
    {
        const juce::ScopedLock sl (reportLock);
        report = json;
    }
    ++reportVersion;
}

void AutoMasterController::onStateRestored (const juce::String& savedReport)
{
    setReport (savedReport);
}

juce::String AutoMasterController::getReportForSaving() const
{
    const juce::ScopedLock sl (reportLock);
    return report;
}

juce::String AutoMasterController::getReportJson() const
{
    return getReportForSaving();
}

// =================================================================================================
// Auto Analyze
// =================================================================================================

namespace
{
    const char* moduleTitle (const std::string& m)
    {
        static const std::map<std::string, const char*> names {
            { "ul", "Unlimiter" }, { "eq", "Tonal EQ" }, { "stem", "Stem EQ" }, { "ts", "Tone Steady" }, { "rt", "Resonance Tamer" },
            { "bass", "Bass Control" }, { "dyn", "Dynamics" }, { "img", "Stereo Imager" }, { "sat", "Saturation" },
            { "clip", "Soft Clipper" }, { "max", "Maximizer" } };
        const auto it = names.find (m);
        return it != names.end() ? it->second : "Settings";
    }

    juce::String nowIso() { return juce::Time::getCurrentTime().toISO8601 (true); }
}

void AutoMasterController::updateTimer()
{
    if (getState() != idle)          startTimerHz (15);
    else if (aaOn || checkRunning)   startTimerHz (5);
    else                             stopTimer();
}

void AutoMasterController::setAutoAnalyze (bool on)
{
    if (aaOn == on)
        return;
    aaOn = on;
    if (! on)
    {
        stopWatching();
        if (checkCancel != nullptr)
            checkCancel->store (true);
        hasFresh = false;
        aaEdits.clear();
        freshInfo = juce::var();
        aaPause = "off";
    }
    else
    {
        aaSeenReport = -1;   // look at the report afresh
        restartWatching();
    }
    ++aaVersion;
    updateTimer();
    if (on) autoTick (juce::Time::getMillisecondCounterHiRes());
}

void AutoMasterController::setEditorOpen (bool open)
{
    editorOpen = open;
}

double AutoMasterController::getAutoAnalyzeHeard() const noexcept
{
    return watching && listener.isPrepared() ? listener.heardSeconds() : 0.0;
}

void AutoMasterController::stopWatching()
{
    if (! watching)
        return;
    watchFlag.store (false);
    while (captureBusy.load())
        std::this_thread::yield();
    watching = false;
}

void AutoMasterController::startWatching (bool freshStart)
{
    const double rate = sampleRate.load();
    if (rate <= 0.0 || getState() != idle)
        return;
    if (freshStart || ! listener.isPrepared() || listener.getSampleRate() != rate)
    {
        if (! listener.isPrepared() || listener.getSampleRate() != rate)
            listener.prepare (rate, kWindowSeconds, kMaxListenSeconds);   // the audio thread is out of it (watchFlag false)
        else
            listener.reset();
        watchHeard = 0;
        watchMusicMs = juce::Time::getMillisecondCounterHiRes();
        watchMixKey = mixKey;
        checkedLoudStart = -1.0;
        checkedLoudLufs = -100.0;
        checkedAtHeard = 0.0;
    }
    watching = true;
    watchFlag.store (true);
}

void AutoMasterController::restartWatching()
{
    stopWatching();
    startWatching (true);
}

float AutoMasterController::paramValue (const juce::String& id) const
{
    if (auto* v = state.getRawParameterValue (id))
        return v->load();
    return 0.0f;
}

bool AutoMasterController::reportSettingsMatch() const
{
    const auto& chosen = aaReport["settings"];
    if (! chosen.isObject())
        return true;   // a report from before v0.27: nothing to compare with
    auto choice = [this] (const char* id) { return juce::roundToInt (paramValue (id)); };
    const juce::String refNow = reference.getState() == ReferenceTrack::loaded ? juce::String::fromUTF8 (reference.getProfile().name.c_str()) : juce::String();
    return static_cast<int> (chosen["genre"]) == choice ("amGenre") && static_cast<int> (chosen["intensity"]) == choice ("amIntensity")
        && static_cast<int> (chosen["target"]) == choice ("amTarget") && chosen["reference"].toString() == refNow;
}

polisher::autoanalyze::Values AutoMasterController::plannedValues() const
{
    polisher::autoanalyze::Values v;
    if (auto* o = aaReport["values"].getDynamicObject())
        for (const auto& p : o->getProperties())
            v[p.name.toString().toStdString()] = static_cast<double> (p.value);
    // Stem EQ is the user's (Auto-Master only suggests it): what the report already suggests counts as planned
    // (it has its own APPLY button), and without a suggestion the knobs as they are. A new suggestion is offered.
    const auto* suggested = aaReport["stemSuggest"].getArray();
    for (int k = 0; k < polisher::kStems; ++k)
        if (const auto* id = ModuleParams::stemIds[k]; v.count (id) == 0)
            v[id] = suggested != nullptr && suggested->size() == polisher::kStems ? static_cast<double> ((*suggested)[k]) : paramValue (id);
    return v;
}

void AutoMasterController::autoTick (double nowMs)
{
    // A check that finished: its result becomes the advice (when it's about this report).
    if (checkReady.exchange (false))
    {
        std::unique_ptr<polisher::AutoMasterResult> result;
        int resultSession = -1;
        {
            const juce::ScopedLock sl (checkLock);
            result = std::move (pendingCheck);
            resultSession = pendingCheckSession;
        }
        checkRunning = false;
        if (result != nullptr && resultSession == aaSession && aaOn)
            takeFresh (*result);
        updateTimer();
    }

    // A new report (Auto-Master ran, undo / redo, an edit applied or kept, a project opened).
    if (const int rv = reportVersion.load(); rv != aaSeenReport)
    {
        aaSeenReport = rv;
        const auto json = getReportJson();
        aaReport = json.isNotEmpty() ? juce::JSON::parse (json) : juce::var();
        const auto time = aaReport["time"].toString();
        if (time != aaBaseline)
        {
            // Another Auto-Master run: the advice starts over, and so does the listen.
            aaBaseline = time;
            ++aaSession;
            hasFresh = false;
            aaEdits.clear();
            freshInfo = juce::var();
            aaCheckedAt = aaUpdatedAt = {};
            if (watching) restartWatching();
            ++aaVersion;
        }
        else if (hasFresh)
            recomputeEdits (false);   // the same run, its plan moved (an edit applied or kept, or undone)
    }

    auto pause = [this] (const char* why)
    {
        if (aaPause != why) { aaPause = why; ++aaVersion; }
    };

    if (! aaOn)                                       { stopWatching(); pause ("off"); return; }
    if (getState() != idle)                           { stopWatching(); pause ("busy"); return; }
    if (! (aaReport.isObject() && static_cast<bool> (aaReport["ok"]))) { stopWatching(); pause ("noReport"); return; }
    if (! reportSettingsMatch())                      { stopWatching(); pause ("settings"); return; }
    if (! editorOpen)                                 { stopWatching(); pause ("closed"); return; }

    if (hasFresh)
        recomputeEdits (false);   // the knobs may have moved: "now -> new" follows them

    if (! watching || listener.getSampleRate() != sampleRate.load())
        restartWatching();
    if (! watching)
        return;

    // The mix as it is now: one stretch of playback at a time.
    const auto heard = listener.heardSamples();
    if (heard != watchHeard) { watchHeard = heard; watchMusicMs = nowMs; }
    const bool stopped = nowMs - watchMusicMs >= kWatchStopMs
                      || static_cast<double> (listener.silentSamples()) >= kWatchStopMs * 0.001 * listener.getSampleRate();
    if (heard > 0 && (stopped || listener.heardSeconds() >= kWatchMaxSeconds || mixKey != watchMixKey))
    {
        restartWatching();
        if (! checkRunning) pause ("stopped");
        return;
    }
    if (checkRunning)                                 { pause ("checking"); return; }
    if (heard == 0)                                   { pause ("stopped"); return; }
    if (listener.heardSeconds() < kWatchEnoughSeconds || ! listener.hasWindow()) { pause ("listening"); return; }

    // Judge the loudest part heard once, and again only when another part becomes the loudest (and not
    // more often than every kWatchRefreshSeconds of music).
    const double loudStart = listener.loudestStartSeconds(), loudLufs = listener.loudestLufs();
    const bool samePart = std::abs (loudStart - checkedLoudStart) < 0.05 && std::abs (loudLufs - checkedLoudLufs) < 0.3;
    if (samePart || (checkedLoudStart >= 0.0 && listener.heardSeconds() - checkedAtHeard < kWatchRefreshSeconds))
        return;

    stopWatching();   // the listener is ours for a moment
    const auto st = listener.stats();
    checkedLoudStart = loudStart;
    checkedLoudLufs = loudLufs;
    checkedAtHeard = listener.heardSeconds();

    // Only a part that plays at the level of the hook Auto-Master set the master from: like with like.
    std::vector<polisher::TimeSpan> hook;
    const auto& listen = aaReport["listen"];
    if (const auto* spans = listen["hookSpans"].getArray())
        for (const auto& sp : *spans)
            if (sp.isArray() && sp.size() == 2)
                hook.push_back ({ static_cast<double> (sp[0]), static_cast<double> (sp[1]) });
    if (hook.empty() && static_cast<bool> (listen["fromHost"]) && static_cast<int> (listen["loudPieces"]) <= 1)
        hook.push_back ({ static_cast<double> (listen["loudStart"]), static_cast<double> (listen["loudEnd"]) });
    const bool atHook = hook.empty() || ! st.positionFromHost
                     || polisher::autoanalyze::overlapSeconds (st.loudestSpans, hook) >= kHookOverlapSeconds;
    if (! atHook)
    {
        startWatching (false);
        pause ("hook");
        return;
    }
    auto clip = listener.loudestWindow();
    startWatching (false);
    launchCheck (std::move (clip));
    pause ("checking");
}

void AutoMasterController::launchCheck (polisher::AudioClip clip)
{
    auto settings = currentSettings();
    settings.songTone = toneTarget.get();   // the song's tone as stored (what the master holds it to)
    const int thisSession = aaSession;
    checkRunning = true;
    checkCancel = std::make_shared<std::atomic<bool>> (false);
    updateTimer();
    pool.addJob ([this, clip = std::move (clip), settings, thisSession, stop = checkCancel]
    {
        std::unique_ptr<polisher::AutoMasterResult> result;
        try
        {
            const polisher::ScopedCancelFlag cancellable (stop.get());
            result = std::make_unique<polisher::AutoMasterResult> (polisher::autoMaster (clip, settings));
        }
        catch (const polisher::AnalysisCancelled&) { result.reset(); }
        catch (...) { result.reset(); }
        {
            const juce::ScopedLock sl (checkLock);
            pendingCheck = std::move (result);
            pendingCheckSession = thisSession;
        }
        checkReady.store (true);
    });
}

void AutoMasterController::takeFresh (const polisher::AutoMasterResult& r)
{
    aaCheckedAt = nowIso();
    if (! r.ok)
    {
        aaPause = "cantJudge";
        ++aaVersion;
        return;
    }
    fresh = polisher::autoanalyze::chainValues (r.chain);
    for (int k = 0; k < polisher::kStems; ++k)   // Stem EQ: its suggestion, or the knobs as they are
        fresh[ModuleParams::stemIds[k]] = r.hasStemSuggestion ? r.stemSuggestion.gainDb[static_cast<size_t> (k)]
                                                              : static_cast<double> (paramValue (ModuleParams::stemIds[k]));
    hasFresh = true;

    // What changed in the mix since Auto-Master listened (the same part of the song, as it arrives).
    auto* info = new juce::DynamicObject();
    const double before = static_cast<double> (aaReport["capturedLufs"]);
    if (before > -70.0 && r.capturedLufs > -70.0)
        info->setProperty ("loudDb", r.capturedLufs - before);
    info->setProperty ("lufs", r.capturedLufs);
    if (const auto* was = aaReport["mixStats"]["bandDb"].getArray(); was != nullptr && was->size() == polisher::kEqBands)
    {
        juce::Array<juce::var> bands;
        for (int b = 0; b < polisher::kEqBands; ++b)
            bands.add (r.analysis.bandDb[static_cast<size_t> (b)] - static_cast<double> ((*was)[b]));
        info->setProperty ("bandDb", bands);
    }
    juce::Array<juce::var> notes;   // advice the earlier report didn't give
    juce::StringArray had;
    if (const auto* old = aaReport["notes"].getArray())
        for (const auto& n : *old) had.add (n.toString().upToFirstOccurrenceOf (":", false, false));
    for (const auto& n : r.notes)
    {
        const auto text = juce::String::fromUTF8 (n.c_str());
        if (! had.contains (text.upToFirstOccurrenceOf (":", false, false)))
            notes.add (text);
    }
    info->setProperty ("notes", notes);
    info->setProperty ("reasons", reasonsToVar (r.reasons));
    freshInfo = juce::var (info);
    recomputeEdits (true);
}

void AutoMasterController::recomputeEdits (bool fromCheck)
{
    polisher::autoanalyze::Values current;
    for (const auto& [id, v] : fresh)
        if (state.getRawParameterValue (juce::String (id)) != nullptr)
            current[id] = paramValue (juce::String (id));
    auto edits = polisher::autoanalyze::planEdits (plannedValues(), fresh, current);

    // Within each control's range, on its steps (a choice: a valid index).
    for (auto& e : edits)
    {
        std::vector<polisher::autoanalyze::Change> kept;
        for (auto c : e.changes)
        {
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (state.getParameter (juce::String (c.id))))
            {
                const auto& range = p->getNormalisableRange();
                c.to = static_cast<double> (range.snapToLegalValue (juce::jlimit (range.start, range.end, static_cast<float> (c.to))));
            }
            if (std::abs (c.to - c.from) > 1.0e-4)
                kept.push_back (c);
        }
        e.changes = std::move (kept);
    }
    edits.erase (std::remove_if (edits.begin(), edits.end(), [] (const auto& e)
    {
        return std::none_of (e.changes.begin(), e.changes.end(), [] (const auto& c) { return c.shown; });
    }), edits.end());

    const bool changed = ! polisher::autoanalyze::sameAdvice (edits, aaEdits);
    bool moved = changed || edits.size() != aaEdits.size();
    for (size_t i = 0; ! moved && i < edits.size(); ++i)
    {
        moved = edits[i].changes.size() != aaEdits[i].changes.size();
        for (size_t j = 0; ! moved && j < edits[i].changes.size(); ++j)
            moved = std::abs (edits[i].changes[j].from - aaEdits[i].changes[j].from) > 1.0e-4
                 || std::abs (edits[i].changes[j].to - aaEdits[i].changes[j].to) > 1.0e-4;
    }
    if (fromCheck && changed)
        aaUpdatedAt = aaCheckedAt;
    if (moved || fromCheck)
    {
        aaEdits = std::move (edits);
        if (fromCheck) aaPause = "";
        ++aaVersion;
    }
}

juce::String AutoMasterController::getAutoAnalyzeJson() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("on", aaOn);
    o->setProperty ("pause", aaPause);
    o->setProperty ("heard", getAutoAnalyzeHeard());
    o->setProperty ("checked", aaCheckedAt);
    o->setProperty ("updated", aaUpdatedAt);
    o->setProperty ("hasCheck", hasFresh);
    if (freshInfo.isObject())
        o->setProperty ("mix", freshInfo);

    auto text = [this] (const std::string& id, double v)
    {
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (state.getParameter (juce::String (id))))
        {
            auto t = p->getText (p->convertTo0to1 (static_cast<float> (v)), 64);
            const auto label = p->getLabel();
            if (label.isNotEmpty() && ! t.containsIgnoreCase (label) && ! dynamic_cast<juce::AudioParameterChoice*> (p)
                && ! dynamic_cast<juce::AudioParameterBool*> (p))
                t << " " << label;
            return t;
        }
        return juce::String (v, 1);
    };

    juce::Array<juce::var> edits;
    for (const auto& e : aaEdits)
    {
        auto* eo = new juce::DynamicObject();
        eo->setProperty ("module", juce::String (e.module));
        eo->setProperty ("title", juce::String (moduleTitle (e.module)));
        juce::Array<juce::var> changes;
        for (const auto& c : e.changes)
        {
            if (! c.shown) continue;
            auto* co = new juce::DynamicObject();
            co->setProperty ("id", juce::String (c.id));
            auto* p = state.getParameter (juce::String (c.id));
            co->setProperty ("name", p != nullptr ? p->getName (64) : juce::String (c.id));
            co->setProperty ("from", c.from);
            co->setProperty ("to", c.to);
            co->setProperty ("fromText", text (c.id, c.from));
            co->setProperty ("toText", text (c.id, c.to));
            changes.add (juce::var (co));
        }
        eo->setProperty ("changes", changes);
        edits.add (juce::var (eo));
    }
    o->setProperty ("edits", edits);
    return juce::JSON::toString (juce::var (o), true);
}

bool AutoMasterController::changeReport (const juce::String& module, bool applyParams)
{
    std::vector<const polisher::autoanalyze::Edit*> chosen;
    for (const auto& e : aaEdits)
        if (module == "all" || module == juce::String (e.module))
            chosen.push_back (&e);
    if (chosen.empty() || ! aaReport.isObject())
        return false;

    juce::StringArray titles;
    for (const auto* e : chosen) titles.add (moduleTitle (e->module));
    const auto label = juce::String ("Auto Analyze: ") + (applyParams ? "" : "keep ") + (chosen.size() > 2 ? juce::String ("every edit") : titles.joinIntoString (" + "))
                     + (applyParams ? "" : " as it is");

    // The report's plan moves to what Auto-Master chooses now for these controls (so the same advice doesn't
    // come back); applying also sets the controls. One undo step with the report (undo brings back both).
    auto doc = juce::JSON::parse (juce::JSON::toString (aaReport, true));   // a copy
    auto* values = doc["values"].getDynamicObject();
    if (values == nullptr)
        return false;
    const bool grouped = onApplyBegin && onApplyEnd;
    if (grouped) onApplyBegin (label);
    bool stem = false;
    for (const auto* e : chosen)
        for (const auto& c : e->changes)
        {
            const juce::String id (c.id);
            if (applyParams)
                setParam (id, static_cast<float> (c.to));
            stem = stem || e->module == "stem";
            if (const auto f = fresh.find (c.id); f != fresh.end() && id != "ulSquashSet")
                values->setProperty (id, static_cast<float> (f->second));
        }
    if (stem && applyParams)
    {
        setParam ("stemOn", 1.0f);
        if (auto* d = doc.getDynamicObject()) d->removeProperty ("stemSuggest");   // done: the report's own button goes away
    }
    setReport (juce::JSON::toString (doc, true));
    if (grouped) onApplyEnd();
    autoTick (juce::Time::getMillisecondCounterHiRes());   // the edits follow at once
    return true;
}

bool AutoMasterController::applyAutoEdit (const juce::String& module) { return changeReport (module, true); }
bool AutoMasterController::keepAutoEdit (const juce::String& module)  { return changeReport (module, false); }

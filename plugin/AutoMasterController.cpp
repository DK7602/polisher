#include "AutoMasterController.h"
#include "ModuleParams.h"

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
    if (analysisCancel != nullptr)
        analysisCancel->store (true);
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
    startTimerHz (15);
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
        stopTimer();
    }
    else if (getState() == analysing)
    {
        // The analysis stops at its next check and hands back nothing; any result already on its way
        // is from a session no longer analysing, so tick() drops it.
        if (analysisCancel != nullptr)
            analysisCancel->store (true);
        stateFlag.store (idle);
        stopTimer();
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
            stopTimer();
        }
        // Otherwise it's from an older run: ignore it and keep going.
    }
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
        stopTimer();
        return;
    }

    // A short Auto-Master listen (finished early) doesn't replace a tone learned from more of the song,
    // as long as that tone is this song's: a preset, a copied Polisher or an older project can carry
    // another song's. It is kept only when what was heard now sits close to it; a tone it can't check
    // (too little music heard at the song's level) isn't kept either.
    const auto stored = toneTarget.get();
    if (stored.valid && tone.valid && tone.seconds < kAutoFinishSeconds && tone.seconds < stored.seconds
        && polisher::toneDistanceDb (stored, tone, listener.getSampleRate()) <= kSameSongToneDb)
        tone = stored;

    polisher::AudioClip clip = listener.loudestWindow();

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

        const auto& c = r.chain;
        auto* values = new juce::DynamicObject();
        auto set = [&] (const juce::String& id, float v) { setParam (id, v); values->setProperty (id, v); };

        set ("bypass", 0.0f);
        set ("eqOn", 1.0f);
        for (int b = 0; b < polisher::kEqBands; ++b)
        {
            set ("eqGain" + juce::String (b + 1), static_cast<float> (c.eq.gainDb[static_cast<size_t> (b)]));
            set ("eqFreq" + juce::String (b + 1), static_cast<float> (polisher::kEqBandSpecs[static_cast<size_t> (b)].defaultHz));
            set ("eqMode" + juce::String (b + 1), 0.0f);   // Stereo: Auto-Master's tone plan works on both channels
        }
        set ("ulOn", 1.0f);
        set ("ulAmount", static_cast<float> (c.unlimiter.amount));
        set ("ulLength", static_cast<float> (c.unlimiter.lengthMs));
        set ("ulFocus", static_cast<float> (static_cast<int> (c.unlimiter.focus)));
        setParam ("ulSquashSet", static_cast<float> (c.unlimiter.squashDb));   // not a control: kept out of the report's values
        set ("tsOn", 1.0f);
        set ("tsAmount", static_cast<float> (c.steady.amount));
        set ("tsRange", static_cast<float> (c.steady.rangeDb));
        set ("tsSpeed", static_cast<float> (c.steady.speed));
        set ("rtOn", 1.0f);
        set ("rtAmount", static_cast<float> (c.resonance.amount));
        set ("rtSens", static_cast<float> (c.resonance.sensitivity));
        set ("rtRelease", static_cast<float> (c.resonance.releaseMs));
        set ("rtFocus", static_cast<float> (static_cast<int> (c.resonance.focus)));
        set ("bassOn", 1.0f);
        set ("bassMono", static_cast<float> (c.bass.monoHz >= polisher::kBassMonoOffHz ? c.bass.monoHz : 0.0));
        set ("bassTame", static_cast<float> (c.bass.tame * 100.0));
        set ("bassPunch", static_cast<float> (c.bass.punch * 100.0));
        set ("dynOn", 1.0f);
        set ("dynAdaptive", c.dynamics.adaptiveRelease ? 1.0f : 0.0f);
        for (int x = 0; x < polisher::kDynBands - 1; ++x)
            set (ModuleParams::band ("dynX", x), static_cast<float> (c.dynamics.crossoverHz[static_cast<size_t> (x)]));
        for (int b = 0; b < polisher::kDynBands; ++b)
        {
            const auto& d = c.dynamics.bands[static_cast<size_t> (b)];
            set (ModuleParams::band ("dynThr", b), static_cast<float> (d.thresholdDb));
            set (ModuleParams::band ("dynRatio", b), static_cast<float> (d.ratio));
            set (ModuleParams::band ("dynAtk", b), static_cast<float> (d.attackMs));
            set (ModuleParams::band ("dynRel", b), static_cast<float> (d.releaseMs));
            set (ModuleParams::band ("dynGain", b), static_cast<float> (d.gainDb));
            set (ModuleParams::band ("dynImpact", b), static_cast<float> (d.impact));     // 0: Auto-Master plans without Impact
            set (ModuleParams::band ("dynSustain", b), static_cast<float> (d.sustain));   // or Sustain
        }
        set ("imgOn", 1.0f);
        set ("imgLow", static_cast<float> (c.imager.lowWidth));
        set ("imgMid", static_cast<float> (c.imager.midWidth));
        set ("imgHigh", static_cast<float> (c.imager.highWidth));
        set ("satOn", 1.0f);
        set ("satMode", static_cast<float> (static_cast<int> (c.saturation.mode)));
        set ("satFocus", static_cast<float> (static_cast<int> (c.saturation.focus)));
        set ("satDrive", static_cast<float> (c.saturation.driveDb));
        set ("satMix", static_cast<float> (c.saturation.mix));
        set ("clipOn", c.clipper.enabled ? 1.0f : 0.0f);
        if (c.clipper.enabled)
        {
            set ("clipLevel", static_cast<float> (c.clipper.thresholdDb - c.limiter.ceilingDbtp));
            set ("clipSoftness", static_cast<float> (c.clipper.softness * 100.0));
            set ("clipOversampling", c.clipper.oversampling >= 8 ? 3.0f : c.clipper.oversampling >= 4 ? 2.0f : c.clipper.oversampling >= 2 ? 1.0f : 0.0f);
        }
        set ("inTrim", static_cast<float> (c.trimDb));
        set ("drive", static_cast<float> (c.driveDb));
        set ("ceiling", static_cast<float> (c.limiter.ceilingDbtp));
        set ("release", static_cast<float> (c.limiter.releaseMs));
        set ("adaptive", c.limiter.adaptiveRelease ? 1.0f : 0.0f);

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
        // Match ref: what Learn needs for its reference / your mix / master comparison.
        if (s.reference.valid)
        {
            auto ref = ReferenceTrack::profileToVar (s.reference);
            if (auto* o = ref.getDynamicObject())
                o->removeProperty ("displayDb");   // the spectrum curve comes from the reference itself
            doc->setProperty ("reference", ref);
            doc->setProperty ("target", "Ref " + juce::String (r.targetLufs, 1).replace ("-", juce::String::fromUTF8 ("\xE2\x88\x92")) + " LUFS");
            doc->setProperty ("mixStats", statsToVar (r.analysis));
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

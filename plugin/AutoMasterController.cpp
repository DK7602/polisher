#include "AutoMasterController.h"
#include "ModuleParams.h"

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

    const char* const kIntensityNames[] = { "Gentle", "Balanced", "Punchy" };
    const char* const kTargetNames[] = { "Stream \xE2\x88\x92" "14", "Loud \xE2\x88\x92" "9", "Club \xE2\x88\x92" "7" };
}

AutoMasterController::AutoMasterController (juce::AudioProcessorValueTreeState& s)
    : state (s)
{
    const auto maxSamples = static_cast<size_t> (kMaxSampleRate * kListenSeconds);
    captureL.assign (maxSamples, 0.0f);
    captureR.assign (maxSamples, 0.0f);
}

AutoMasterController::~AutoMasterController()
{
    stopTimer();
    pool.removeAllJobs (true, 60000);
}

void AutoMasterController::prepare (double newSampleRate)
{
    if (newSampleRate <= 0.0)
        return;
    const double old = sampleRate.exchange (newSampleRate);
    capacity.store (static_cast<int> (std::min (newSampleRate * kListenSeconds, static_cast<double> (captureL.size()))));
    if (old != newSampleRate && stateFlag.load() == listening)
        written.store (0);   // different sample rate: start the listening over
}

bool AutoMasterController::start()
{
    if (getState() == analysing || capacity.load() == 0)
        return false;
    ++session;
    written.store (0);
    heardAudio.store (false);
    hearingAudio = false;
    quietTicks = 0;
    stateFlag.store (listening, std::memory_order_release);
    startTimerHz (15);
    return true;
}

void AutoMasterController::cancel()
{
    // Only listening can be cancelled; once analysis has started it finishes (it takes a moment).
    if (getState() == listening)
    {
        stateFlag.store (idle);
        stopTimer();
    }
}

float AutoMasterController::getProgress() const noexcept
{
    const int cap = capacity.load();
    return cap > 0 ? juce::jmin (1.0f, static_cast<float> (written.load()) / static_cast<float> (cap)) : 0.0f;
}

void AutoMasterController::timerCallback()
{
    if (heardAudio.exchange (false)) { hearingAudio = true; quietTicks = 0; }
    else if (++quietTicks > 20)       hearingAudio = false;   // ~1.3 s without music

    if (getState() == listening && written.load (std::memory_order_acquire) >= capacity.load())
        launchAnalysis();

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
    stateFlag.store (analysing);

    const int cap = capacity.load();
    polisher::AudioClip clip;
    clip.sampleRate = sampleRate.load();
    clip.channels = { std::vector<float> (captureL.begin(), captureL.begin() + cap),
                      std::vector<float> (captureR.begin(), captureR.begin() + cap) };

    auto choice = [this] (const char* id) { return juce::roundToInt (state.getRawParameterValue (id)->load()); };
    polisher::AutoMasterSettings settings;
    settings.genre = static_cast<polisher::Genre> (juce::jlimit (0, polisher::kNumGenres - 1, choice ("amGenre")));
    settings.intensity = static_cast<polisher::Intensity> (juce::jlimit (0, 2, choice ("amIntensity")));
    settings.target = static_cast<polisher::LoudnessTarget> (juce::jlimit (0, 2, choice ("amTarget")));
    // Stem EQ is the user's: Auto-Master hears the mix with it and leaves it as it is.
    settings.stem.enabled = state.getRawParameterValue ("stemOn")->load() > 0.5f;
    for (int k = 0; k < polisher::kStems; ++k)
        settings.stem.gainDb[static_cast<size_t> (k)] = state.getRawParameterValue (ModuleParams::stemIds[k])->load();
    pendingSettings = settings;

    const int thisSession = session;
    pool.addJob ([this, clip = std::move (clip), settings, thisSession]
    {
        std::unique_ptr<polisher::AutoMasterResult> result;
        try
        {
            result = std::make_unique<polisher::AutoMasterResult> (polisher::autoMaster (clip, settings));
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

    if (r.ok)
    {
        // Undo snapshot of everything Auto-Master may change.
        undoSnapshot.clear();
        undoValid.store (true);
        for (auto* p : state.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (! rp->getParameterID().startsWith ("am") && rp->getParameterID() != "autoreset"
                    && rp->getParameterID() != "resetOnChange")
                    undoSnapshot.emplace_back (rp, rp->getValue());

        const auto& c = r.chain;
        auto* values = new juce::DynamicObject();
        auto set = [&] (const juce::String& id, float v) { setParam (id, v); values->setProperty (id, v); };

        set ("bypass", 0.0f);
        set ("eqOn", 1.0f);
        for (int b = 0; b < polisher::kEqBands; ++b)
        {
            set ("eqGain" + juce::String (b + 1), static_cast<float> (c.eq.gainDb[static_cast<size_t> (b)]));
            set ("eqFreq" + juce::String (b + 1), static_cast<float> (polisher::kEqBandSpecs[static_cast<size_t> (b)].defaultHz));
        }
        set ("ulOn", 1.0f);
        set ("ulAmount", static_cast<float> (c.unlimiter.amount));
        set ("ulLength", static_cast<float> (c.unlimiter.lengthMs));
        set ("ulFocus", static_cast<float> (static_cast<int> (c.unlimiter.focus)));
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
        }
        set ("imgOn", 1.0f);
        set ("imgLow", static_cast<float> (c.imager.lowWidth));
        set ("imgMid", static_cast<float> (c.imager.midWidth));
        set ("imgHigh", static_cast<float> (c.imager.highWidth));
        set ("satOn", 1.0f);
        set ("satMode", static_cast<float> (static_cast<int> (c.saturation.mode)));
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
        doc->setProperty ("bassWidth", r.analysis.bassWidthPercent);
    }

    // Saved with the plug-in state (see getReportForSaving), so it survives reopening the project.
    setReport (juce::JSON::toString (juce::var (doc), true));
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
    undoValid.store (false);
    setReport (savedReport);
}

juce::String AutoMasterController::getReportForSaving() const
{
    const juce::ScopedLock sl (reportLock);
    return report;
}

bool AutoMasterController::undo()
{
    if (! canUndo())
        return false;
    for (auto& [p, v] : undoSnapshot)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (v);
        p->endChangeGesture();
    }
    undoSnapshot.clear();
    undoValid.store (false);
    setReport ({});
    return true;
}

juce::String AutoMasterController::getReportJson() const
{
    return getReportForSaving();
}

#include "PluginProcessor.h"
#include "polisher/MixHandoff.h"
#if ! POLISHER_HEADLESS                 // the plug-in-level tests build the processor without its editors
 #include "PluginEditor.h"
 #include "PolisherWebEditor.h"
#endif

namespace ParamIDs
{
    inline constexpr const char* drive    = "drive";
    inline constexpr const char* ceiling  = "ceiling";
    inline constexpr const char* release  = "release";
    inline constexpr const char* adaptive = "adaptive";
    inline constexpr const char* bassFirst = "maxBassFirst";
    inline constexpr const char* dither = "maxDither";
    inline constexpr const char* bypass   = "bypass";
    inline constexpr const char* autoReset = "autoreset";
    inline constexpr const char* resetOnChange = "resetOnChange";
    inline constexpr const char* levelMatch = "levelMatch";
    inline constexpr const char* clipOn        = "clipOn";
    inline constexpr const char* clipLevel     = "clipLevel";
    inline constexpr const char* clipSoftness  = "clipSoftness";
    inline constexpr const char* clipOversampling = "clipOversampling";
    inline constexpr const char* eqOn = "eqOn";
    inline constexpr const char* bassOn = "bassOn";
    inline constexpr const char* bassMono = "bassMono";
    inline constexpr const char* bassTame = "bassTame";
    inline constexpr const char* bassPunch = "bassPunch";
    inline juce::String eqGain (int band) { return "eqGain" + juce::String (band + 1); }
    inline juce::String eqFreq (int band) { return "eqFreq" + juce::String (band + 1); }
    inline juce::String eqMode (int band) { return "eqMode" + juce::String (band + 1); }
}

namespace
{
    const char* const kEqBandNames[polisher::kEqBands] = { "Low End", "Bass", "Low Mid", "Mid", "Upper Mid", "Presence", "Air" };

    juce::String hzText (float v)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    }

    /** A pick restored from a preset (or the host's A/B compare, or a default preset) carries the time it was made,
        often newer than this song's pick: it would switch the style (or DAW) of all three plug-ins. When another one
        is live in the suite, it comes back as "not picked" and follows that one. Opening a project is unaffected:
        nothing holds a choice then (a closed or inactive project left it). Any thread. */
    void followLiveChoice (suite::SharedChoice& c, suite::Choice which)
    {
        suite::StyleInfo live;
        if (c.style().pickedAt != 0 && suite::Link::process().readChoice (which, live, suite::nowMs()) && live.key != c.style().key)
            c.setUnpicked (c.style().key, c.style().name);
    }
}

PolisherAudioProcessor::PolisherAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PolisherParams", createParameterLayout())
{
    driveParam    = parameters.getRawParameterValue (ParamIDs::drive);
    ceilingParam  = parameters.getRawParameterValue (ParamIDs::ceiling);
    releaseParam  = parameters.getRawParameterValue (ParamIDs::release);
    adaptiveParam = parameters.getRawParameterValue (ParamIDs::adaptive);
    bassFirstParam = parameters.getRawParameterValue (ParamIDs::bassFirst);
    ditherParam = parameters.getRawParameterValue (ParamIDs::dither);
    bypassParam   = parameters.getRawParameterValue (ParamIDs::bypass);
    trimParam     = parameters.getRawParameterValue ("inTrim");
    autoResetParam = parameters.getRawParameterValue (ParamIDs::autoReset);
    clipOnParam   = parameters.getRawParameterValue (ParamIDs::clipOn);
    clipLevelParam = parameters.getRawParameterValue (ParamIDs::clipLevel);
    clipSoftnessParam  = parameters.getRawParameterValue (ParamIDs::clipSoftness);
    clipOversamplingParam = parameters.getRawParameterValue (ParamIDs::clipOversampling);
    eqOnParam = parameters.getRawParameterValue (ParamIDs::eqOn);
    levelMatchParam = parameters.getRawParameterValue (ParamIDs::levelMatch);
    stateRevisionParam = parameters.getParameter (kStateRevisionId);
    moduleParams.attach (parameters);
    imgOnParam   = parameters.getRawParameterValue ("imgOn");
    imgLowParam  = parameters.getRawParameterValue ("imgLow");
    imgMidParam  = parameters.getRawParameterValue ("imgMid");
    imgHighParam = parameters.getRawParameterValue ("imgHigh");
    bassOnParam    = parameters.getRawParameterValue (ParamIDs::bassOn);
    bassMonoParam  = parameters.getRawParameterValue (ParamIDs::bassMono);
    bassTameParam  = parameters.getRawParameterValue (ParamIDs::bassTame);
    bassPunchParam = parameters.getRawParameterValue (ParamIDs::bassPunch);
    for (int b = 0; b < polisher::kEqBands; ++b)
    {
        eqGainParams[static_cast<size_t> (b)] = parameters.getRawParameterValue (ParamIDs::eqGain (b));
        eqFreqParams[static_cast<size_t> (b)] = parameters.getRawParameterValue (ParamIDs::eqFreq (b));
        eqModeParams[static_cast<size_t> (b)] = parameters.getRawParameterValue (ParamIDs::eqMode (b));
    }

    // Undo / redo: an Auto-Master run is one step, and it carries the report and Tone Steady's target
    // (undo brings back the ones before; snapshots store and recall them too). Snapshots tell MATCH which
    // slot is playing (see handleMatchRequests).
    history.setExtraState ([this] { return captureExtraState(); },
                           [this] (const juce::String& extra) { restoreExtraState (extra); });
    autoMaster.onApplyBegin = [this] (const juce::String& label) { history.beginGroup (label); };
    // Auto-Master usually moves the level by several dB: MATCH learns it afresh (matched within about half
    // a second of B, exactly from ~3 s) instead of sliding there from the old level over ~20 s.
    autoMaster.onApplyEnd = [this]
    {
        history.endGroup();
        markStateChanged();   // its report and Tone Steady's target are saved with the project, but aren't parameters
        matchRelearnRequest.store (std::max (1, matchRelearnRequest.load()));
    };
    snapshots.onSwitch = [this] (int leaving, int entering) { matchSwitchRequest.store (0x100 | (leaving + 1) | ((entering + 1) << 4)); };
    snapshots.onStore = [this] (int slot)
    {
        matchForgetRequest.fetch_or (1 << slot);
        markStateChanged();   // the slots are saved with the project, but they are not parameters
    };

    // The suite link: Mixology (in the same project) sees what this Masterology aims for, and hands it the mix.
    suiteId = static_cast<std::uint64_t> (juce::Random::getSystemRandom().nextInt64()) ^ reinterpret_cast<std::uintptr_t> (this);
    if (suiteId == 0) suiteId = 1;
    daw.setUnpicked (detectedDaw, suite::dawName (detectedDaw));   // until you pick one (here or in Mixology / Voxology)
    startTimerHz (4);
}

PolisherAudioProcessor::~PolisherAudioProcessor()
{
    stopTimer();
    leaveSuite();
}

void PolisherAudioProcessor::releaseResources()
{
    suitePresence.stop();   // off the suite link at the next tick (see updateSuite)
}

void PolisherAudioProcessor::leaveSuite()
{
    // Mixology stops seeing this Masterology at once, and a style or DAW only this instance kept alive lapses
    // straight away (another project, or the next one opened, doesn't take it).
    auto& link = suite::Link::process();
    link.releaseMaster (suiteSlot, suiteId);
    suiteSlot = -1;
    hasHandoff = false;
    placement.forget();
    lastSuiteMs = 0;
    const juce::ScopedLock sl (styleLock);
    songStyle.leave (link, suiteId);
    daw.leave (link, suiteId);
}

void PolisherAudioProcessor::updateSuite()
{
    auto& link = suite::Link::process();
    const auto now = suite::nowMs();
    // Only while the host runs this Masterology. The block is one per host process, and a project that is open but
    // not active (or an insert switched off) keeps its timer running: it must neither take another song's style nor
    // look present to Mixology. It joins again at the next prepareToPlay; the newest pick still wins.
    const auto step = suitePresence.update (now);
    if (step == suite::Presence::Step::leave)
        leaveSuite();
    if (! suitePresence.takingPart())
        return;
    if (step == suite::Presence::Step::join)
        findDawAgain();
    hasHandoff = link.readMix (handoff, now);
    // Is the hand-off about the mix this Masterology hears? It comes from whichever Mixology publishes (on a group, after
    // Masterology, in another open project...): Mixology right before Masterology measures the same short-term loudness.
    const auto dt = lastSuiteMs == 0 ? std::uint64_t { 0 } : std::min<std::uint64_t> (now - lastSuiteMs, 1000);
    lastSuiteMs = now;
    const float inShort = meters.inputShortTerm.load();
    const bool music = meters.transport.load() != 0 && inShort > suitecheck::Placement::kMusicLufs;
    inputMono.update (dt, music, meters.correlation.load());   // (L/R stay as they come in: mono in, mono there)
    if (hasHandoff)
    {
        lastHandoffMs = now;
        placement.update (dt, music, inShort, handoff.shortTermLufs);
    }
    else if (now - lastHandoffMs > 3000)
        placement.forget();   // (a read that fails now and then keeps the verdict)
    // Auto Analyze hears the mix afresh when Mixology's word on it changes (what its Analyze Mix found). A read
    // that fails now and then (Mixology busy, or silent a moment) keeps the last key, so the listen isn't restarted.
    if (hasHandoff)
    {
        std::uint64_t key = 1469598103934665603ull;
        auto mixIn = [&key] (std::uint64_t v) { key = (key ^ v) * 1099511628211ull; };
        mixIn (handoff.flags & suite::MixInfo::analysed);
        for (float d : { handoff.lowDb, handoff.lowMidDb, handoff.presenceDb, handoff.airDb })
            mixIn (static_cast<std::uint64_t> (std::lround (d * 10.0f)));
        mixIn (suite::nameHash (handoff.styleKey));
        autoMaster.setMixKey (key);
    }
    // The song's style: the newest choice in Voxology, Mixology or Masterology wins (from a moment after joining,
    // so the plug-ins of a project just switched away from have left first).
    bool adopted = false;
    if (suitePresence.settled (now))
    {
        const juce::ScopedLock sl (styleLock);
        adopted = songStyle.sync (link, suiteId, now);
        if (daw.sync (link, suiteId, now)) ++styleVersion;
    }
    if (adopted)
    {
        ++styleVersion;
        applyStyleGenre();
    }
    if (suiteSlot < 0) suiteSlot = link.claimMaster (suiteId, now);
    if (suiteSlot < 0) return;   // four Masterologys already

    suite::MasterInfo g;
    const int target = juce::jlimit (0, 2, juce::roundToInt (parameters.getRawParameterValue ("amTarget")->load()));
    const int genre = juce::jlimit (0, polisher::kNumGenres - 1, juce::roundToInt (parameters.getRawParameterValue ("amGenre")->load()));
    if (reference.getVersion() != suiteRefVersion)   // a reference sets the loudness instead of the target
    {
        suiteRefVersion = reference.getVersion();
        suiteRef = reference.getState() == ReferenceTrack::loaded ? reference.getProfile() : polisher::ReferenceProfile {};
    }
    if (suiteRef.valid)
    {
        g.goalName = "Matching " + suiteRef.name;
        g.targetLufs = static_cast<float> (std::clamp (polisher::referenceLoudness (suiteRef), polisher::kRefMinLufs, polisher::kRefMaxLufs));
    }
    else
    {
        g.goalName = polisher::handoff::targetName (target);
        g.targetLufs = static_cast<float> (polisher::targetLufs (static_cast<polisher::LoudnessTarget> (target)));
    }
    g.genreName = polisher::genreName (static_cast<polisher::Genre> (genre));
    if (const auto st = songStyleNow(); ! st.name.empty()) g.genreName = st.name;
    g.ceilingDbtp = ceilingParam->load();
    g.inputLufs = meters.inputIntegrated.load();
    g.inputPeakDbtp = meters.inputTruePeak.load();
    g.version = static_cast<std::uint32_t> (JucePlugin_VersionCode);
    if (! link.publishMaster (suiteSlot, suiteId, g, now)) suiteSlot = -1;   // taken over while silent: claim again
}

std::string PolisherAudioProcessor::findDaw()
{
    return suite::dawFromHost (juce::PluginHostType::getHostPath().toStdString(), juce::PluginHostType().getHostDescription());
}

void PolisherAudioProcessor::findDawAgain()
{
    if (dawDetected() != "other")
        return;
    const auto found = findDaw();
    if (found == "other")
        return;
    {
        const juce::ScopedLock sl (styleLock);
        detectedDaw = found;
        if (daw.style().pickedAt != 0)
            return;   // you picked one: that stays
        daw.setUnpicked (detectedDaw, suite::dawName (detectedDaw));
    }
    ++styleVersion;
}

void PolisherAudioProcessor::pickStyle (const std::string& key, const std::string& name)
{
    {
        const juce::ScopedLock sl (styleLock);
        if (key == songStyle.style().key) return;
        songStyle.pick (key, name);
    }
    ++styleVersion;
    applyStyleGenre();
    markStateChanged();
    updateSuite();   // the others follow at once
}

void PolisherAudioProcessor::pickDaw (const std::string& key)
{
    {
        const juce::ScopedLock sl (styleLock);
        // "Found: <your DAW>" ("") is a fresh pick of the DAW found from the host: the newest pick, so every plug-in
        // follows. Going back to "not picked" would be undone at once: the earlier pick is still alive in the shared
        // slot (this very instance kept it there), and a choice that isn't picked always takes the shared one.
        const auto& wanted = key.empty() ? detectedDaw : key;
        if (wanted == daw.style().key && daw.style().pickedAt != 0) return;
        daw.pick (wanted, suite::dawName (wanted));
    }
    ++styleVersion;
    markStateChanged();
    updateSuite();   // the others follow at once
}

void PolisherAudioProcessor::applyStyleGenre()
{
    // Auto-Master's genre (6) is what the song's style (75) belongs to. Only Auto-Master's next run and the dotted
    // target line use it: the sound you have now doesn't change.
    const int genre = polisher::handoff::genreForMixStyle (songStyleNow().key);
    if (genre < 0) return;
    auto* p = parameters.getParameter ("amGenre");
    const float v = p->convertTo0to1 (static_cast<float> (genre));
    if (std::abs (p->getValue() - v) > 1e-4f)
    {
        p->beginChangeGesture();   // VST3 hosts expect beginEdit / endEdit around a change the plug-in makes
        p->setValueNotifyingHost (v);
        p->endChangeGesture();
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout PolisherAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto dbText = [] (float v, int) { return String (v, 1) + " dB"; };

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::drive, 1 }, "Drive",
        NormalisableRange<float> (0.0f, 24.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::ceiling, 1 }, "Ceiling",
        NormalisableRange<float> (-3.0f, 0.0f, 0.01f), -1.0f,
        AudioParameterFloatAttributes().withLabel ("dBTP")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " dBTP"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::release, 1 }, "Release",
        NormalisableRange<float> (10.0f, 1000.0f, 0.1f, 0.4f), 120.0f,
        AudioParameterFloatAttributes().withLabel ("ms")
            .withStringFromValueFunction ([] (float v, int) { return String (juce::roundToInt (v)) + " ms"; })));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::adaptive, 1 }, "Adaptive Release", true));
    // Bass First and Dither (version hint 11). Their defaults (0 %, Off) are also what a state saved before
    // they existed loads as: exactly how it sounded then.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::bassFirst, 11 }, "Bass First",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return String (juce::roundToInt (v)) + " %"; })));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::dither, 11 }, "Dither", StringArray { "Off", "16-bit", "24-bit" }, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::bypass, 1 }, "Bypass", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::autoReset, 1 }, "Reset Meter On Play", true,
                                                      AudioParameterBoolAttributes().withAutomatable (false)));
    // Read by the editor: restart the loudness measurement shortly after you change a control, so
    // Integrated describes the current settings instead of averaging in the old ones.
    // Level-matched A/B: while on, B (the master) is turned down to the original's loudness so a
    // comparison is about tone and punch, not volume. Off during offline export, when the editor
    // closes and when a project opens (setStateInformation), so it can't end up in a bounce by accident.
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::levelMatch, 1 }, "Level Match", false,
                                                      AudioParameterBoolAttributes().withAutomatable (false)));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::resetOnChange, 1 }, "Reset Meter On Change", true,
                                                      AudioParameterBoolAttributes().withAutomatable (false)));

    // Soft Clipper (sits between Drive and the Maximizer's limiter).
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::clipOn, 1 }, "Clipper On", false));
    // Clip Level is relative to the Maximizer ceiling: 0 = clipping starts at the ceiling,
    // negative = clip harder (with makeup gain so peaks still reach the ceiling), positive = only
    // the biggest overs are clipped and the limiter does more.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::clipLevel, 1 }, "Clipper Level",
        NormalisableRange<float> (-9.0f, 3.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; })));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::clipSoftness, 1 }, "Clipper Softness",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
        AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return String (juce::roundToInt (v)) + " %"; })));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::clipOversampling, 1 }, "Clipper Oversampling",
        StringArray { "1x", "2x", "4x", "8x" }, 2));

    // Tonal EQ (after the Unlimiter, before Stem EQ). On by default: flat bands are skipped, so it costs nothing.
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::eqOn, 1 }, "EQ On", true));
    for (int b = 0; b < polisher::kEqBands; ++b)
    {
        const auto& spec = polisher::kEqBandSpecs[static_cast<size_t> (b)];
        const String name = kEqBandNames[b];
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamIDs::eqGain (b), 1 }, "EQ " + name + " Gain",
            NormalisableRange<float> (-static_cast<float> (polisher::kEqMaxGainDb), static_cast<float> (polisher::kEqMaxGainDb), 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; })));

        NormalisableRange<float> freqRange (static_cast<float> (spec.minHz), static_cast<float> (spec.maxHz), 0.1f);
        freqRange.setSkewForCentre (static_cast<float> (std::sqrt (spec.minHz * spec.maxHz)));   // feels logarithmic
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamIDs::eqFreq (b), 1 }, "EQ " + name + " Freq", freqRange, static_cast<float> (spec.defaultHz),
            AudioParameterFloatAttributes().withLabel ("Hz")
                .withStringFromValueFunction ([] (float v, int) { return hzText (v); })));
        // Which part of the stereo image the band works on (version hint 10). Stereo, the default, is
        // also what a state saved before it existed loads as: exactly how it sounded then.
        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { ParamIDs::eqMode (b), 10 }, "EQ " + name + " Channels", StringArray { "Stereo", "Mid", "Side" }, 0));
    }

    // Bass Control (after the EQ). On by default with every tool at its neutral setting.
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::bassOn, 1 }, "Bass On", true));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::bassMono, 1 }, "Bass Mono Below",
        NormalisableRange<float> (0.0f, static_cast<float> (polisher::kBassMonoMaxHz), 1.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return v < polisher::kBassMonoOffHz ? String ("Off") : String (roundToInt (v)) + " Hz"; })));
    auto percent = AudioParameterFloatAttributes().withLabel ("%")
                       .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; });
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::bassTame, 1 }, "Bass Sub Tame",
                                                       NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::bassPunch, 1 }, "Bass Punch",
                                                       NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, percent));

    // Stereo Imager (after Bass Control): width per band, 100 % = unchanged.
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "imgOn", 1 }, "Imager On", true));
    for (auto [id, name] : { std::pair { "imgLow", "Imager Low Width" }, std::pair { "imgMid", "Imager Mid Width" }, std::pair { "imgHigh", "Imager High Width" } })
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f,
                                                           AudioParameterFloatAttributes().withLabel ("%")
                                                               .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; })));

    // Dynamics (after Bass Control) and Saturation (after the Imager).
    ModuleParams::addTo (layout);

    // Auto-Master choices (saved with the project; not automatable).
    auto choice = [&] (const char* id, const char* name, StringArray items, int def)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def,
                                                            AudioParameterChoiceAttributes().withAutomatable (false)));
    };
    choice ("amGenre", "Auto-Master Genre", { "Trap / Hip-Hop", "R&B", "Pop", "EDM", "Rock", "Acoustic" }, 0);
    choice ("amIntensity", "Auto-Master Intensity", { "Gentle", "Balanced", "Punchy" }, 1);
    choice ("amTarget", "Auto-Master Target", { "Stream -14", "Loud -9", "Club -7" }, 1);

    // Not a control: it goes up by one whenever something saved with the project that isn't a parameter changes
    // (markStateChanged). Logic (AU) ignores JUCE's "state changed" call, and so does a VST3 host that ignores
    // setDirty, but every host marks the project changed when a parameter moves. Not automatable (Logic and Cubase
    // don't offer it), and not a setting: undo, snapshots and A/B ignore it (SettingsScope::isSetting). Version hint 14.
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { kStateRevisionId, 14 }, "State Revision", 0, kStateRevisions - 1, 0,
                                                     AudioParameterIntAttributes().withAutomatable (false)));

    return layout;
}

bool PolisherAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void PolisherAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int channels = juce::jmax (1, getTotalNumOutputChannels());
    // Every module gets its current settings before it is prepared, so it starts in the saved state.
    // Set afterwards, Bass Control, Glue, Dynamics, the Imager and the spectral stage would fade in from
    // neutral over the first ~0.3 s of playback (and of an export that starts at bar 1). The EQ is handed
    // its settings even when they equal its defaults (updateLimiterParams skips unchanged ones), so it
    // knows it is configured and glides on the next change instead of jumping.
    eq.setParams (eq.getParams());
    updateLimiterParams();
    limiter.prepare (sampleRate, channels);
    dither.reset();
    clipper.prepare (channels, sampleRate);
    unlimiter.prepare (sampleRate, channels);
    eq.prepare (sampleRate, channels);
    stemEq.prepare (sampleRate, channels);
    bass.prepare (sampleRate, channels);
    glue.prepare (sampleRate, channels);
    dynamics.prepare (sampleRate, channels);
    imager.prepare (sampleRate);
    saturation.prepare (sampleRate, channels);
    autoMaster.prepare (sampleRate);
    levelMatch.prepare (sampleRate);
    currentMatchGain = 1.0;
    outputMeter.prepare (sampleRate, channels);
    inputMeter.prepare (sampleRate, channels);
    updateLimiterParams();
    currentDriveGain = targetDriveGain();
    currentTrimGain = targetTrimGain();
    // Constant latency whatever the settings: Unlimiter + Stem EQ look-ahead + saturation + clipper (all
    // oversampling modes) + limiter look-ahead.
    setLatencySamples (unlimiter.latencySamples() + stemEq.latencySamples() + polisher::Saturation::latencySamples() + polisher::SoftClipper::latencySamples()
                       + limiter.latencySamples());
    tailSeconds = getLatencySamples() / sampleRate + 0.2;

    // Monitoring: starts on the plain master; the editor's request (if any) is picked up again.
    monCapacity = juce::jmax (512, samplesPerBlock);
    for (auto* v : { &monPre, &monSig, &monAux })
        for (auto& ch : *v) ch.assign (static_cast<size_t> (monCapacity), 0.0);
    monLimGain.assign (static_cast<size_t> (monCapacity), 1.0);
    monGlueGain.assign (static_cast<size_t> (monCapacity), 1.0);
    monClipGain.assign (static_cast<size_t> (monCapacity), 1.0);
    const int longest = polisher::Saturation::latencySamples() + polisher::SoftClipper::latencySamples() + limiter.latencySamples();
    monForm.prepare (longest);
    monOut.prepare (longest);
    monClipDelay.prepare (longest);
    listenMix.prepare (sampleRate);
    monitorOut.prepare (sampleRate);
    stemEq.setTamerDelta (false);
    dynamics.setMonitorBand (-1, false);
    suitePresence.start();   // on the suite link from the next tick (see updateSuite)
}

namespace
{
    /** For a listen mode: the delay that lines its module's input up with its output (form) and the one
        that carries the result on to the output, past the modules after it (out). */
    std::pair<int, int> listenDelays (int mode, int limiterLatency) noexcept
    {
        namespace L = polisher::listen;
        const int sat = polisher::Saturation::latencySamples(), clip = polisher::SoftClipper::latencySamples();
        if (mode == L::satAdded)    return { sat, clip + limiterLatency };
        if (mode == L::clipRemoved) return { clip, limiterLatency };
        if (mode == L::maxRemoved)  return { limiterLatency, 0 };
        return { 0, sat + clip + limiterLatency };   // Resonance Tamer and Dynamics: formed inside the module
    }
}

int PolisherAudioProcessor::listenArmSamples (int mode) const noexcept
{
    namespace L = polisher::listen;
    if (mode == L::off)
        return 0;
    // The monitored signal is complete once its delay lines are full (plus a little margin); the
    // spectral stage needs ~1.25 frames to overlap-add a whole output, the Dynamics split ~30 ms to settle.
    const double sr = juce::jmax (8000.0, getSampleRate());
    const auto [form, out] = listenDelays (mode, limiter.latencySamples());
    int extra = 32;
    if (mode == L::tamerRemoved) extra += 2 * polisher::StemEQ::fftSizeFor (sr);
    if (L::isDynamics (mode)) extra += static_cast<int> (0.03 * sr);
    return form + out + extra;
}

void PolisherAudioProcessor::startListenMode (int mode) noexcept
{
    const auto [form, out] = listenDelays (mode, limiter.latencySamples());
    monForm.setDelay (form);
    monOut.setDelay (out);
    monClipDelay.setDelay (limiter.latencySamples());
}

void PolisherAudioProcessor::updateLimiterParams()
{
    // Drive is applied before the clipper (see processAnyPrecision), so the limiter gets none.
    polisher::LimiterParams p;
    p.driveDb         = 0.0;
    p.ceilingDbtp     = ceilingParam->load();
    p.releaseMs       = releaseParam->load();
    p.adaptiveRelease = adaptiveParam->load() > 0.5f;
    p.bypassed        = bypassParam->load() > 0.5f;
    p.bassFirst       = juce::jlimit (0.0, 1.0, static_cast<double> (bassFirstParam->load()) / 100.0);

    const auto& cur = limiter.getParams();
    if (p.driveDb != cur.driveDb || p.ceilingDbtp != cur.ceilingDbtp || p.releaseMs != cur.releaseMs
        || p.adaptiveRelease != cur.adaptiveRelease || p.bypassed != cur.bypassed || p.bassFirst != cur.bassFirst)
        limiter.setParams (p);
    static constexpr int kDitherBits[] = { 0, 16, 24 };
    dither.setBits (kDitherBits[juce::jlimit (0, 2, juce::roundToInt (ditherParam->load()))]);

    static constexpr int kOversampling[] = { 1, 2, 4, 8 };
    polisher::ClipperParams c;
    c.enabled      = clipOnParam->load() > 0.5f && ! p.bypassed;
    const double clipLevel = clipLevelParam->load();
    c.thresholdDb  = p.ceilingDbtp + clipLevel;
    c.makeupDb     = juce::jmax (0.0, -clipLevel);
    c.softness     = clipSoftnessParam->load() / 100.0;
    c.oversampling = kOversampling[juce::jlimit (0, 3, juce::roundToInt (clipOversamplingParam->load()))];

    polisher::TonalEQParams e;
    e.enabled = eqOnParam->load() > 0.5f && ! p.bypassed;
    for (size_t b = 0; b < static_cast<size_t> (polisher::kEqBands); ++b)
    {
        e.gainDb[b] = eqGainParams[b]->load();
        e.freqHz[b] = eqFreqParams[b]->load();
        e.channels[b] = static_cast<polisher::EqChannels> (juce::jlimit (0, 2, juce::roundToInt (eqModeParams[b]->load())));
    }
    const auto& ce = eq.getParams();
    if (e.enabled != ce.enabled || e.gainDb != ce.gainDb || e.freqHz != ce.freqHz || e.channels != ce.channels)
        eq.setParams (e);

    polisher::BassParams bp;
    bp.enabled = bassOnParam->load() > 0.5f && ! p.bypassed;
    bp.monoHz  = bassMonoParam->load();
    bp.tame    = bassTameParam->load() / 100.0;
    bp.punch   = bassPunchParam->load() / 100.0;
    const auto& cb = bass.getParams();
    if (bp.enabled != cb.enabled || bp.monoHz != cb.monoHz || bp.tame != cb.tame || bp.punch != cb.punch)
        bass.setParams (bp);

    polisher::ImagerParams ip;
    ip.enabled = imgOnParam->load() > 0.5f && ! p.bypassed;
    ip.lowWidth = imgLowParam->load();
    ip.midWidth = imgMidParam->load();
    ip.highWidth = imgHighParam->load();
    imager.setParams (ip);

    unlimiter.setParams (moduleParams.unlimiter (p.bypassed));
    stemEq.setParams (moduleParams.stem (p.bypassed));
    // Tone Steady holds the song to its stored tone when there is one, moved by Input Trim and the
    // Tonal EQ in front.
    auto steady = moduleParams.steady (p.bypassed);
    const bool newTone = toneTarget.fetch (songTone, songToneVersion);
    const double trimDb = static_cast<double> (trimParam->load());
    if (newTone || getSampleRate() != stageToneRate || trimDb != stageToneTrimDb || e.enabled != stageToneEq.enabled
        || e.gainDb != stageToneEq.gainDb || e.freqHz != stageToneEq.freqHz || e.channels != stageToneEq.channels)
    {
        stageToneEq = e;
        stageToneRate = getSampleRate();
        stageToneTrimDb = trimDb;
        stageTone = polisher::toneTargetAtStage (songTone, e, stageToneRate, trimDb);
    }
    steady.target = stageTone;
    stemEq.setToneSteady (steady);
    stemEq.setResonance (moduleParams.resonance (p.bypassed));
    glue.setParams (moduleParams.glue (p.bypassed));
    dynamics.setParams (moduleParams.dynamics (p.bypassed));
    saturation.setParams (moduleParams.saturation (p.bypassed));

    const auto& cc = clipper.getParams();
    if (c.enabled != cc.enabled || c.thresholdDb != cc.thresholdDb || c.softness != cc.softness || c.makeupDb != cc.makeupDb
        || c.oversampling != cc.oversampling)
        clipper.setParams (c);
}

double PolisherAudioProcessor::targetTrimGain() const
{
    return bypassParam->load() > 0.5f ? 1.0 : juce::Decibels::decibelsToGain (static_cast<double> (trimParam->load()));
}

double PolisherAudioProcessor::targetDriveGain() const
{
    return bypassParam->load() > 0.5f ? 1.0 : juce::Decibels::decibelsToGain (static_cast<double> (driveParam->load()));
}

template <typename Sample>
void PolisherAudioProcessor::processAnyPrecision (juce::AudioBuffer<Sample>& buffer)
{
    juce::ScopedNoDenormals noDenormals;

    // A host may call with no samples (e.g. only to hand over parameter changes). Nothing to do: the
    // Drive, Trim and MATCH ramps would otherwise jump to their new gain without ramping.
    if (buffer.getNumSamples() <= 0)
        return;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int c = numIn; c < numOut; ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    // Fresh loudness measurement each time the host transport starts (optional).
    bool isPlaying = wasPlaying, hostTransport = false;
    double hostSeconds = -1.0;   // where this block sits on the DAW's timeline (Auto-Master's report), if known
    if (auto* hostPlayHead = getPlayHead())
        if (auto position = hostPlayHead->getPosition())
        {
            isPlaying = position->getIsPlaying();
            hostTransport = true;
            if (const auto t = position->getTimeInSeconds(); t.hasValue() && isPlaying)
                hostSeconds = *t;
        }
    const bool justStarted = isPlaying && ! wasPlaying;
    wasPlaying = isPlaying;
    meters.transport.store (hostTransport ? (isPlaying ? 1 : 0) : -1);   // the loudness history pauses while stopped

    const bool resetPressed = meterResetRequested.exchange (false);
    if (resetPressed || (justStarted && autoResetParam->load() > 0.5f))
    {
        outputMeter.reset();
        inputMeter.reset();
    }
    // MATCH remembers the original's loudest peak across playback passes (so A never clips at the
    // start of a pass); RESET forgets it, e.g. after the mix was turned down, and makes MATCH learn the
    // level afresh (after a big change made by hand).
    if (resetPressed)
    {
        levelMatch.resetPeak();
        levelMatch.relearn();
    }
    if (statsResetRequested.exchange (false))
    {
        outputMeter.resetStatistics();
        inputMeter.resetStatistics();
    }

    updateLimiterParams();

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // Monitoring: the listen mode whose signal is formed this block (off: the plain master, and the
    // hooks below cost nothing). An offline export never contains any of it.
    namespace L = polisher::listen;
    const bool offline = isNonRealtime();
    int lm = L::off;
    if (offline)
    {
        if (listenMix.isActive() || monitorOut.isActive())
        {
            listenMix.reset();
            monitorOut.reset();
        }
    }
    else if (numChannels > 0 && numSamples <= monCapacity)
    {
        const int wanted = listenRequest.load();
        listenMix.setAutoLevel (listenAutoLevel.load());
        listenMix.request (wanted, listenArmSamples (wanted));
        monitorOut.setMono (monoRequest.load());
        monitorOut.setPhone (phoneRequest.load());
        monitorOut.setListenWanted (wanted != L::off);
        lm = listenMix.beginBlock();
        if (listenMix.startedArming())
            startListenMode (lm);
    }
    stemEq.setTamerDelta (lm == L::tamerRemoved);
    dynamics.setMonitorBand (L::isDynamics (lm) ? L::dynBand (lm) : -1, lm >= L::dynRemoved);
    double* pre[2] = { monPre[0].data(), monPre[1].data() };
    double* sig[2] = { monSig[0].data(), monSig[1].data() };
    double* aux[2] = { monAux[0].data(), monAux[1].data() };
    const auto in = [&buffer, numChannels] (int c, int i) { return static_cast<double> (buffer.getSample (juce::jmin (c, numChannels - 1), i)); };
    const auto copyPre = [&]
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < numSamples; ++i) pre[c][i] = in (c, i);
    };

    double inputPeak = 0.0;
    for (int c = 0; c < juce::jmin (numIn, numChannels); ++c)
        inputPeak = juce::jmax (inputPeak, static_cast<double> (buffer.getMagnitude (c, 0, numSamples)));

    inputTap.push (buffer.getArrayOfReadPointers(), numChannels, numSamples);
    inputMeter.process (buffer.getArrayOfReadPointers(), numChannels, numSamples);
    autoMaster.capture (buffer, hostSeconds);   // the untouched mix, while Auto-Master is listening

    // Input Trim (ramped per block): turns a too-hot mix down before the whole chain. The input meter,
    // the spectrum's "your mix" line, MATCH and Auto-Master all take the mix as it arrives, above.
    const double newTrim = targetTrimGain();
    if (newTrim != 1.0 || currentTrimGain != 1.0)
        buffer.applyGainRamp (0, numSamples, static_cast<Sample> (currentTrimGain), static_cast<Sample> (newTrim));
    currentTrimGain = newTrim;

    unlimiter.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);
    eq.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);
    if (lm == L::tamerRemoved)
        stemEq.process (buffer.getArrayOfWritePointers(), numChannels, numSamples, aux);   // + what the tamer cut
    else
        stemEq.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);
    bass.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);
    if (lm == L::tamerRemoved)
    {
        // The tamer's cut was formed before the Glue: it gets the Glue's gain, sample for sample, as the
        // master does (one broadband gain, no latency).
        glue.process (buffer.getArrayOfWritePointers(), numChannels, numSamples, monGlueGain.data());
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < numSamples; ++i) aux[c][i] *= monGlueGain[static_cast<size_t> (i)];
    }
    else
        glue.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);
    if (L::isDynamics (lm))
        dynamics.process (buffer.getArrayOfWritePointers(), numChannels, numSamples, aux);  // + the monitored band
    else
        dynamics.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);
    imager.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);
    if (lm == L::satAdded) copyPre();
    saturation.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);

    // Drive (ramped per block so knob moves don't click) -> Soft Clipper -> limiter.
    const double newGain = targetDriveGain();
    if (lm != L::off && lm != L::clipRemoved && lm != L::maxRemoved)
    {
        // Monitored signals formed before Drive: what Saturation added (output minus its aligned input),
        // or the tamer's cut / the Dynamics band (with mono input, channel 1 repeats channel 0). They get
        // the same Drive the master gets here, then travel on to the output (and get the Soft Clipper's
        // and the limiter's gain there, below).
        const double driveStep = (newGain - currentDriveGain) / numSamples;
        for (int i = 0; i < numSamples; ++i)
        {
            double l, r;
            if (lm == L::satAdded)
            {
                double dl = pre[0][i], dr = pre[1][i];
                monForm.process (dl, dr);
                l = in (0, i) - dl;
                r = in (1, i) - dr;
            }
            else
            {
                l = aux[0][i];
                r = numChannels > 1 ? aux[1][i] : l;
            }
            const double g = currentDriveGain + driveStep * i;
            l *= g;
            r *= g;
            monOut.process (l, r);
            sig[0][i] = l;
            sig[1][i] = r;
        }
    }
    buffer.applyGainRamp (0, numSamples, static_cast<Sample> (currentDriveGain), static_cast<Sample> (newGain));
    currentDriveGain = newGain;

    if (lm == L::clipRemoved) copyPre();
    clipper.process (buffer.getArrayOfWritePointers(), numChannels, numSamples, lm != L::off && lm != L::maxRemoved ? monClipGain.data() : nullptr);
    if (lm == L::clipRemoved)
    {
        // What the clipping shaved off: the aligned input at the clipper's clean gain (its makeup) minus its output.
        for (int i = 0; i < numSamples; ++i)
        {
            double l = pre[0][i], r = pre[1][i];
            monForm.process (l, r);
            l = l * monClipGain[static_cast<size_t> (i)] - in (0, i);
            r = r * monClipGain[static_cast<size_t> (i)] - in (1, i);
            monOut.process (l, r);
            sig[0][i] = l;
            sig[1][i] = r;
        }
    }
    limiter.process (buffer.getArrayOfWritePointers(), numChannels, numSamples, lm != L::off ? monLimGain.data() : nullptr,
                     lm == L::maxRemoved ? aux : nullptr);
    if (lm == L::maxRemoved)
    {
        // What the limiter took off: its input with no limiting (delayed, through Bass First's split when that
        // is in the path, so the split's phase turn is not heard as "taken off") minus its output.
        for (int i = 0; i < numSamples; ++i)
        {
            sig[0][i] = aux[0][i] - in (0, i);
            sig[1][i] = (numChannels > 1 ? aux[1][i] : aux[0][i]) - in (1, i);
        }
    }
    else if (lm != L::off)
    {
        // Everything formed before the limiter is turned down with the master, sample by sample, so it
        // plays at the level it really has in the master. What was formed before the Soft Clipper also
        // gets the clipper's clean gain (its makeup when Clip Level is below 0; 1 when the clipper is
        // off), as the clipper applied it, carried past the limiter's look-ahead so it lines up.
        const bool beforeClipper = lm != L::clipRemoved;
        for (int i = 0; i < numSamples; ++i)
        {
            double g = monLimGain[static_cast<size_t> (i)];
            if (beforeClipper)
            {
                double clipGain = monClipGain[static_cast<size_t> (i)], unused = 0.0;
                monClipDelay.process (clipGain, unused);
                g *= clipGain;
            }
            sig[0][i] *= g;
            sig[1][i] *= g;
        }
    }
    // Dither (16 / 24-bit exports): the master's last step, so the host's conversion to that depth is exact.
    if (bypassParam->load() <= 0.5f)
        dither.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);
    outputMeter.process (buffer.getArrayOfReadPointers(), numChannels, numSamples);
    outputTap.push (buffer.getArrayOfReadPointers(), numChannels, numSamples);

    // Output channel peaks for the L/R meter (held until the editor reads them).
    auto channelPeakDb = [&] (int ch)
    {
        const auto mag = static_cast<double> (buffer.getMagnitude (juce::jmin (ch, numChannels - 1), 0, numSamples));
        return static_cast<float> (mag > 1.0e-5 ? 20.0 * std::log10 (mag) : -100.0);
    };
    if (numChannels > 0)
    {
        const float l = channelPeakDb (0), r = channelPeakDb (1);
        if (l > meters.peakLeft.load())  meters.peakLeft.store (l);
        if (r > meters.peakRight.load()) meters.peakRight.store (r);
    }

    publishMeters();

    // Level-matched A/B. Learn the loudness gap while the master plays (both readings are taken
    // before this gain, so the meters and spectrum always show the real master)...
    handleMatchRequests();
    const bool bypassed = bypassParam->load() > 0.5f;
    levelMatch.update (inputMeter.shortTermLufs(), outputMeter.shortTermLufs(), inputPeak, numSamples, ! bypassed,
                       inputMeter.momentaryLufs(), outputMeter.momentaryLufs());
    const double sideGainDb = bypassed ? levelMatch.gainForADb() : levelMatch.gainForBDb();
    meters.matchDb.store (static_cast<float> (sideGainDb));
    meters.correlation.store (static_cast<float> (imager.correlation()));

    // Listen / Solo: the monitored signal takes the master's place (after every meter and the spectrum,
    // which keep showing the real master).
    const bool listening = ! offline && listenMix.isActive();
    if (listening)
        listenMix.process (buffer.getArrayOfWritePointers(), numChannels, sig, numSamples,
                           juce::Decibels::decibelsToGain (static_cast<double> (ceilingParam->load())));
    meters.listenPlaying.store (listening ? listenMix.audible() : 0);
    meters.listenGainDb.store (static_cast<float> (listening ? listenMix.appliedGainDb() : 0.0));
    meters.listenUnderDb.store (static_cast<float> (listening ? listenMix.levelUnderMasterDb() : -100.0));

    // ...and while MATCH is on, turn the louder side down to the quieter one, with headroom so A
    // doesn't clip the interface (never during offline export).
    const bool matching = levelMatchParam->load() > 0.5f && ! offline;
    const double targetGain = matching ? juce::Decibels::decibelsToGain (sideGainDb) : 1.0;
    if (targetGain != 1.0 || currentMatchGain != 1.0)
        buffer.applyGainRamp (0, numSamples, static_cast<Sample> (currentMatchGain), static_cast<Sample> (targetGain));
    currentMatchGain = targetGain;

    // MONO / PHONE checks at the very end (after MATCH, so A and B get the same treatment), and the
    // 0 dBFS safety of all monitoring.
    if (! offline && (listening || monitorOut.isActive()))
        monitorOut.process (buffer.getArrayOfWritePointers(), numChannels, numSamples, true);
}

void PolisherAudioProcessor::clearToneTarget()
{
    if (! toneTarget.isSet())
        return;
    history.beginGroup ("Tone Steady: follow the music");
    toneTarget.clear();
    history.endGroup();
    markStateChanged();   // not a parameter: tell the host the song changed, so the project asks to be saved
}

void PolisherAudioProcessor::markStateChanged()
{
    // VST3: JUCE passes this on as the host's setDirty. The AU wrapper drops it (Logic never hears of it), and some
    // VST3 hosts ignore setDirty: the hidden State Revision parameter moving marks the project changed there too.
    updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));
    if (juce::MessageManager::existsAndIsCurrentThread())
        bumpStateRevision();
    else
        stateRevisionPending.store (true);   // the timer moves it, on the message thread
}

void PolisherAudioProcessor::bumpStateRevision()
{
    // As one gesture, like a control moved in the window (VST3 hosts expect beginEdit / endEdit around a change the
    // plug-in makes). The value only has to differ from the saved one; it wraps around.
    auto* p = stateRevisionParam;
    const int next = (juce::roundToInt (p->convertFrom0to1 (p->getValue())) + 1) % kStateRevisions;
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (next)));
    p->endChangeGesture();
}

void PolisherAudioProcessor::loadReference (const juce::File& file)
{
    reference.load (file);
    referenceLoadPending = true;
    markStateChanged();
}

void PolisherAudioProcessor::clearReference()
{
    reference.clear();
    referenceLoadPending = false;
    markStateChanged();
}

void PolisherAudioProcessor::pollReference()
{
    if (referenceLoadPending && reference.getState() != ReferenceTrack::loading)
    {
        referenceLoadPending = false;
        markStateChanged();
    }
}

void PolisherAudioProcessor::setAutoAnalyze (bool on)
{
    if (autoMaster.getAutoAnalyze() == on)
        return;
    autoMaster.setAutoAnalyze (on);
    markStateChanged();
}

juce::String PolisherAudioProcessor::captureExtraState() const
{
    // Besides the controls, an undo step or a snapshot carries what explains and shapes the sound without
    // being a parameter: the Auto-Master report and Tone Steady's stored target (the song's tone).
    auto* o = new juce::DynamicObject();
    o->setProperty ("polisherExtra", 1);
    o->setProperty ("report", autoMaster.getReportForSaving());
    o->setProperty ("tone", toneTarget.toJson());
    return juce::JSON::toString (juce::var (o), true);
}

void PolisherAudioProcessor::restoreExtraState (const juce::String& extra)
{
    const auto before = captureExtraState();
    const auto v = extra.isNotEmpty() ? juce::JSON::parse (extra) : juce::var();
    if (! v.isObject() || ! v.hasProperty ("polisherExtra"))
    {
        autoMaster.restoreReport (extra);   // a snapshot stored before Tone Steady targets: the report alone
    }
    else
    {
        autoMaster.restoreReport (v["report"].toString());
        if (const auto tone = v["tone"].toString(); tone != toneTarget.toJson())
            toneTarget.restoreFromJson (tone);
    }
    // Not parameters: tell the host the song changed (undo, redo or a recall brought back another report
    // or tone), so the project asks to be saved.
    if (captureExtraState() != before)
        markStateChanged();
}

void PolisherAudioProcessor::handleMatchRequests() noexcept
{
    // New settings stored in a slot (or another project): forget what MATCH learned for it, and count
    // the learning time afresh (what it learned so far belongs to the settings before).
    if (const int forget = matchForgetRequest.exchange (0); forget != 0)
    {
        for (size_t k = 0; k < matchMemory.size(); ++k)
            if ((forget >> k) & 1) matchMemory[k].valid = false;
        levelMatch.restartLearnedSeconds();
        relearnedLevel.valid = false;
    }
    if (const int relearn = matchRelearnRequest.exchange (0); relearn != 0)
    {
        // After Auto-Master the snapshot that was playing is told it was left a moment later (the editor
        // notices the change): the level learned until now is still its own.
        relearnedLevel = { relearn == 1 && levelMatch.hasLearned() && levelMatch.learnedSeconds() >= kMatchLearnSeconds, levelMatch.gapDb() };
        levelMatch.relearn();
    }
    const int req = matchSwitchRequest.exchange (0);
    if (req == 0)
        return;
    // The slot in place changed (a recall, or an undo / redo onto or off a slot). Keep the level MATCH
    // learned for the slot that played, if it played long enough for the level to be its own (learning
    // starts from a clean reading of that slot, so ~10 s of it is a settled level); start a slot heard
    // before from its level, holding it while the 3 s short-term readings still hold the settings from
    // before; learn a slot not heard yet afresh (roughly matched after half a second, exactly from ~3 s).
    const int leaving = (req & 15) - 1, entering = ((req >> 4) & 15) - 1;
    const auto slots = static_cast<int> (matchMemory.size());
    if (leaving >= 0 && leaving < slots)
    {
        if (levelMatch.hasLearned() && levelMatch.learnedSeconds() >= kMatchLearnSeconds)
            matchMemory[static_cast<size_t> (leaving)] = { true, levelMatch.gapDb() };
        else if (relearnedLevel.valid)
            matchMemory[static_cast<size_t> (leaving)] = relearnedLevel;
    }
    relearnedLevel.valid = false;
    if (entering >= 0 && entering < slots)
    {
        if (matchMemory[static_cast<size_t> (entering)].valid)
        {
            levelMatch.setGapDb (matchMemory[static_cast<size_t> (entering)].gapDb);
            levelMatch.holdLearning (static_cast<int> (kMatchHoldSeconds * juce::jmax (8000.0, getSampleRate())));
        }
        else
        {
            levelMatch.relearn();
        }
    }
    else
    {
        levelMatch.restartLearnedSeconds();   // a change left the slot: MATCH keeps learning as always
    }
}

template <typename Sample>
void PolisherAudioProcessor::processInChunks (juce::AudioBuffer<Sample>& buffer)
{
    const int total = buffer.getNumSamples();
    if (total <= monCapacity || monCapacity <= 0)
    {
        processAnyPrecision (buffer);
        return;
    }
    // A host block longer than prepareToPlay announced: process it in parts the monitoring buffers hold
    // (the chain streams sample by sample, so the master is the same either way).
    for (int start = 0; start < total; start += monCapacity)
    {
        juce::AudioBuffer<Sample> part (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, juce::jmin (monCapacity, total - start));
        processAnyPrecision (part);
    }
}

void PolisherAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processInChunks (buffer);
}

void PolisherAudioProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processInChunks (buffer);
}

void PolisherAudioProcessor::publishMeters()
{
    auto clampDb = [] (double v) { return static_cast<float> (std::isfinite (v) ? juce::jmax (v, -100.0) : -100.0); };

    meters.momentary.store (clampDb (outputMeter.momentaryLufs()));
    meters.shortTerm.store (clampDb (outputMeter.shortTermLufs()));
    meters.integrated.store (clampDb (outputMeter.integratedLufs()));
    meters.loudnessRange.store (static_cast<float> (outputMeter.loudnessRangeLu()));
    meters.truePeakMax.store (clampDb (outputMeter.truePeakDbtp()));
    meters.inputShortTerm.store (clampDb (inputMeter.shortTermLufs()));
    meters.inputIntegrated.store (clampDb (inputMeter.integratedLufs()));
    meters.inputTruePeak.store (clampDb (inputMeter.truePeakDbtp()));

    // Peak-hold style values: keep the largest until the editor reads and clears them.
    const float recent = clampDb (outputMeter.takeRecentTruePeakDbtp());
    if (recent > meters.truePeakRecent.load())
        meters.truePeakRecent.store (recent);
    const float tame = static_cast<float> (bass.takeMaxTameDb());
    if (tame < meters.bassTame.load())
        meters.bassTame.store (tame);
    const float punch = static_cast<float> (bass.takeMaxPunchDb());
    if (punch > meters.bassPunch.load())
        meters.bassPunch.store (punch);
    meters.bassWidth.store (static_cast<float> (bass.bassWidthPercent()));

    const float glueGr = static_cast<float> (glue.takeMaxReductionDb());
    if (glueGr < meters.glueGr.load())
        meters.glueGr.store (glueGr);
    for (int b = 0; b < polisher::kDynBands; ++b)
    {
        const float d = static_cast<float> (dynamics.takeMaxGrDb (b));
        auto& slot = meters.dynGr[static_cast<size_t> (b)];
        if (d < slot.load())
            slot.store (d);
        const float im = static_cast<float> (dynamics.takeMaxImpactDb (b));
        auto& imSlot = meters.dynImpact[static_cast<size_t> (b)];
        if (std::abs (im) > std::abs (imSlot.load()))
            imSlot.store (im);
        const float su = static_cast<float> (dynamics.takeMaxSustainDb (b));
        auto& suSlot = meters.dynSustain[static_cast<size_t> (b)];
        if (std::abs (su) > std::abs (suSlot.load()))
            suSlot.store (su);
    }
    const float lift = static_cast<float> (unlimiter.takeMaxBoostDb());
    if (lift > meters.ulBoost.load())
        meters.ulBoost.store (lift);
    meters.ulSquash.store (static_cast<float> (unlimiter.squashDb()));
    double rtDb = 0.0, rtHz = 0.0, tsDb = 0.0, tsHz = 0.0;
    stemEq.takeResonanceMeter (rtDb, rtHz);
    if (rtDb < meters.rtCut.load()) { meters.rtCut.store (static_cast<float> (rtDb)); meters.rtHz.store (static_cast<float> (rtHz)); }
    stemEq.takeToneSteadyMeter (tsDb, tsHz);
    if (std::abs (tsDb) > std::abs (meters.tsDb.load())) { meters.tsDb.store (static_cast<float> (tsDb)); meters.tsHz.store (static_cast<float> (tsHz)); }
    meters.tsDistance.store (static_cast<float> (stemEq.toneTargetDistanceDb()));
    meters.tsFar.store (stemEq.toneTargetFar());
    std::array<double, polisher::kStems> stemE {};
    meters.stemKeep.store (static_cast<float> (stemEq.keepLevelDb()));
    stemEq.takeStemLoudness (stemE);   // K-weighted: each stem's share of the loudness of the mix coming in
    for (size_t k = 0; k < stemE.size(); ++k)
        addTo (meters.stemEnergy[k], static_cast<float> (stemE[k]));
    double satRes = 0.0, satSig = 0.0;
    saturation.takeEnergies (satRes, satSig);
    addTo (meters.satResidual, static_cast<float> (satRes));
    addTo (meters.satSignal, static_cast<float> (satSig));

    const float clip = static_cast<float> (clipper.takeMaxClipDb());
    if (clip < meters.clipReduction.load())
        meters.clipReduction.store (clip);
    const float bassCut = static_cast<float> (limiter.takeMaxBassCutDb());
    if (bassCut < meters.bassFirstCut.load())
        meters.bassFirstCut.store (bassCut);
    const float gr = static_cast<float> (limiter.takeMaxGainReductionDb());
    if (gr < meters.gainReduction.load())
        meters.gainReduction.store (gr);
}

void PolisherAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
    {
        xml->setAttribute ("amReport", autoMaster.getReportForSaving());
        if (autoMaster.getAutoAnalyze())
            xml->setAttribute ("autoAnalyze", 1);   // absent: off (also every state saved before it existed)
        {
            // The song's style (shared with Voxology and Mixology) and when you picked it. Absent before one was chosen.
            const juce::ScopedLock sl (styleLock);
            if (! songStyle.style().key.empty())
                xml->setAttribute ("songStyle", juce::String::fromUTF8 (songStyle.save().c_str()));
            if (daw.style().pickedAt != 0)   // your DAW, when you picked one (else it is found from the host each time)
                xml->setAttribute ("daw", juce::String::fromUTF8 (daw.save().c_str()));
        }
        // Match ref: the reference song's measurements and name (no audio). Absent when none is loaded.
        if (const auto ref = reference.getJsonForSaving(); ref.isNotEmpty())
            xml->setAttribute ("amReference", ref);
        // Tone Steady's stored target (the song's own tone). Absent when it follows the music.
        if (const auto tone = toneTarget.toJson(); tone.isNotEmpty())
            xml->setAttribute ("toneTarget", tone);
        // Snapshots S1..S4 (a child element with a format version). Absent while every slot is empty.
        if (auto snaps = snapshots.toXml())
            xml->addChildElement (snaps.release());
        copyXmlToBinary (*xml, destData);
    }
}

void PolisherAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            const auto savedReport = xml->getStringAttribute ("amReport");
            const auto savedReference = xml->getStringAttribute ("amReference");   // "" in states from before Match ref
            const auto savedTone = xml->getStringAttribute ("toneTarget");         // "": Tone Steady follows the music (as before)
            const bool savedAuto = xml->getIntAttribute ("autoAnalyze", 0) != 0;
            xml->removeAttribute ("autoAnalyze");
            xml->removeAttribute ("amReport");
            xml->removeAttribute ("amReference");
            xml->removeAttribute ("toneTarget");
            {
                // The song's style ("" before one was chosen: the project then follows Mixology's or Voxology's).
                const juce::ScopedLock sl (styleLock);
                songStyle = suite::SongStyle { suite::StyleInfo::From::masterology };
                songStyle.restore (xml->getStringAttribute ("songStyle").toStdString());
                followLiveChoice (songStyle, suite::Choice::style);   // a preset doesn't change the song's style
                ++styleVersion;
            }
            xml->removeAttribute ("songStyle");
            {
                const juce::ScopedLock sl (styleLock);
                daw = suite::SharedChoice { suite::StyleInfo::From::masterology, suite::Choice::daw };
                if (! daw.restore (xml->getStringAttribute ("daw").toStdString()))
                    daw.setUnpicked (detectedDaw, suite::dawName (detectedDaw));
                followLiveChoice (daw, suite::Choice::daw);   // nor the DAW (a preset made in another one)
            }
            xml->removeAttribute ("daw");
            // Snapshots: their own element ("" in states from before snapshots: every slot empty).
            std::unique_ptr<juce::XmlElement> savedSnapshots;
            if (auto* e = xml->getChildByName (SettingsSnapshots::kTag))
            {
                savedSnapshots = std::make_unique<juce::XmlElement> (*e);
                xml->removeChildElement (e, true);
            }
            // Stem EQ Keep loudness (added after v0.15.0) is on for new instances, but a song saved before it
            // existed was mixed without it: load that state with it off, so it sounds as it did.
            // (JUCE would give a control missing from the state its default, which is on.)
            bool hasKeep = false;
            for (auto* child : xml->getChildIterator())
                hasKeep = hasKeep || child->getStringAttribute ("id") == "stemKeep";
            if (! hasKeep)
            {
                auto* keep = xml->createNewChildElement ("PARAM");
                keep->setAttribute ("id", "stemKeep");
                keep->setAttribute ("value", 0.0);
            }
            // MATCH is a listening aid, but the host saves it like any parameter: a song saved while
            // comparing would reopen with the master turned down (also in a Realtime Export, with no
            // window open to show it). A project always opens with MATCH off.
            for (auto* child : xml->getChildIterator())
                if (child->getStringAttribute ("id") == "levelMatch")
                    child->setAttribute ("value", 0.0);
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
            autoMaster.onStateRestored (savedReport);
            if (juce::MessageManager::existsAndIsCurrentThread())
            {
                pendingAutoAnalyze.store (-1);
                autoMaster.setAutoAnalyze (savedAuto);
            }
            else
                pendingAutoAnalyze.store (savedAuto ? 1 : 0);
            reference.restoreFromJson (savedReference);
            toneTarget.restoreFromJson (savedTone);
            snapshots.fromXml (savedSnapshots.get());
            // Another project or preset: undo steps and MATCH's levels (per snapshot, and the one in use)
            // no longer apply.
            history.requestClear();
            matchForgetRequest.store ((1 << SettingsSnapshots::kSlots) - 1);
            matchRelearnRequest.store (2);
        }
}

juce::AudioProcessorEditor* PolisherAudioProcessor::createEditor()
{
   #if POLISHER_HEADLESS
    return nullptr;
   #else
    // The liquid-glass web UI; falls back to the basic native editor if no web view is available
    // (for example a Windows machine without the WebView2 runtime).
    if (PolisherWebEditor::isSupported())
        return new PolisherWebEditor (*this);
    return new PolisherAudioProcessorEditor (*this);
   #endif
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PolisherAudioProcessor();
}

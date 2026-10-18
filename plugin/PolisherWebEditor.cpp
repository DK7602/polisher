#include "PolisherWebEditor.h"
#include "polisher/MixHandoff.h"

#include "PolisherUIData.h"

#include <algorithm>
#include <cstring>

namespace
{
    juce::String mimeTypeFor (const juce::String& file)
    {
        const auto ext = file.fromLastOccurrenceOf (".", false, false).toLowerCase();
        if (ext == "html") return "text/html";
        if (ext == "js")   return "text/javascript";
        if (ext == "css")  return "text/css";
        if (ext == "png")  return "image/png";
        if (ext == "webp") return "image/webp";
        if (ext == "svg")  return "image/svg+xml";
        if (ext == "ttf")  return "font/ttf";
        if (ext == "json") return "application/json";
        if (ext == "md")   return "text/markdown; charset=utf-8";
        return "text/plain";
    }

    juce::RangedAudioParameter& param (PolisherAudioProcessor& p, const char* id)
    {
        auto* rp = p.parameters.getParameter (id);
        jassert (rp != nullptr);
        return *rp;
    }

    // Rounded in double and divided by a whole number (10 for a step of 0.1), so the frame's JSON says -12.3, not
    // -12.300000190734863 (a float rounded, then widened to double): the frames are much shorter.
    double roundTo (double v, double step)
    {
        const double perStep = std::round (1.0 / step);
        return std::round (v * perStep) / perStep;
    }

    // The window remembers where you were. The size, the open module and LEARN on / off are properties of the
    // processor's state, so they are saved with the project. The loudness history the page draws is kept only
    // while the plug-in runs (a preset or a copy of Masterology mustn't carry another song's graph): per
    // instance, keyed by the processor's state tree, which loading a project or preset replaces.
    const juce::Identifier uiWidthId { "uiWidth" }, uiModuleId { "uiModule" }, uiLearnId { "uiLearn" };

    juce::String& loudnessHistoryFor (PolisherAudioProcessor& p)
    {
        struct Kept { juce::ValueTree state; juce::String history; };
        static std::vector<Kept> kept;   // message thread only
        // An instance that is gone (or loaded another state) leaves its tree held only here: forget it.
        kept.erase (std::remove_if (kept.begin(), kept.end(), [] (const Kept& k) { return k.state.getReferenceCount() <= 1; }), kept.end());
        for (auto& k : kept)
            if (k.state == p.parameters.state)
                return k.history;
        kept.push_back ({ p.parameters.state, {} });
        return kept.back().history;
    }
}

juce::WebBrowserComponent::Options PolisherWebEditor::makeBaseOptions()
{
    using Options = juce::WebBrowserComponent::Options;
    return Options{}
       #if JUCE_WINDOWS
        // WebView2 (Chromium) on Windows. The legacy IE backend can't run this UI, so it is never
        // used; if WebView2 is missing, isSupported() is false and the basic editor is shown instead.
        .withBackend (Options::Backend::webview2)
       #endif
        .withWinWebView2Options (Options::WinWebView2{}
                                     .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                              .getChildFile ("PolisherWebView"))
                                     .withBackgroundColour (juce::Colour (0xff050f22))
                                     .withStatusBarDisabled())
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withNativeIntegrationEnabled();
}

bool PolisherWebEditor::isSupported()
{
    return juce::WebBrowserComponent::areOptionsSupported (makeBaseOptions());
}

std::vector<std::unique_ptr<juce::WebSliderRelay>> PolisherWebEditor::makeSliderRelays (const juce::String& prefix)
{
    std::vector<std::unique_ptr<juce::WebSliderRelay>> relays;
    for (int b = 1; b <= polisher::kEqBands; ++b)
        relays.push_back (std::make_unique<juce::WebSliderRelay> (prefix + juce::String (b)));
    return relays;
}

juce::WebBrowserComponent::Options PolisherWebEditor::makeEditorOptions()
{
    auto options = makeBaseOptions()
                       .withOptionsFrom (driveRelay)
                       .withOptionsFrom (ceilingRelay)
                       .withOptionsFrom (releaseRelay)
                       .withOptionsFrom (adaptiveRelay)
                       .withOptionsFrom (bypassRelay)
                       .withOptionsFrom (autoResetRelay)
                       .withOptionsFrom (clipOnRelay)
                       .withOptionsFrom (resetOnChangeRelay)
                       .withOptionsFrom (levelMatchRelay)
                       .withOptionsFrom (clipLevelRelay)
                       .withOptionsFrom (clipSoftnessRelay)
                       .withOptionsFrom (clipOversamplingRelay)
                       .withOptionsFrom (eqOnRelay)
                       .withOptionsFrom (bassOnRelay)
                       .withOptionsFrom (imgOnRelay)
                       .withOptionsFrom (imgLowRelay)
                       .withOptionsFrom (imgMidRelay)
                       .withOptionsFrom (imgHighRelay)
                       .withOptionsFrom (bassMonoRelay)
                       .withOptionsFrom (bassTameRelay)
                       .withOptionsFrom (bassPunchRelay)
                       .withOptionsFrom (amGenreRelay)
                       .withOptionsFrom (amIntensityRelay)
                       .withOptionsFrom (amTargetRelay);
    for (auto& r : eqGainRelays) options = options.withOptionsFrom (*r);
    for (auto& r : eqFreqRelays) options = options.withOptionsFrom (*r);
    for (auto& r : moduleSliderRelays) options = options.withOptionsFrom (*r);
    for (auto& r : moduleToggleRelays) options = options.withOptionsFrom (*r);
    for (auto& r : moduleComboRelays) options = options.withOptionsFrom (*r);

    return options
        .withNativeFunction ("resetMeter", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 // resetMeter("stats") after a control change; resetMeter() = full reset.
                                 if (args.size() > 0 && args[0].toString() == "stats")
                                     audioProcessor.requestStatisticsReset();
                                 else
                                     audioProcessor.requestMeterReset();
                                 complete (true);
                             })
        // startAutoMaster() listens to the song and masters it; startAutoMaster("tone") only learns the
        // song's tone for Tone Steady. finishAutoMaster() = "use what you've heard" (12 s of music on).
        .withNativeFunction ("startAutoMaster", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 const bool toneOnly = args.size() > 0 && args[0].toString() == "tone";
                                 complete (audioProcessor.autoMaster.start (toneOnly ? AutoMasterController::toneOnly
                                                                                     : AutoMasterController::fullMaster));
                             })
        .withNativeFunction ("finishAutoMaster", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 complete (audioProcessor.autoMaster.finish());
                             })
        .withNativeFunction ("cancelAutoMaster", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 audioProcessor.autoMaster.cancel();
                                 complete (true);
                             })
        // Tone Steady goes back to following the music (forgets the song's stored tone): its own undo step.
        .withNativeFunction ("clearToneTarget", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 audioProcessor.clearToneTarget();
                                 complete (true);
                             })
        // Undo / redo (the processor's history): each returns what it did and what is next, so the
        // buttons update at once. historyGroup(true, label) ... historyGroup(false) makes the changes the
        // page sends in between one step (Apply of the Stem EQ suggestion).
        .withNativeFunction ("historyUndo", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 complete (historyResult (audioProcessor.history.undo()));
                             })
        .withNativeFunction ("historyRedo", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 complete (historyResult (audioProcessor.history.redo()));
                             })
        .withNativeFunction ("historyGroup", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 // One group at a time from the page; one left open is closed with the window.
                                 const bool begin = args.size() > 0 && static_cast<bool> (args[0]);
                                 if (begin && ! pageGroupOpen)
                                     audioProcessor.history.beginGroup (args.size() > 1 ? args[1].toString() : juce::String ("Changes"));
                                 else if (! begin && pageGroupOpen)
                                     audioProcessor.history.endGroup();
                                 pageGroupOpen = begin;
                                 complete (true);
                             })
        // Snapshots S1..S4: store the current settings in a slot, or recall one (an undo step).
        .withNativeFunction ("snapshotStore", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 audioProcessor.snapshots.store (args.size() > 0 ? static_cast<int> (args[0]) : -1);
                                 complete (true);
                             })
        .withNativeFunction ("snapshotRecall", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 complete (audioProcessor.snapshots.recall (args.size() > 0 ? static_cast<int> (args[0]) : -1));
                             })
        .withNativeFunction ("getAutoMasterReport", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 complete (audioProcessor.autoMaster.getReportJson());
                             })
        // Auto Analyze: switch it on / off (saved with the project), fetch the advice, apply or keep an edit
        // (a module id, or "all").
        .withNativeFunction ("setAutoAnalyze", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 audioProcessor.setAutoAnalyze (args.size() > 0 && static_cast<bool> (args[0]));
                                 complete (audioProcessor.autoMaster.getAutoAnalyze());
                             })
        .withNativeFunction ("getAutoAnalyze", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 complete (audioProcessor.autoMaster.getAutoAnalyzeJson());
                             })
        .withNativeFunction ("applyAutoEdit", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 complete (audioProcessor.autoMaster.applyAutoEdit (args.size() > 0 ? args[0].toString() : juce::String()));
                             })
        .withNativeFunction ("keepAutoEdit", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 complete (audioProcessor.autoMaster.keepAutoEdit (args.size() > 0 ? args[0].toString() : juce::String()));
                             })
        // Match ref: pick a reference song (the dialog runs asynchronously; the song is then read and
        // measured on a background thread), forget it, or fetch what's loaded.
        .withNativeFunction ("loadReference", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 auto folder = audioProcessor.reference.getLastFolder();
                                 if (! folder.isDirectory())
                                     folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
                                 referenceChooser = std::make_unique<juce::FileChooser> (
                                     "Choose a reference song: a released track you want your master to sound like",
                                     folder, "*.wav;*.wave;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
                                 juce::Component::SafePointer<PolisherWebEditor> safe (this);
                                 referenceChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                                                [safe] (const juce::FileChooser& chooser)
                                                                {
                                                                    const auto file = chooser.getResult();
                                                                    if (safe != nullptr && file.existsAsFile())
                                                                        safe->audioProcessor.loadReference (file);   // marks the project changed
                                                                });
                                 complete (true);
                             })
        .withNativeFunction ("clearReference", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 audioProcessor.clearReference();   // marks the project changed
                                 complete (true);
                             })
        .withNativeFunction ("getReference", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 complete (audioProcessor.reference.getInfoJson());
                             })
        // Monitoring: setMonitor(listenMode, mono, phone). Listening aids only: not parameters, back to
        // off when this window closes, never in an offline export.
        .withNativeFunction ("setMonitor", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 audioProcessor.setListenMode (args.size() > 0 ? static_cast<int> (args[0]) : 0);
                                 audioProcessor.setMonoCheck (args.size() > 1 && static_cast<bool> (args[1]));
                                 audioProcessor.setPhoneCheck (args.size() > 2 && static_cast<bool> (args[2]));
                                 complete (true);
                             })
        // The song's style (Mixology's list, the same in all three plug-ins): pickStyle(key, name). Sets the
        // Auto-Master genre it belongs to; Voxology and Mixology in the project follow.
        .withNativeFunction ("pickStyle", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 if (args.size() > 1) audioProcessor.pickStyle (args[0].toString().toStdString(), args[1].toString().toStdString());
                                 complete (true);
                             })
        // Your DAW ("" = the one Masterology found): Voxology and Mixology in the project follow.
        .withNativeFunction ("pickDaw", [this] (const juce::Array<juce::var>& args, auto complete)
                             { audioProcessor.pickDaw (args.size() > 0 ? args[0].toString().toStdString() : std::string()); complete (true); })
        // The spectrum's dotted line: a genre's typical balance (what Auto-Master's EQ aims for) as the
        // display draws it at this sample rate, computed by the engine so the page holds no numbers of
        // its own. getGenreCurve(genreIndex) -> JSON { genre, name, lufs, sr, displayDb }.
        .withNativeFunction ("getGenreCurve", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 const int g = args.size() > 0 ? juce::jlimit (0, polisher::kNumGenres - 1, static_cast<int> (args[0])) : 0;
                                 const auto genre = static_cast<polisher::Genre> (g);
                                 const double sr = audioProcessor.getSampleRate() > 0.0 ? audioProcessor.getSampleRate() : 48000.0;
                                 juce::Array<juce::var> points;
                                 for (const double v : polisher::genreDisplayCurve (genre, sr))
                                     points.add (v);
                                 auto* o = new juce::DynamicObject();
                                 o->setProperty ("genre", g);
                                 o->setProperty ("name", juce::String::fromUTF8 (polisher::genreName (genre)));
                                 o->setProperty ("lufs", polisher::kGenreCurveLufs);
                                 o->setProperty ("sr", sr);
                                 o->setProperty ("displayDb", points);
                                 complete (juce::JSON::toString (juce::var (o), true));
                             })
        // What the page told us to remember (see loudnessHistoryFor): rememberUi({ module, learn, history }).
        .withNativeFunction ("rememberUi", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 if (auto* o = args.size() > 0 ? args[0].getDynamicObject() : nullptr)
                                 {
                                     auto& state = audioProcessor.parameters.state;
                                     const auto module = o->getProperty ("module").toString();
                                     if (module.isNotEmpty() && module.length() <= 16 && module.containsOnly ("abcdefghijklmnopqrstuvwxyz"))
                                         state.setProperty (uiModuleId, module, nullptr);
                                     if (o->hasProperty ("learn"))
                                         state.setProperty (uiLearnId, static_cast<bool> (o->getProperty ("learn")), nullptr);
                                     if (o->hasProperty ("history") && o->getProperty ("history").toString().length() <= 32768)
                                         loudnessHistoryFor (audioProcessor) = o->getProperty ("history").toString();
                                 }
                                 complete (true);
                             })
        .withInitialisationData ("polisherUi", [this]
                                 {
                                     const auto& state = audioProcessor.parameters.state;
                                     auto* o = new juce::DynamicObject();
                                     o->setProperty ("module", state.getProperty (uiModuleId, juce::String()));
                                     o->setProperty ("learn", state.getProperty (uiLearnId, true));
                                     o->setProperty ("history", loudnessHistoryFor (audioProcessor));
                                     return juce::var (o);
                                 }())
        // The page shows the plug-in's version (Settings) from here: the build sets it, so it can't go stale.
        .withInitialisationData ("polisherVersion", juce::String (JucePlugin_VersionString))
        .withResourceProvider ([] (const auto& url) { return getResource (url); });
}

PolisherWebEditor::PolisherWebEditor (PolisherAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      webView (makeEditorOptions()),
      driveAttachment    (param (p, "drive"),     driveRelay,     nullptr),
      ceilingAttachment  (param (p, "ceiling"),   ceilingRelay,   nullptr),
      releaseAttachment  (param (p, "release"),   releaseRelay,   nullptr),
      adaptiveAttachment (param (p, "adaptive"),  adaptiveRelay,  nullptr),
      bypassAttachment   (param (p, "bypass"),    bypassRelay,    nullptr),
      autoResetAttachment (param (p, "autoreset"), autoResetRelay, nullptr),
      clipOnAttachment (param (p, "clipOn"), clipOnRelay, nullptr),
      resetOnChangeAttachment (param (p, "resetOnChange"), resetOnChangeRelay, nullptr),
      levelMatchAttachment (param (p, "levelMatch"), levelMatchRelay, nullptr),
      clipLevelAttachment (param (p, "clipLevel"), clipLevelRelay, nullptr),
      clipSoftnessAttachment (param (p, "clipSoftness"), clipSoftnessRelay, nullptr),
      clipOversamplingAttachment (param (p, "clipOversampling"), clipOversamplingRelay, nullptr),
      eqOnAttachment (param (p, "eqOn"), eqOnRelay, nullptr),
      bassOnAttachment (param (p, "bassOn"), bassOnRelay, nullptr),
      imgOnAttachment (param (p, "imgOn"), imgOnRelay, nullptr),
      imgLowAttachment (param (p, "imgLow"), imgLowRelay, nullptr),
      imgMidAttachment (param (p, "imgMid"), imgMidRelay, nullptr),
      imgHighAttachment (param (p, "imgHigh"), imgHighRelay, nullptr),
      bassMonoAttachment (param (p, "bassMono"), bassMonoRelay, nullptr),
      bassTameAttachment (param (p, "bassTame"), bassTameRelay, nullptr),
      bassPunchAttachment (param (p, "bassPunch"), bassPunchRelay, nullptr),
      amGenreAttachment (param (p, "amGenre"), amGenreRelay, nullptr),
      amIntensityAttachment (param (p, "amIntensity"), amIntensityRelay, nullptr),
      amTargetAttachment (param (p, "amTarget"), amTargetRelay, nullptr)
{
    for (int b = 0; b < polisher::kEqBands; ++b)
    {
        const auto i = static_cast<size_t> (b);
        const auto n = juce::String (b + 1);
        eqAttachments.push_back (std::make_unique<juce::WebSliderParameterAttachment> (param (p, ("eqGain" + n).toRawUTF8()), *eqGainRelays[i], nullptr));
        eqAttachments.push_back (std::make_unique<juce::WebSliderParameterAttachment> (param (p, ("eqFreq" + n).toRawUTF8()), *eqFreqRelays[i], nullptr));
    }

    const auto sliders = ModuleParams::sliderIds(), toggles = ModuleParams::toggleIds(), combos = ModuleParams::comboIds();
    for (int i = 0; i < sliders.size(); ++i)
        moduleSliderAttachments.push_back (std::make_unique<juce::WebSliderParameterAttachment> (
            param (p, sliders[i].toRawUTF8()), *moduleSliderRelays[static_cast<size_t> (i)], nullptr));
    for (int i = 0; i < toggles.size(); ++i)
        moduleToggleAttachments.push_back (std::make_unique<juce::WebToggleButtonParameterAttachment> (
            param (p, toggles[i].toRawUTF8()), *moduleToggleRelays[static_cast<size_t> (i)], nullptr));
    for (int i = 0; i < combos.size(); ++i)
        moduleComboAttachments.push_back (std::make_unique<juce::WebComboBoxParameterAttachment> (
            param (p, combos[i].toRawUTF8()), *moduleComboRelays[static_cast<size_t> (i)], nullptr));

    addAndMakeVisible (webView);
    webView.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    // Fixed 16:9 design, freely resizable; the page scales itself to fit. Not smaller than 70 % of the
    // design: below that the smallest texts (meter labels, Learn) shrink under ~8 px and can't be read.
    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, 2400, 1350);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (static_cast<double> (kDesignWidth) / kDesignHeight);
    // The size it had last time (resized() keeps it), else 1280 x 720.
    const int width = juce::jlimit (kMinWidth, 2400, static_cast<int> (audioProcessor.parameters.state.getProperty (uiWidthId, 1280)));
    setSize (width, juce::roundToInt (width * static_cast<double> (kDesignHeight) / kDesignWidth));

    // Energy meters piled up while the editor was closed: start fresh.
    for (auto& e : audioProcessor.meters.stemEnergy) e.store (0.0f);
    audioProcessor.meters.satResidual.store (0.0f);
    audioProcessor.meters.satSignal.store (0.0f);
    audioProcessor.autoMaster.setEditorOpen (true);   // Auto Analyze listens while the window is open
    startTimerHz (30);
}

PolisherWebEditor::~PolisherWebEditor()
{
    stopTimer();
    audioProcessor.autoMaster.setEditorOpen (false);
    // Level match is a listening aid: switch it off when the window closes so it can't end up in a bounce.
    if (auto* p = audioProcessor.parameters.getParameter ("levelMatch"))
        if (p->getValue() > 0.5f)
            p->setValueNotifyingHost (0.0f);
    // So are Listen / Solo and the MONO / PHONE checks: back to the plain master.
    audioProcessor.resetMonitoring();
    if (pageGroupOpen)
        audioProcessor.history.endGroup();   // an undo group the page opened and never closed
}

void PolisherWebEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff050f22));
}

void PolisherWebEditor::resized()
{
    webView.setBounds (getLocalBounds());
    audioProcessor.parameters.state.setProperty (uiWidthId, getWidth(), nullptr);   // opens at this size next time
}

std::optional<juce::WebBrowserComponent::Resource> PolisherWebEditor::getResource (const juce::String& url)
{
    auto path = url == "/" ? juce::String ("index.html") : url.fromFirstOccurrenceOf ("/", false, false);
    path = path.upToFirstOccurrenceOf ("?", false, false).upToFirstOccurrenceOf ("#", false, false);
    if (path.isEmpty())
        path = "index.html";

    // Resources are embedded flat (file names are unique), so match on the file name.
    const auto fileName = path.fromLastOccurrenceOf ("/", false, false);

    for (int i = 0; i < PolisherUI::namedResourceListSize; ++i)
    {
        if (fileName == PolisherUI::originalFilenames[i])
        {
            int size = 0;
            if (const auto* data = PolisherUI::getNamedResource (PolisherUI::namedResourceList[i], size))
            {
                std::vector<std::byte> bytes (static_cast<size_t> (size));
                std::memcpy (bytes.data(), data, bytes.size());
                return juce::WebBrowserComponent::Resource { std::move (bytes), mimeTypeFor (fileName) };
            }
        }
    }
    return std::nullopt;
}

juce::var PolisherWebEditor::historyResult (const juce::String& done)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("done", done);   // "" = there was nothing to undo / redo
    o->setProperty ("undo", audioProcessor.history.undoLabel());
    o->setProperty ("redo", audioProcessor.history.redoLabel());
    return juce::var (o);
}

void PolisherWebEditor::addHistoryState (juce::DynamicObject& frame)
{
    // Undo / redo: what each would do ("" = nothing). Snapshots: which slots hold settings, which one is
    // playing and whether it was changed since.
    frame.setProperty ("undo", audioProcessor.history.undoLabel());
    frame.setProperty ("redo", audioProcessor.history.redoLabel());
    const auto st = audioProcessor.snapshots.getStatus();
    juce::Array<juce::var> slots;
    for (const auto& slot : st.slots)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("filled", slot.filled);
        o->setProperty ("time", slot.time);
        o->setProperty ("note", slot.note);
        slots.add (juce::var (o));
    }
    frame.setProperty ("snaps", slots);
    frame.setProperty ("snapActive", st.active);
    frame.setProperty ("snapModified", st.modified);
}

juce::var PolisherWebEditor::makeSpectrum (const SpectrumTap& tap, Analyser& state)
{
    std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);
    tap.copyLatest (fftBuffer.data(), kFftSize);
    window.multiplyWithWindowingTable (fftBuffer.data(), static_cast<size_t> (kFftSize));
    fft.performFrequencyOnlyForwardTransform (fftBuffer.data(), true);

    const double sampleRate = juce::jmax (8000.0, audioProcessor.getSampleRate());
    const double binHz = sampleRate / kFftSize;
    const float fullScale = kFftSize * 0.25f;   // Hann-windowed full-scale sine
    constexpr int lastBin = kFftSize / 2 - 1;

    juce::Array<juce::var> out;
    out.ensureStorageAllocated (kSpectrumPoints);

    for (int i = 0; i < kSpectrumPoints; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, static_cast<double> (i) / (kSpectrumPoints - 1));
        const int lo = juce::jlimit (1, lastBin, static_cast<int> (std::floor (f * std::pow (2.0, -1.0 / 12.0) / binHz)));
        const int hi = juce::jlimit (lo, lastBin, static_cast<int> (std::ceil (f * std::pow (2.0, 1.0 / 12.0) / binHz)));

        double power = 0.0;
        for (int b = lo; b <= hi; ++b)
            power += static_cast<double> (fftBuffer[static_cast<size_t> (b)]) * fftBuffer[static_cast<size_t> (b)];
        const double mag = std::sqrt (power / (hi - lo + 1)) / fullScale;

        // 4.5 dB/octave tilt around 1 kHz so a well-balanced mix reads roughly flat.
        float db = static_cast<float> (20.0 * std::log10 (mag + 1.0e-9) + 4.5 * std::log2 (f / 1000.0));
        db = juce::jlimit (-120.0f, 12.0f, db);

        auto& s = state.smoothed[static_cast<size_t> (i)];
        if (! state.primed)       s = db;
        else if (db > s)          s = 0.55f * db + 0.45f * s;   // fast rise
        else                      s = 0.88f * s + 0.12f * db;   // slow fall
        out.add (roundTo (s, 0.1f));
    }
    state.primed = true;
    return out;
}

void PolisherWebEditor::timerCallback()
{
    auto& m = audioProcessor.meters;

    const float gr = m.gainReduction.exchange (0.0f);
    grHold = gr < grHold ? gr : grHold * 0.8f + gr * 0.2f;
    const float clip = m.clipReduction.exchange (0.0f);
    clipHold = clip < clipHold ? clip : clipHold * 0.8f + clip * 0.2f;
    const float bassCut = m.bassFirstCut.exchange (0.0f);
    bassCutHold = bassCut < bassCutHold ? bassCut : bassCutHold * 0.8f + bassCut * 0.2f;

    auto* frame = new juce::DynamicObject();
    frame->setProperty ("bassCut",    roundTo (bassCutHold, 0.1f));
    frame->setProperty ("momentary",  roundTo (m.momentary.load(), 0.1f));
    frame->setProperty ("shortTerm",  roundTo (m.shortTerm.load(), 0.1f));
    frame->setProperty ("integrated", roundTo (m.integrated.load(), 0.1f));
    const float range = m.loudnessRange.load();   // the one reading the processor doesn't clamp
    frame->setProperty ("range",      roundTo (std::isfinite (range) ? juce::jlimit (0.0f, 99.0f, range) : 0.0f, 0.1f));
    frame->setProperty ("truePeak",   roundTo (m.truePeakMax.load(), 0.1f));
    frame->setProperty ("peakRecent", roundTo (m.truePeakRecent.exchange (-100.0f), 0.1f));
    const float peakL = m.peakLeft.exchange (-100.0f), peakR = m.peakRight.exchange (-100.0f);
    frame->setProperty ("peakL",      roundTo (peakL, 0.1f));
    frame->setProperty ("peakR",      roundTo (peakR, 0.1f));
    const auto nowMs = juce::Time::getMillisecondCounter();
    if (peakL > -100.0f || peakR > -100.0f)
        lastSoundMs = nowMs;   // the processor ran and something came out since the last frame
    frame->setProperty ("gr",         roundTo (grHold, 0.1f));
    frame->setProperty ("clip",       roundTo (clipHold, 0.1f));
    const float tame = m.bassTame.exchange (0.0f);
    tameHold = tame < tameHold ? tame : tameHold * 0.8f + tame * 0.2f;
    const float punch = m.bassPunch.exchange (0.0f);
    punchHold = punch > punchHold ? punch : punchHold * 0.8f + punch * 0.2f;
    frame->setProperty ("tame",       roundTo (tameHold, 0.1f));
    frame->setProperty ("punch",      roundTo (punchHold, 0.1f));
    frame->setProperty ("bassWidth",  juce::roundToInt (m.bassWidth.load()));
    auto& am = audioProcessor.autoMaster;
    frame->setProperty ("amState",    static_cast<int> (am.getState()));
    frame->setProperty ("amProgress", roundTo (am.getProgress(), 0.01f));
    frame->setProperty ("amHearing",  am.isHearingAudio());
    addHistoryState (*frame);
    frame->setProperty ("amReport",   am.getReportVersion());
    // Whole-song listen: music heard, the loudest part so far (where, how loud), and whether Finish works.
    frame->setProperty ("amMode",     static_cast<int> (am.getMode()));
    frame->setProperty ("amHeard",    roundTo (static_cast<float> (am.getHeardSeconds()), 0.1f));
    frame->setProperty ("amLoudAt",   roundTo (static_cast<float> (am.getLoudestStartSeconds()), 0.1f));
    frame->setProperty ("amLoudLufs", roundTo (static_cast<float> (am.getLoudestLufs()), 0.1f));
    frame->setProperty ("amLoudEnd",  roundTo (static_cast<float> (am.getLoudestEndSeconds()), 0.1f));   // -1: unknown or jumped inside
    frame->setProperty ("amLoudJump", am.loudestPartJumped());
    frame->setProperty ("amCanFinish", am.canFinish());
    frame->setProperty ("amByItself", am.finishedByItself());
    // Auto Analyze: on, why it waits ("" = checked), music heard in this stretch, edits on offer, and a
    // version that changes whenever its advice or status does (the page then fetches getAutoAnalyze).
    frame->setProperty ("aaOn",       am.getAutoAnalyze());
    frame->setProperty ("aaPause",    am.getAutoAnalyzeStatus());
    frame->setProperty ("aaHeard",    roundTo (static_cast<float> (am.getAutoAnalyzeHeard()), 0.1f));
    frame->setProperty ("aaEdits",    am.getAutoEditCount());
    frame->setProperty ("aaVersion",  am.getAutoAnalyzeVersion());
    // Tone Steady's stored target: how much of the song it was learned from (0 = following the music).
    {
        const auto tone = audioProcessor.toneTarget.get();
        frame->setProperty ("tsTarget", tone.valid ? roundTo (static_cast<float> (juce::jmax (1.0, tone.seconds)), 0.1f) : 0.0f);
        frame->setProperty ("tsLearnFail", am.getToneLearnFailures());
        // ... and whether the music playing sits far from it for long (another song's tone?).
        frame->setProperty ("tsFar",      tone.valid && m.tsFar.load());
        frame->setProperty ("tsDist",     tone.valid ? roundTo (m.tsDistance.load(), 0.1f) : -1.0f);
    }
    audioProcessor.pollReference();   // a reference load that just ended marks the project changed again
    frame->setProperty ("refVersion", audioProcessor.reference.getVersion());
    frame->setProperty ("refState",   static_cast<int> (audioProcessor.reference.getState()));
    frame->setProperty ("corr",       roundTo (m.correlation.load(), 0.01f));
    {
        const float g = m.glueGr.exchange (0.0f);
        glueHold = g < glueHold ? g : glueHold * 0.8f + g * 0.2f;
        frame->setProperty ("glueGr", roundTo (glueHold, 0.1f));
    }
    juce::Array<juce::var> dynGr;
    for (size_t b = 0; b < dynHold.size(); ++b)
    {
        const float d = m.dynGr[b].exchange (0.0f);
        dynHold[b] = d < dynHold[b] ? d : dynHold[b] * 0.8f + d * 0.2f;
        dynGr.add (roundTo (dynHold[b], 0.1f));
    }
    frame->setProperty ("dynGr",      dynGr);
    juce::Array<juce::var> dynImpact;
    for (size_t b = 0; b < impactHold.size(); ++b)
    {
        const float d = m.dynImpact[b].exchange (0.0f);
        impactHold[b] = std::abs (d) > std::abs (impactHold[b]) ? d : impactHold[b] * 0.8f + d * 0.2f;
        dynImpact.add (roundTo (impactHold[b], 0.1f));
    }
    frame->setProperty ("dynImpact",  dynImpact);
    juce::Array<juce::var> dynSustain;
    for (size_t b = 0; b < sustainHold.size(); ++b)
    {
        const float d = m.dynSustain[b].exchange (0.0f);
        sustainHold[b] = std::abs (d) > std::abs (sustainHold[b]) ? d : sustainHold[b] * 0.8f + d * 0.2f;
        dynSustain.add (roundTo (sustainHold[b], 0.1f));
    }
    frame->setProperty ("dynSustain", dynSustain);
    // Average energies over ~0.3 s (never dB values, or the quiet gaps between hits drag it down).
    satResHold = satResHold * 0.9 + m.satResidual.exchange (0.0f);
    satSigHold = satSigHold * 0.9 + m.satSignal.exchange (0.0f);
    const double ratio = satSigHold > 1.0e-12 ? satResHold / satSigHold : 0.0;
    frame->setProperty ("satHarm",    roundTo (ratio > 1.0e-10 ? static_cast<float> (10.0 * std::log10 (ratio)) : -100.0f, 0.1f));
    // Resonance Tamer / Tone Steady: hold the strongest move for a moment so it can be read.
    const float rtNow = m.rtCut.exchange (0.0f), rtHzNow = m.rtHz.exchange (0.0f);
    rtHold = rtNow < rtHold ? rtNow : rtHold * 0.85f + rtNow * 0.15f;
    if (rtNow < -0.3f) rtHzHold = rtHzNow;
    const float tsNow = m.tsDb.exchange (0.0f), tsHzNow = m.tsHz.exchange (0.0f);
    tsHold = std::abs (tsNow) > std::abs (tsHold) ? tsNow : tsHold * 0.85f + tsNow * 0.15f;
    if (std::abs (tsNow) > 0.3f) tsHzHold = tsHzNow;
    const float ulNow = m.ulBoost.exchange (0.0f);
    ulHold = ulNow > ulHold ? ulNow : ulHold * 0.85f + ulNow * 0.15f;
    frame->setProperty ("ulBoost", roundTo (ulHold, 0.1f));
    frame->setProperty ("ulSquash", roundTo (m.ulSquash.load(), 0.1f));
    frame->setProperty ("rtCut", roundTo (rtHold, 0.1f));
    frame->setProperty ("rtHz",  juce::roundToInt (rtHzHold));
    frame->setProperty ("tsDb",  roundTo (tsHold, 0.1f));
    frame->setProperty ("tsHz",  juce::roundToInt (tsHzHold));
    // Stem EQ: share of the loudness of the mix coming in (K-weighted energy, like a LUFS meter) that
    // the model credits to vocals / drums / bass / other (%, ~1 s average): what a move of each does.
    double stemTotal = 0.0;
    for (size_t k = 0; k < stemHold.size(); ++k)
    {
        stemHold[k] = stemHold[k] * 0.97 + m.stemEnergy[k].exchange (0.0f);
        stemTotal += stemHold[k];
    }
    juce::Array<juce::var> stemShare;
    for (double e : stemHold)
        stemShare.add (juce::roundToInt (stemTotal > 1.0e-6 ? 100.0 * e / stemTotal : 0.0));
    frame->setProperty ("stemShare",  stemShare);
    frame->setProperty ("stemKeep",   roundTo (m.stemKeep.load(), 0.1f));
    frame->setProperty ("matchDb",    roundTo (m.matchDb.load(), 0.1f));
    // Monitoring: what was asked for (the banner shows while anything is on) and what is heard now.
    frame->setProperty ("listen",     audioProcessor.getListenMode());
    frame->setProperty ("mono",       audioProcessor.getMonoCheck());
    frame->setProperty ("phone",      audioProcessor.getPhoneCheck());
    frame->setProperty ("listenNow",  m.listenPlaying.load());
    frame->setProperty ("listenGain", roundTo (m.listenGainDb.load(), 0.5f));
    frame->setProperty ("listenUnder", roundTo (m.listenUnderDb.load(), 1.0f));
    frame->setProperty ("sr",         audioProcessor.getSampleRate());
    frame->setProperty ("inShort",    roundTo (m.inputShortTerm.load(), 0.1f));
    frame->setProperty ("inIntegrated", roundTo (m.inputIntegrated.load(), 0.1f));
    // The loudness history records only while the song plays: the host's transport (or, without one,
    // e.g. the standalone app, while sound comes in), and only while the processor is still being called
    // (a host that stops calling it would otherwise leave the meters frozen at their last reading).
    const int transport = m.transport.load();
    const bool running = nowMs - lastSoundMs < 1000;
    frame->setProperty ("playing",    running && (transport == 1 || (transport < 0 && m.momentary.load() > -70.0f)));
    {
        // The song's style, shared with Voxology and Mixology ("" = none chosen yet).
        const auto st = audioProcessor.songStyleNow();
        auto* so = new juce::DynamicObject();
        so->setProperty ("key", juce::String::fromUTF8 (st.key.c_str()));
        so->setProperty ("name", juce::String::fromUTF8 (st.name.c_str()));
        so->setProperty ("from", static_cast<int> (st.from));   // 1 Voxology, 2 Mixology, 3 Masterology
        so->setProperty ("version", static_cast<int> (audioProcessor.songStyleVersion()));
        frame->setProperty ("songStyle", juce::var (so));
        const auto d = audioProcessor.dawNow();
        auto* dw = new juce::DynamicObject();
        dw->setProperty ("key", juce::String::fromUTF8 (d.key.c_str()));
        dw->setProperty ("picked", d.pickedAt != 0);   // false: found from the host
        dw->setProperty ("from", static_cast<int> (d.from));
        dw->setProperty ("detected", juce::String::fromUTF8 (audioProcessor.dawDetected().c_str()));
        frame->setProperty ("daw", juce::var (dw));
    }
    // The suite link: Mixology's hand-off for this song (null without a Mixology in the project).
    if (suite::MixInfo h; audioProcessor.mixologyHandoff (h))
    {
        auto* mo = new juce::DynamicObject();
        mo->setProperty ("style", juce::String::fromUTF8 (h.styleName.c_str()));
        mo->setProperty ("genre", polisher::handoff::genreForMixStyle (h.styleKey));
        mo->setProperty ("peak", roundTo (h.peakDbtp, 0.1f));
        mo->setProperty ("lufs", roundTo (h.integratedLufs, 0.1f));
        mo->setProperty ("analysed", (h.flags & suite::MixInfo::analysed) != 0);
        mo->setProperty ("mono", (h.flags & suite::MixInfo::monoMix) != 0);
        mo->setProperty ("limited", (h.flags & suite::MixInfo::limited) != 0);
        juce::Array<juce::var> tone;   // low, low mids, presence, air against the style (dB)
        for (float d : { h.lowDb, h.lowMidDb, h.presenceDb, h.airDb }) tone.add (roundTo (d, 0.1f));
        mo->setProperty ("tone", tone);
        frame->setProperty ("fromMixology", juce::var (mo));
    }
    else
        frame->setProperty ("fromMixology", juce::var());
    frame->setProperty ("mix",        makeSpectrum (audioProcessor.inputTap, mixAnalyser));
    frame->setProperty ("master",     makeSpectrum (audioProcessor.outputTap, masterAnalyser));

    webView.emitEventIfBrowserIsVisible ("polisherMeters", juce::var (frame));
}

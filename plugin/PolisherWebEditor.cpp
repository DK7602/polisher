#include "PolisherWebEditor.h"

#include "PolisherUIData.h"

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

    float roundTo (float v, float step) { return std::round (v / step) * step; }
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
        .withNativeFunction ("startAutoMaster", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 complete (audioProcessor.autoMaster.start());
                             })
        .withNativeFunction ("cancelAutoMaster", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 audioProcessor.autoMaster.cancel();
                                 complete (true);
                             })
        .withNativeFunction ("undoAutoMaster", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 complete (audioProcessor.autoMaster.undo());
                             })
        .withNativeFunction ("getAutoMasterReport", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 complete (audioProcessor.autoMaster.getReportJson());
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
                                                                        safe->audioProcessor.reference.load (file);
                                                                });
                                 complete (true);
                             })
        .withNativeFunction ("clearReference", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 audioProcessor.reference.clear();
                                 complete (true);
                             })
        .withNativeFunction ("getReference", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 complete (audioProcessor.reference.getInfoJson());
                             })
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

    // Fixed 16:9 design, freely resizable; the page scales itself to fit.
    setResizable (true, true);
    setResizeLimits (800, 450, 2400, 1350);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (static_cast<double> (kDesignWidth) / kDesignHeight);
    setSize (1280, 720);

    // Energy meters piled up while the editor was closed: start fresh.
    for (auto& e : audioProcessor.meters.stemEnergy) e.store (0.0f);
    audioProcessor.meters.satResidual.store (0.0f);
    audioProcessor.meters.satSignal.store (0.0f);
    startTimerHz (30);
}

PolisherWebEditor::~PolisherWebEditor()
{
    stopTimer();
    // Level match is a listening aid: switch it off when the window closes so it can't end up in a bounce.
    if (auto* p = audioProcessor.parameters.getParameter ("levelMatch"))
        if (p->getValue() > 0.5f)
            p->setValueNotifyingHost (0.0f);
}

void PolisherWebEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff050f22));
}

void PolisherWebEditor::resized()
{
    webView.setBounds (getLocalBounds());
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

    auto* frame = new juce::DynamicObject();
    frame->setProperty ("momentary",  roundTo (m.momentary.load(), 0.1f));
    frame->setProperty ("shortTerm",  roundTo (m.shortTerm.load(), 0.1f));
    frame->setProperty ("integrated", roundTo (m.integrated.load(), 0.1f));
    frame->setProperty ("range",      roundTo (m.loudnessRange.load(), 0.1f));
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
    frame->setProperty ("amUndo",     am.canUndo());
    frame->setProperty ("amReport",   am.getReportVersion());
    frame->setProperty ("refVersion", audioProcessor.reference.getVersion());
    frame->setProperty ("refState",   static_cast<int> (audioProcessor.reference.getState()));
    frame->setProperty ("corr",       roundTo (m.correlation.load(), 0.01f));
    juce::Array<juce::var> dynGr;
    for (size_t b = 0; b < dynHold.size(); ++b)
    {
        const float d = m.dynGr[b].exchange (0.0f);
        dynHold[b] = d < dynHold[b] ? d : dynHold[b] * 0.8f + d * 0.2f;
        dynGr.add (roundTo (dynHold[b], 0.1f));
    }
    frame->setProperty ("dynGr",      dynGr);
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
    // Stem EQ: share of what you hear (A-weighted energy) that the model credits to vocals / drums /
    // bass / other (%, ~1 s average). Raw energy would be mostly sub: an 808 would read 70-90 %.
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
    frame->setProperty ("sr",         audioProcessor.getSampleRate());
    frame->setProperty ("inShort",    roundTo (m.inputShortTerm.load(), 0.1f));
    frame->setProperty ("inIntegrated", roundTo (m.inputIntegrated.load(), 0.1f));
    // The loudness history records only while the song plays: the host's transport (or, without one,
    // e.g. the standalone app, while sound comes in), and only while the processor is still being called
    // (a host that stops calling it would otherwise leave the meters frozen at their last reading).
    const int transport = m.transport.load();
    const bool running = nowMs - lastSoundMs < 1000;
    frame->setProperty ("playing",    running && (transport == 1 || (transport < 0 && m.momentary.load() > -70.0f)));
    frame->setProperty ("mix",        makeSpectrum (audioProcessor.inputTap, mixAnalyser));
    frame->setProperty ("master",     makeSpectrum (audioProcessor.outputTap, masterAnalyser));

    webView.emitEventIfBrowserIsVisible ("polisherMeters", juce::var (frame));
}

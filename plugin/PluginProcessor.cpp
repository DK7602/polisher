#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PolisherWebEditor.h"

namespace ParamIDs
{
    inline constexpr const char* drive    = "drive";
    inline constexpr const char* ceiling  = "ceiling";
    inline constexpr const char* release  = "release";
    inline constexpr const char* adaptive = "adaptive";
    inline constexpr const char* bypass   = "bypass";
    inline constexpr const char* autoReset = "autoreset";
    inline constexpr const char* resetOnChange = "resetOnChange";
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
}

namespace
{
    const char* const kEqBandNames[polisher::kEqBands] = { "Low End", "Bass", "Low Mid", "Mid", "Upper Mid", "Presence", "Air" };

    juce::String hzText (float v)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
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
    bypassParam   = parameters.getRawParameterValue (ParamIDs::bypass);
    autoResetParam = parameters.getRawParameterValue (ParamIDs::autoReset);
    clipOnParam   = parameters.getRawParameterValue (ParamIDs::clipOn);
    clipLevelParam = parameters.getRawParameterValue (ParamIDs::clipLevel);
    clipSoftnessParam  = parameters.getRawParameterValue (ParamIDs::clipSoftness);
    clipOversamplingParam = parameters.getRawParameterValue (ParamIDs::clipOversampling);
    eqOnParam = parameters.getRawParameterValue (ParamIDs::eqOn);
    bassOnParam    = parameters.getRawParameterValue (ParamIDs::bassOn);
    bassMonoParam  = parameters.getRawParameterValue (ParamIDs::bassMono);
    bassTameParam  = parameters.getRawParameterValue (ParamIDs::bassTame);
    bassPunchParam = parameters.getRawParameterValue (ParamIDs::bassPunch);
    for (int b = 0; b < polisher::kEqBands; ++b)
    {
        eqGainParams[static_cast<size_t> (b)] = parameters.getRawParameterValue (ParamIDs::eqGain (b));
        eqFreqParams[static_cast<size_t> (b)] = parameters.getRawParameterValue (ParamIDs::eqFreq (b));
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
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::bypass, 1 }, "Bypass", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::autoReset, 1 }, "Reset Meter On Play", true,
                                                      AudioParameterBoolAttributes().withAutomatable (false)));
    // Read by the editor: restart the loudness measurement shortly after you change a control, so
    // Integrated describes the current settings instead of averaging in the old ones.
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

    // Tonal EQ (first in the chain). On by default: flat bands are skipped, so it costs nothing.
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

    return layout;
}

bool PolisherAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void PolisherAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    const int channels = juce::jmax (1, getTotalNumOutputChannels());
    limiter.prepare (sampleRate, channels);
    clipper.prepare (channels);
    eq.prepare (sampleRate, channels);
    bass.prepare (sampleRate, channels);
    outputMeter.prepare (sampleRate, channels);
    inputMeter.prepare (sampleRate, channels);
    updateLimiterParams();
    currentDriveGain = targetDriveGain();
    // Constant latency whatever the settings: clipper (all oversampling modes) + limiter look-ahead.
    setLatencySamples (polisher::SoftClipper::latencySamples() + limiter.latencySamples());
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

    const auto& cur = limiter.getParams();
    if (p.driveDb != cur.driveDb || p.ceilingDbtp != cur.ceilingDbtp || p.releaseMs != cur.releaseMs
        || p.adaptiveRelease != cur.adaptiveRelease || p.bypassed != cur.bypassed)
        limiter.setParams (p);

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
    }
    const auto& ce = eq.getParams();
    if (e.enabled != ce.enabled || e.gainDb != ce.gainDb || e.freqHz != ce.freqHz)
        eq.setParams (e);

    polisher::BassParams bp;
    bp.enabled = bassOnParam->load() > 0.5f && ! p.bypassed;
    bp.monoHz  = bassMonoParam->load();
    bp.tame    = bassTameParam->load() / 100.0;
    bp.punch   = bassPunchParam->load() / 100.0;
    const auto& cb = bass.getParams();
    if (bp.enabled != cb.enabled || bp.monoHz != cb.monoHz || bp.tame != cb.tame || bp.punch != cb.punch)
        bass.setParams (bp);

    const auto& cc = clipper.getParams();
    if (c.enabled != cc.enabled || c.thresholdDb != cc.thresholdDb || c.softness != cc.softness || c.makeupDb != cc.makeupDb
        || c.oversampling != cc.oversampling)
        clipper.setParams (c);
}

double PolisherAudioProcessor::targetDriveGain() const
{
    return bypassParam->load() > 0.5f ? 1.0 : juce::Decibels::decibelsToGain (static_cast<double> (driveParam->load()));
}

template <typename Sample>
void PolisherAudioProcessor::processAnyPrecision (juce::AudioBuffer<Sample>& buffer)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int c = numIn; c < numOut; ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    // Fresh loudness measurement each time the host transport starts (optional).
    bool isPlaying = wasPlaying;
    if (auto* hostPlayHead = getPlayHead())
        if (auto position = hostPlayHead->getPosition())
            isPlaying = position->getIsPlaying();
    const bool justStarted = isPlaying && ! wasPlaying;
    wasPlaying = isPlaying;

    if (meterResetRequested.exchange (false) || (justStarted && autoResetParam->load() > 0.5f))
    {
        outputMeter.reset();
        inputMeter.reset();
    }
    if (statsResetRequested.exchange (false))
    {
        outputMeter.resetStatistics();
        inputMeter.resetStatistics();
    }

    updateLimiterParams();

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    inputTap.push (buffer.getArrayOfReadPointers(), numChannels, numSamples);
    inputMeter.process (buffer.getArrayOfReadPointers(), numChannels, numSamples);

    eq.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);
    bass.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);

    // Drive (ramped per block so knob moves don't click) -> Soft Clipper -> limiter.
    const double newGain = targetDriveGain();
    buffer.applyGainRamp (0, numSamples, static_cast<Sample> (currentDriveGain), static_cast<Sample> (newGain));
    currentDriveGain = newGain;

    clipper.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);
    limiter.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);
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
}

void PolisherAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processAnyPrecision (buffer);
}

void PolisherAudioProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processAnyPrecision (buffer);
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

    const float clip = static_cast<float> (clipper.takeMaxClipDb());
    if (clip < meters.clipReduction.load())
        meters.clipReduction.store (clip);
    const float gr = static_cast<float> (limiter.takeMaxGainReductionDb());
    if (gr < meters.gainReduction.load())
        meters.gainReduction.store (gr);
}

void PolisherAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PolisherAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* PolisherAudioProcessor::createEditor()
{
    // The liquid-glass web UI; falls back to the basic native editor if no web view is available
    // (for example a Windows machine without the WebView2 runtime).
    if (PolisherWebEditor::isSupported())
        return new PolisherWebEditor (*this);
    return new PolisherAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PolisherAudioProcessor();
}

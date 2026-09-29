#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace ParamIDs
{
    inline constexpr const char* drive    = "drive";
    inline constexpr const char* ceiling  = "ceiling";
    inline constexpr const char* release  = "release";
    inline constexpr const char* adaptive = "adaptive";
    inline constexpr const char* bypass   = "bypass";
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
    outputMeter.prepare (sampleRate, channels);
    updateLimiterParams();
    setLatencySamples (limiter.latencySamples());
}

void PolisherAudioProcessor::updateLimiterParams()
{
    polisher::LimiterParams p;
    p.driveDb         = driveParam->load();
    p.ceilingDbtp     = ceilingParam->load();
    p.releaseMs       = releaseParam->load();
    p.adaptiveRelease = adaptiveParam->load() > 0.5f;
    p.bypassed        = bypassParam->load() > 0.5f;

    const auto& cur = limiter.getParams();
    if (p.driveDb != cur.driveDb || p.ceilingDbtp != cur.ceilingDbtp || p.releaseMs != cur.releaseMs
        || p.adaptiveRelease != cur.adaptiveRelease || p.bypassed != cur.bypassed)
        limiter.setParams (p);
}

template <typename Sample>
void PolisherAudioProcessor::processAnyPrecision (juce::AudioBuffer<Sample>& buffer)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int c = numIn; c < numOut; ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    if (meterResetRequested.exchange (false))
        outputMeter.reset();

    updateLimiterParams();

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    limiter.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);
    outputMeter.process (buffer.getArrayOfReadPointers(), numChannels, numSamples);

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

    // Peak-hold style values: keep the largest until the editor reads and clears them.
    const float recent = clampDb (outputMeter.takeRecentTruePeakDbtp());
    if (recent > meters.truePeakRecent.load())
        meters.truePeakRecent.store (recent);
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
    return new PolisherAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PolisherAudioProcessor();
}

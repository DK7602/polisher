#include "PluginEditor.h"

namespace Colours
{
    const juce::Colour navy      { 0xff050f22 };
    const juce::Colour panel     { 0xff0f2c54 };
    const juce::Colour powder    { 0xff9ccbf0 };
    const juce::Colour red       { 0xffe0141e };
    const juce::Colour pearl     { 0xfff4faff };
    const juce::Colour muted     { 0xffb9d4ec };
}

namespace
{
    // Microsoft's download page for the Evergreen WebView2 Runtime (free, a few MB).
    const char* const webView2Url = "https://developer.microsoft.com/microsoft-edge/webview2/";

   #if JUCE_WINDOWS
    const char* const noticeText = "BASIC VIEW: Polisher's full screen needs the Microsoft Edge WebView2 Runtime. Click GET WEBVIEW2, "
                                   "install it (free), then reopen Polisher. All 11 modules keep working meanwhile; Auto-Master and "
                                   "Learn need the full screen.";
   #else
    const char* const noticeText = "BASIC VIEW: Polisher's full screen couldn't start because this system has no web view. "
                                   "All 11 modules keep working with their saved settings; Auto-Master and Learn need the full screen.";
   #endif

    juce::String formatLufs (float v, const char* unit)
    {
        return v <= -99.0f ? juce::String ("-inf ") + unit : juce::String (v, 1) + " " + unit;
    }
}

PolisherAudioProcessorEditor::PolisherAudioProcessorEditor (PolisherAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    lookAndFeel.setColourScheme ({ Colours::navy, Colours::panel, Colours::panel, Colours::powder.withAlpha (0.4f),
                                   Colours::pearl, Colours::red, Colours::pearl, Colours::red, Colours::pearl });
    lookAndFeel.setColour (juce::Slider::rotarySliderFillColourId, Colours::red);
    lookAndFeel.setColour (juce::Slider::rotarySliderOutlineColourId, Colours::powder.withAlpha (0.35f));
    lookAndFeel.setColour (juce::Slider::thumbColourId, Colours::pearl);
    setLookAndFeel (&lookAndFeel);

    auto setupKnob = [this] (juce::Slider& s, juce::Label& l, const juce::String& name)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 22);
        addAndMakeVisible (s);
        l.setText (name, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, Colours::muted);
        addAndMakeVisible (l);
    };
    setupKnob (driveKnob, driveLabel, "DRIVE");
    setupKnob (ceilingKnob, ceilingLabel, "CEILING");
    setupKnob (releaseKnob, releaseLabel, "RELEASE");

    addAndMakeVisible (adaptiveButton);
    addAndMakeVisible (bypassButton);
    addAndMakeVisible (autoResetButton);
    addAndMakeVisible (resetButton);
    resetButton.onClick = [this] { audioProcessor.requestMeterReset(); };
   #if JUCE_WINDOWS
    addAndMakeVisible (getWebViewButton);
    getWebViewButton.onClick = [] { juce::URL (webView2Url).launchInDefaultBrowser(); };
   #endif

    auto& apvts = audioProcessor.parameters;
    driveAttachment    = std::make_unique<SliderAttachment> (apvts, "drive", driveKnob);
    ceilingAttachment  = std::make_unique<SliderAttachment> (apvts, "ceiling", ceilingKnob);
    releaseAttachment  = std::make_unique<SliderAttachment> (apvts, "release", releaseKnob);
    adaptiveAttachment = std::make_unique<ButtonAttachment> (apvts, "adaptive", adaptiveButton);
    bypassAttachment   = std::make_unique<ButtonAttachment> (apvts, "bypass", bypassButton);
    autoResetAttachment = std::make_unique<ButtonAttachment> (apvts, "autoreset", autoResetButton);

    const char* labels[] = { "INTEGRATED", "SHORT-TERM", "MOMENTARY", "TRUE PEAK", "RANGE", "GAIN REDUCTION" };
    for (size_t i = 0; i < readouts.size(); ++i)
        readouts[i].label = labels[i];

    setSize (720, 460);
    startTimerHz (15);
}

PolisherAudioProcessorEditor::~PolisherAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PolisherAudioProcessorEditor::timerCallback()
{
    auto& m = audioProcessor.meters;

    // Gain reduction and peaks decay smoothly on screen, hold fresh maxima immediately.
    const float gr = m.gainReduction.exchange (0.0f);
    grDisplay = gr < grDisplay ? gr : grDisplay * 0.8f + gr * 0.2f;
    const float pk = m.truePeakRecent.exchange (-100.0f);
    peakDisplay = pk > peakDisplay ? pk : juce::jmax (-100.0f, peakDisplay - 1.5f);

    readouts[0].value = formatLufs (m.integrated.load(), "LUFS");
    readouts[1].value = formatLufs (m.shortTerm.load(), "LUFS");
    readouts[2].value = formatLufs (m.momentary.load(), "LUFS");
    readouts[3].value = formatLufs (m.truePeakMax.load(), "dBTP");
    readouts[4].value = juce::String (m.loudnessRange.load(), 1) + " LU";
    readouts[5].value = grDisplay > -0.05f ? juce::String ("0.0 dB") : juce::String (grDisplay, 1) + " dB";
    repaint();
}

void PolisherAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::navy);

    auto header = getLocalBounds().removeFromTop (56).reduced (16, 8);
    g.setColour (Colours::pearl);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("POLISHER", header, juce::Justification::centredLeft);
    g.setColour (Colours::muted);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Basic view  |  Maximizer + loudness meter  |  v" JucePlugin_VersionString, header, juce::Justification::centredRight);

    g.setColour (Colours::red.withAlpha (0.18f));
    g.fillRoundedRectangle (noticeArea.toFloat(), 8.0f);
    g.setColour (Colours::red);
    g.drawRoundedRectangle (noticeArea.toFloat().reduced (0.5f), 8.0f, 1.0f);
    auto noticeTextArea = noticeArea.reduced (12, 6);
    if (getWebViewButton.isVisible())
        noticeTextArea.removeFromRight (getWebViewButton.getWidth() + 8);
    g.setColour (Colours::pearl);
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText (noticeText, noticeTextArea, juce::Justification::centredLeft, 3);

    for (const auto& r : readouts)
    {
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (r.bounds.toFloat(), 10.0f);
        g.setColour (Colours::powder.withAlpha (0.5f));
        g.drawRoundedRectangle (r.bounds.toFloat().reduced (0.5f), 10.0f, 1.0f);

        auto b = r.bounds.reduced (12, 8);
        g.setColour (Colours::muted);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (r.label, b.removeFromTop (16), juce::Justification::centredLeft);
        g.setColour (Colours::pearl);
        g.setFont (juce::FontOptions (22.0f));
        g.drawText (r.value, b, juce::Justification::centredLeft);
    }

    // Gain-reduction bar (0 .. -12 dB) and true-peak bar (-24 .. 0 dBTP).
    auto bars = getLocalBounds().removeFromBottom (44).reduced (16, 10);
    auto grBar = bars.removeFromLeft (bars.getWidth() / 2 - 8);
    bars.removeFromLeft (16);
    auto pkBar = bars;

    auto drawBar = [&g] (juce::Rectangle<int> area, float fraction, juce::Colour c, const juce::String& text)
    {
        g.setColour (juce::Colour (0xff030a16));
        g.fillRoundedRectangle (area.toFloat(), 6.0f);
        auto fill = area.toFloat().withWidth (area.getWidth() * juce::jlimit (0.0f, 1.0f, fraction));
        g.setColour (c);
        g.fillRoundedRectangle (fill, 6.0f);
        g.setColour (Colours::pearl);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (text, area.reduced (8, 0), juce::Justification::centredLeft);
    };
    drawBar (grBar, -grDisplay / 12.0f, Colours::red, "GR");
    drawBar (pkBar, (peakDisplay + 24.0f) / 24.0f, Colours::powder, "PEAK");
}

void PolisherAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (56);
    noticeArea = area.removeFromTop (60).reduced (16, 4);
    getWebViewButton.setBounds (noticeArea.reduced (10, 12).removeFromRight (120));
    area.removeFromBottom (44);
    area.reduce (16, 8);

    auto knobs = area.removeFromLeft (360);
    auto toggles = knobs.removeFromBottom (64);
    auto toggleRow2 = toggles.removeFromBottom (32);

    const int knobW = knobs.getWidth() / 3;
    auto place = [&] (juce::Slider& s, juce::Label& l)
    {
        auto col = knobs.removeFromLeft (knobW).reduced (6, 0);
        l.setBounds (col.removeFromTop (20));
        s.setBounds (col.reduced (0, 4));
    };
    place (driveKnob, driveLabel);
    place (ceilingKnob, ceilingLabel);
    place (releaseKnob, releaseLabel);

    adaptiveButton.setBounds (toggles.removeFromLeft (150));
    bypassButton.setBounds (toggles.removeFromLeft (90));
    autoResetButton.setBounds (toggleRow2.removeFromLeft (150));
    resetButton.setBounds (toggleRow2.removeFromLeft (110).reduced (0, 3));

    area.removeFromLeft (16);
    const int cellH = area.getHeight() / 3;
    const int cellW = area.getWidth() / 2;
    for (size_t i = 0; i < readouts.size(); ++i)
    {
        const int row = static_cast<int> (i) / 2;
        const int col = static_cast<int> (i) % 2;
        readouts[i].bounds = juce::Rectangle<int> (area.getX() + col * cellW, area.getY() + row * cellH, cellW, cellH).reduced (5);
    }
}

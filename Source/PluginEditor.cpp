#include "PluginEditor.h"

namespace
{
    const juce::Colour bg     { 0xff1d2a3a }; // bleu nuit, clin d'oeil au boîtier d'origine
    const juce::Colour panel  { 0xff253548 };
    const juce::Colour accent { 0xffe8eef5 };
    const juce::Colour ledOn  { 0xffff3b30 };

    juce::String noteName (float hz)
    {
        if (hz <= 0.0f) return "-";
        static const char* names[] = { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
        const int midi = (int) std::round (69.0 + 12.0 * std::log2 (hz / 440.0));
        return juce::String (names[(midi % 12 + 12) % 12]) + juce::String (midi / 12 - 1);
    }
}

CloneOC3Editor::CloneOC3Editor (CloneOC3Processor& p) : AudioProcessorEditor (&p), processor (p)
{
    lnf.setColour (juce::Slider::rotarySliderFillColourId, accent);
    lnf.setColour (juce::Slider::rotarySliderOutlineColourId, panel.brighter (0.15f));
    lnf.setColour (juce::Slider::thumbColourId, accent);
    lnf.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setLookAndFeel (&lnf);

    setupKnob (range, "range", "RANGE");
    setupKnob (direct, "direct", "DIRECT");
    setupKnob (octave, "octave", "OCTAVE");
    setupKnob (compact, "compact", "COMPACT");

    // Bypass : coupe l'octave et grise le bouton OCTAVE
    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonColourId, panel.brighter (0.15f));
    bypassButton.setColour (juce::TextButton::buttonOnColourId, ledOn);
    bypassButton.setColour (juce::TextButton::textColourOffId, accent);
    bypassButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    bypassButton.onStateChange = [this] { updateBypassState(); };
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "bypass", bypassButton);
    updateBypassState();

    setSize (520, 300);
    startTimerHz (20);
}

CloneOC3Editor::~CloneOC3Editor() { setLookAndFeel (nullptr); }

void CloneOC3Editor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& title)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 20);
    k.slider.setColour (juce::Slider::textBoxTextColourId, accent);
    addAndMakeVisible (k.slider);
    k.label.setText (title, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, accent);
    k.label.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramId, k.slider);
}

void CloneOC3Editor::updateBypassState()
{
    const bool on = bypassButton.getToggleState();
    octave.slider.setEnabled (! on);
    octave.label.setEnabled (! on);
    octave.slider.setAlpha (on ? 0.35f : 1.0f);
    octave.label.setAlpha (on ? 0.35f : 1.0f);
}

void CloneOC3Editor::timerCallback()
{
    const auto hz = processor.detectedHz.load();
    const auto newText = noteName (hz);
    const auto newLed = processor.octaveActive.load();
    if (newText != noteText || std::abs (newLed - led) > 0.05f)
    {
        noteText = newText;
        led = newLed;
        repaint (getLocalBounds().removeFromTop (56));
    }
}

void CloneOC3Editor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    auto top = getLocalBounds().removeFromTop (56).reduced (16, 10);

    g.setColour (accent);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("CLONE OC3", top.removeFromLeft (200), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("POLY", top.removeFromLeft (60), juce::Justification::centredLeft);

    // Témoin : rouge quand l'octave est active
    auto ledArea = top.removeFromRight (22).withSizeKeepingCentre (14, 14).toFloat();
    g.setColour (ledOn.withMultipliedBrightness (0.3f + 0.7f * led));
    g.fillEllipse (ledArea);

    g.setColour (accent.withAlpha (0.8f));
    g.drawText ("Note grave : " + noteText, top.removeFromRight (170), juce::Justification::centredRight);

    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().withTrimmedTop (56).reduced (10).toFloat(), 8.0f);
}

void CloneOC3Editor::resized()
{
    auto area = getLocalBounds().withTrimmedTop (56).reduced (20, 16);
    bypassButton.setBounds (area.removeFromBottom (30).withSizeKeepingCentre (120, 28));
    area.removeFromBottom (6);
    const int w = area.getWidth() / 4;
    for (auto* k : { &range, &direct, &octave, &compact })
    {
        auto col = area.removeFromLeft (w).reduced (6, 0);
        k->label.setBounds (col.removeFromTop (22));
        k->slider.setBounds (col);
    }
}

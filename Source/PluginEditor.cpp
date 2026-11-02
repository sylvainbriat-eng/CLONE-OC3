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
        static const char* names[] = { "Do", "Do#", "R\xc3\xa9", "R\xc3\xa9#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
        const int midi = (int) std::round (69.0 + 12.0 * std::log2 (hz / 440.0));
        return juce::String (juce::CharPointer_UTF8 (names[(midi % 12 + 12) % 12])) + juce::String (midi / 12 - 1);
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
    setupKnob (time, "time", "TIME");
    setupKnob (direct, "direct", "DIRECT");
    setupKnob (octave, "octave", "OCTAVE");
    setupKnob (compact, "compact", "COMPACT");
    setupKnob (sens, "sensitivity", juce::String (juce::CharPointer_UTF8 ("SENSIBILIT\xc3\x89")));

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

    // AUTO : le plafond suit le jeu, le bouton RANGE est grisé
    for (auto* b : { &autoButton })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonColourId, panel.brighter (0.15f));
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3a8ee6));
        b->setColour (juce::TextButton::textColourOffId, accent);
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    }
    autoButton.onStateChange = [this] { updateAutoState(); };
    addAndMakeVisible (autoButton);
    autoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "auto", autoButton);
    updateAutoState();

    setSize (760, 360);
    startTimerHz (30);
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

void CloneOC3Editor::updateAutoState()
{
    const bool on = autoButton.getToggleState();
    range.slider.setEnabled (! on);
    range.label.setEnabled (! on);
    range.slider.setAlpha (on ? 0.35f : 1.0f);
    range.label.setAlpha (on ? 0.35f : 1.0f);
    // TIME ne sert qu'en mode AUTO
    time.slider.setEnabled (on);
    time.label.setEnabled (on);
    time.slider.setAlpha (on ? 1.0f : 0.35f);
    time.label.setAlpha (on ? 1.0f : 0.35f);
}

void CloneOC3Editor::timerCallback()
{
    const auto hz = processor.detectedHz.load();
    const auto newText = noteName (hz);
    const auto newLed = processor.octaveActive.load();
    // barre de notes : rafraîchie à chaque tick, lissée pour un mouvement fluide
    const float c = processor.ceilingHz.load();
    barCeiling += 0.5f * (c - barCeiling);
    barNote = hz;
    barMemory = autoButton.getToggleState() ? processor.memoryHz.load() : 0.0f;
    repaint (barArea);

    const auto newCeil = autoButton.getToggleState() ? "plafond " + noteName (processor.ceilingHz.load()) : juce::String();
    if (newText != noteText || newCeil != ceilingText || std::abs (newLed - led) > 0.05f)
    {
        noteText = newText;
        ceilingText = newCeil;
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
    g.drawText ("Note grave : " + noteText + (ceilingText.isNotEmpty() ? "  -  " + ceilingText : juce::String()),
                top.removeFromRight (300), juce::Justification::centredRight);

    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().withTrimmedTop (116).reduced (10).toFloat(), 8.0f);

    paintBar (g);
}

namespace
{
    // position horizontale (échelle de notes : log de la fréquence), 40 Hz -> 1100 Hz
    constexpr float barMinHz = 40.0f, barMaxHz = 1100.0f;
    float hzToX (float hz, juce::Rectangle<float> r)
    {
        hz = juce::jlimit (barMinHz, barMaxHz, hz);
        return r.getX() + r.getWidth() * std::log (hz / barMinHz) / std::log (barMaxHz / barMinHz);
    }
}

void CloneOC3Editor::paintBar (juce::Graphics& g)
{
    auto area = barArea.toFloat();
    auto bar = area.withTrimmedTop (4).withHeight (22);
    const bool isAuto = autoButton.getToggleState();
    const juce::Colour zone = isAuto ? juce::Colour (0xff3a8ee6) : ledOn;

    g.setColour (panel);
    g.fillRoundedRectangle (bar, 4.0f);

    // zone octaviée : de la gauche jusqu'au plafond
    const float xc = hzToX (barCeiling, bar);
    g.setColour (zone.withAlpha (0.45f));
    g.fillRoundedRectangle (bar.withRight (xc), 4.0f);

    // repères des cordes à vide (accordage standard)
    g.setFont (juce::FontOptions (10.0f));
    const std::pair<float, const char*> marks[] = { { 82.41f, "Mi" }, { 110.0f, "La" }, { 146.83f, "R\xc3\xa9" },
                                                    { 196.0f, "Sol" }, { 246.94f, "Si" }, { 329.63f, "Mi" }, { 659.26f, "Mi" } };
    for (auto& m : marks)
    {
        const float x = hzToX (m.first, bar);
        g.setColour (accent.withAlpha (0.25f));
        g.drawVerticalLine ((int) x, bar.getY(), bar.getBottom());
        g.setColour (accent.withAlpha (0.6f));
        g.drawText (juce::String (juce::CharPointer_UTF8 (m.second)), juce::Rectangle<float> (x - 15, bar.getBottom() + 1, 30, 12), juce::Justification::centred);
    }

    // note la plus grave mémorisée (mode AUTO) : d'où part le plafond
    if (barMemory > 0.0f)
    {
        const float xm = hzToX (barMemory, bar);
        g.setColour (zone.brighter (0.6f));
        juce::Path tri;
        tri.addTriangle (xm - 5, bar.getY() - 4, xm + 5, bar.getY() - 4, xm, bar.getY() + 3);
        g.fillPath (tri);
    }

    // plafond
    g.setColour (accent);
    g.fillRect (juce::Rectangle<float> (xc - 1.5f, bar.getY() - 3, 3.0f, bar.getHeight() + 6));

    // note jouée : verte si octaviée, grise sinon
    if (barNote > 0.0f)
    {
        const float xn = hzToX (barNote, bar);
        g.setColour (barNote <= barCeiling * 1.03f && led > 0.1f ? juce::Colour (0xff34c759) : accent.withAlpha (0.5f));
        g.fillEllipse (xn - 6, bar.getCentreY() - 6, 12, 12);
    }

    // légende
    g.setColour (accent.withAlpha (0.7f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (isAuto ? juce::String (juce::CharPointer_UTF8 ("AUTO : zone octavi\xc3\xa9" "e (bleu) = sous le plafond, qui suit la note la plus grave (triangle) au rythme de TIME"))
                       : juce::String (juce::CharPointer_UTF8 ("Zone octavi\xc3\xa9" "e (rouge) = sous le RANGE  -  point vert : note grave octavi\xc3\xa9" "e")),
                area.withTrimmedTop (40), juce::Justification::centredLeft);
}

void CloneOC3Editor::resized()
{
    barArea = getLocalBounds().withTrimmedTop (56).removeFromTop (60).reduced (20, 2);
    auto area = getLocalBounds().withTrimmedTop (116).reduced (20, 16);
    auto buttons = area.removeFromBottom (30);
    const int bw = buttons.getWidth() / 6;
    autoButton.setBounds (buttons.removeFromLeft (2 * bw).withSizeKeepingCentre (120, 28)); // sous RANGE + TIME
    buttons.removeFromLeft (bw);
    bypassButton.setBounds (buttons.removeFromLeft (bw).withSizeKeepingCentre (100, 28));   // sous OCTAVE
    area.removeFromBottom (6);
    const int w = area.getWidth() / 6;
    for (auto* k : { &range, &time, &direct, &octave, &compact, &sens })
    {
        auto col = area.removeFromLeft (w).reduced (6, 0);
        k->label.setBounds (col.removeFromTop (22));
        k->slider.setBounds (col);
    }
}

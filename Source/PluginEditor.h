#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class CloneOC3Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CloneOC3Editor (CloneOC3Processor&);
    ~CloneOC3Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& title);

    CloneOC3Processor& processor;
    juce::LookAndFeel_V4 lnf;
    Knob range, time, direct, octave, compact, sens;
    juce::TextButton bypassButton { "BYPASS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    void updateBypassState();
    juce::TextButton autoButton { "AUTO" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoAttachment;
    void updateAutoState();
    juce::String ceilingText;

    // Barre de notes : zone octaviée, plafond, note la plus grave mémorisée, note jouée
    juce::Rectangle<int> barArea;
    float barCeiling = 150.0f, barNote = 0.0f, barMemory = 0.0f;
    void paintBar (juce::Graphics&);
    juce::String noteText { "-" };
    float led = 0.0f;
};

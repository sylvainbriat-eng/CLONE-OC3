#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/PolyOctaveEngine.h"

class CloneOC3Processor : public juce::AudioProcessor
{
public:
    CloneOC3Processor();
    ~CloneOC3Processor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "CLONE OC3"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Lu par l'interface (affichage de la note suivie)
    std::atomic<float> detectedHz { 0.0f };
    std::atomic<float> octaveActive { 0.0f };
    std::atomic<float> ceilingHz { 150.0f };
    std::atomic<float> memoryHz { 0.0f };

private:
    oc3::PolyOctaveEngine engine;
    juce::SmoothedValue<float> directGain, octaveGain;
    juce::HeapBlock<float> monoBuffer;
    int monoCapacity = 0;

    std::atomic<float>* rangeParam = nullptr;
    std::atomic<float>* directParam = nullptr;
    std::atomic<float>* octaveParam = nullptr;
    std::atomic<float>* compactParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* sensParam = nullptr;
    std::atomic<float>* autoParam = nullptr;
    std::atomic<float>* timeParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloneOC3Processor)
};

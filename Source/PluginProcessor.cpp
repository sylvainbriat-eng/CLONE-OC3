#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace ids
{
    static const juce::String range   = "range";
    static const juce::String direct  = "direct";
    static const juce::String octave  = "octave";
    static const juce::String compact = "compact";
    static const juce::String bypass  = "bypass";
    static const juce::String sens    = "sensitivity";
    static const juce::String autoR   = "auto";
    static const juce::String time    = "time";
}

juce::AudioProcessorValueTreeState::ParameterLayout CloneOC3Processor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    // Range : de 50 Hz (sous le Si grave d'une baryton) à 400 Hz (Sol de la 3e corde à vide x2)
    NormalisableRange<float> rangeHz (50.0f, 400.0f);
    rangeHz.setSkewForCentre (140.0f);
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::range, 1 }, "Range", rangeHz, 150.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 0) + " Hz"; })));

    auto dB = [] (float v, int) { return v <= -59.9f ? String ("-inf") : String (v, 1) + " dB"; };
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::direct, 1 }, "Direct",
        NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (dB)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::octave, 1 }, "Octave",
        NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (dB)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::compact, 1 }, "Compact",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String ((int) v) + " %"; })));

    // Anti-percussion : 0 % = attaque franche, 100 % = seules les vraies notes sont octaviées
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::sens, 1 }, "Sensibilite",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 30.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String ((int) v) + " %"; })));

    // AUTO : le plafond suit le jeu (une octave au-dessus de la note la plus grave récente)
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::autoR, 1 }, "Auto", false));

    // TIME (mode AUTO) : vitesse à laquelle le plafond remonte quand on joue plus haut
    NormalisableRange<float> timeRange (0.05f, 10.0f);
    timeRange.setSkewForCentre (1.0f);
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::time, 1 }, "Time", timeRange, 2.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            { return v < 1.0f ? String ((int) std::round (v * 1000.0f)) + " ms" : String (v, 1) + " s"; })));

    // Bypass de l'octave : seul le son direct passe (comme le footswitch de la pédale)
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, 1 }, "Bypass", false));

    return { p.begin(), p.end() };
}

CloneOC3Processor::CloneOC3Processor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    rangeParam   = apvts.getRawParameterValue (ids::range);
    directParam  = apvts.getRawParameterValue (ids::direct);
    octaveParam  = apvts.getRawParameterValue (ids::octave);
    compactParam = apvts.getRawParameterValue (ids::compact);
    bypassParam  = apvts.getRawParameterValue (ids::bypass);
    sensParam    = apvts.getRawParameterValue (ids::sens);
    autoParam    = apvts.getRawParameterValue (ids::autoR);
    timeParam    = apvts.getRawParameterValue (ids::time);
}

bool CloneOC3Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void CloneOC3Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    directGain.reset (sampleRate, 0.02);
    octaveGain.reset (sampleRate, 0.02);
    directGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (directParam->load(), -59.9f));
    octaveGain.setCurrentAndTargetValue (1.0f);
    monoCapacity = juce::jmax (samplesPerBlock, 1);
    monoBuffer.allocate ((size_t) monoCapacity, true);
    setLatencySamples (0); // aucune latence ajoutée
}

void CloneOC3Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();

    if (n > monoCapacity) { monoCapacity = n; monoBuffer.allocate ((size_t) n, true); } // filet de sécurité

    // Copie mono de l'entrée (moyenne des canaux) avant de modifier le buffer
    float* mono = monoBuffer.get();
    if (numIn > 0)
    {
        juce::FloatVectorOperations::copy (mono, buffer.getReadPointer (0), n);
        for (int ch = 1; ch < numIn; ++ch) juce::FloatVectorOperations::add (mono, buffer.getReadPointer (ch), n);
        if (numIn > 1) juce::FloatVectorOperations::multiply (mono, 1.0f / (float) numIn, n);
    }
    else juce::FloatVectorOperations::clear (mono, n);

    // entrée mono -> sortie stéréo : on duplique le signal direct
    for (int ch = numIn; ch < numOut; ++ch) buffer.copyFrom (ch, 0, buffer, 0, 0, n);

    oc3::PolyOctaveEngine::Params p;
    p.rangeHz = rangeParam->load();
    p.compact = compactParam->load() / 100.0;
    p.octaveGain = 1.0; // le niveau est lissé ci-dessous
    p.sensitivity = sensParam->load() / 100.0;
    p.autoRange = autoParam->load() > 0.5f;
    p.timeSeconds = timeParam->load();
    engine.setParams (p);

    directGain.setTargetValue (juce::Decibels::decibelsToGain (directParam->load(), -59.9f));
    const bool bypassed = bypassParam->load() > 0.5f;
    octaveGain.setTargetValue (bypassed ? 0.0f : juce::Decibels::decibelsToGain (octaveParam->load(), -59.9f));

    for (int i = 0; i < n; ++i)
    {
        const float sub = (float) engine.processSample ((double) mono[i]) * octaveGain.getNextValue();
        const float dg = directGain.getNextValue();
        for (int ch = 0; ch < numOut; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            d[i] = d[i] * dg + sub;
        }
    }

    detectedHz.store ((float) engine.getMeasuredHz());
    ceilingHz.store ((float) engine.getCeilingHz());
    octaveActive.store (bypassed ? 0.0f : (float) engine.getGate());
}

juce::AudioProcessorEditor* CloneOC3Processor::createEditor() { return new CloneOC3Editor (*this); }

void CloneOC3Processor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void CloneOC3Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CloneOC3Processor(); }

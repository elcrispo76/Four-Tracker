// SPDX-License-Identifier: AGPL-3.0-or-later
// Graphic EQ - Copyright (C) 2026 elcrispo76

#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

AudioProcessorValueTreeState::ParameterLayout EqProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout l;
    auto dbText = [] (float v, int) { return (v > 0.0f ? "+" : "") + String (v, 1) + " dB"; };
    for (int b = 0; b < eq::kBands; ++b)
    {
        const double hz = eq::bandHz (b);
        const String name = hz >= 1000.0 ? String (hz / 1000.0, hz >= 10000.0 || std::fmod (hz, 1000.0) == 0.0 ? 0 : 1) + " kHz" : String ((int) hz) + " Hz";
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { bandId (b), 1 }, name, NormalisableRange<float> (-eq::kRange, eq::kRange, 0.1f), 0.0f,
                                                      AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));
    }
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));
    return l;
}

EqProcessor::EqProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "GRAPHICEQ", createLayout())
{
}

std::array<float, eq::kBands> EqProcessor::readKnobs() const
{
    std::array<float, eq::kBands> k {};
    for (int b = 0; b < eq::kBands; ++b)
        if (auto* v = params.getRawParameterValue (bandId (b))) k[(size_t) b] = jlimit (-eq::kRange, eq::kRange, v->load());
    return k;
}

float EqProcessor::readOutput() const
{
    if (auto* v = params.getRawParameterValue ("output")) return jlimit (-12.0f, 12.0f, v->load());
    return 0.0f;
}

bool EqProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != AudioChannelSet::stereo() && out != AudioChannelSet::mono()) return false;
    return l.getMainInputChannelSet() == out;
}

void EqProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
}

void EqProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);
    engine.setKnobs (readKnobs(), readOutput());
    engine.process (buffer.getWritePointer (0), buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr, n);
}

// ---- presets ----------------------------------------------------------------------------
//                                   60   170   310   600   800  1000  3000  6000 13000 16000
const std::vector<EqProcessor::Preset>& EqProcessor::presets()
{
    static const std::vector<Preset> list = {
        { "FLAT",            { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { "BASS BOOST",      { 8, 5, 2, 0, 0, 0, 0, 0, 0, 0 } },
        { "SMILE (LOUDNESS)",{ 6, 4, 1, -1, -2, -2, 0, 2, 4, 5 } },
        { "MID SCOOP",       { 2, 0, -2, -5, -6, -5, -2, 0, 1, 2 } },
        { "VOCAL PRESENCE",  { -3, -2, 0, 0, 1, 2, 4, 3, 1, 0 } },
        { "AIR",             { 0, 0, 0, 0, 0, 0, 0, 2, 5, 7 } },
        { "WARM",            { 3, 4, 3, 1, 0, 0, -1, -2, -3, -4 } },
        { "KICK THUMP",      { 7, 2, -4, -3, -1, 0, 3, 1, 0, 0 } },
        { "SNARE SNAP",      { -4, 3, 2, -2, -1, 0, 4, 3, 1, 0 } },
        { "HIHAT SHINE",     { -12, -12, -8, -4, -2, 0, 1, 3, 5, 6 } },
        { "BASS LINE",       { 4, 6, 2, -2, -1, 1, 3, 0, -2, -3 } },
        { "TELEPHONE",       { -12, -12, -6, 3, 6, 6, 4, -8, -12, -12 } },
        { "LO-FI",           { -6, 0, 2, 3, 3, 2, 0, -6, -12, -12 } },
        { "CLEAR MUD",       { 0, -2, -5, -2, 0, 0, 1, 1, 0, 0 } },
    };
    return list;
}

void EqProcessor::applyPreset (int index)
{
    if (! isPositiveAndBelow (index, (int) presets().size())) return;
    const auto& p = presets()[(size_t) index];
    for (int b = 0; b < eq::kBands; ++b)
        if (auto* rp = params.getParameter (bandId (b)))
            rp->setValueNotifyingHost (rp->convertTo0to1 (p.knobs[(size_t) b]));
}

void EqProcessor::flat() { applyPreset (0); }

int EqProcessor::getNumPrograms() { return (int) presets().size(); }

void EqProcessor::setCurrentProgram (int index)
{
    if (! isPositiveAndBelow (index, (int) presets().size())) return;
    currentProgram = index;
    applyPreset (index);
}

const String EqProcessor::getProgramName (int index)
{
    return isPositiveAndBelow (index, (int) presets().size()) ? String (presets()[(size_t) index].name) : String();
}

// ---- state ----------------------------------------------------------------------------------

void EqProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = params.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void EqProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (params.state.getType()))
            params.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessorEditor* EqProcessor::createEditor() { return new EqEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EqProcessor(); }

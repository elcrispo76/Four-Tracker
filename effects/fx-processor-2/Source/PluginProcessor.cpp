// SPDX-License-Identifier: AGPL-3.0-or-later
// FX Processor 2 - Copyright (C) 2026 elcrispo76

#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

AudioProcessorValueTreeState::ParameterLayout FxProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout l;
    auto onOff = [&l] (const char* id, const char* name)
    {
        l.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, false));
    };
    auto num = [&l] (const char* id, const char* name, int lo, int hi, int def)
    {
        l.add (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def));
    };
    auto choice = [&l] (const char* id, const char* name, StringArray items, int def)
    {
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def));
    };

    const StringArray syncs { "FREE", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS" };

    onOff  ("phaserOn", "Phaser on");
    choice ("phSync", "Phaser speed", syncs, 6);
    num    ("phRate", "Phaser free Hz", 0, 127, 30);
    num    ("phDepth", "Phaser depth", 0, 127, 80);
    num    ("phCenter", "Phaser center", 0, 127, 64);
    num    ("phFeedback", "Phaser feedback", 0, 95, 50);
    choice ("phStages", "Phaser stages", { "4", "6", "8", "12" }, 1);
    num    ("phMix", "Phaser mix", 0, 100, 100);

    onOff  ("flangerOn", "Flanger on");
    choice ("flSync", "Flanger speed", syncs, 7);
    num    ("flRate", "Flanger free Hz", 0, 127, 20);
    num    ("flDepth", "Flanger depth", 0, 127, 90);
    num    ("flDelay", "Flanger delay", 0, 127, 30);
    num    ("flFeedback", "Flanger feedback", -95, 95, 60);
    num    ("flMix", "Flanger mix", 0, 100, 70);

    onOff  ("ringOn", "Ring mod on");
    num    ("rmFreq", "Ring mod frequency", 0, 127, 60);
    choice ("rmWave", "Ring mod wave", { "SINE", "TRI", "SQUARE" }, 0);
    choice ("rmSync", "Ring mod LFO speed", syncs, 0);
    num    ("rmRate", "Ring mod LFO free Hz", 0, 127, 30);
    num    ("rmLfoDepth", "Ring mod LFO depth", 0, 127, 0);
    num    ("rmMix", "Ring mod mix", 0, 100, 100);

    num    ("output", "Output", -24, 6, 0);
    num    ("tempo", "Own tempo", 40, 300, 125);
    return l;
}

FxProcessor::FxProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "FXPROCESSOR", createLayout())
{
}

int FxProcessor::get (const char* id) const
{
    if (auto* v = params.getRawParameterValue (id)) return roundToInt (v->load());
    return 0;
}

fx::Settings FxProcessor::readSettings() const
{
    fx::Settings s;
    s.phaserOn = get ("phaserOn") != 0;  s.phSync = get ("phSync");  s.phRate = get ("phRate");  s.phDepth = get ("phDepth");
    s.phCenter = get ("phCenter");  s.phFeedback = get ("phFeedback");  s.phStages = get ("phStages");  s.phMix = get ("phMix");
    s.flangerOn = get ("flangerOn") != 0;  s.flSync = get ("flSync");  s.flRate = get ("flRate");  s.flDepth = get ("flDepth");
    s.flDelay = get ("flDelay");  s.flFeedback = get ("flFeedback");  s.flMix = get ("flMix");
    s.ringOn = get ("ringOn") != 0;  s.rmFreq = get ("rmFreq");  s.rmWave = get ("rmWave");  s.rmSync = get ("rmSync");
    s.rmRate = get ("rmRate");  s.rmLfoDepth = get ("rmLfoDepth");  s.rmMix = get ("rmMix");
    s.outputDb = get ("output");
    return s;
}

bool FxProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != AudioChannelSet::stereo() && out != AudioChannelSet::mono()) return false;
    return l.getMainInputChannelSet() == out;
}

void FxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
}

void FxProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);

    // tempo and song position from the host when it sends them, otherwise our own TEMPO
    fx::Transport t;
    bool fromHost = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm(); b.hasValue() && *b > 1.0)
            {
                fromHost = true;
                t.bpm = *b;
                if (auto p = pos->getPpqPosition(); p.hasValue() && pos->getIsPlaying())
                {
                    t.playing = true;
                    t.ppq = *p;
                }
            }
    if (! fromHost) t.bpm = jlimit (40, 300, get ("tempo"));
    shownBpm = t.bpm;
    hostTempo = fromHost;

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    engine.process (L, R, n, t, readSettings());
    lfos[0] = engine.getPhaserLfo();
    lfos[1] = engine.getFlangerLfo();
    lfos[2] = engine.getRingLfo();
}

// ---- presets ----------------------------------------------------------------------------

const std::vector<FxProcessor::Preset>& FxProcessor::presets()
{
    static const std::vector<Preset> list = {
        { "INIT", {} },
        { "SLOW PHASER",    { { "phaserOn", 1 }, { "phSync", 6 }, { "phDepth", 90 }, { "phFeedback", 40 }, { "phStages", 2 } } },
        { "PHASE 1/8",      { { "phaserOn", 1 }, { "phSync", 2 }, { "phDepth", 60 }, { "phFeedback", 65 }, { "phStages", 0 } } },
        { "DEEP PHASE",     { { "phaserOn", 1 }, { "phSync", 7 }, { "phDepth", 110 }, { "phFeedback", 80 }, { "phStages", 3 }, { "phCenter", 50 } } },
        { "JET FLANGER",    { { "flangerOn", 1 }, { "flSync", 7 }, { "flDepth", 110 }, { "flDelay", 20 }, { "flFeedback", 80 }, { "flMix", 80 } } },
        { "METAL FLANGE",   { { "flangerOn", 1 }, { "flSync", 1 }, { "flDepth", 60 }, { "flDelay", 10 }, { "flFeedback", -85 }, { "flMix", 70 } } },
        { "CHORUS FLANGE",  { { "flangerOn", 1 }, { "flSync", 4 }, { "flDepth", 70 }, { "flDelay", 90 }, { "flFeedback", 0 }, { "flMix", 50 } } },
        { "ROBOT VOICE",    { { "ringOn", 1 }, { "rmFreq", 40 }, { "rmWave", 0 }, { "rmMix", 100 } } },
        { "BELL RING",      { { "ringOn", 1 }, { "rmFreq", 95 }, { "rmWave", 0 }, { "rmMix", 60 } } },
        { "SQUARE BUZZ",    { { "ringOn", 1 }, { "rmFreq", 55 }, { "rmWave", 2 }, { "rmMix", 70 } } },
        { "RING SWEEP",     { { "ringOn", 1 }, { "rmFreq", 80 }, { "rmSync", 6 }, { "rmLfoDepth", 100 }, { "rmMix", 80 } } },
        { "RING TREMOLO",   { { "ringOn", 1 }, { "rmFreq", 8 }, { "rmWave", 1 }, { "rmMix", 50 } } },
        { "PHASE + FLANGE", { { "phaserOn", 1 }, { "phSync", 6 }, { "phDepth", 80 }, { "phFeedback", 40 },
                              { "flangerOn", 1 }, { "flSync", 7 }, { "flDepth", 80 }, { "flFeedback", 50 }, { "flMix", 50 } } },
        { "SPACE DRONE",    { { "phaserOn", 1 }, { "phSync", 7 }, { "phDepth", 100 }, { "phFeedback", 70 }, { "phStages", 3 },
                              { "ringOn", 1 }, { "rmFreq", 70 }, { "rmSync", 7 }, { "rmLfoDepth", 60 }, { "rmMix", 35 } } },
    };
    return list;
}

void FxProcessor::applyPreset (int index)
{
    if (! isPositiveAndBelow (index, (int) presets().size())) return;
    // everything back to its default first, then the preset's own values
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            if (rp->getParameterID() != "tempo" && rp->getParameterID() != "output")
                rp->setValueNotifyingHost (rp->getDefaultValue());
    for (auto& [id, value] : presets()[(size_t) index].values)
        if (auto* rp = params.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 ((float) value));
}

int FxProcessor::getNumPrograms() { return (int) presets().size(); }

void FxProcessor::setCurrentProgram (int index)
{
    if (! isPositiveAndBelow (index, (int) presets().size())) return;
    currentProgram = index;
    applyPreset (index);
}

const String FxProcessor::getProgramName (int index)
{
    return isPositiveAndBelow (index, (int) presets().size()) ? String (presets()[(size_t) index].name) : String();
}

// ---- state ----------------------------------------------------------------------------------

void FxProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = params.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void FxProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (params.state.getType()))
            params.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessorEditor* FxProcessor::createEditor() { return new FxEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FxProcessor(); }

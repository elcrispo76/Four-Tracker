// SPDX-License-Identifier: AGPL-3.0-or-later
// FX Processor - Copyright (C) 2026 elcrispo76

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

    onOff  ("filterOn", "Filter on");
    choice ("filterMode", "Filter mode", { "LOW-PASS", "BAND-PASS", "HIGH-PASS" }, 0);
    num    ("cutoff", "Filter cutoff", 0, 127, 100);
    num    ("reso", "Filter resonance", 0, 127, 20);

    onOff  ("pwOn", "PW on");
    choice ("pwRate", "PW rate", { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" }, 2);
    num    ("pwWidth", "PW width", 5, 95, 50);
    num    ("pwDepth", "PW depth", 0, 100, 100);

    choice ("lfoShape", "LFO shape", { "SINE", "TRI", "SAW", "SQUARE", "S&H" }, 0);
    choice ("lfoSync", "LFO sync", { "FREE", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS" }, 5);
    num    ("lfoRate", "LFO rate (free)", 0, 127, 40);
    num    ("lfoToFilter", "LFO to filter", -64, 63, 0);
    num    ("lfoToPw", "LFO to PW", 0, 100, 0);

    onOff  ("chorusOn", "Chorus on");
    num    ("chorusRate", "Chorus rate", 0, 127, 30);
    num    ("chorusDepth", "Chorus depth", 0, 127, 60);
    num    ("chorusMix", "Chorus mix", 0, 100, 50);

    onOff  ("delayOn", "Delay on");
    choice ("delayTime", "Delay time", { "1/16", "1/8", "1/8 D", "1/4", "1/4 D", "1/2", "1 BAR" }, 2);
    num    ("delayFeedback", "Delay feedback", 0, 95, 40);
    num    ("delayTone", "Delay tone", 0, 127, 90);
    num    ("delayMix", "Delay mix", 0, 100, 30);
    choice ("delayPing", "Delay ping-pong", { "OFF", "ON" }, 0);

    onOff  ("reverbOn", "Reverb on");
    num    ("reverbSize", "Reverb size", 0, 127, 70);
    num    ("reverbDamp", "Reverb damping", 0, 127, 50);
    num    ("reverbWidth", "Reverb width", 0, 127, 100);
    num    ("reverbMix", "Reverb mix", 0, 100, 25);

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
    s.filterOn = get ("filterOn") != 0;  s.filterMode = get ("filterMode");  s.cutoff = get ("cutoff");  s.reso = get ("reso");
    s.pwOn = get ("pwOn") != 0;  s.pwRate = get ("pwRate");  s.pwWidth = get ("pwWidth");  s.pwDepth = get ("pwDepth");
    s.lfoShape = get ("lfoShape");  s.lfoSync = get ("lfoSync");  s.lfoRate = get ("lfoRate");
    s.lfoToFilter = get ("lfoToFilter");  s.lfoToPw = get ("lfoToPw");
    s.chorusOn = get ("chorusOn") != 0;  s.chorusRate = get ("chorusRate");  s.chorusDepth = get ("chorusDepth");  s.chorusMix = get ("chorusMix");
    s.delayOn = get ("delayOn") != 0;  s.delayTime = get ("delayTime");  s.delayFeedback = get ("delayFeedback");
    s.delayTone = get ("delayTone");  s.delayMix = get ("delayMix");  s.delayPing = get ("delayPing") != 0;
    s.reverbOn = get ("reverbOn") != 0;  s.reverbSize = get ("reverbSize");  s.reverbDamp = get ("reverbDamp");
    s.reverbWidth = get ("reverbWidth");  s.reverbMix = get ("reverbMix");
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
    lfoNow = engine.getLfo();
    gateNow = engine.getGate();
}

// ---- presets ----------------------------------------------------------------------------

const std::vector<FxProcessor::Preset>& FxProcessor::presets()
{
    static const std::vector<Preset> list = {
        { "INIT", {} },
        { "TRANCE GATE",   { { "pwOn", 1 }, { "pwRate", 2 }, { "pwWidth", 55 }, { "pwDepth", 100 },
                             { "reverbOn", 1 }, { "reverbSize", 60 }, { "reverbMix", 18 } } },
        { "PUMP",          { { "pwOn", 1 }, { "pwRate", 0 }, { "pwWidth", 70 }, { "pwDepth", 60 } } },
        { "GATE + SWEEP",  { { "pwOn", 1 }, { "pwRate", 2 }, { "pwWidth", 50 }, { "lfoSync", 6 }, { "lfoShape", 1 },
                             { "lfoToPw", 80 }, { "filterOn", 1 }, { "cutoff", 80 }, { "reso", 50 }, { "lfoToFilter", 30 } } },
        { "FILTER SWEEP",  { { "filterOn", 1 }, { "filterMode", 0 }, { "cutoff", 70 }, { "reso", 70 },
                             { "lfoShape", 0 }, { "lfoSync", 6 }, { "lfoToFilter", 50 } } },
        { "WOBBLE",        { { "filterOn", 1 }, { "cutoff", 60 }, { "reso", 80 }, { "lfoShape", 0 }, { "lfoSync", 2 }, { "lfoToFilter", 45 } } },
        { "ACID BAND",     { { "filterOn", 1 }, { "filterMode", 1 }, { "cutoff", 75 }, { "reso", 105 },
                             { "lfoShape", 4 }, { "lfoSync", 1 }, { "lfoToFilter", 30 } } },
        { "HIGH-PASS RISE",{ { "filterOn", 1 }, { "filterMode", 2 }, { "cutoff", 40 }, { "reso", 40 },
                             { "lfoShape", 2 }, { "lfoSync", 7 }, { "lfoToFilter", -40 } } },
        { "DUB DELAY",     { { "delayOn", 1 }, { "delayTime", 4 }, { "delayFeedback", 70 }, { "delayTone", 60 }, { "delayMix", 40 },
                             { "reverbOn", 1 }, { "reverbSize", 80 }, { "reverbMix", 20 } } },
        { "PING PONG",     { { "delayOn", 1 }, { "delayTime", 1 }, { "delayFeedback", 55 }, { "delayTone", 95 },
                             { "delayMix", 35 }, { "delayPing", 1 } } },
        { "SPACE CHORUS",  { { "chorusOn", 1 }, { "chorusRate", 25 }, { "chorusDepth", 90 }, { "chorusMix", 55 },
                             { "reverbOn", 1 }, { "reverbSize", 100 }, { "reverbWidth", 127 }, { "reverbMix", 35 } } },
        { "BIG HALL",      { { "reverbOn", 1 }, { "reverbSize", 120 }, { "reverbDamp", 30 }, { "reverbMix", 45 } } },
        { "80S STRINGS",   { { "chorusOn", 1 }, { "chorusRate", 45 }, { "chorusDepth", 70 }, { "chorusMix", 50 },
                             { "delayOn", 1 }, { "delayTime", 3 }, { "delayFeedback", 30 }, { "delayMix", 20 },
                             { "reverbOn", 1 }, { "reverbSize", 90 }, { "reverbMix", 30 } } },
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

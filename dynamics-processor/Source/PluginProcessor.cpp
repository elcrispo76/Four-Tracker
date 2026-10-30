// SPDX-License-Identifier: AGPL-3.0-or-later
// Dynamics Processor - Copyright (C) 2026 elcrispo76

#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

AudioProcessorValueTreeState::ParameterLayout DynProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout l;
    auto num = [&l] (const char* id, const char* name, int lo, int hi, int def)
    {
        l.add (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def));
    };
    auto choice = [&l] (const char* id, const char* name, StringArray items, int def)
    {
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def));
    };

    auto onOff = [&l] (const char* id, const char* name, bool def)
    {
        l.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };
    choice ("instrument", "Instrument", { "KICK", "SNARE", "HIHATS", "PERCUSSION", "BASSLINE", "SYNTH/ARP", "PADS", "VOICE" }, 0);
    onOff  ("tsOn", "Transient on", false);
    num    ("tsAttack", "Transient attack", -100, 100, 0);
    num    ("tsSustain", "Transient sustain", -100, 100, 0);
    onOff  ("compOn", "Compressor on", true);
    num    ("threshold", "Threshold", -60, 0, -18);
    choice ("ratio", "Ratio", { "1.5:1", "2:1", "3:1", "4:1", "6:1", "8:1", "12:1", "20:1", "LIMIT" }, 3);
    num    ("attack", "Attack", 0, 127, 89);
    num    ("release", "Release", 0, 127, 55);
    num    ("knee", "Knee", 0, 24, 6);
    num    ("makeup", "Makeup", -12, 24, 0);
    num    ("mix", "Mix", 0, 100, 100);
    choice ("scFilter", "Sidechain filter", { "OFF", "40 HZ", "60 HZ", "80 HZ", "100 HZ", "150 HZ", "250 HZ", "400 HZ" }, 0);
    choice ("detect", "Detector", { "PEAK", "RMS" }, 0);
    onOff  ("satOn", "Saturation on", false);
    num    ("satDrive", "Saturation drive", 0, 100, 30);
    choice ("satMode", "Saturation mode", { "TAPE", "TUBE", "CLIP" }, 0);
    num    ("satTone", "Saturation tone", 0, 100, 100);
    num    ("satMix", "Saturation mix", 0, 100, 100);
    num    ("output", "Output", -24, 6, 0);
    return l;
}

DynProcessor::DynProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "DYNAMICSPROCESSOR", createLayout())
{
}

DynProcessor::~DynProcessor() { cancelPendingUpdate(); }

int DynProcessor::get (const char* id) const
{
    if (auto* v = params.getRawParameterValue (id)) return roundToInt (v->load());
    return 0;
}

void DynProcessor::setParam (const char* id, int value)
{
    if (auto* rp = params.getParameter (id))
        rp->setValueNotifyingHost (rp->convertTo0to1 ((float) value));
}

dyn::Settings DynProcessor::readSettings() const
{
    dyn::Settings s;
    s.threshold = get ("threshold");  s.ratio = get ("ratio");  s.attack = get ("attack");  s.release = get ("release");
    s.knee = get ("knee");  s.makeup = get ("makeup");  s.mix = get ("mix");  s.scFilter = get ("scFilter");
    s.detect = get ("detect");  s.outputDb = get ("output");
    s.tsOn = get ("tsOn") != 0;  s.tsAttack = get ("tsAttack");  s.tsSustain = get ("tsSustain");
    s.compOn = get ("compOn") != 0;
    s.satOn = get ("satOn") != 0;  s.satDrive = get ("satDrive");  s.satMode = get ("satMode");
    s.satTone = get ("satTone");  s.satMix = get ("satMix");
    return s;
}

int DynProcessor::getInstrument() const { return jlimit (0, (int) dyn::numInstruments - 1, get ("instrument")); }

bool DynProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != AudioChannelSet::stereo() && out != AudioChannelSet::mono()) return false;
    return l.getMainInputChannelSet() == out;
}

void DynProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const bool wasListening = isListening();
    engine.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (roundToInt (engine.getLatency()));
    if (wasListening) engine.startListening();
}

void DynProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    engine.process (L, R, n, readSettings());

    if (engine.getListenState() == dyn::Engine::heard && ! asked.exchange (true))
        triggerAsyncUpdate();
}

// ---- instruments and AUTO ----------------------------------------------------------------

void DynProcessor::selectInstrument (int instrument)
{
    instrument = jlimit (0, (int) dyn::numInstruments - 1, instrument);
    const auto& p = dyn::profile (instrument);
    setParam ("instrument", instrument);
    setParam ("ratio", p.ratio);
    setParam ("attack", dyn::attackParam (p.attackMs));
    setParam ("release", dyn::releaseParam (p.releaseMs));
    setParam ("knee", p.knee);
    setParam ("scFilter", p.scFilter);
    setParam ("detect", p.detect);
    setParam ("tsAttack", p.tsAttack);
    setParam ("tsSustain", p.tsSustain);
    setParam ("satMode", p.satMode);
    setParam ("satDrive", p.satDrive);
    setParam ("satTone", p.satTone);
    for (auto* id : { "tsOn", "compOn", "satOn" }) setParam (id, 1);
    if (! isListening())
        autoMessage = String (dyn::instrumentName (instrument)) + ": PLAY THE TRACK AND CLICK AUTO";
}

void DynProcessor::toggleAuto()
{
    if (isListening())
    {
        engine.cancelListening();
        asked = false;
        autoMessage = "AUTO STOPPED";
        return;
    }
    asked = false;
    engine.startListening();
    autoMessage = "AUTO IS LISTENING - PLAY THE TRACK";
}

void DynProcessor::handleAsyncUpdate()
{
    if (engine.getListenState() != dyn::Engine::heard) { asked = false; return; }
    const int inst = getInstrument();
    lastResult = engine.analyse (readSettings(), inst);
    engine.finishListening();
    asked = false;
    if (! lastResult.ok)
    {
        autoMessage = "AUTO HEARD NO SOUND - PLAY THE TRACK AND TRY AGAIN";
        return;
    }
    setParam ("threshold", lastResult.threshold);
    setParam ("makeup", lastResult.makeup);
    setParam ("release", lastResult.release);
    setParam ("tsAttack", lastResult.tsAttack);
    setParam ("tsSustain", lastResult.tsSustain);
    setParam ("satDrive", lastResult.satDrive);
    for (auto* id : { "tsOn", "compOn", "satOn" }) setParam (id, 1);
    auto pct = [] (int v) { return (v > 0 ? "+" : "") + String (v); };
    autoMessage = String (dyn::instrumentName (inst)) + " FIXED: ATTACK " + pct (lastResult.tsAttack) + " SUSTAIN " + pct (lastResult.tsSustain)
                + "  THR " + String (lastResult.threshold) + " DB (" + String (roundToInt (lastResult.gainReduction)) + " DB SQUEEZE)  MAKEUP "
                + pct (lastResult.makeup) + "  DRIVE " + String (lastResult.satDrive);
}

// ---- presets ----------------------------------------------------------------------------

const std::vector<DynProcessor::Preset>& DynProcessor::presets()
{
    static const std::vector<Preset> list = {
        { "INIT",             -1, {} },
        { "KICK PUNCH",       dyn::kick,       { { "threshold", -20 }, { "makeup", 4 } } },
        { "SNARE CRACK",      dyn::snare,      { { "threshold", -20 }, { "makeup", 4 } } },
        { "HIHAT TIGHT",      dyn::hihats,     { { "threshold", -24 }, { "makeup", 2 } } },
        { "PERC GROOVE",      dyn::percussion, { { "threshold", -22 }, { "makeup", 3 } } },
        { "BASS GROWL",       dyn::bassline,   { { "threshold", -22 }, { "makeup", 4 }, { "satDrive", 50 } } },
        { "ARP GLUE",         dyn::synthArp,   { { "threshold", -20 }, { "makeup", 3 } } },
        { "PAD SOFT",         dyn::pads,       { { "threshold", -24 }, { "makeup", 2 } } },
        { "VOICE UP FRONT",   dyn::voice,      { { "threshold", -24 }, { "makeup", 5 } } },
        { "BREAK SMASH",      dyn::percussion, { { "tsAttack", 60 }, { "tsSustain", 30 }, { "threshold", -30 }, { "ratio", 4 },
                                                 { "attack", dyn::attackParam (1.0) }, { "release", dyn::releaseParam (60.0) },
                                                 { "makeup", 9 }, { "mix", 60 }, { "satMode", 2 }, { "satDrive", 45 } } },
        { "DRUM BUS GLUE",    dyn::percussion, { { "tsAttack", 15 }, { "tsSustain", 0 }, { "threshold", -16 }, { "ratio", 1 },
                                                 { "attack", dyn::attackParam (30.0) }, { "release", dyn::releaseParam (200.0) },
                                                 { "knee", 8 }, { "makeup", 2 }, { "satDrive", 15 } } },
        { "SNAP ONLY",        dyn::snare,      { { "compOn", 0 }, { "satOn", 0 }, { "tsAttack", 60 }, { "tsSustain", -20 } } },
        { "TAPE WARMTH",      dyn::synthArp,   { { "tsOn", 0 }, { "compOn", 0 }, { "satMode", 0 }, { "satDrive", 40 }, { "satTone", 75 } } },
        { "LO-FI CRUNCH",     dyn::percussion, { { "tsAttack", 20 }, { "threshold", -28 }, { "ratio", 5 }, { "makeup", 6 },
                                                 { "satMode", 2 }, { "satDrive", 80 }, { "satTone", 30 } } },
        { "BRICKWALL LIMIT",  dyn::synthArp,   { { "tsOn", 0 }, { "satOn", 0 }, { "threshold", -6 }, { "ratio", 8 }, { "attack", 0 },
                                                 { "release", dyn::releaseParam (80.0) }, { "knee", 0 }, { "makeup", 4 } } },
    };
    return list;
}

void DynProcessor::applyPreset (int index)
{
    if (! isPositiveAndBelow (index, (int) presets().size())) return;
    // everything back to its default first, then the instrument's knobs, then the preset's own values
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            if (rp->getParameterID() != "output")
                rp->setValueNotifyingHost (rp->getDefaultValue());
    const auto& pr = presets()[(size_t) index];
    if (pr.instrument >= 0) selectInstrument (pr.instrument);
    for (auto& [id, value] : pr.values) setParam (id, value);
}

int DynProcessor::getNumPrograms() { return (int) presets().size(); }

void DynProcessor::setCurrentProgram (int index)
{
    if (! isPositiveAndBelow (index, (int) presets().size())) return;
    currentProgram = index;
    applyPreset (index);
}

const String DynProcessor::getProgramName (int index)
{
    return isPositiveAndBelow (index, (int) presets().size()) ? String (presets()[(size_t) index].name) : String();
}

// ---- state ----------------------------------------------------------------------------------

void DynProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = params.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void DynProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (params.state.getType()))
            params.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessorEditor* DynProcessor::createEditor() { return new DynEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DynProcessor(); }

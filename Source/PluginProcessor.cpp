// SPDX-License-Identifier: AGPL-3.0-or-later
// Breakbeat Processor - Copyright (C) 2026 elcrispo76

#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

namespace
{
    const StringArray actionChoices { "-", "1/4", "2/4", "3/4", "4/4", "REV", "STOP", "BIT" };
    // SLICE: how much of the channel's sound is cut into 1/4 - 4/4; STEPS: what the 8 steps cover.
    // 8, 16, 32 or 64 rows in Four Tracker = 1/2, 1, 2 or 4 bars in a DAW
    const StringArray lengthChoices { "1/2 BAR (8 ROWS)", "1 BAR (16 ROWS)", "2 BARS (32 ROWS)", "4 BARS (64 ROWS)" };
    constexpr double lengthBeats[] = { 2.0, 4.0, 8.0, 16.0 };
}

AudioProcessorValueTreeState::ParameterLayout BreakbeatProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < bb::kNumSteps; ++i)
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { stepId (i), 1 }, "Step " + String (i + 1), actionChoices, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "length", 1 }, "Slice", lengthChoices, 3));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "steps", 1 }, "Steps cover", lengthChoices, 1));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "bits", 1 }, "Bits", 1, 16, 6));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "rate", 1 }, "Rate", 1, 32, 6));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "smooth", 1 }, "Smooth", 0, 20, 3,
                                                     AudioParameterIntAttributes().withLabel ("ms")));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "mix", 1 }, "Mix", 0, 100, 100,
                                                     AudioParameterIntAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "ahead", 1 }, "Read ahead", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "sync", 1 }, "Sync", StringArray { "SONG", "SOUND" }, 0));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "tempo", 1 }, "Own tempo", 40, 300, 125,
                                                     AudioParameterIntAttributes().withLabel ("BPM")));
    return layout;
}

BreakbeatProcessor::BreakbeatProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "BREAKBEAT", createLayout())
{
    for (int i = 0; i < bb::kNumSteps; ++i) stepParams[(size_t) i] = params.getRawParameterValue (stepId (i));
    lengthParam = params.getRawParameterValue ("length");
    bitsParam   = params.getRawParameterValue ("bits");
    rateParam   = params.getRawParameterValue ("rate");
    smoothParam = params.getRawParameterValue ("smooth");
    mixParam    = params.getRawParameterValue ("mix");
    aheadParam  = params.getRawParameterValue ("ahead");
    stepsParam  = params.getRawParameterValue ("steps");
    syncParam   = params.getRawParameterValue ("sync");
    tempoParam  = params.getRawParameterValue ("tempo");
}

bool BreakbeatProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != AudioChannelSet::stereo() && out != AudioChannelSet::mono()) return false;
    return l.getMainInputChannelSet() == out;
}

void BreakbeatProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    ownRunning = false;
    ownPpq = 0.0;
    ownSilence = 0;
    songValid = false;
    lastHostSample = -1;
    sawHostPlaying = false;
}

void BreakbeatProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);

    bb::Transport t;
    bool hostTransport = false;
    double hostTempo = 0.0;
    bool hostRestarted = false, hostStopped = false;

    // A host that stops calling the plugin while it is stopped (some trackers): a pause in the
    // calls of more than a quarter of a second also means "started again".
    {
        const double now = Time::getMillisecondCounterHiRes();
        const double expectedMs = 1000.0 * n / jmax (8000.0, getSampleRate());
        if (lastCallMs > 0.0 && now - lastCallMs > expectedMs + 250.0) hostRestarted = true;
        lastCallMs = now;
    }
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            // Even hosts that send no tempo usually say where they are (in samples) and whether they
            // play. Use that to notice play / stop / start again, so the steps restart at row 0.
            if (auto smp = pos->getTimeInSamples())
            {
                if (lastHostSample >= 0 && (*smp < lastHostSample || *smp > lastHostSample + 4 * (int64) n + 4096))
                    hostRestarted = true;                              // the host jumped back (play from the start) or ahead
                lastHostSample = *smp;
            }
            if (pos->getIsPlaying()) sawHostPlaying = true;
            else if (sawHostPlaying) hostStopped = true;                // only trusted once the host has said "playing"
            if (auto bpmValue = pos->getBpm()) hostTempo = *bpmValue;
            if (hostTempo > 1.0 && (pos->getPpqPosition().hasValue() || pos->getTimeInSeconds().hasValue()))
            {
                hostTransport = true;
                t.playing = pos->getIsPlaying();
                t.bpm = hostTempo;
                if (auto p = pos->getPpqPosition()) t.ppq = *p;
                else t.ppq = *pos->getTimeInSeconds() * t.bpm / 60.0;
            }
        }

    const double sr = jmax (8000.0, getSampleRate());
    const bool syncSound = syncParam->load() > 0.5f;

    if (hostTransport && ! syncSound)
    {
        // SYNC SONG: follow the host's song position. Some hosts (trackers especially) only
        // update the position now and then, or in coarse steps: keep our own smooth clock that
        // runs a little faster or slower to catch up with the host (never jumps, so no crunch),
        // and jump only when the host really jumps (start, loop, moving the song position).
        const double rate = jmax (1.0, t.bpm) / 60.0 / sr;           // beats per sample
        if (! t.playing) songValid = false;
        else
        {
            const double err = t.ppq - songPpq;
            double r = rate;
            if (! songValid || std::abs (err) > 1.0) songPpq = t.ppq;     // start, or the host jumped
            else r = rate * (1.0 + jlimit (-0.02, 0.02, err / (rate * sr) ));   // catch up over about a second
            songValid = true;
            t.ppq = songPpq;
            t.beatsPerSample = r;
            songPpq += n * r;
        }
    }
    else
    {
        // SYNC SOUND, or a host that sends no song position: our own clock. The steps start
        // with the first sound after a silence (row 0 = the first note) and stop again after
        // half a second of silence, or when the host stops / starts again. Tempo: the host's when
        // it sends one, otherwise TEMPO.
        songValid = false;
        const double bpm = hostTempo > 1.0 ? hostTempo : jlimit (40.0, 300.0, (double) tempoParam->load());
        const double spb = sr * 60.0 / bpm;
        constexpr float threshold = 0.0005f;      // about -66 dB
        int onset = -1;
        float peak = 0.0f;
        for (int c = 0; c < jmin (2, getTotalNumInputChannels()); ++c)
        {
            const float* d = buffer.getReadPointer (c);
            for (int i = 0; i < n; ++i)
            {
                const float a = std::abs (d[i]);
                peak = jmax (peak, a);
                if (a > threshold && (onset < 0 || i < onset)) onset = i;
            }
        }
        if (hostRestarted || hostStopped) ownRunning = false;       // wait for the first sound again
        if (! ownRunning && onset >= 0 && ! hostStopped)
        {
            ownRunning = true;
            ownPpq = -onset / spb;                // beat 0 = the first sound
            ownSilence = 0;
        }
        if (ownRunning)
        {
            ownSilence = peak > threshold ? 0 : ownSilence + n;
            if (ownSilence > (int) (sr * 0.5)) ownRunning = false;    // half a second of silence: the next sound is row 0
        }
        t.playing = ownRunning;
        t.bpm = bpm;
        t.ppq = ownPpq;
        if (ownRunning) ownPpq += n / spb;
        hostTransport = false;
    }
    hostBpm = t.bpm;
    hostPlaying = t.playing;
    usingOwnClock = ! hostTransport;

    bb::Settings s;
    for (int i = 0; i < bb::kNumSteps; ++i) s.steps[(size_t) i] = jlimit (0, (int) bb::NumActions - 1, roundToInt (stepParams[(size_t) i]->load()));
    s.lengthBeats = lengthBeats[jlimit (0, 3, roundToInt (lengthParam->load()))];
    s.stepsBeats  = jmin (s.lengthBeats, lengthBeats[jlimit (0, 3, roundToInt (stepsParam->load()))]);
    s.bits     = roundToInt (bitsParam->load());
    s.rate     = roundToInt (rateParam->load());
    s.smoothMs = jmax (0.2, (double) smoothParam->load());
    s.mix      = (float) mixParam->load() / 100.0f;
    s.readAhead = aheadParam->load() > 0.5f;

    // READ AHEAD: tell the host how late the sound is, so it lines everything up
    const int lat = engine.latencyFor (t.bpm, s);
    if (lat != getLatencySamples()) setLatencySamples (lat);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    engine.process (L, R, n, t, s);
    currentStep = engine.currentStep();
}

// ---- patterns / programs ----------------------------------------------------------

const std::vector<BreakbeatProcessor::Pattern>& BreakbeatProcessor::patterns()
{
    using namespace bb;
    static const std::vector<Pattern> list = {
        { "CLEAR",          { Thru, Thru, Thru, Thru, Thru, Thru, Thru, Thru } },
        { "AMEN SHUFFLE",   { Q1, Q2, Q3, Q3, Q1, Q4, Q2, Rev } },
        { "CHOP",           { Q1, Q2, Q1, Q4, Q3, Q2, Q1, Q4 } },
        { "STUTTER END",    { Thru, Thru, Thru, Thru, Thru, Thru, Q4, Q4 } },
        { "DOUBLE KICK",    { Q1, Q1, Thru, Thru, Q1, Q1, Thru, Thru } },
        { "REVERSE TAIL",   { Thru, Thru, Thru, Thru, Thru, Thru, Rev, Rev } },
        { "BACKWARDS",      { Rev, Rev, Rev, Rev, Rev, Rev, Rev, Rev } },
        { "GATE",           { Thru, Stop, Thru, Stop, Thru, Stop, Thru, Bit } },
        { "BIT BREAK",      { Thru, Thru, Bit, Thru, Thru, Thru, Bit, Bit } },
        { "DROP OUT",       { Thru, Thru, Thru, Thru, Thru, Thru, Stop, Stop } },
        { "ROLL",           { Thru, Thru, Thru, Thru, Q3, Q3, Q4, Q4 } },
        { "JUNGLE",         { Q1, Q3, Q2, Rev, Q1, Bit, Q4, Q2 } },
    };
    return list;
}

void BreakbeatProcessor::applyPattern (const std::array<int, bb::kNumSteps>& steps)
{
    for (int i = 0; i < bb::kNumSteps; ++i)
        if (auto* p = params.getParameter (stepId (i)))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) steps[(size_t) i]));
}

int BreakbeatProcessor::getNumPrograms() { return (int) patterns().size(); }

void BreakbeatProcessor::setCurrentProgram (int index)
{
    if (! isPositiveAndBelow (index, (int) patterns().size())) return;
    currentProgram = index;
    applyPattern (patterns()[(size_t) index].steps);
}

const String BreakbeatProcessor::getProgramName (int index)
{
    return isPositiveAndBelow (index, (int) patterns().size()) ? String (patterns()[(size_t) index].name) : String();
}

// ---- state ------------------------------------------------------------------------

void BreakbeatProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = params.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void BreakbeatProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (params.state.getType()))
            params.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessorEditor* BreakbeatProcessor::createEditor() { return new BreakbeatEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BreakbeatProcessor(); }

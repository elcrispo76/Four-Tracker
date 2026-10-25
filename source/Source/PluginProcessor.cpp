// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DemoContent.h"
#include "FxHost.h"

using namespace ft;

namespace ids
{
    static const juce::Identifier root ("FOURTRACKER"), patterns ("PATTERNS"), pat ("P"), insts ("INSTRUMENTS"), inst ("I"),
        mixer ("MIXER"), chan ("CH"), patternSet ("PATTERNSET"), module ("MODULE");
}

FourTrackerProcessor::FourTrackerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formats.registerBasicFormats();

    createDemoInstruments (instruments);
    createDemoPatterns (engine);
    for (int i = 1; i < kNumInstruments; ++i)
        engine.publishInstrument (i, instruments[(size_t) i].makeSnapshot());


   #if FOURTRACKER_FX_HOST
    // VST effects only in the standalone app: inside a DAW, use the DAW's own effects
    if (wrapperType == wrapperType_Standalone)
    {
        fx = std::make_unique<FxHost>();
        engine.channelInsert = [this] (int ch, float* l, float* r, int n) { fx->process (ch, l, r, n); };
        engine.channelRoute = [this] (int ch) { return fx->getRoute (ch); };
    }
   #endif

    startTimer (1000);
    knobPump.tick = [this] { applyMidiKnobs(); };
    knobPump.startTimerHz (60);
}

FourTrackerProcessor::~FourTrackerProcessor()
{
    stopTimer();
    knobPump.stopTimer();
   #if FOURTRACKER_FX_HOST
    engine.channelInsert = nullptr;
    engine.channelRoute = nullptr;
    if (fx != nullptr) { fx->saveSettings(); fx.reset(); }
   #endif
}

void FourTrackerProcessor::clearAllEffects()
{
   #if FOURTRACKER_FX_HOST
    if (fx != nullptr) fx->clearAll();
   #endif
}

FxHost* FourTrackerProcessor::getFxHost() const noexcept
{
   #if FOURTRACKER_FX_HOST
    return fx.get();
   #else
    return nullptr;
   #endif
}

void FourTrackerProcessor::timerCallback()
{
    engine.collectGarbage();
}

void FourTrackerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
   #if FOURTRACKER_FX_HOST
    if (fx != nullptr) fx->prepare (sampleRate, samplesPerBlock);
   #endif
}

bool FourTrackerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void FourTrackerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    double hostBpm = -1.0;
    if (wrapperType != wrapperType_Standalone)
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm())
                    hostBpm = *b;

    engine.process (buffer, midi, hostBpm);
    midi.clear();
    midi.swapWith (engine.midiOut);       // notes from MIDI instruments go out (device or DAW)
    engine.midiOut.clear();
}

juce::AudioProcessorEditor* FourTrackerProcessor::createEditor()
{
    return new FourTrackerEditor (*this);
}

// ===========================================================================
// instruments
// ===========================================================================

void FourTrackerProcessor::instrumentChanged (int slot)
{
    if (slot <= 0 || slot >= kNumInstruments) return;
    auto& in = instruments[(size_t) slot];
    in.sanitise();
    engine.publishInstrument (slot, in.makeSnapshot());
    sendChangeMessage();
}

// ---------------------------------------------------------------------------
// synth MIDI LEARN
// ---------------------------------------------------------------------------

namespace
{
    // MIDI LEARN targets: 0..7 = SYNTH 1's sliders, kSynth2Knob + n = SYNTH 2's knob n
    bool validKnobTarget (int param)
    {
        return juce::isPositiveAndBelow (param, (int) ft::numSynthParams)
            || juce::isPositiveAndBelow (param - FourTrackerProcessor::kSynth2Knob, (int) ft::s2NumParams);
    }
    int* knobSlot (ft::InstrumentParams& p, int param)
    {
        if (param >= FourTrackerProcessor::kSynth2Knob) return &p.cc2[(size_t) (param - FourTrackerProcessor::kSynth2Knob)];
        return &p.cc[(size_t) param];
    }
    juce::String knobTargetName (int param)
    {
        return param >= FourTrackerProcessor::kSynth2Knob ? juce::String (ft::synth2::name (param - FourTrackerProcessor::kSynth2Knob))
                                                          : juce::String (ft::synthParamName (param));
    }
}

void FourTrackerProcessor::startSynthLearn (int slot, int param)
{
    if (slot <= 0 || slot >= kNumInstruments || ! validKnobTarget (param)) return;
    int cc = 0, v = 0;
    while (engine.popController (cc, v)) {}      // only a knob moved from now on counts
    learnSlot = slot;
    learnParam = param;
    ++knobRevision;
}

void FourTrackerProcessor::cancelSynthLearn()
{
    if (learnSlot < 0) return;
    learnSlot = learnParam = -1;
    ++knobRevision;
}

void FourTrackerProcessor::forgetSynthKnob (int slot, int param)
{
    if (slot <= 0 || slot >= kNumInstruments || ! validKnobTarget (param)) return;
    *knobSlot (instruments[(size_t) slot].params, param) = -1;
    instrumentChanged (slot);
    ++knobRevision;
}

void FourTrackerProcessor::applyMidiKnobs()
{
    int cc = 0, value = 0;
    std::array<int, 128> latest;              // only the last value of each knob counts
    latest.fill (-1);
    bool any = false;
    while (engine.popController (cc, value))
    {
        if (learnSlot > 0)
        {
            // one knob moves one slider: take it away from wherever it was
            for (auto& in : instruments)
            {
                for (auto& c : in.params.cc)  if (c == cc) c = -1;
                for (auto& c : in.params.cc2) if (c == cc) c = -1;
            }
            auto& in = instruments[(size_t) learnSlot];
            *knobSlot (in.params, learnParam) = cc;
            knobMessage = "CC " + juce::String (cc) + " NOW MOVES " + knobTargetName (learnParam)
                        + " OF SLOT " + juce::String::toHexString (learnSlot).paddedLeft ('0', 2).toUpperCase();
            const int slot = learnSlot;
            learnSlot = learnParam = -1;
            instrumentChanged (slot);
            ++knobRevision;
            continue;   // the knob that was just learned doesn't jump the value yet
        }
        latest[(size_t) cc] = value;
        any = true;
    }
    if (! any) return;

    bool moved = false;
    for (int slot = 1; slot < kNumInstruments; ++slot)
    {
        auto& in = instruments[(size_t) slot];
        bool changed = false;
        if (in.isSynth())
            for (int p = 0; p < ft::numSynthParams; ++p)
            {
                const int c = in.params.cc[(size_t) p];
                if (c < 0 || latest[(size_t) c] < 0) continue;
                int& ref = ft::synthParamRef (in.params, p);
                const int v = ft::synthParamFromMidi (p, latest[(size_t) c]);
                if (v != ref) { ref = v; changed = true; }
            }
        else if (in.isSynth2())
            for (int p = 0; p < ft::s2NumParams; ++p)
            {
                const int c = in.params.cc2[(size_t) p];
                if (c < 0 || latest[(size_t) c] < 0) continue;
                int& ref = in.params.s2[(size_t) p];
                const int v = juce::roundToInt (latest[(size_t) c] * ft::synth2::maxValue (p) / 127.0);
                if (v != ref) { ref = v; changed = true; }
            }
        else continue;
        if (changed)
        {
            in.sanitise();
            engine.publishInstrument (slot, in.makeSnapshot());   // heard at once; no full UI refresh
            moved = true;
        }
    }
    if (moved) ++knobRevision;
}

juce::String FourTrackerProcessor::loadInstrument (int slot, const juce::File& file)
{
    if (slot <= 0 || slot >= kNumInstruments) return "Pick an instrument slot from 01 upwards";
    Instrument tmp;
    auto err = tmp.loadFromFile (file, formats);
    if (err.isNotEmpty()) return err;
    instruments[(size_t) slot] = tmp;
    instrumentChanged (slot);
    return {};
}

void FourTrackerProcessor::discardInstrument (int slot)
{
    if (slot <= 0 || slot >= kNumInstruments) return;
    instruments[(size_t) slot].clear();
    instrumentChanged (slot);
}

bool FourTrackerProcessor::auditionFile (const juce::File& file, bool play)
{
    if (file != auditionSource || audition.isEmpty())
    {
        ft::Instrument tmp;
        if (! file.existsAsFile() || tmp.loadFromFile (file, formats).isNotEmpty())
        {
            stopAudition();
            audition.clear();
            auditionSource = juce::File();
            engine.publishAudition (nullptr);
            return false;
        }
        tmp.params.loop = false;     // a browser preview always plays once, to the end
        tmp.params.volume = 64;
        tmp.sanitise();
        audition = tmp;
        auditionSource = file;
        engine.publishAudition (audition.makeSnapshot());
    }
    if (play) engine.requestAudition();
    return true;
}

void FourTrackerProcessor::stopAudition()
{
    engine.requestStopPreview();
}

juce::String FourTrackerProcessor::getSupportedAudioWildcard() const
{
    // + Amiga samples: IFF 8SVX, raw 8-bit, and files without an extension ("*.")
    return formats.getWildcardForAllFormats() + ";*.iff;*.8svx;*.8sv;*.raw;*.smp;*.sam;*.snd;*.sample;*.";
}

// ===========================================================================
// serialisation
// ===========================================================================

juce::ValueTree FourTrackerProcessor::patternsToTree() const
{
    juce::ValueTree t (ids::patterns);
    auto& eng = const_cast<TrackerEngine&> (engine);
    for (int p = 0; p < kMaxPatterns; ++p)
    {
        const auto& pat = eng.getPattern (p);
        const int rows = pat.getNumRows();
        if (pat.isEmpty() && rows == kDefaultRows) continue;

        const int chans = juce::jlimit (1, kNumChannels, engine.numChannels.load());
        juce::MemoryBlock mb ((size_t) (rows * chans * 4));
        auto* d = static_cast<uint8_t*> (mb.getData());
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < chans; ++c)
            {
                const auto& cell = pat.at (r, c);
                *d++ = cell.note; *d++ = cell.inst; *d++ = cell.fx; *d++ = cell.param;
            }

        juce::ValueTree pt (ids::pat);
        pt.setProperty ("index", p, nullptr);
        pt.setProperty ("rows", rows, nullptr);
        pt.setProperty ("chans", chans, nullptr);
        pt.setProperty ("data", mb, nullptr);
        t.appendChild (pt, nullptr);
    }
    return t;
}

void FourTrackerProcessor::patternsFromTree (const juce::ValueTree& t)
{
    const juce::ScopedLock sl (engine.getDataLock());
    for (int p = 0; p < kMaxPatterns; ++p)
    {
        engine.getPattern (p).clear();
        engine.getPattern (p).numRows = kDefaultRows;
    }

    for (auto pt : t)
    {
        if (! pt.hasType (ids::pat)) continue;
        const int p = pt.getProperty ("index", -1);
        if (p < 0 || p >= kMaxPatterns) continue;

        auto& pat = engine.getPattern (p);
        const int rows = juce::jlimit (1, kMaxRows, (int) pt.getProperty ("rows", kDefaultRows));
        pat.numRows = rows;

        if (auto* mb = pt.getProperty ("data").getBinaryData())
        {
            const auto* d = static_cast<const uint8_t*> (mb->getData());
            const int chans = juce::jlimit (1, kNumChannels, (int) pt.getProperty ("chans", 4));   // old files: 4
            const int cells = juce::jmin (rows * chans, (int) mb->getSize() / 4);
            for (int i = 0; i < cells; ++i)
            {
                auto& cell = pat.at (i / chans, i % chans);
                cell.note = d[0] <= kNoteOff ? d[0] : 0;
                cell.inst = d[1] < kNumInstruments ? d[1] : 0;
                cell.fx = d[2];
                cell.param = d[3];
                d += 4;
            }
        }
    }
}

juce::ValueTree FourTrackerProcessor::instrumentsToTree() const
{
    juce::ValueTree t (ids::insts);
    for (int i = 1; i < kNumInstruments; ++i)
    {
        const auto& in = instruments[(size_t) i];
        if (in.isEmpty()) continue;

        juce::ValueTree it (ids::inst);
        const auto& p = in.params;
        it.setProperty ("slot", i, nullptr);
        it.setProperty ("name", in.name, nullptr);
        it.setProperty ("path", in.sourcePath, nullptr);
        it.setProperty ("volume", p.volume, nullptr);
        it.setProperty ("transpose", p.transpose, nullptr);
        it.setProperty ("finetune", p.fineTune, nullptr);
        it.setProperty ("loop", p.loop, nullptr);
        it.setProperty ("pingpong", p.pingPong, nullptr);
        it.setProperty ("loopBackward", p.loopBackward, nullptr);
        it.setProperty ("reverse", p.reverse, nullptr);
        it.setProperty ("start", p.start, nullptr);
        it.setProperty ("end", p.end, nullptr);
        it.setProperty ("loopStart", p.loopStart, nullptr);
        it.setProperty ("loopEnd", p.loopEnd, nullptr);
        if (p.midi)
        {
            it.setProperty ("midi", true, nullptr);
            it.setProperty ("midiCh", p.midiChannel, nullptr);
            it.setProperty ("midiProg", p.midiProgram, nullptr);
        }
        if (p.synth)
        {
            it.setProperty ("synth", true, nullptr);
            it.setProperty ("wave", p.synthWave, nullptr);
            it.setProperty ("attack", p.attack, nullptr);
            it.setProperty ("decay", p.decay, nullptr);
            it.setProperty ("sustain", p.sustain, nullptr);
            it.setProperty ("release", p.release, nullptr);
            it.setProperty ("cutoff", p.cutoff, nullptr);
            it.setProperty ("resonance", p.resonance, nullptr);
            it.setProperty ("distortion", p.distortion, nullptr);
            it.setProperty ("accent", p.accent, nullptr);
            juce::StringArray ccs;
            for (auto c : p.cc) ccs.add (juce::String (c));
            it.setProperty ("knobs", ccs.joinIntoString (","), nullptr);
        }
        if (p.synth2)
        {
            it.setProperty ("synth2", true, nullptr);
            it.setProperty ("s2", ft::synth2::toString (p.s2), nullptr);
            juce::StringArray ccs;
            for (auto c : p.cc2) ccs.add (juce::String (c));
            it.setProperty ("knobs2", ccs.joinIntoString (","), nullptr);
        }
        it.setProperty ("rate", in.data->sampleRate, nullptr);
        it.setProperty ("bits", in.data->bitDepth, nullptr);
        it.setProperty ("channels", in.data->getNumChannels(), nullptr);
        it.setProperty ("length", in.data->getLength(), nullptr);

        const int len = in.data->getLength(), nch = in.data->getNumChannels();
        juce::MemoryBlock mb ((size_t) len * (size_t) nch * sizeof (float));
        for (int c = 0; c < nch; ++c)
            mb.copyFrom (in.data->buffer.getReadPointer (c), (size_t) c * (size_t) len * sizeof (float), (size_t) len * sizeof (float));
        it.setProperty ("pcm", mb, nullptr);
        t.appendChild (it, nullptr);
    }
    return t;
}

void FourTrackerProcessor::instrumentsFromTree (const juce::ValueTree& t)
{
    for (auto& in : instruments) in.clear();

    for (auto it : t)
    {
        if (! it.hasType (ids::inst)) continue;
        const int slot = it.getProperty ("slot", 0);
        if (slot <= 0 || slot >= kNumInstruments) continue;

        const int len = it.getProperty ("length", 0), nch = juce::jlimit (1, 2, (int) it.getProperty ("channels", 1));
        auto* mb = it.getProperty ("pcm").getBinaryData();
        if (mb == nullptr || len <= 0 || mb->getSize() < (size_t) len * (size_t) nch * sizeof (float)) continue;

        SampleData::Ptr d = new SampleData();
        d->sampleRate = it.getProperty ("rate", 44100.0);
        d->bitDepth = it.getProperty ("bits", 16);
        d->buffer.setSize (nch, len);
        for (int c = 0; c < nch; ++c)
            memcpy (d->buffer.getWritePointer (c), static_cast<const char*> (mb->getData()) + (size_t) c * (size_t) len * sizeof (float), (size_t) len * sizeof (float));

        auto& in = instruments[(size_t) slot];
        in.data = d;
        in.name = it.getProperty ("name").toString();
        in.sourcePath = it.getProperty ("path").toString();
        auto& p = in.params;
        p.volume = it.getProperty ("volume", 64);
        p.transpose = it.getProperty ("transpose", 0);
        p.fineTune = it.getProperty ("finetune", 0);
        p.loop = it.getProperty ("loop", false);
        p.pingPong = it.getProperty ("pingpong", false);
        p.loopBackward = it.getProperty ("loopBackward", false);
        p.reverse = it.getProperty ("reverse", false);
        p.start = it.getProperty ("start", 0);
        p.end = it.getProperty ("end", len - 1);
        p.loopStart = it.getProperty ("loopStart", 0);
        p.loopEnd = it.getProperty ("loopEnd", len - 1);
        p.synth = it.getProperty ("synth", false);
        p.synthWave = it.getProperty ("wave", 0);
        p.attack = it.getProperty ("attack", 2);
        p.decay = it.getProperty ("decay", 250);
        p.sustain = it.getProperty ("sustain", 40);
        p.release = it.getProperty ("release", 200);
        p.cutoff = it.getProperty ("cutoff", 127);
        p.resonance = it.getProperty ("resonance", 0);
        p.distortion = it.getProperty ("distortion", 0);
        p.accent = it.getProperty ("accent", 0);
        {
            juce::StringArray ccs;
            ccs.addTokens (it.getProperty ("knobs", "").toString(), ",", "");
            for (int k = 0; k < (int) p.cc.size(); ++k) p.cc[(size_t) k] = k < ccs.size() ? ccs[k].getIntValue() : -1;
        }
        p.synth2 = it.getProperty ("synth2", false);
        if (p.synth2)
        {
            p.s2 = ft::synth2::fromString (it.getProperty ("s2", "").toString());
            juce::StringArray ccs;
            ccs.addTokens (it.getProperty ("knobs2", "").toString(), ",", "");
            for (int k = 0; k < (int) p.cc2.size(); ++k) p.cc2[(size_t) k] = k < ccs.size() ? ccs[k].getIntValue() : -1;
        }
        p.midi = it.getProperty ("midi", false);
        p.midiChannel = it.getProperty ("midiCh", 1);
        p.midiProgram = it.getProperty ("midiProg", 0);
        in.sanitise();
    }

    for (int i = 1; i < kNumInstruments; ++i)
        engine.publishInstrument (i, instruments[(size_t) i].makeSnapshot());
}

// ---------------------------------------------------------------------------
// The "song": everything a module file holds (patterns, samples, instrument
// settings, speed/BPM, loop, mixer). App settings such as MIDI mapping are
// not part of it.
// ---------------------------------------------------------------------------

void FourTrackerProcessor::writeSong (juce::ValueTree& t) const
{
    t.setProperty ("speed", engine.speed.load(), nullptr);
    t.setProperty ("bpm", engine.bpm.load(), nullptr);
    t.setProperty ("loop", engine.loopPattern.load(), nullptr);
    t.setProperty ("selectedPattern", engine.selectedPattern.load(), nullptr);
    t.setProperty ("selectedInstrument", selectedInstrument, nullptr);
    t.setProperty ("channels", engine.numChannels.load(), nullptr);

    juce::ValueTree mixer (ids::mixer);
    for (int c = 0; c < kNumChannels; ++c)
    {
        juce::ValueTree ch (ids::chan);
        ch.setProperty ("vol", engine.mix[c].volume.load(), nullptr);
        ch.setProperty ("pan", engine.mix[c].pan.load(), nullptr);
        ch.setProperty ("pitch", engine.mix[c].pitch.load(), nullptr);
        ch.setProperty ("mute", engine.mix[c].mute.load(), nullptr);
        ch.setProperty ("solo", engine.mix[c].solo.load(), nullptr);
        mixer.appendChild (ch, nullptr);
    }
    t.appendChild (mixer, nullptr);
    t.appendChild (patternsToTree(), nullptr);
    t.appendChild (instrumentsToTree(), nullptr);
}

void FourTrackerProcessor::readSong (const juce::ValueTree& t)
{
    engine.requestStop();
    // modules from before 8 / 16 channels have no "channels": they are 4-channel songs
    engine.numChannels = normaliseChannels ((int) t.getProperty ("channels", 4));
    engine.speed = juce::jlimit (1, 31, (int) t.getProperty ("speed", 6));
    engine.bpm = juce::jlimit (32, 255, (int) t.getProperty ("bpm", 125));
    engine.loopPattern = (bool) t.getProperty ("loop", true);
    selectedInstrument = juce::jlimit (1, kNumInstruments - 1, (int) t.getProperty ("selectedInstrument", 1));

    auto mixer = t.getChildWithName (ids::mixer);
    for (int c = 0; c < kNumChannels; ++c)
    {
        auto ch = mixer.getChild (c);   // invalid tree -> defaults
        engine.mix[c].volume = juce::jlimit (0, 64, (int) ch.getProperty ("vol", 64));
        engine.mix[c].pan = juce::jlimit (-64, 64, (int) ch.getProperty ("pan", 0));
        engine.mix[c].pitch = juce::jlimit (-24, 24, (int) ch.getProperty ("pitch", 0));
        engine.mix[c].mute = (bool) ch.getProperty ("mute", false);
        engine.mix[c].solo = (bool) ch.getProperty ("solo", false);
        engine.lastInstrumentOnChannel[c] = 0;
    }

    patternsFromTree (t.getChildWithName (ids::patterns));
    instrumentsFromTree (t.getChildWithName (ids::insts));
    engine.requestPattern (juce::jlimit (0, kMaxPatterns - 1, (int) t.getProperty ("selectedPattern", 0)));
}

void FourTrackerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root (ids::root);
    root.setProperty ("version", 1, nullptr);
    writeSong (root);
    root.setProperty ("midiBase", engine.midiBaseNote.load(), nullptr);
    root.setProperty ("midiChannel", engine.midiChannel.load(), nullptr);
    root.setProperty ("switchMode", engine.switchMode.load(), nullptr);
    root.setProperty ("followHost", engine.followHostTempo.load(), nullptr);
    root.setProperty ("midiStartStop", engine.respondToMidiStartStop.load(), nullptr);
    root.setProperty ("master", engine.masterVolume.load(), nullptr);
    root.setProperty ("hex", hexMode, nullptr);

    juce::MemoryOutputStream mos (destData, false);
    root.writeToStream (mos);
}

void FourTrackerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto root = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! root.hasType (ids::root)) return;

    // The standalone app always starts with the demo song: only its settings
    // (MIDI mapping, master volume ...) are restored. A plugin restores the
    // whole song, because it belongs to the DAW project.
    const bool restoreSong = wrapperType != wrapperType_Standalone;

    engine.midiBaseNote = juce::jlimit (0, 127 - kMaxPatterns, (int) root.getProperty ("midiBase", 36));
    engine.midiChannel = juce::jlimit (0, 16, (int) root.getProperty ("midiChannel", 0));
    engine.switchMode = juce::jlimit (0, 3, (int) root.getProperty ("switchMode", 0));
    engine.followHostTempo = (bool) root.getProperty ("followHost", false);
    engine.respondToMidiStartStop = (bool) root.getProperty ("midiStartStop", true);
    engine.masterVolume = juce::jlimit (0, 100, (int) root.getProperty ("master", 80));
    hexMode = root.getProperty ("hex", true);

    if (restoreSong)
        readSong (root);
    sendChangeMessage();
}

// ===========================================================================
// new / demo / modules
// ===========================================================================

int FourTrackerProcessor::normaliseChannels (int n)
{
    return juce::jlimit (1, kNumChannels, n);
}

bool FourTrackerProcessor::trackHasNotes (int channel)
{
    for (int p = 0; p < kMaxPatterns; ++p)
        for (int r = 0; r < kMaxRows; ++r)
            if (! engine.getPattern (p).at (r, channel).isEmpty()) return true;
    return false;
}

void FourTrackerProcessor::setNumChannels (int n)
{
    n = normaliseChannels (n);
    engine.requestStop();
    {
        const juce::ScopedLock sl (engine.getDataLock());
        for (int p = 0; p < kMaxPatterns; ++p)
            for (int r = 0; r < kMaxRows; ++r)
                for (int c = n; c < kNumChannels; ++c)
                    engine.getPattern (p).at (r, c).clear();
        for (int c = n; c < kNumChannels; ++c) { engine.mix[c].solo = false; engine.mix[c].mute = false; }
        engine.numChannels = n;
    }
   #if FOURTRACKER_FX_HOST
    if (fx != nullptr)       // a removed track takes its effects with it
        for (int c = n; c < kNumChannels; ++c)
            for (int i = 0; i < 2; ++i)
                if (fx->hasPlugin (c, i)) fx->clearSlot (c, i);
    if (fx != nullptr)       // ...and its chaining, both from it and into it
        for (int c = 0; c < kNumChannels; ++c)
            if ((c >= n && fx->getRoute (c) != -1) || fx->getRoute (c) >= n) fx->setRoute (c, -1);
   #endif
    sendChangeMessage();
}

void FourTrackerProcessor::newPatternSet()
{
    engine.requestStop();
    {
        const juce::ScopedLock sl (engine.getDataLock());
        for (int p = 0; p < kMaxPatterns; ++p)
        {
            engine.getPattern (p).clear();
            engine.getPattern (p).numRows = kDefaultRows;
        }
    }

    for (int i = 1; i < kNumInstruments; ++i)
    {
        instruments[(size_t) i].clear();
        engine.publishInstrument (i, nullptr);
    }

    engine.speed = 6;
    engine.bpm = 125;
    engine.loopPattern = true;
    for (int c = 0; c < kNumChannels; ++c)
    {
        engine.mix[c].volume = 64;
        engine.mix[c].pan = 0;
        engine.mix[c].pitch = 0;
        engine.mix[c].mute = false;
        engine.mix[c].solo = false;
        engine.lastInstrumentOnChannel[c] = 0;
    }
    engine.requestPattern (0);
    selectedInstrument = 1;
    sendChangeMessage();
}

void FourTrackerProcessor::loadDemoSong()
{
    newPatternSet();
    {
        const juce::ScopedLock sl (engine.getDataLock());
        createDemoPatterns (engine);
    }
    createDemoInstruments (instruments);
    for (int i = 1; i < kNumInstruments; ++i)
        engine.publishInstrument (i, instruments[(size_t) i].makeSnapshot());
    sendChangeMessage();
}

juce::File FourTrackerProcessor::getUserFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Four Tracker");
}

void FourTrackerProcessor::ensureDemoFolder()
{
    const auto demoDir = getUserFolder().getChildFile ("Demo");
    const auto song = demoDir.getChildFile ("Demo Song.ftm");
    const auto samplesDir = demoDir.getChildFile ("Samples");
    if (song.existsAsFile() && samplesDir.isDirectory()) return;

    if (! samplesDir.createDirectory()) return;

    // a second processor that starts with the demo song (on the heap: it is too big for the stack on Windows)
    AudioProcessor::setTypeOfNextNewPlugin (AudioProcessor::wrapperType_Undefined);
    auto demoPtr = std::make_unique<FourTrackerProcessor>();
    auto& demo = *demoPtr;
    if (! song.existsAsFile())
        demo.saveModule (song);

    juce::WavAudioFormat wav;
    for (int i = 1; i < kNumInstruments; ++i)
    {
        const auto& in = demo.getInstrument (i);
        if (in.isEmpty()) continue;
        auto f = samplesDir.getChildFile (in.name.isNotEmpty() ? in.name : ("SAMPLE" + hex2 (i) + ".WAV"));
        if (! f.hasFileExtension ("wav")) f = f.withFileExtension ("wav");
        if (f.existsAsFile()) continue;

        std::unique_ptr<juce::OutputStream> out (new juce::FileOutputStream (f));
        if (! static_cast<juce::FileOutputStream*> (out.get())->openedOk()) continue;
        auto opts = juce::AudioFormatWriterOptions{}
                        .withSampleRate (in.data->sampleRate)
                        .withNumChannels (in.data->getNumChannels())
                        .withBitsPerSample (16);
        if (auto writer = wav.createWriterFor (out, opts))
            writer->writeFromAudioSampleBuffer (in.data->buffer, 0, in.data->getLength());
    }
}

static const char kModuleMagic[4]     = { 'F', 'T', 'M', 'D' };
static const char kPatternSetMagic[4] = { 'F', 'T', 'P', 'S' };

bool FourTrackerProcessor::saveModule (const juce::File& file)
{
    juce::ValueTree t (ids::module);
    t.setProperty ("version", 1, nullptr);
    t.setProperty ("name", file.getFileNameWithoutExtension(), nullptr);
    writeSong (t);

    juce::MemoryOutputStream raw;
    t.writeToStream (raw);

    const auto tmp = file.getSiblingFile (file.getFileName() + ".tmp");
    tmp.deleteFile();
    {
        juce::FileOutputStream fos (tmp);
        if (! fos.openedOk()) return false;
        fos.write (kModuleMagic, 4);
        {
            juce::GZIPCompressorOutputStream gz (fos, 6);
            gz.write (raw.getData(), raw.getDataSize());
            gz.flush();
        }
        fos.flush();
        if (fos.getStatus().failed()) return false;
    }
    return tmp.moveFileTo (file);   // replaces the old file only once the new one is complete
}

juce::String FourTrackerProcessor::loadModule (const juce::File& file)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb) || mb.getSize() < 4)
        return "Can't read " + file.getFileName();

    const auto* bytes = static_cast<const char*> (mb.getData());

    if (memcmp (bytes, kModuleMagic, 4) == 0)
    {
        juce::MemoryInputStream mis (bytes + 4, mb.getSize() - 4, false);
        juce::GZIPDecompressorInputStream gz (mis);
        juce::MemoryBlock unpacked;
        gz.readIntoMemoryBlock (unpacked);
        auto t = juce::ValueTree::readFromData (unpacked.getData(), unpacked.getSize());
        if (! t.hasType (ids::module))
            return file.getFileName() + " is damaged";
        readSong (t);
        sendChangeMessage();
        return {};
    }

    if (memcmp (bytes, kPatternSetMagic, 4) == 0)
    {
        // old pattern set: patterns + speed/BPM only, samples are left as they are
        auto t = juce::ValueTree::readFromData (bytes + 4, mb.getSize() - 4);
        if (! t.hasType (ids::patternSet))
            return file.getFileName() + " is damaged";
        engine.speed = juce::jlimit (1, 31, (int) t.getProperty ("speed", 6));
        engine.bpm = juce::jlimit (32, 255, (int) t.getProperty ("bpm", 125));
        patternsFromTree (t.getChildWithName (ids::patterns));
        sendChangeMessage();
        return {};
    }

    return file.getFileName() + " is not a Four Tracker module";
}

// ===========================================================================

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FourTrackerProcessor();
}

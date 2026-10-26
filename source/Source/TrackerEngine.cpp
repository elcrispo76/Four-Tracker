// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#include "TrackerEngine.h"

namespace ft
{

TrackerEngine::TrackerEngine()
{
    for (auto& p : *patterns) p.clear();
    for (auto& a : lastInstrumentOnChannel) a = 0;
    for (auto& a : channelLevel) a = 0;
    resetChannels (false);
}

void TrackerEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate > 0 ? sampleRate : 44100.0;
    chanScratchSize = juce::jmax (4096, maxBlockSize);
    midiOut.ensureSize (8192);
    chanScratch.assign ((size_t) (kNumChannels * 2 * chanScratchSize), 0.0f);
    smoothCoeff = (float) (1.0 - std::exp (-1.0 / (0.0015 * sr)));
    fadeStep    = (float) (1.0 / (0.004 * sr));
    monoScratch.assign (16384, 0.0f);

    // SYNTH 2 FX: up to 2.5 s of delay and 50 ms of chorus per channel
    s2tmpL.assign ((size_t) chanScratchSize, 0.0f);
    s2tmpR.assign ((size_t) chanScratchSize, 0.0f);
    for (auto& fx : s2fx)
    {
        fx.dl.assign ((size_t) (sr * 2.5) + 2, 0.0f);
        fx.dr.assign (fx.dl.size(), 0.0f);
        fx.cl.assign ((size_t) (sr * 0.05) + 4, 0.0f);
        fx.cr.assign (fx.cl.size(), 0.0f);
        fx.reverb.setSampleRate (sr);
        fx.clear();
    }
}

void TrackerEngine::Synth2Fx::clear()
{
    std::fill (dl.begin(), dl.end(), 0.0f);
    std::fill (dr.begin(), dr.end(), 0.0f);
    std::fill (cl.begin(), cl.end(), 0.0f);
    std::fill (cr.begin(), cr.end(), 0.0f);
    dw = cw = 0;
    dampL = dampR = 0.0f;
    reverb.reset();
    tail = 0;
}

double TrackerEngine::rowSeconds() const noexcept
{
    return juce::jmax (1, speed.load()) * samplesPerTick() / sr;
}

// ===========================================================================
// commands
// ===========================================================================

void TrackerEngine::push (Cmd c)
{
    const juce::SpinLock::ScopedLockType l (pushLock);
    int s1, n1, s2, n2;
    cmdFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0)      cmdBuffer[(size_t) s1] = c;
    else if (n2 > 0) cmdBuffer[(size_t) s2] = c;
    cmdFifo.finishedWrite (n1 + n2);
}

void TrackerEngine::handleCommand (const Cmd& c)
{
    switch (c.type)
    {
        case Cmd::StartStop:      if (playing) stopPlayback(); else startPlayback(); break;
        case Cmd::Start:          startPlayback(); break;
        case Cmd::Stop:           stopPlayback(); break;
        case Cmd::SelectPattern:  selectPattern (c.a); break;

        case Cmd::Preview:
            startPreviewVoice (previewVoice, c.a, c.b, c.c != 0, previewMidiNote, previewMidiChan, -1);
            break;

        case Cmd::JamOn:
            if (juce::isPositiveAndBelow (c.c, kNumChannels))
                startPreviewVoice (jamVoices[(size_t) c.c], c.a, c.b, false, jamMidiNote[(size_t) c.c], jamMidiChan[(size_t) c.c], c.c);
            break;

        case Cmd::JamOff:
            if (juce::isPositiveAndBelow (c.a, kNumChannels))
            {
                fadeOutVoice (jamVoices[(size_t) c.a]);
                midiNoteOff (jamMidiNote[(size_t) c.a], jamMidiChan[(size_t) c.a]);
            }
            break;

        case Cmd::StopPreview:    fadeOutVoice (previewVoice); midiNoteOff (previewMidiNote, previewMidiChan); break;
    }
}

void TrackerEngine::startPreviewVoice (Voice& v, int inst, int note, bool loopOnly, int& midiNote, int& midiChan, int channel)
{
    if (inst <= 0 || inst > kAuditionSlot) return;
    auto snap = audioSnapshots[(size_t) inst];
    if (snap == nullptr) return;

    if (v.active) fadeOutVoice (v);   // quick declick
    v.inst = snap;
    v.slot = inst;
    v.loopOnly = loopOnly;
    v.pos = v.loopOnly && snap->looping ? (double) snap->vLoopStart : 0.0;
    v.inLoop = v.loopOnly;
    v.dir = 1;
    v.active = true;
    v.fading = false;
    v.fade = 1.0f;
    startEnvelope (v);

    const bool silenced = channel >= 0 && isChannelSilenced (channel);
    midiNoteOff (midiNote, midiChan);
    if (snap->p.midi && ! silenced)
    {
        midiChan = snap->p.midiChannel;
        midiNote = juce::jlimit (0, 127, note - 1 + 12 + snap->p.transpose + (channel >= 0 ? mix[channel].pitch.load() : 0));
        int vel = snap->p.volume * 127 / 64;
        if (channel >= 0) vel = vel * mix[channel].volume.load() / 64;
        midiNoteOn (midiNote, midiChan, juce::jlimit (1, 127, vel), snap->p.midiProgram);
    }

    const double p = ((note - 1 + snap->p.transpose + (channel >= 0 ? mix[channel].pitch.load() : 0)) * 64.0) + snap->p.fineTune / 2.0;
    v.step = std::pow (2.0, (p - (kNativeNote - 1) * 64.0) / 768.0) * snap->data->sampleRate / sr;
    float g = 0.8f * (float) snap->p.volume / 64.0f;
    float gl = g, gr = g;
    if (channel >= 0)
    {
        // a CHORD note follows its track's VOL, PAN and MUTE / SOLO
        g *= silenced ? 0.0f : (float) mix[channel].volume.load() / 64.0f;
        const float pan = (float) juce::jlimit (0, 255, 128 + mix[channel].pan.load() * 2) / 255.0f;
        gl = g * (pan <= 0.5f ? 1.0f : 2.0f * (1.0f - pan));
        gr = g * (pan >= 0.5f ? 1.0f : 2.0f * pan);
    }
    v.gainL = v.targetL = gl;
    v.gainR = v.targetR = gr;
}

void TrackerEngine::handleMidi (const juce::MidiMessage& m)
{
    if (respondToMidiStartStop)
    {
        if (m.isMidiStart()) { startPlayback(); return; }
        if (m.isMidiStop())  { stopPlayback();  return; }
    }

    const int chan = midiChannel.load();

    // knobs: queued for the message thread (synth MIDI LEARN); 120-127 are channel mode messages
    if (m.isController() && m.getControllerNumber() < 120)
    {
        if (chan > 0 && m.getChannel() != chan) return;
        const auto w = ccFifo.write (1);
        if (w.blockSize1 > 0) ccBuffer[(size_t) w.startIndex1] = { (uint8_t) m.getControllerNumber(), (uint8_t) m.getControllerValue() };
        lastController = m.getControllerNumber();
        return;
    }

    if (! m.isNoteOn()) return;
    if (chan > 0 && m.getChannel() != chan) return;

    const int note = m.getNoteNumber();
    lastMidiNote = note;

    if (midiLearn.exchange (false))
    {
        midiBaseNote = juce::jlimit (0, 127 - kMaxPatterns, note);
        return;
    }

    const int base = midiBaseNote.load();
    if (note == base)
    {
        if (playing) stopPlayback(); else startPlayback();
    }
    else if (note > base && note <= base + kMaxPatterns)
    {
        selectPattern (note - base - 1);
    }
}

bool TrackerEngine::popController (int& cc, int& value) noexcept
{
    const auto r = ccFifo.read (1);
    if (r.blockSize1 <= 0) return false;
    cc = ccBuffer[(size_t) r.startIndex1].first;
    value = ccBuffer[(size_t) r.startIndex1].second;
    return true;
}

// ===========================================================================
// instruments
// ===========================================================================

void TrackerEngine::publishInstrument (int slot, InstrumentSnapshot::Ptr snap)
{
    if (slot <= 0 || slot >= kNumInstruments) return;
    publishSnapshot (slot, snap);
}

void TrackerEngine::publishSnapshot (int slot, InstrumentSnapshot::Ptr snap)
{
    InstrumentSnapshot::Ptr old;
    {
        const juce::SpinLock::ScopedLockType l (snapLock);
        old = snapshots[(size_t) slot];
        snapshots[(size_t) slot] = snap;
    }
    // Keep the old one alive until the audio thread has let go of it,
    // so it is never freed on the audio thread.
    if (old != nullptr) garbage.add (old);
}

void TrackerEngine::collectGarbage()
{
    for (int i = garbage.size(); --i >= 0;)
        if (garbage.getObjectPointerUnchecked (i)->getReferenceCount() == 1)
            garbage.remove (i);
}

// ===========================================================================
// transport
// ===========================================================================

void TrackerEngine::resetChannels (bool keepVoices)
{
    for (int i = 0; i < kNumChannels; ++i)
    {
        auto& cs = chans[i];
        Voice v = cs.voice, g = cs.ghost;
        auto inst = cs.inst;
        const int instNum = cs.instNum;
        int midiNote = cs.midiNote;
        const int midiChan = cs.midiChan;
        if (! keepVoices) midiNoteOff (midiNote, midiChan);
        cs = ChannelState();
        cs.midiNote = midiNote; cs.midiChan = midiChan;
        if (keepVoices) { cs.voice = v; cs.ghost = g; cs.inst = inst; cs.instNum = instNum; }
        else            { cs.voice = v; cs.ghost = g; fadeOutVoice (cs.voice); fadeOutVoice (cs.ghost); }
    }
}

void TrackerEngine::startPlayback()
{
    lfoClock = 0.0;     // SYNTH 2's tempo LFO starts with the pattern
    insertBase = 0.0;   // and so does the song position the effects see
    insertLastRow = 0.0;
    insertSamples = 0;
    int p = selectedPattern.load();
    const int q = queuedPattern.exchange (-1);
    if (q >= 0) p = q;

    pattern = juce::jlimit (0, kMaxPatterns - 1, p);
    selectedPattern = pattern;
    row = 0;
    tick = 0;
    patternDelay = 0;
    globalVolume = 64;
    breakPending = loopJumpPending = false;
    resetChannels (false);
    samplesToNextTick = 0.0;
    playPattern = pattern;
    playRow = 0;
    playing = true;
}

void TrackerEngine::stopPlayback()
{
    playing = false;
    queuedPattern = -1;
    for (auto& cs : chans) { fadeOutVoice (cs.voice); fadeOutVoice (cs.ghost); midiNoteOff (cs.midiNote, cs.midiChan); }
    row = 0;
    tick = 0;
    pattern = selectedPattern.load();
    playPattern = pattern;
    playRow = 0;
}

void TrackerEngine::selectPattern (int p)
{
    p = juce::jlimit (0, kMaxPatterns - 1, p);
    selectedPattern = p;

    if (! playing)
    {
        pattern = p;
        playPattern = p;
        queuedPattern = -1;
        return;
    }

    if (p == pattern)
    {
        queuedPattern = -1;    // pressing the current pattern cancels a pending switch
        return;
    }

    if ((SwitchMode) switchMode.load() == SwitchMode::Restart)
    {
        pattern = p;
        row = 0;
        tick = 0;
        patternDelay = 0;
        breakPending = loopJumpPending = false;
        samplesToNextTick = 0.0;
        queuedPattern = -1;
        playPattern = p;
        return;
    }

    queuedPattern = p;
}

double TrackerEngine::samplesPerTick() const noexcept
{
    double b = (double) bpm.load();
    if (followHostTempo && currentHostBpm > 0.0) b = currentHostBpm;
    b = juce::jlimit (32.0, 999.0, b);
    return sr * 2.5 / b;
}

// ===========================================================================
// sequencing
// ===========================================================================

void TrackerEngine::doTick()
{
    const int used = juce::jlimit (1, kNumChannels, numChannels.load());
    if (tick == 0)
        processRow();
    else
        for (int ch = 0; ch < used; ++ch)
            processChannelTick (ch);

    for (int ch = 0; ch < kNumChannels; ++ch)
    {
        updateVoiceTargets (ch);
        auto& cs = chans[ch];
        if (cs.midiPending)
        {
            cs.midiPending = false;
            const int vel = juce::jlimit (1, 127, cs.volume * mix[ch].volume.load() / 64 * 127 / 64);
            midiNoteOn (cs.midiNote, cs.midiChan, vel, cs.midiProgram);
        }
    }

    playRow = row;
    playPattern = pattern;

    if (++tick >= juce::jmax (1, speed.load()) * (1 + patternDelay))
    {
        tick = 0;
        patternDelay = 0;
        advanceRow();
    }
}

void TrackerEngine::processRow()
{
    const int used = juce::jlimit (1, kNumChannels, numChannels.load());
    for (int ch = 0; ch < used; ++ch)
        processChannelRowStart (ch);
}

void TrackerEngine::switchToQueued (bool keepPosition)
{
    const int q = queuedPattern.exchange (-1);
    if (q < 0) return;

    const int nextRow = row;   // already the row we are about to play
    pattern = q;
    const int len = (*patterns)[(size_t) pattern].getNumRows();
    row = keepPosition ? (nextRow % juce::jmax (1, len)) : 0;
    for (auto& cs : chans) { cs.loopRow = 0; cs.loopCount = 0; }
}

void TrackerEngine::advanceRow()
{
    const int len = (*patterns)[(size_t) pattern].getNumRows();

    if (loopJumpPending)
    {
        loopJumpPending = false;
        row = juce::jlimit (0, len - 1, loopJumpRow);
        return;
    }

    if (breakPending)
    {
        breakPending = false;
        if (queuedPattern.load() >= 0)        switchToQueued (false);
        else if (! loopPattern.load())        { stopPlayback(); return; }
        const int newLen = (*patterns)[(size_t) pattern].getNumRows();
        row = juce::jlimit (0, newLen - 1, breakRow);
        return;
    }

    const int next = row + 1;
    const bool atEnd = next >= len;

    if (queuedPattern.load() >= 0)
    {
        switch ((SwitchMode) switchMode.load())
        {
            case SwitchMode::LegatoNextRow:
                row = atEnd ? 0 : next;
                switchToQueued (true);
                return;

            case SwitchMode::LegatoNextBeat:
                row = atEnd ? 0 : next;
                if (atEnd || (next % 4) == 0) switchToQueued (true);
                return;

            case SwitchMode::EndOfPattern:
            case SwitchMode::Restart:
                if (atEnd) { row = 0; switchToQueued (false); }
                else       row = next;
                return;
        }
    }

    if (atEnd)
    {
        if (loopPattern.load()) row = 0;
        else                    stopPlayback();
        return;
    }

    row = next;
}

void TrackerEngine::processChannelRowStart (int ch)
{
    auto& cs = chans[ch];
    const auto& pat = (*patterns)[(size_t) pattern];
    cs.cell = (row < pat.getNumRows()) ? pat.at (row, ch) : Cell();

    cs.pitchOffset = 0;
    cs.volOffset = 0;
    cs.delayTick = -1;
    cs.cutTick = -1;
    cs.keyOffTick = -1;
    cs.tremorOn = true;

    const auto& c = cs.cell;
    if (c.fx == 0x0E && (c.param >> 4) == 0xD && (c.param & 15) > 0)
    {
        cs.delayTick = c.param & 15;   // EDx: whole row starts later
        return;
    }

    triggerCell (ch, c);
}

void TrackerEngine::triggerCell (int ch, const Cell& c)
{
    auto& cs = chans[ch];
    const uint8_t fx = c.fx, prm = c.param;
    const int hi = prm >> 4, lo = prm & 15;
    const bool tonePorta = (fx == 0x03 || fx == 0x05);

    auto noteToPitch = [&] (int note)
    {
        auto snap = audioSnapshots[(size_t) cs.instNum];
        const int transpose = snap != nullptr ? snap->p.transpose : 0;
        return ((note - 1 + transpose) * 64.0) + cs.fineTune / 2.0;
    };

    if (c.inst > 0 && c.inst < kNumInstruments)
    {
        cs.instNum = c.inst;
        lastInstrumentOnChannel[ch] = c.inst;
        if (auto snap = audioSnapshots[(size_t) c.inst])
        {
            cs.volume = snap->p.volume;
            cs.fineTune = snap->p.fineTune;
        }
    }

    if (c.note >= 1 && c.note <= kNoteMax)
    {
        const double target = noteToPitch (c.note);

        if (tonePorta && cs.voice.active && ! cs.voice.fading)
        {
            cs.targetPitch = target;
        }
        else
        {
            cs.note = c.note;
            cs.inst = audioSnapshots[(size_t) cs.instNum];
            cs.pitch = cs.targetPitch = target;
            cs.keyedOff = false;
            if (cs.vibWave < 4)  cs.vibPos = 0;
            if (cs.tremWave < 4) cs.tremPos = 0;
            cs.retrigCount = 0;
            cs.tremorPos = 0;

            int offset = 0;
            if (fx == 0x09)
            {
                if (prm) cs.mOffset = prm;
                offset = cs.mOffset * 256;
            }
            triggerVoice (cs, offset);
        }
    }
    else if (c.note == kNoteOff)
    {
        releaseChannel (cs);
    }

    switch (fx)
    {
        case 0x01: if (prm) cs.mPortaUp = prm; break;
        case 0x02: if (prm) cs.mPortaDown = prm; break;
        case 0x03: if (prm) cs.mTonePorta = prm; break;
        case 0x04: if (hi) cs.vibSpeed = hi; if (lo) cs.vibDepth = lo; break;
        case 0x05:
        case 0x06:
        case 0x0A: if (prm) cs.mVolSlide = prm; break;
        case 0x07: if (hi) cs.tremSpeed = hi; if (lo) cs.tremDepth = lo; break;
        case 0x08: cs.pan = prm; break;

        case 0x0B:
            if (prm < kMaxPatterns) { queuedPattern = prm; selectedPattern = prm; }
            breakPending = true;
            breakRow = 0;
            break;

        case 0x0C: cs.volume = juce::jmin ((int) prm, 64); break;

        case 0x0D:
            breakPending = true;
            breakRow = hi * 10 + lo;
            break;

        case 0x0E:
            switch (hi)
            {
                case 0x1: if (lo) cs.mFinePortaUp = (uint8_t) lo;   cs.pitch += cs.mFinePortaUp * 4; break;
                case 0x2: if (lo) cs.mFinePortaDown = (uint8_t) lo; cs.pitch -= cs.mFinePortaDown * 4; break;
                case 0x4: cs.vibWave = lo; break;
                case 0x5: cs.fineTune = (lo - 8) * 16; break;
                case 0x6:
                    if (lo == 0) cs.loopRow = row;
                    else
                    {
                        if (cs.loopCount == 0)      { cs.loopCount = lo; loopJumpPending = true; loopJumpRow = cs.loopRow; }
                        else if (--cs.loopCount > 0) { loopJumpPending = true; loopJumpRow = cs.loopRow; }
                    }
                    break;
                case 0x7: cs.tremWave = lo; break;
                case 0x8: cs.pan = lo * 17; break;
                case 0xA: if (lo) cs.mFineVolUp = (uint8_t) lo;   cs.volume += cs.mFineVolUp; break;
                case 0xB: if (lo) cs.mFineVolDown = (uint8_t) lo; cs.volume -= cs.mFineVolDown; break;
                case 0xC: if (lo == 0) cs.volume = 0; else cs.cutTick = lo; break;
                case 0xE: if (patternDelay == 0) patternDelay = lo; break;
                default: break;
            }
            break;

        case 0x0F:
            if (prm == 0) break;
            if (prm < 0x20) speed = prm; else bpm = prm;
            break;

        case 0x10: globalVolume = juce::jmin ((int) prm, 64); break;
        case 0x11: if (prm) cs.mGlobalSlide = prm; break;
        case 0x14:
            if (prm == 0) releaseChannel (cs);
            else cs.keyOffTick = prm;
            break;
        case 0x19: if (prm) cs.mPanSlide = prm; break;
        case 0x1B:
            if (hi) cs.mRetrig = (uint8_t) ((cs.mRetrig & 0x0f) | (hi << 4));
            if (lo) cs.mRetrig = (uint8_t) ((cs.mRetrig & 0xf0) | lo);
            break;
        case 0x1D: if (prm) cs.mTremor = prm; break;
        case 0x21:
            if (hi == 1) { if (lo) cs.mXFineUp = (uint8_t) lo;   cs.pitch += cs.mXFineUp; }
            if (hi == 2) { if (lo) cs.mXFineDown = (uint8_t) lo; cs.pitch -= cs.mXFineDown; }
            break;
        // Four Tracker filter commands, 0X00-0X64 cutoff and 0Z00-0Z64 resonance (64 = open / most).
        // They stay on the channel until changed or playback restarts.
        case fxFilterCutoff:    cs.fxCutoff = filterCommandValue (prm) * 127 / 64; break;
        case fxFilterResonance: cs.fxReso   = filterCommandValue (prm) * 127 / 64; break;
        case fxDistortion:      cs.fxDrive  = filterCommandValue (prm); break;     // 0Yvv
        case fxAccent:          cs.fxAccent = filterCommandValue (prm); break;     // 0Vvv
        default: break;
    }

    cs.volume = juce::jlimit (0, 64, cs.volume);
    cs.pan = juce::jlimit (0, 255, cs.pan);
}

void TrackerEngine::processChannelTick (int ch)
{
    auto& cs = chans[ch];
    const auto& c = cs.cell;
    const uint8_t fx = c.fx, prm = c.param;
    const int hi = prm >> 4, lo = prm & 15;
    const int spd = juce::jmax (1, speed.load());
    const int t = tick % spd;

    cs.pitchOffset = 0;
    cs.volOffset = 0;

    if (cs.delayTick == t) triggerCell (ch, c);
    if (cs.cutTick == t)   { cs.volume = 0; midiNoteOff (cs.midiNote, cs.midiChan); }
    if (cs.keyOffTick == t) releaseChannel (cs);

    auto volSlide = [&cs]
    {
        const int x = cs.mVolSlide >> 4, y = cs.mVolSlide & 15;
        cs.volume = juce::jlimit (0, 64, x ? cs.volume + x : cs.volume - y);
    };
    auto tonePortaStep = [&cs]
    {
        const double d = cs.mTonePorta * 4.0;
        if (cs.pitch < cs.targetPitch) cs.pitch = juce::jmin (cs.pitch + d, cs.targetPitch);
        else                           cs.pitch = juce::jmax (cs.pitch - d, cs.targetPitch);
    };
    auto vibrato = [&cs]
    {
        cs.vibPos = (cs.vibPos + cs.vibSpeed) & 63;
        cs.pitchOffset = waveValue (cs.vibWave & 3, cs.vibPos) * cs.vibDepth / 60.0;
    };
    auto retrigger = [this, &cs] { triggerVoice (cs, 0); };

    switch (fx)
    {
        case 0x00:
            if (prm)
            {
                const int k = t % 3;
                cs.pitchOffset = (k == 1 ? hi : k == 2 ? lo : 0) * 64.0;
            }
            break;
        case 0x01: cs.pitch += cs.mPortaUp * 4.0; break;
        case 0x02: cs.pitch -= cs.mPortaDown * 4.0; break;
        case 0x03: tonePortaStep(); break;
        case 0x04: vibrato(); break;
        case 0x05: tonePortaStep(); volSlide(); break;
        case 0x06: vibrato(); volSlide(); break;
        case 0x07:
            cs.tremPos = (cs.tremPos + cs.tremSpeed) & 63;
            cs.volOffset = waveValue (cs.tremWave & 3, cs.tremPos) * cs.tremDepth / 64;
            break;
        case 0x0A: volSlide(); break;
        case 0x0E:
            if (hi == 0x9 && lo > 0 && (t % lo) == 0) retrigger();
            break;
        case 0x11:
        {
            const int x = cs.mGlobalSlide >> 4, y = cs.mGlobalSlide & 15;
            globalVolume = juce::jlimit (0, 64, x ? globalVolume + x : globalVolume - y);
            break;
        }
        case 0x19:
        {
            const int x = cs.mPanSlide >> 4, y = cs.mPanSlide & 15;
            cs.pan = juce::jlimit (0, 255, x ? cs.pan + x : cs.pan - y);
            break;
        }
        case 0x1B:
        {
            const int interval = cs.mRetrig & 15;
            if (interval > 0 && ++cs.retrigCount >= interval)
            {
                cs.retrigCount = 0;
                int v = cs.volume;
                switch (cs.mRetrig >> 4)
                {
                    case 0x1: v -= 1; break;  case 0x2: v -= 2; break;  case 0x3: v -= 4; break;
                    case 0x4: v -= 8; break;  case 0x5: v -= 16; break; case 0x6: v = v * 2 / 3; break;
                    case 0x7: v /= 2; break;  case 0x9: v += 1; break;  case 0xA: v += 2; break;
                    case 0xB: v += 4; break;  case 0xC: v += 8; break;  case 0xD: v += 16; break;
                    case 0xE: v = v * 3 / 2; break; case 0xF: v *= 2; break;
                    default: break;
                }
                cs.volume = juce::jlimit (0, 64, v);
                retrigger();
            }
            break;
        }
        case 0x1D:
        {
            const int on = (cs.mTremor >> 4) + 1, off = (cs.mTremor & 15) + 1;
            cs.tremorOn = (cs.tremorPos % (on + off)) < on;
            ++cs.tremorPos;
            break;
        }
        default: break;
    }

    cs.pitch = juce::jlimit (-48.0 * 64.0, 144.0 * 64.0, cs.pitch);
}

void TrackerEngine::triggerVoice (ChannelState& cs, int startOffset)
{
    if (cs.voice.active)
    {
        cs.ghost = cs.voice;      // let the old note fade out instead of clicking
        cs.ghost.fading = true;
    }

    // MIDI: end the channel's previous MIDI note, start the new one if it is a MIDI instrument
    midiNoteOff (cs.midiNote, cs.midiChan);
    if (cs.inst != nullptr && cs.inst->p.midi)
    {
        const int ch = (int) (&cs - chans);
        cs.midiChan = cs.inst->p.midiChannel;
        cs.midiProgram = cs.inst->p.midiProgram;
        cs.midiNote = juce::jlimit (0, 127, cs.note - 1 + 12 + cs.inst->p.transpose + mix[ch].pitch.load());
        cs.midiPending = ! isChannelSilenced (ch);      // sent at the end of the tick, with the row's volume
        if (! cs.midiPending) cs.midiNote = -1;
    }

    auto& v = cs.voice;
    v.inst = cs.inst;
    v.slot = cs.instNum;
    if (v.inst == nullptr) { v.active = false; return; }

    v.pos = (double) juce::jlimit (0, v.inst->length - 1, startOffset);
    v.dir = 1;
    v.inLoop = false;
    v.loopOnly = false;
    v.active = true;
    v.fading = false;
    v.fade = 1.0f;
    v.gainL = v.gainR = -1.0f;    // marks "fresh": snap to target on first update
    startEnvelope (v);
}

void TrackerEngine::startEnvelope (Voice& v)
{
    v.fL1 = v.fL2 = v.fR1 = v.fR2 = 0.0f;
    v.fCut = -1.0f;
    v.relStep = 0.0f;
    if (v.inst != nullptr && (v.inst->p.synth || v.inst->p.synth2))
    {
        v.envStage = 1;
        v.env = 0.0f;
        v.fenv = 1.0f;
        v.ph1 = 0.0;
        v.ph2 = 0.37;          // a little apart, so two detuned saws don't start in phase
    }
    else
    {
        v.envStage = 0;
        v.env = 1.0f;
    }
    v.accentEnv = 1.0f;
}

void TrackerEngine::releaseChannel (ChannelState& cs)
{
    fadeOutVoice (cs.voice);
    cs.keyedOff = true;
    midiNoteOff (cs.midiNote, cs.midiChan);
}

void TrackerEngine::midiNoteOn (int note, int chan, int velocity, int program)
{
    chan = juce::jlimit (1, 16, chan);
    if (program > 0 && lastProgram[chan] != program)
    {
        midiOut.addEvent (juce::MidiMessage::programChange (chan, program - 1), blockOffset);
        lastProgram[chan] = program;
    }
    midiOut.addEvent (juce::MidiMessage::noteOn (chan, note, (juce::uint8) velocity), blockOffset);
}

void TrackerEngine::midiNoteOff (int& note, int chan)
{
    for (auto& c : chans) if (&c.midiNote == &note && c.midiPending) { c.midiPending = false; note = -1; return; }   // never sent
    if (note < 0) return;
    midiOut.addEvent (juce::MidiMessage::noteOff (juce::jlimit (1, 16, chan), note), blockOffset);
    note = -1;
}

void TrackerEngine::fadeOutVoice (Voice& v)
{
    if (! v.active) return;
    // a synth note goes into its release; anything else fades out in 4 ms
    const double relMs = v.inst == nullptr ? 0.0
                       : v.inst->p.synth2 ? synth2::envMs (v.inst->p.s2[s2Release]) : (double) v.inst->p.release;
    if (v.envStage > 0 && v.envStage < 4 && relMs >= 1.0 && ! v.fading)
    {
        v.envStage = 4;
        v.relStep = v.env / (float) juce::jmax (1.0, relMs * 0.001 * sr);
        return;
    }
    v.fading = true;
}

void TrackerEngine::updateVoiceTargets (int ch)
{
    auto& cs = chans[ch];
    auto& v = cs.voice;
    if (! v.active || v.inst == nullptr) return;

    const double p = cs.pitch + cs.pitchOffset + mix[ch].pitch.load() * 64.0;
    v.step = std::pow (2.0, (p - (kNativeNote - 1) * 64.0) / 768.0) * v.inst->data->sampleRate / sr;

    int vol = juce::jlimit (0, 64, cs.volume + cs.volOffset);
    if (! cs.tremorOn) vol = 0;

    float gain = (float) vol / 64.0f
               * (float) v.inst->p.volume / 64.0f
               * (float) globalVolume / 64.0f
               * (float) mix[ch].volume.load() / 64.0f;
    if (isChannelSilenced (ch)) gain = 0.0f;

    const float pan = (float) juce::jlimit (0, 255, cs.pan + mix[ch].pan.load() * 2) / 255.0f;
    v.targetL = gain * (pan <= 0.5f ? 1.0f : 2.0f * (1.0f - pan));
    v.targetR = gain * (pan >= 0.5f ? 1.0f : 2.0f * pan);

    if (v.gainL < 0.0f) { v.gainL = v.targetL; v.gainR = v.targetR; }
}

// ===========================================================================
// audio
// ===========================================================================

int TrackerEngine::waveValue (int wave, int pos) noexcept
{
    pos &= 63;
    switch (wave)
    {
        case 1:  return 255 - pos * 8;                         // ramp down
        case 2:  return pos < 32 ? 255 : -255;                 // square
        default: return (int) std::lround (255.0 * std::sin (pos * juce::MathConstants<double>::twoPi / 64.0));
    }
}

float TrackerEngine::readFrame (const InstrumentSnapshot& s, int ch, double pos) noexcept
{
    const int len = s.length;
    const int i = (int) std::floor (pos);
    const float f = (float) (pos - i);
    const float* d = s.data->buffer.getReadPointer (juce::jmin (ch, s.data->getNumChannels() - 1));

    auto at = [&] (int v) { return d[s.physicalIndex (juce::jlimit (0, len - 1, v))]; };

    const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

void TrackerEngine::renderVoice (Voice& v, float* L, float* R, int numSamples, float& peak, int channel)
{
    if (! v.active || v.inst == nullptr) return;

    const auto& s = *v.inst;
    const bool stereo = s.data->getNumChannels() > 1;
    const int len = s.length;
    const bool looping = s.looping || v.loopOnly;
    const int ls = s.looping ? s.vLoopStart : 0;
    const int le = s.looping ? s.vLoopEnd : len;
    const double ll = (double) juce::jmax (1, le - ls);

    // The instrument's current settings, so moving a slider is heard on notes
    // that are already playing (the voice keeps its snapshot for the sample data).
    const InstrumentParams* lp = &s.p;
    if (v.slot > 0 && v.slot <= kAuditionSlot)
        if (const auto& now = audioSnapshots[(size_t) v.slot]; now != nullptr && now->data == s.data)
            lp = &now->p;

    if (lp->synth2) { renderSynth2 (v, *lp, L, R, numSamples, peak, channel); return; }

    // synth envelope
    const bool synth = v.envStage > 0;
    const float atkStep = synth ? 1.0f / (float) juce::jmax (1.0, juce::jmax (1, lp->attack) * 0.001 * sr) : 0.0f;
    const float susLvl  = (float) lp->sustain / 64.0f;
    const float decStep = synth ? juce::jmax (0.0f, 1.0f - susLvl) / (float) juce::jmax (1.0, lp->decay * 0.001 * sr) : 0.0f;

    // filter: the instrument's CUTOFF / RESONANCE, or the channel's 22xx / 23xx commands
    float cutTarget = (float) lp->cutoff, resTarget = (float) lp->resonance;
    if (juce::isPositiveAndBelow (channel, kNumChannels))
    {
        if (chans[channel].fxCutoff >= 0) cutTarget = (float) chans[channel].fxCutoff;
        if (chans[channel].fxReso >= 0)   resTarget = (float) chans[channel].fxReso;
    }
    // ACCENT: the note starts louder and with the filter opened up, settling over ~150 ms
    // DISTORTION: drive into a soft clipper, level-matched
    // (the synth's sliders, or the channel's 0Vvv / 0Yvv commands, which also work on samples)
    int accentVal = lp->synth ? lp->accent : 0, distVal = lp->synth ? lp->distortion : 0;
    if (juce::isPositiveAndBelow (channel, kNumChannels))
    {
        if (chans[channel].fxAccent >= 0) accentVal = chans[channel].fxAccent;
        if (chans[channel].fxDrive >= 0)  distVal = chans[channel].fxDrive;
    }
    const float accentAmt = (float) accentVal / 64.0f;
    const float accentDecay = (float) std::exp (-1.0 / (0.15 * sr));
    const float drive = distVal > 0 ? 1.0f + (float) distVal / 64.0f * 24.0f : 0.0f;
    const float driveNorm = drive > 0.0f ? 0.8f / std::tanh (drive * 0.8f) : 1.0f;

    const bool filtered = cutTarget < 127.0f || resTarget > 0.0f || (v.fCut >= 0.0f && v.fCut < 126.5f);
    if (v.fCut < 0.0f) { v.fCut = cutTarget; v.fRes = resTarget; }

    float fa1 = 0, fa2 = 0, fa3 = 0;
    auto updateFilter = [&]
    {
        // glide towards the new setting so sweeps don't step or click
        const float target = juce::jmin (127.0f, cutTarget + accentAmt * v.accentEnv * 70.0f);
        v.fCut += (target - v.fCut) * 0.08f;
        v.fRes += (resTarget - v.fRes) * 0.08f;
        // TPT state-variable low-pass (stable at any setting)
        const double hz = 30.0 * std::pow (2.0, v.fCut / 127.0 * 9.4);       // 30 Hz .. ~20 kHz
        const double fc = juce::jmin (hz, sr * 0.45);
        const double g = std::tan (juce::MathConstants<double>::pi * fc / sr);
        const float k = (float) (2.0 - 1.96 * v.fRes / 127.0);
        fa1 = (float) (1.0 / (1.0 + g * (g + k)));
        fa2 = (float) g * fa1;
        fa3 = (float) g * fa2;
    };
    if (filtered) updateFilter();

    auto lowpass = [&] (float x, float& ic1, float& ic2)
    {
        const float v3 = x - ic2;
        const float v1 = fa1 * ic1 + fa2 * v3;
        const float v2 = ic2 + fa2 * ic1 + fa3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return v2;
    };

    for (int i = 0; i < numSamples; ++i)
    {
        v.gainL += (v.targetL - v.gainL) * smoothCoeff;
        v.gainR += (v.targetR - v.gainR) * smoothCoeff;

        float g = 1.0f;
        if (v.fading)
        {
            v.fade -= fadeStep;
            if (v.fade <= 0.0f) { v.active = false; break; }
            g = v.fade;
        }

        if (synth)
        {
            switch (v.envStage)
            {
                case 1:  v.env += atkStep; if (v.env >= 1.0f) { v.env = 1.0f; v.envStage = 2; } break;
                case 2:  v.env -= decStep; if (v.env <= susLvl) { v.env = susLvl; v.envStage = 3; } break;
                case 3:  v.env += (susLvl - v.env) * 0.002f; if (susLvl <= 0.0f && v.env < 0.0005f) v.env = 0.0f; break;  // follows the SUSTAIN slider
                case 4:  v.env -= v.relStep; break;
                default: break;
            }
            if (v.env <= 0.0f && v.envStage >= 3) { v.active = false; break; }   // decayed to silence or released
            g *= v.env;
        }
        if (accentAmt > 0.0f) g *= 1.0f + 0.7f * accentAmt * v.accentEnv;
        v.accentEnv *= accentDecay;

        float sl = readFrame (s, 0, v.pos);
        float sr2 = stereo ? readFrame (s, 1, v.pos) : sl;
        if (filtered)
        {
            if ((i & 15) == 15) updateFilter();
            sl = lowpass (sl, v.fL1, v.fL2);
            sr2 = stereo ? lowpass (sr2, v.fR1, v.fR2) : sl;
        }
        if (drive > 0.0f)
        {
            sl = std::tanh (sl * drive) * driveNorm;
            sr2 = stereo ? std::tanh (sr2 * drive) * driveNorm : sl;
        }
        const float outL = sl * v.gainL * g, outR = sr2 * v.gainR * g;
        L[i] += outL;
        R[i] += outR;
        peak = juce::jmax (peak, std::abs (outL), std::abs (outR));

        v.pos += v.step * v.dir;

        if (! looping)
        {
            if (v.pos >= len) { v.active = false; break; }
        }
        else if (s.p.pingPong)
        {
            const double top = le - 1;
            if (v.dir > 0 && v.pos >= top)     { v.pos = juce::jmax ((double) ls, top - (v.pos - top)); v.dir = -1; v.inLoop = true; }
            else if (v.dir < 0 && v.pos < ls)  { v.pos = juce::jmin (top, ls + (ls - v.pos)); v.dir = 1; }
        }
        else if (s.p.loopBackward)
        {
            if (v.dir > 0 && v.pos >= le)      { v.pos = juce::jmax ((double) ls, (le - 1) - (v.pos - le)); v.dir = -1; v.inLoop = true; }
            else if (v.dir < 0 && v.pos < ls)  { v.pos = ls + std::fmod (v.pos - ls + ll * 64.0, ll); if (v.pos < ls) v.pos += ll; }
        }
        else
        {
            if (v.pos >= le) { v.pos = ls + std::fmod (v.pos - ls, ll); v.inLoop = true; }
        }
    }
}


// ---------------------------------------------------------------------------
// SYNTH 2
// ---------------------------------------------------------------------------

namespace
{
    inline float polyBlep (double t, double dt) noexcept
    {
        if (t < dt)       { t /= dt; return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }

    inline float oscillator (int wave, double t, double dt, float pw, juce::uint32& noise) noexcept
    {
        switch (wave)
        {
            case 0:                                                                                       // saw
            {
                // PW on a saw: a second saw, shifted by up to 45 % of a cycle, is added
                // (50 % = a plain saw; less = a hollower, phasing saw that PWM makes move)
                float y = (float) (2.0 * t - 1.0) - polyBlep (t, dt);
                const double off = 0.5 - pw;
                if (off > 0.001)
                {
                    const double t2 = std::fmod (t + off, 1.0);
                    y = 0.5f * (y + (float) (2.0 * t2 - 1.0) - polyBlep (t2, dt)) * (1.0f + (float) off);
                }
                return y;
            }
            case 1:                                                                                       // pulse
            {
                float y = t < pw ? 1.0f : -1.0f;
                y += polyBlep (t, dt);
                y -= polyBlep (std::fmod (t + 1.0 - pw, 1.0), dt);
                return y * 0.8f;
            }
            case 2:                                                                                       // triangle
            {
                // PW skews the peak: 50 % = triangle, 5 % = almost a falling saw
                const double q = (double) pw;
                return (float) (t < q ? 2.0 * t / q - 1.0 : 1.0 - 2.0 * (t - q) / (1.0 - q));
            }
            case 3:                                                                                       // sine
            {
                // PW = phase distortion (CZ style): 50 % = a pure sine, lower = brighter, more saw-like
                const double q = (double) pw;
                const double w = t < q ? 0.5 * t / q : 0.5 + 0.5 * (t - q) / (1.0 - q);
                return (float) std::sin (w * juce::MathConstants<double>::twoPi);
            }
            default:                                                                                      // noise
                noise = noise * 1664525u + 1013904223u;
                return (float) ((int) (noise >> 9) - 4194304) / 4194304.0f * 0.7f;
        }
    }

    inline float lfoShape (int shape, double ph) noexcept   // -1..1
    {
        switch (shape)
        {
            case 1:  return (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);
            case 2:  return (float) (1.0 - 2.0 * ph);
            case 3:  return ph < 0.5 ? 1.0f : -1.0f;
            default: return (float) std::sin (ph * juce::MathConstants<double>::twoPi);
        }
    }
}

void TrackerEngine::renderSynth2 (Voice& v, const InstrumentParams& p, float* L, float* R, int numSamples, float& peak, int channel)
{
    const auto& k = p.s2;
    const auto kv = [&k] (int i) { return k[(size_t) i]; };

    // envelopes
    const float atkStep = 1.0f / (float) juce::jmax (1.0, synth2::envMs (kv (s2Attack)) * 0.001 * sr);
    const float susLvl  = (float) kv (s2Sustain) / 127.0f;
    const float decStep = juce::jmax (0.0f, 1.0f - susLvl) / (float) juce::jmax (1.0, synth2::envMs (kv (s2Decay)) * 0.001 * sr);
    const float fdecay  = (float) std::exp (-1.0 / (synth2::filterDecayMs (kv (s2FDecay)) * 0.001 * sr));

    // oscillators: the voice's speed through the 128-frame cycle is the pitch
    const double inc = v.step / 128.0;
    const double semi2 = (kv (s2Osc2Semi) - 24) + (kv (s2Osc2Fine) - 64) / 63.0 * 0.5;
    const double ratio2 = std::pow (2.0, semi2 / 12.0);
    const float mix = (float) kv (s2OscMix) / 127.0f;
    const float pwBase = 0.5f - 0.45f * (float) kv (s2Osc1PW) / 127.0f;
    const int w1 = kv (s2Osc1Wave), w2 = kv (s2Osc2Wave);

    // LFO: free in Hz, or a length in rows (1/16 ... two patterns) at the current tempo
    const int patRows = getPattern (juce::jlimit (0, kMaxPatterns - 1, playPattern.load())).getNumRows();
    const double syncRows = synth2::lfoSyncRows (kv (s2LfoSync), patRows);
    const double period = syncRows > 0.0 ? syncRows * rowSeconds() : 1.0 / synth2::lfoHz (kv (s2LfoRate));
    const double lpx = kv (s2LfoPitch) / 127.0;
    const float lfoPitchSemis = (float) (12.0 * lpx * lpx);
    const float lfoPwm = 0.4f * (float) kv (s2LfoPwm) / 127.0f;
    const float lfoFilter = 64.0f * (float) kv (s2LfoFilter) / 127.0f;

    // filter: the knobs, or the channel's 0X / 0Z commands
    float cutBase = (float) kv (s2Cutoff), reso = (float) kv (s2Reso);
    int accentVal = 0, distVal = 0;
    if (juce::isPositiveAndBelow (channel, kNumChannels))
    {
        const auto& c = chans[channel];
        if (c.fxCutoff >= 0) cutBase = (float) c.fxCutoff;
        if (c.fxReso >= 0)   reso = (float) c.fxReso;
        if (c.fxAccent >= 0) accentVal = c.fxAccent;
        if (c.fxDrive >= 0)  distVal = c.fxDrive;
    }
    const float envAmt = 80.0f * (float) kv (s2EnvAmt) / 127.0f;
    const float accentAmt = (float) accentVal / 64.0f;
    const float accentDecay = (float) std::exp (-1.0 / (0.15 * sr));
    const float drive = distVal > 0 ? 1.0f + (float) distVal / 64.0f * 24.0f : 0.0f;
    const float driveNorm = drive > 0.0f ? 0.8f / std::tanh (drive * 0.8f) : 1.0f;

    float fa1 = 0, fa2 = 0, fa3 = 0, pitchMul = 1.0f, pw = pwBase;
    auto control = [&] (int i)
    {
        double ph = std::fmod ((lfoNow + i / sr) / period, 1.0);
        if (ph < 0.0) ph += 1.0;
        const float lfo = lfoShape (kv (s2LfoShape), ph);
        pitchMul = lfoPitchSemis > 0.0f ? std::pow (2.0f, lfoPitchSemis * lfo / 12.0f) : 1.0f;
        pw = juce::jlimit (0.05f, 0.95f, pwBase + lfoPwm * lfo);
        const float target = juce::jlimit (0.0f, 127.0f, cutBase + envAmt * v.fenv + lfoFilter * lfo + accentAmt * v.accentEnv * 60.0f);
        if (v.fCut < 0.0f) v.fCut = target;
        v.fCut += (target - v.fCut) * 0.5f;
        const double fc = juce::jmin (synth2::cutoffHz (v.fCut), sr * 0.45);
        const double g = std::tan (juce::MathConstants<double>::pi * fc / sr);
        const float kq = (float) (2.0 - 1.96 * reso / 127.0);
        fa1 = (float) (1.0 / (1.0 + g * (g + kq)));
        fa2 = (float) g * fa1;
        fa3 = (float) g * fa2;
    };

    for (int i = 0; i < numSamples; ++i)
    {
        if ((i & 15) == 0) control (i);
        v.gainL += (v.targetL - v.gainL) * smoothCoeff;
        v.gainR += (v.targetR - v.gainR) * smoothCoeff;

        float g = 1.0f;
        if (v.fading)
        {
            v.fade -= fadeStep;
            if (v.fade <= 0.0f) { v.active = false; break; }
            g = v.fade;
        }
        switch (v.envStage)
        {
            case 1:  v.env += atkStep; if (v.env >= 1.0f) { v.env = 1.0f; v.envStage = 2; } break;
            case 2:  v.env -= decStep; if (v.env <= susLvl) { v.env = susLvl; v.envStage = 3; } break;
            case 3:  v.env += (susLvl - v.env) * 0.002f; if (susLvl <= 0.0f && v.env < 0.0005f) v.env = 0.0f; break;
            case 4:  v.env -= v.relStep; break;
            default: break;
        }
        if (v.env <= 0.0f && v.envStage >= 3) { v.active = false; break; }
        g *= v.env;
        if (accentAmt > 0.0f) g *= 1.0f + 0.7f * accentAmt * v.accentEnv;
        v.accentEnv *= accentDecay;
        v.fenv *= fdecay;

        const double d1 = inc * pitchMul, d2 = d1 * ratio2;
        const float o1 = oscillator (w1, v.ph1, d1, pw, v.noise);
        const float o2 = oscillator (w2, v.ph2, d2, pw, v.noise);
        float x = ((1.0f - mix) * o1 + mix * o2) * 0.5f;

        // TPT state-variable low-pass
        {
            const float v3 = x - v.fL2;
            const float v1 = fa1 * v.fL1 + fa2 * v3;
            const float v2 = v.fL2 + fa2 * v.fL1 + fa3 * v3;
            v.fL1 = 2.0f * v1 - v.fL1;
            v.fL2 = 2.0f * v2 - v.fL2;
            x = v2;
        }
        if (drive > 0.0f) x = std::tanh (x * drive) * driveNorm;

        const float outL = x * v.gainL * g, outR = x * v.gainR * g;
        L[i] += outL;
        R[i] += outR;
        peak = juce::jmax (peak, std::abs (outL), std::abs (outR));

        v.ph1 += d1; if (v.ph1 >= 1.0) v.ph1 -= std::floor (v.ph1);
        v.ph2 += d2; if (v.ph2 >= 1.0) v.ph2 -= std::floor (v.ph2);
    }
}

void TrackerEngine::processSynth2Fx (int ch, float* L, float* R, int n)
{
    auto& fx = s2fx[(size_t) ch];
    if (fx.dl.empty() || fx.cl.empty()) return;

    // is anything coming in? then keep the FX running for a while after it stops
    float in = 0.0f;
    for (int i = 0; i < n; ++i) in = juce::jmax (in, std::abs (L[i]), std::abs (R[i]));
    if (in > 1.0e-6f) fx.tail = (int) (sr * 10.0);
    else if (fx.tail <= 0) return;
    fx.tail -= n;

    const InstrumentSnapshot* snap = fx.slot > 0 && fx.slot < (int) audioSnapshots.size() ? audioSnapshots[(size_t) fx.slot].get() : nullptr;
    if (snap == nullptr || ! snap->p.synth2) return;
    const auto& k = snap->p.s2;

    // chorus: two modulated taps, one per side
    const float cho = (float) k[s2Chorus] / 127.0f;
    const int csz = (int) fx.cl.size();
    const double base = 0.012 * sr, depth = 0.004 * sr, cinc = juce::MathConstants<double>::twoPi * 0.45 / sr;
    // delay: tempo-synced, ping-pong
    const float dmix = (float) k[s2DelayMix] / 127.0f;
    const float fb = 0.85f * (float) k[s2DelayFb] / 127.0f;
    const int dsz = (int) fx.dl.size();
    const int dlen = juce::jlimit (1, dsz - 1, (int) (synth2::delayRows (k[s2DelayTime]) * rowSeconds() * sr));

    for (int i = 0; i < n; ++i)
    {
        float xl = L[i], xr = R[i];

        fx.cl[(size_t) fx.cw] = xl;
        fx.cr[(size_t) fx.cw] = xr;
        if (cho > 0.0f)
        {
            auto tap = [&] (const std::vector<float>& b, double d)
            {
                double rp = fx.cw - d;
                while (rp < 0.0) rp += csz;
                const int i0 = (int) rp;
                const float fr = (float) (rp - i0);
                return b[(size_t) i0] * (1.0f - fr) + b[(size_t) ((i0 + 1) % csz)] * fr;
            };
            const float tl = tap (fx.cl, base + depth * std::sin (fx.cph));
            const float tr = tap (fx.cr, base + depth * std::sin (fx.cph + juce::MathConstants<double>::halfPi));
            xl = xl * (1.0f - 0.45f * cho) + tl * 0.75f * cho;
            xr = xr * (1.0f - 0.45f * cho) + tr * 0.75f * cho;
        }
        fx.cph += cinc;
        if (fx.cph > juce::MathConstants<double>::twoPi) fx.cph -= juce::MathConstants<double>::twoPi;
        if (++fx.cw >= csz) fx.cw = 0;

        int rd = fx.dw - dlen;
        if (rd < 0) rd += dsz;
        const float el = fx.dl[(size_t) rd], er = fx.dr[(size_t) rd];
        fx.dampL += (el - fx.dampL) * 0.35f;     // a little darker on each repeat
        fx.dampR += (er - fx.dampR) * 0.35f;
        fx.dl[(size_t) fx.dw] = xl + fx.dampR * fb;   // ping-pong: each side feeds the other
        fx.dr[(size_t) fx.dw] = xr * 0.3f + fx.dampL * fb;
        if (++fx.dw >= dsz) fx.dw = 0;
        if (dmix > 0.0f) { xl += el * dmix; xr += er * dmix; }

        L[i] = xl;
        R[i] = xr;
    }

    const int rsz = k[s2ReverbSize], rmx = k[s2ReverbMix];
    if (rmx > 0)
    {
        if (rsz != fx.revSize || rmx != fx.revMix)
        {
            juce::Reverb::Parameters rp;
            rp.roomSize = 0.4f + 0.58f * (float) rsz / 127.0f;
            rp.damping = 0.45f;
            rp.wetLevel = 0.5f * (float) rmx / 127.0f;
            rp.dryLevel = 1.0f;
            rp.width = 1.0f;
            fx.reverb.setParameters (rp);
            fx.revSize = rsz;
            fx.revMix = rmx;
        }
        fx.reverb.processStereo (L, R, n);
    }

    if (fx.tail <= 0) fx.clear();   // quiet for 10 s: start clean next time
}

void TrackerEngine::renderVoices (float* L, float* R, int offset, int numSamples)
{
    for (int ch = 0; ch < kNumChannels; ++ch)
    {
        float peak = 0.0f;
        float* cl = useChannelBuffers ? chanL (ch) + offset : L + offset;
        float* cr = useChannelBuffers ? chanR (ch) + offset : R + offset;
        auto& cs = chans[ch];
        renderWithSynth2Fx ({ &cs.voice, &cs.ghost, &jamVoices[(size_t) ch] }, ch, cl, cr, numSamples, peak, ch);
        const int lvl = juce::jmin (1000, (int) (peak * 1000.0f));
        if (lvl > channelLevel[ch].load()) channelLevel[ch] = lvl;
    }

    // the preview / jam voice (computer keys, clicking a slot) has its own SYNTH 2 FX
    float dummy = 0.0f;
    renderWithSynth2Fx ({ &previewVoice }, kNumChannels, L + offset, R + offset, numSamples, dummy, -1);
}

void TrackerEngine::renderWithSynth2Fx (std::initializer_list<Voice*> voices, int fxIndex, float* L, float* R, int numSamples, float& peak, int channel)
{
    auto isS2 = [] (const Voice& v) { return v.active && v.inst != nullptr && v.inst->p.synth2; };
    auto& fx = s2fx[(size_t) fxIndex];
    bool s2 = false;
    for (auto* v : voices)
        if (isS2 (*v)) { if (! s2) fx.slot = v->slot; s2 = true; }

    if (! s2 && fx.tail <= 0)
    {
        lfoNow = lfoClock;
        for (auto* v : voices) renderVoice (*v, L, R, numSamples, peak, channel);
        return;
    }

    // SYNTH 2 voices go through the chorus / delay / reverb first; the tail rings on after the note
    const int chunk = juce::jmax (1, (int) s2tmpL.size());
    for (int done = 0; done < numSamples; done += chunk)
    {
        const int len = juce::jmin (chunk, numSamples - done);
        float* tl = s2tmpL.data();
        float* tr = s2tmpR.data();
        juce::FloatVectorOperations::clear (tl, len);
        juce::FloatVectorOperations::clear (tr, len);
        lfoNow = lfoClock + done / sr;
        for (auto* v : voices)
        {
            if (isS2 (*v)) renderVoice (*v, tl, tr, len, peak, channel);
            else           renderVoice (*v, L + done, R + done, len, peak, channel);
        }
        processSynth2Fx (fxIndex, tl, tr, len);
        juce::FloatVectorOperations::add (L + done, tl, len);
        juce::FloatVectorOperations::add (R + done, tr, len);
    }
}

void TrackerEngine::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, double hostBpm)
{
    const int n = out.getNumSamples();
    out.clear();

    const juce::ScopedTryLock dl (dataLock);
    if (! dl.isLocked()) return;    // UI is doing a bulk edit; skip this block

    {
        const juce::SpinLock::ScopedTryLockType sl (snapLock);
        if (sl.isLocked()) audioSnapshots = snapshots;
    }

    currentHostBpm = hostBpm;

    {
        int s1, n1, s2, n2;
        blockOffset = 0;
        cmdFifo.prepareToRead (cmdFifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) handleCommand (cmdBuffer[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) handleCommand (cmdBuffer[(size_t) (s2 + i)]);
        cmdFifo.finishedRead (n1 + n2);
    }

    float* L = out.getWritePointer (0);
    float* R = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;
    if (R == nullptr)
    {
        if ((int) monoScratch.size() < n) monoScratch.resize ((size_t) n);   // only for mono hosts
        std::fill (monoScratch.begin(), monoScratch.begin() + n, 0.0f);
        R = monoScratch.data();
    }

    // per-channel rendering for the channel inserts (VST effects)
    useChannelBuffers = channelInsert != nullptr && n <= chanScratchSize;
    const bool playingAtStart = playing.load();
    const int spd = juce::jmax (1, speed.load());
    const double spt = samplesPerTick();
    const double beatsPerSecond = sr * 2.5 / spt * 6.0 / spd / 60.0;
    if (playingAtStart)
    {
        // where the sound is now, in rows: the tick about to start, or part-way through the last one
        double rowPos;
        if (samplesToNextTick <= 0.0) rowPos = row + (double) juce::jmin (tick, spd - 1) / spd;
        else
        {
            const int heard = tick == 0 ? spd - 1 : tick - 1;
            rowPos = playRow.load() + (juce::jmin (heard, spd - 1) + juce::jlimit (0.0, 1.0, 1.0 - samplesToNextTick / spt)) / spd;
        }
        if (rowPos + 1.0e-9 < insertLastRow)              // back to the top: the next 16 beats
            insertBase = (std::floor ((insertBase + insertLastRow / 4.0) / 16.0) + 1.0) * 16.0;
        insertLastRow = rowPos;
    }
    insertTransport = { playingAtStart, beatsPerSecond * 60.0, insertBase + insertLastRow / 4.0, insertSamples };
    if (useChannelBuffers)
        for (int ch = 0; ch < kNumChannels; ++ch)
        {
            juce::FloatVectorOperations::clear (chanL (ch), n);
            juce::FloatVectorOperations::clear (chanR (ch), n);
        }

    auto renderRange = [&] (int from, int to)
    {
        while (from < to)
        {
            if (playing && samplesToNextTick <= 0.0)
            {
                blockOffset = from;
                doTick();
                samplesToNextTick += samplesPerTick();
            }

            int len = to - from;
            if (playing)
                len = juce::jmin (len, juce::jmax (1, (int) std::ceil (samplesToNextTick)));

            renderVoices (L, R, from, len);
            lfoClock += len / sr;

            if (playing) samplesToNextTick -= len;
            from += len;
        }
    };

    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        renderRange (pos, at);
        pos = at;
        blockOffset = at;
        handleMidi (meta.getMessage());
    }
    renderRange (pos, n);

    if (! playing) samplesToNextTick = 0.0;

    if (useChannelBuffers)
    {
        // where each channel goes after its effects: -1 = out, else into that channel's effects
        const int used = juce::jlimit (1, kNumChannels, numChannels.load());
        int next[kNumChannels], depth[kNumChannels];
        for (int ch = 0; ch < kNumChannels; ++ch)
        {
            int t = channelRoute != nullptr && ch < used ? channelRoute (ch) : -1;
            if (t < 0 || t >= used || t == ch) t = -1;
            next[ch] = t;
        }
        int maxDepth = 0;
        for (int ch = 0; ch < kNumChannels; ++ch)
        {
            int d = 0, t = ch;
            while (next[t] >= 0 && d <= kNumChannels) { t = next[t]; ++d; }
            if (d > kNumChannels) { next[ch] = -1; d = 0; }       // a loop: this one goes straight out
            depth[ch] = d;
            maxDepth = juce::jmax (maxDepth, d);
        }
        // furthest up the chain first, so a channel has everything sent into it before its effects run
        for (int d = maxDepth; d >= 0; --d)
            for (int ch = 0; ch < kNumChannels; ++ch)
            {
                if (depth[ch] != d) continue;
                float* cl = chanL (ch);
                float* cr = chanR (ch);
                channelInsert (ch, cl, cr, n);
                const int t = next[ch];
                juce::FloatVectorOperations::add (t >= 0 ? chanL (t) : L, cl, n);
                juce::FloatVectorOperations::add (t >= 0 ? chanR (t) : R, cr, n);
            }
    }

    if (playing.load())
    {
        if (playingAtStart) insertSamples += n;
    }

    // master gain + soft knee limiter so four loud channels never clip hard
    const float master = (float) masterVolume.load() / 100.0f * 0.7f;
    auto shape = [] (float x)
    {
        const float a = std::abs (x);
        if (a <= 0.8f) return x;
        return std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f), x);
    };
    if (out.getNumChannels() > 1)
    {
        for (int i = 0; i < n; ++i) { L[i] = shape (L[i] * master); R[i] = shape (R[i] * master); }
    }
    else
    {
        for (int i = 0; i < n; ++i) L[i] = shape (0.5f * (L[i] + R[i]) * master);
    }

    if (previewVoice.active && previewVoice.inst != nullptr)
        previewPosition = previewVoice.inst->physicalIndex ((int) previewVoice.pos);
    else
        previewPosition = -1;
}

} // namespace ft

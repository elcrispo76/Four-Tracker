// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "TrackerTypes.h"

namespace ft
{

/** Immutable block of sample frames. Edits create a new SampleData. */
struct SampleData : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

    juce::AudioBuffer<float> buffer;   // 1 or 2 channels
    double sampleRate = 44100.0;
    int    bitDepth   = 16;

    int getLength()      const noexcept { return buffer.getNumSamples(); }
    int getNumChannels() const noexcept { return buffer.getNumChannels(); }
};

struct InstrumentParams
{
    int  volume    = 64;     // 0..64
    int  transpose = 0;      // semitones, -48..48
    int  fineTune  = 0;      // -128..127 (1/128 semitone)
    bool loop      = false;
    bool pingPong  = false;
    bool loopBackward = false;   // loop region plays in reverse
    bool reverse   = false;      // whole sample plays backwards (non-destructive)
    int  start     = 0;          // inclusive frame index
    int  end       = 0;          // inclusive frame index
    int  loopStart = 0;          // inclusive
    int  loopEnd   = 0;          // inclusive

    // built-in synth (synth == true): the sample is one generated cycle that
    // loops, shaped by an ADSR envelope in the engine
    bool synth     = false;
    int  synthWave = 0;          // SynthWave
    int  attack    = 2;          // ms
    int  decay     = 250;        // ms
    int  sustain   = 40;         // level 0..64
    int  release   = 200;        // ms
    int  cutoff    = 127;        // low-pass filter 0..127 (127 = open)
    int  resonance = 0;          // 0..127
    int  distortion = 0;         // 0..64, drive into a soft clipper (0 = clean)
    int  accent    = 0;          // 0..64, each note starts louder and brighter, then settles

    // MIDI instrument (midi == true): notes are sent to MIDI out instead of played
    bool midi        = false;
    int  midiChannel = 1;        // 1..16
    int  midiProgram = 0;        // 0 = don't send, 1..128 = program change before the first note
};

enum SynthWave { waveSine, waveTriangle, waveSaw, waveSquare, waveNoise, numSynthWaves };
const char* synthWaveName (int wave);

/** Read-only view handed to the audio thread. */
struct InstrumentSnapshot : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<InstrumentSnapshot>;

    SampleData::Ptr  data;
    InstrumentParams p;

    // Precomputed playback geometry in "virtual" frames (0 .. length-1),
    // already mirrored when the sample is reversed.
    int length = 0;
    int vLoopStart = 0;
    int vLoopEnd   = 0;     // exclusive
    bool looping = false;

    int physicalIndex (int v) const noexcept { return p.reverse ? (p.end - v) : (p.start + v); }
};

/** Message-thread side instrument (owned by the processor). */
struct Instrument
{
    juce::String name;          // shown in the UI, e.g. "KICK.WAV"
    juce::String sourcePath;    // file it was loaded from (informational)
    SampleData::Ptr data;
    InstrumentParams params;

    bool isEmpty() const noexcept { return data == nullptr || data->getLength() == 0; }
    void clear() { *this = Instrument(); }

    /** Clamps start/end/loop points to the sample. */
    void sanitise();

    /** Resets points to cover the whole sample. */
    void resetPoints();

    InstrumentSnapshot::Ptr makeSnapshot() const;

    // ---- destructive edits (all return false if nothing happened) -------
    bool cut       (int from, int to, juce::AudioBuffer<float>& clip);
    void copy      (int from, int to, juce::AudioBuffer<float>& clip) const;
    bool paste     (int at, const juce::AudioBuffer<float>& clip);
    bool fadeIn    (int from, int to);
    bool fadeOut   (int from, int to);
    bool normalise (int from, int to);
    bool reverseRange (int from, int to);
    /** Mixes another instrument's sample into this one, starting at frame `at` (like OctaMED's
        MIX). The other sample is resampled to this sample's rate and may make it longer; mono
        becomes stereo if either is stereo. Scaled down only if the sum would clip. */
    bool mixIn (const Instrument& other, int at);

    juce::String loadFromFile (const juce::File& file, juce::AudioFormatManager& formats); // returns error or empty

    /** Turns this slot into a built-in synth with the given waveform. Keeps
        volume, tuning and envelope if it already was a synth. */
    void makeSynth (int wave);
    bool isSynth() const noexcept { return ! isEmpty() && params.synth; }

    /** Turns this slot into a MIDI instrument: its notes go to MIDI out (a device, or the DAW). */
    void makeMidi();
    bool isMidi() const noexcept { return ! isEmpty() && params.midi; }
};

} // namespace ft

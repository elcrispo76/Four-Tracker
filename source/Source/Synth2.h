// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

// SYNTH 2: a two-oscillator synth with PWM, a filter with its own envelope,
// an LFO that can follow the tempo (up to two patterns long), and chorus,
// delay and reverb. Every knob is a whole number (most 0..127, so a MIDI knob
// maps straight onto it), which keeps patches, modules and MIDI LEARN simple.

#include <juce_core/juce_core.h>
#include <array>

namespace ft
{

enum Synth2Param
{
    s2Osc1Wave, s2Osc1PW, s2Osc2Wave, s2Osc2Semi, s2Osc2Fine, s2OscMix,
    s2Cutoff, s2Reso, s2EnvAmt, s2FDecay,
    s2LfoShape, s2LfoSync, s2LfoRate,
    s2Attack, s2Decay, s2Sustain, s2Release,
    s2LfoPitch, s2LfoPwm, s2LfoFilter,
    s2Chorus, s2DelayTime, s2DelayFb, s2DelayMix, s2ReverbSize, s2ReverbMix,
    s2NumParams
};

using Synth2Values = std::array<int, s2NumParams>;

namespace synth2
{
    const char* name (int param);           // full name, e.g. "LFO RATE" (menus, status line)
    const char* label (int param);          // short label on the knob, e.g. "RATE" (under its group)
    const char* key (int param);            // short id used in files, e.g. "cut"
    int maxValue (int param);               // 0..max
    int defaultValue (int param);
    Synth2Values defaults();                // the INIT patch
    juce::String format (int param, int value);   // value text under the knob ("650 HZ", "+7", "1/8 D")
    int clamp (int param, int value);

    // value mapping used by the engine (and the knob texts)
    double envMs (int v);                   // ATTACK / DECAY / RELEASE, 0..5000 ms
    double filterDecayMs (int v);           // 5..4005 ms
    double cutoffHz (double v);             // 0..127 -> 30 Hz .. ~20 kHz
    double lfoHz (int v);                   // FREE rate 0.05 .. 20 Hz
    /** LFO SYNC choice -> length in rows (16th notes), 0 = FREE. Pattern lengths use patternRows. */
    double lfoSyncRows (int choice, int patternRows);
    double delayRows (int choice);          // DELAY TIME choice -> rows (16th notes)

    // ---- patches --------------------------------------------------------------
    struct Patch
    {
        juce::String name;
        Synth2Values values {};
        bool factory = true;
        juce::File file;                    // user patches
    };
    /** The factory patches (INIT, then trance, italo and 80s sounds). */
    const std::vector<Patch>& factoryPatches();
    /** Documents/Four Tracker/Synth 2 Patches */
    juce::File userPatchFolder();
    /** Factory patches followed by the user's saved patches (sorted by name). */
    std::vector<Patch> allPatches();
    /** Saves a patch file; returns the file or an error. */
    juce::Result savePatch (const juce::String& name, const Synth2Values& values, juce::File& written);
    bool loadPatchFile (const juce::File& f, Patch& out);
    juce::String toString (const Synth2Values& v);              // "0,0,0,24,..." for modules
    Synth2Values fromString (const juce::String& s);
}

} // namespace ft

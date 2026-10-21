// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#include "TrackerTypes.h"

namespace ft
{

static const char* const kNoteNames[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };

juce::String noteToString (uint8_t note)
{
    if (note == kNoteNone) return "---";
    if (note == kNoteOff)  return "===";
    if (note > kNoteMax)   return "???";
    const int n = note - 1;
    return juce::String (kNoteNames[n % 12]) + juce::String (n / 12);
}

juce::String midiNoteName (int midiNote)
{
    if (midiNote < 0 || midiNote > 127) return "---";
    const int oct = midiNote / 12 - 1;
    return juce::String (kNoteNames[midiNote % 12]) + (oct < 0 ? juce::String ("-") + juce::String (-oct) : juce::String (oct));
}

juce::String formatNumber (int value, int digits, bool hex)
{
    if (hex) return juce::String::toHexString (value).toUpperCase().paddedLeft ('0', digits);
    return juce::String (value).paddedLeft ('0', digits);
}

juce::String hex2 (int v) { return juce::String::toHexString (v & 0xff).toUpperCase().paddedLeft ('0', 2); }

juce::String fxToString (uint8_t fx, uint8_t param) { return hex2 (fx) + hex2 (param); }

juce::String panToString (int pan)
{
    if (pan == 0) return "C";
    return (pan < 0 ? "L" : "R") + juce::String (std::abs (pan));
}

juce::String signedToString (int v)
{
    if (v > 0) return "+" + juce::String (v);
    return juce::String (v);
}

} // namespace ft

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

juce::String fxToString (uint8_t fx, uint8_t param)
{
    // Four Tracker's filter commands are shown with their letters: 0Xvv cutoff, 0Zvv resonance
    if (fx == fxFilterCutoff)    return "0X" + hex2 (param);
    if (fx == fxFilterResonance) return "0Z" + hex2 (param);
    if (fx == fxDistortion)      return "0Y" + hex2 (param);
    if (fx == fxAccent)          return "0V" + hex2 (param);
    return hex2 (fx) + hex2 (param);
}

int filterCommandValue (uint8_t param)
{
    // typed as a decimal number 00-64 (so 0X64 = fully open); hex digits A-F still work as hex
    const int hi = param >> 4, lo = param & 15;
    const int v = (hi <= 9 && lo <= 9) ? hi * 10 + lo : (int) param;
    return juce::jlimit (0, 64, v);
}

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

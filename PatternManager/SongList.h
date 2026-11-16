// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker Pattern Manager - Copyright (C) 2026 elcrispo76
//
// A song as a list of patterns, and the MIDI keys that play it in Four Tracker:
// each pattern has its own key (the start/stop key + 1 + pattern number), as on a MIDI keyboard.

#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace pmgr
{

constexpr int kMaxPatterns = 64;

struct Entry
{
    int pattern = 0;     // 00 .. 3F
    int rows = 64;       // the pattern's length in Four Tracker (1 .. 256)
};

struct Song
{
    std::vector<Entry> entries { Entry{} };
    int bpm = 125;       // as set in Four Tracker (it can't be read over MIDI)
    int speed = 6;       // as set in Four Tracker (6 = 4 rows to a beat at BPM)
    int startKey = 36;   // Four Tracker's START/STOP KEY (default C-2 = 36); pattern n is startKey + 1 + n
    int channel = 1;     // MIDI channel 1 .. 16
    bool loop = false;   // at the end: start again from the top (false: stop)

    int noteFor (int pattern) const { return juce::jlimit (0, 127, startKey + 1 + pattern); }
    /** Seconds one row lasts. */
    double rowSeconds() const { return 15.0 / juce::jmax (1, bpm) * juce::jmax (1, speed) / 6.0; }
    double entrySeconds (int i) const;
    double totalSeconds() const;

    juce::var toVar() const;
    bool fromVar (const juce::var&);
};

/** One MIDI key press the player sends, at a time from PLAY. */
struct KeyEvent
{
    double time;      // seconds after PLAY
    int note;
    bool stop = false;   // the start/stop key at the end
};

/** What the player sends to play the song from position `from`:
    - the first pattern's key, then the start/stop key (start),
    - for each next position with another pattern: its key half way through the one before
      (Four Tracker, set to PATTERN SWITCH = END OF PATTERN, switches exactly at the end),
    - at the end the start/stop key (stop), or, with LOOP, the first pattern's key again. */
std::vector<KeyEvent> schedule (const Song& song, int from, int loops = 1);

/** "C-2", "C#2" ... like Four Tracker writes notes (MIDI 36 = C-2). */
juce::String midiNoteName (int note);

} // namespace pmgr

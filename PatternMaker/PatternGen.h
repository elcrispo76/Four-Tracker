// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker Pattern Maker - Copyright (C) 2026 elcrispo76
//
// The pattern generator: beats, basslines and arps for a style, as 16th-note steps
// (one step = one tracker row, 4 rows to a beat), turned into a Four Tracker pattern.

#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>
#include "TrackerTypes.h"

namespace pm
{

constexpr int kMaxSteps = 64;       // 4 bars of 16ths

enum class LaneKind { Kick, Snare, Clap, ClosedHat, OpenHat, Perc, Bass, Arp, Extra };
constexpr int kNumLanes = 16;       // 1-8: the style's lanes (KICK/SLICE ...), 9-16: EMPTY/SLICE (yours)
constexpr int kNumGenLanes = 8;     // the lanes GENERATE makes

struct Step
{
    bool on = false;
    int  vel = 64;        // 0..64; under 64 becomes a C (volume) command
    int  note = 0;        // bass / arp: semitones from the key's root
    int  len = 1;         // bass / arp: how many steps it sounds (a note off follows)
    bool operator== (const Step& o) const { return on == o.on && vel == o.vel && note == o.note && len == o.len; }
    bool operator!= (const Step& o) const { return ! (*this == o); }
};

struct Lane
{
    LaneKind kind = LaneKind::Kick;
    bool on = true;
    juce::String sound;               // file of the sample (empty: the built-in sound; lanes 9-16: no sound)
    bool slice = false;               // a slice from the SLICER: played at its own pitch, never as notes
    int volume = 64;                  // the instrument's volume, 0..64
    std::array<Step, kMaxSteps> steps {};
};

enum class Style { House, Techno, DnB, Jungle, Breakbeat, Electro, HipHop, Trance, ItaloDisco, Ambient, NumStyles };
enum class Scale { Minor, Major, Dorian, Phrygian, HarmonicMinor, MinorPentatonic, NumScales };

struct Settings
{
    Style style = Style::House;
    int bpm = 124;
    int bars = 4;                     // 1, 2 or 4 (16, 32 or 64 rows)
    int swing = 0;                    // 0..3 ticks: every second 16th starts later (EDx)
    int key = 0;                      // 0 = C .. 11 = B
    Scale scale = Scale::Minor;
    int busy = 50;                    // 0..100: fewer or more extra hits
    bool fill = true;                 // a fill in the last bar
    juce::int64 seed = 1;
};

struct Pattern
{
    Settings settings;
    std::array<Lane, kNumLanes> lanes;
    Pattern()
    {
        for (int i = kNumGenLanes; i < kNumLanes; ++i) { lanes[(size_t) i].kind = LaneKind::Extra; lanes[(size_t) i].on = false; }
    }
    int steps() const { return juce::jlimit (16, kMaxSteps, settings.bars * 16); }
};

juce::String styleName (Style);
int defaultBpm (Style);
int defaultSwing (Style);
juce::String scaleName (Scale);
juce::String keyName (int key);
juce::String laneName (LaneKind);
/** The lane's label: KICK/SLICE ... ARP/SLICE, EMPTY/SLICE 9 ... EMPTY/SLICE 16. */
juce::String laneLabel (int lane);
bool isMelodic (LaneKind k);
/** Plays notes (bass / arp, and not a slice). */
inline bool laneMelodic (const Lane& l) { return isMelodic (l.kind) && ! l.slice; }

/** A fresh pattern for the settings (every lane), from settings.seed. */
Pattern generate (const Settings& s);
/** Makes one lane again (the dice on a lane), keeping the others. */
void regenerateLane (Pattern& p, int lane, juce::int64 seed);
/** The steps of bar 1 copied to the other bars (after editing the grid). */
void copyFirstBar (Pattern& p, int lane);

/** Semitones of the scale's degrees (7, or 5 for pentatonic). */
const std::vector<int>& scaleSteps (Scale s);

/** One tracker cell for a lane's step (inst = the slot the lane's sound is in). */
struct CellOut { int row, channel; ft::Cell cell; };
std::vector<CellOut> toCells (const Pattern& p, const std::array<int, kNumLanes>& slots);

// ---- BLANK + REC: playing a pattern in with pads or keys ---------------------

/** Which MIDI note plays each lane (-1: none). The default is a row of pads from C-2 (36) up, the way
    pad controllers send them (Arturia BeatStep, Akai MPD / MPK, Novation Launchkey ...): pad 1 kick,
    2 snare, 3 clap, 4 closed hat, 5 open hat, 6 perc, 7 bass, 8 arp, 9-16 lanes 9-16. */
struct PadMap
{
    std::array<int, kNumLanes> note { 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51 };
    /** The General MIDI drum map, for drum kits and keyboards: kick 36, rim 37, snare 38, clap 39,
        closed hat 42, open hat 46; bass and arp from the keys. */
    static PadMap generalMidi() { PadMap m; m.note = { 36, 38, 39, 42, 46, 37, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }; return m; }
    int laneFor (int midiNote) const;          // -1: no lane
    void assign (int lane, int midiNote);      // takes the note away from any other lane
};

/** BLANK: every step off (the sounds, volumes and settings stay). */
void clearSteps (Pattern& p);

/** The step a hit lands on: pos = rows since row 0 of the pattern (negative in the count-in,
    over the length after the first time round), to the nearest 16th. -1: too early to record. */
int quantizeStep (double pos, int steps);

/** The MIDI note a lane's root sounds on, for a keyboard: C-3 (48) for the bass, C-4 (60) for the arp. */
int keysRoot (LaneKind k);

/** A step's note (semitones from the key's root, as the generator writes them) for a key played on
    a keyboard: it sounds at that pitch whatever key the pattern is in. */
int noteForKey (const Settings& s, LaneKind k, int midiNote);

/** Writes a hit into a lane: on, with the velocity (1..127, or full if useVelocity is off) and the note. */
void recordHit (Pattern& p, int lane, int step, int velocity127, int note, bool useVelocity);

} // namespace pm

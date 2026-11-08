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

enum class LaneKind { Kick, Snare, Clap, ClosedHat, OpenHat, Perc, Bass, Arp };
constexpr int kNumLanes = 8;

struct Step
{
    bool on = false;
    int  vel = 64;        // 0..64; under 64 becomes a C (volume) command
    int  note = 0;        // bass / arp: semitones from the key's root
    int  len = 1;         // bass / arp: how many steps it sounds (a note off follows)
};

struct Lane
{
    LaneKind kind = LaneKind::Kick;
    bool on = true;
    juce::String sound;               // file of the sample (empty: the built-in sound)
    int volume = 64;                  // the instrument's volume, 0..64
    std::array<Step, kMaxSteps> steps {};
};

enum class Style { House, Techno, DnB, Jungle, Breakbeat, Electro, HipHop, Trance, NumStyles };
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
    int steps() const { return juce::jlimit (16, kMaxSteps, settings.bars * 16); }
};

juce::String styleName (Style);
int defaultBpm (Style);
int defaultSwing (Style);
juce::String scaleName (Scale);
juce::String keyName (int key);
juce::String laneName (LaneKind);
bool isMelodic (LaneKind k);

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

} // namespace pm

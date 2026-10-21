// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <array>
#include <cstdint>

namespace ft
{

constexpr int kNumChannels    = 4;
constexpr int kMaxPatterns    = 64;
constexpr int kMaxRows        = 256;
constexpr int kNumInstruments = 17;      // slot 0 means "no instrument"; usable slots are 01..10 (hex)
constexpr int kNumSlots       = kNumInstruments - 1;   // 16 usable instruments
constexpr int kDefaultRows    = 64;

constexpr uint8_t kNoteNone = 0;
constexpr uint8_t kNoteMax  = 96;        // 1..96 = C-0 .. B-7
constexpr uint8_t kNoteOff  = 97;

// Index of the note that plays a sample at its own sample rate (C-4).
constexpr int kNativeNote = 49;

/** One pattern cell. Effect column is shown as four hex digits: CCPP
    (CC = FastTracker 2 effect number, PP = parameter). */
struct Cell
{
    uint8_t note  = kNoteNone;
    uint8_t inst  = 0;
    uint8_t fx    = 0;
    uint8_t param = 0;

    bool isEmpty() const noexcept { return note == 0 && inst == 0 && fx == 0 && param == 0; }
    void clear() noexcept         { *this = {}; }
};

struct Pattern
{
    std::atomic<int> numRows { kDefaultRows };
    Cell cells[kMaxRows][kNumChannels] {};

    Cell&       at (int row, int ch)       noexcept { return cells[row][ch]; }
    const Cell& at (int row, int ch) const noexcept { return cells[row][ch]; }

    int  getNumRows() const noexcept { return numRows.load(); }
    void clear() noexcept
    {
        for (auto& r : cells) for (auto& c : r) c.clear();
    }
    bool isEmpty() const noexcept
    {
        for (int r = 0; r < getNumRows(); ++r)
            for (int c = 0; c < kNumChannels; ++c)
                if (! cells[r][c].isEmpty()) return false;
        return true;
    }
};

/** How a pattern chosen while playing takes over. */
enum class SwitchMode : int
{
    LegatoNextRow = 0,   // next row, keep row position, notes keep ringing (default)
    LegatoNextBeat,      // on the next 4-row boundary, keep row position
    EndOfPattern,        // when the current pattern finishes
    Restart              // jump now, from row 0
};

// ---- text helpers ---------------------------------------------------------
juce::String noteToString (uint8_t note);        // "C-4", "F#2", "---", "==="
juce::String midiNoteName (int midiNote);         // MIDI 36 -> "C-2" (MIDI 60 = C-4)
juce::String formatNumber (int value, int digits, bool hex);
juce::String hex2 (int v);
juce::String fxToString (uint8_t fx, uint8_t param);   // "0C40" or "0000"
juce::String panToString (int pan);                    // -64..64 -> "L32", "C", "R10"
juce::String signedToString (int v);                   // "0", "+3", "-5"

} // namespace ft

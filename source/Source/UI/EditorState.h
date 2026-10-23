// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include "../PluginProcessor.h"

/** UI-side state shared by all pages of the editor. */
struct EditorState
{
    explicit EditorState (FourTrackerProcessor& p) : proc (p) {}

    FourTrackerProcessor& proc;
    ft::TrackerEngine& engine() { return proc.getEngine(); }

    // pattern cursor
    int cursorRow = 0, cursorCh = 0, cursorCol = 0;   // col: 0 note, 1-2 instrument, 3-6 effect
    bool editMode = false;   // off: note keys only play the selected instrument
    int octave = 4;
    int editStep = 1;

    // block selection (inclusive), valid when hasSelection
    bool hasSelection = false;
    int selRow0 = 0, selCh0 = 0, selRow1 = 0, selCh1 = 0;
    int anchorRow = 0, anchorCh = 0;

    // clipboards
    std::vector<ft::Cell> trackClip;                 // one channel
    std::vector<std::vector<ft::Cell>> blockClip;    // [row][ch]  marked selection (Ctrl+C/X/V)
    std::vector<ft::Cell> patternClip;               // whole pattern, all 4 channels (Ctrl+F3/F4/F5)
    int patternClipRows = 0;

    // undo (whole pattern snapshots)
    struct UndoStep { int pattern; int rows; std::vector<ft::Cell> cells; };
    std::vector<UndoStep> undoStack, redoStack;

    juce::String statusMessage;
    juce::uint32 statusTime = 0;

    bool hex() const noexcept { return proc.hexMode; }

    /** Channels in use (4, 8 or 16) and the first of the 4 shown (the view follows the cursor). */
    int numChannels() { return juce::jlimit (1, ft::kNumChannels, engine().numChannels.load()); }
    int firstVisibleChannel()
    {
        cursorCh = juce::jlimit (0, numChannels() - 1, cursorCh);
        return (cursorCh / ft::kVisibleChannels) * ft::kVisibleChannels;
    }
    int  selectedInstrument() const noexcept { return proc.selectedInstrument; }

    /** The pattern shown in the editor: the playing one while playing. */
    int viewPattern()
    {
        auto& e = engine();
        return e.playing ? e.playPattern.load() : e.selectedPattern.load();
    }

    void setStatus (const juce::String& s)
    {
        statusMessage = s;
        statusTime = juce::Time::getMillisecondCounter();
    }

    std::function<void()> onAnythingChanged;
    std::function<void()> openSampleBrowser;   // set by the editor
    void changed() { if (onAnythingChanged) onAnythingChanged(); }

    void setEditMode (bool on)
    {
        editMode = on;
        setStatus (on ? "EDIT MODE ON - KEYS WRITE NOTES" : "EDIT MODE OFF - KEYS ONLY PLAY");
        changed();
    }

    void pushUndo (int pattern)
    {
        UndoStep u { pattern, engine().getPattern (pattern).getNumRows(), {} };
        auto& pat = engine().getPattern (pattern);
        u.cells.assign (&pat.cells[0][0], &pat.cells[0][0] + ft::kMaxRows * ft::kNumChannels);
        undoStack.push_back (std::move (u));
        if (undoStack.size() > 64) undoStack.erase (undoStack.begin());
        redoStack.clear();
    }

    bool applyUndo (std::vector<UndoStep>& from, std::vector<UndoStep>& to)
    {
        if (from.empty()) return false;
        auto u = std::move (from.back());
        from.pop_back();

        auto& pat = engine().getPattern (u.pattern);
        UndoStep cur { u.pattern, pat.getNumRows(), {} };
        cur.cells.assign (&pat.cells[0][0], &pat.cells[0][0] + ft::kMaxRows * ft::kNumChannels);
        to.push_back (std::move (cur));

        const juce::ScopedLock sl (engine().getDataLock());
        std::copy (u.cells.begin(), u.cells.end(), &pat.cells[0][0]);
        pat.numRows = u.rows;
        return true;
    }

    bool undo() { return applyUndo (undoStack, redoStack); }
    bool redo() { return applyUndo (redoStack, undoStack); }
};

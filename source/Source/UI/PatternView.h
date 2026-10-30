// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include "Widgets.h"
#include "EditorState.h"

/** The four-column pattern grid with cursor, block selection and editing. */
class PatternView : public juce::Component
{
public:
    explicit PatternView (EditorState& s);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** Pattern editing keys. Returns true if the key was used. */
    bool handleKey (const juce::KeyPress&);
    /** Returns the note (1..96) a computer key plays, or -1. */
    int noteForKey (const juce::KeyPress&) const;

    // block / track operations (act on the pattern in view)
    void copyBlock();
    void cutBlock();
    void pasteBlock();
    // whole pattern = 1 block (all 4 channels, every row)
    void copyWholeBlock();
    void cutWholeBlock();
    void pasteWholeBlock();

    void copyTrack();
    void cutTrack();
    void pasteTrack();
    void clearPattern();

    void refreshFromEngine();   // called by the editor timer

    /** CHORD: writes a note (with the selected instrument) at row / channel, optionally
        moving the cursor down SPACING rows afterwards. */
    void writeChordNote (int row, int channel, int note, bool advanceCursor);

private:
    struct Geometry
    {
        float colW, gap, rowH, textH, charW;
        float rowNumX, dividerX, noteX, instX, fxX;
        int visibleRows;
    };
    Geometry geometry() const;
    bool cellAt (juce::Point<float>, int& row, int& ch, int& col) const;
    int topRowFor (int focusRow, int visibleRows) const;

    void moveCursor (int dRow, int dCol, bool extendSelection);
    void setCursorRow (int r, bool extendSelection);
    void updateSelectionFromAnchor();
    /** The cursor moved onto a cell with an instrument number: that instrument is selected. */
    void followInstrument();
    void enterNote (int note);
    void enterHexDigit (int digit);
    void clearField();
    void insertRow();
    void deleteRow();
    ft::Pattern& pattern();
    int numRows();
    void selectionBounds (int& r0, int& r1, int& c0, int& c1);

    EditorState& st;
    int lastPlayRow = -1, lastPattern = -1;
    bool lastPlaying = false;
};

// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#include "PatternView.h"

using namespace juce;
using namespace ft;

PatternView::PatternView (EditorState& s) : st (s)
{
    setWantsKeyboardFocus (false);
    setMouseCursor (MouseCursor::IBeamCursor);
}

Pattern& PatternView::pattern() { return st.engine().getPattern (st.viewPattern()); }
int PatternView::numRows()      { return pattern().getNumRows(); }

PatternView::Geometry PatternView::geometry() const
{
    Geometry g;
    g.gap = 20.0f;
    g.colW = ((float) getWidth() - 3.0f * g.gap) / 4.0f;
    g.rowH = jmin (34.0f, ((float) getHeight() - 10.0f) / 16.0f);
    g.textH = 21.0f;
    g.charW = g.textH / 7.0f * 6.0f;
    g.visibleRows = jmax (1, (int) ((getHeight() - 8.0f) / g.rowH));
    g.rowNumX = 8.0f;
    g.dividerX = g.rowNumX + g.charW * 3.0f + 12.0f;
    g.noteX = g.dividerX + 16.0f;
    g.instX = g.noteX + g.charW * 3.0f + 28.0f;
    g.fxX = g.colW - 10.0f - g.charW * 4.0f;
    return g;
}

int PatternView::topRowFor (int focusRow, int visibleRows) const
{
    return (focusRow / visibleRows) * visibleRows;   // page flip, like the mockup (000-015, 016-031 ...)
}

void PatternView::refreshFromEngine()
{
    auto& e = st.engine();
    const bool playing = e.playing.load();
    const int pr = e.playRow.load(), pp = e.playPattern.load();
    if (playing != lastPlaying || pr != lastPlayRow || pp != lastPattern)
    {
        lastPlaying = playing; lastPlayRow = pr; lastPattern = pp;
        if (st.cursorRow >= numRows()) st.cursorRow = numRows() - 1;
        repaint();
    }
}

void PatternView::paint (Graphics& g)
{
    const auto geo = geometry();
    auto& e = st.engine();
    auto& pat = pattern();
    const int rows = pat.getNumRows();
    const bool playing = e.playing.load();
    const int playRow = e.playRow.load();
    const int focusRow = playing ? playRow : st.cursorRow;
    const int top = topRowFor (jlimit (0, rows - 1, focusRow), geo.visibleRows);
    const bool hex = st.hex();

    int sr0 = 0, sr1 = -1, sc0 = 0, sc1 = -1;
    if (st.hasSelection) selectionBounds (sr0, sr1, sc0, sc1);

    // 4 channels at a time; the view pages sideways with the cursor (CH1-4, CH5-8 ...)
    const int firstCh = st.firstVisibleChannel(), usedCh = st.numChannels();
    for (int v = 0; v < kVisibleChannels; ++v)
    {
        const int ch = firstCh + v;
        const float x0 = v * (geo.colW + geo.gap);
        auto col = Rectangle<float> (x0, 0.0f, geo.colW, (float) getHeight());
        g.setColour (Colour (0xff4a4d53));
        g.fillRect (col);
        g.setColour (Theme::screen);
        g.fillRect (col.reduced (2.0f));
        if (ch >= usedCh) continue;

        const bool muted = e.mix[ch].mute.load();
        const Colour textCol = muted ? Theme::screenDim.darker (0.4f) : Theme::screenText;

        // divider between row numbers and data
        g.setColour (Colour (0xff2c2e33));
        g.fillRect (x0 + geo.dividerX, 2.0f, 2.0f, (float) getHeight() - 4.0f);

        for (int i = 0; i < geo.visibleRows; ++i)
        {
            const int r = top + i;
            if (r >= rows) break;
            const float y = 6.0f + i * geo.rowH;
            auto rowRect = Rectangle<float> (x0 + 2.0f, y, geo.colW - 4.0f, geo.rowH);

            if (r % 16 == 0)      { g.setColour (Colour (0xff121316)); g.fillRect (rowRect); }
            if (playing && r == playRow)
            {
                g.setColour (Colour (0xff3a3d44));
                g.fillRect (rowRect);
            }
            else if (! playing && r == st.cursorRow)
            {
                g.setColour (Colour (0xff24262b));
                g.fillRect (rowRect);
            }

            if (st.hasSelection && r >= sr0 && r <= sr1 && ch >= sc0 && ch <= sc1)
            {
                g.setColour (Colour (0x55aab4ff));
                g.fillRect (rowRect.withTrimmedLeft (geo.dividerX));
            }

            const auto& c = pat.at (r, ch);
            const float ty = y + (geo.rowH - geo.textH) * 0.5f;
            const bool beat = (r % 4) == 0;

            g.setColour (beat ? Theme::screenText : Theme::screenDim);
            PixelFont::drawAt (g, formatNumber (r, 3, hex), x0 + geo.rowNumX, ty, geo.textH, beat);

            g.setColour (c.note ? textCol : textCol.withAlpha (0.55f));
            PixelFont::drawAt (g, noteToString (c.note), x0 + geo.noteX, ty, geo.textH, true);
            g.setColour (c.inst ? textCol : textCol.withAlpha (0.55f));
            PixelFont::drawAt (g, hex2 (c.inst), x0 + geo.instX, ty, geo.textH, true);
            g.setColour ((c.fx | c.param) ? textCol : textCol.withAlpha (0.55f));
            PixelFont::drawAt (g, fxToString (c.fx, c.param), x0 + geo.fxX, ty, geo.textH, true);

            // cursor cell
            if (r == st.cursorRow && ch == st.cursorCh && (! playing || st.editMode))
            {
                Rectangle<float> cr;
                const float pad = 4.0f;
                if (st.cursorCol == 0)      cr = { x0 + geo.noteX - pad, y + 2.0f, geo.charW * 3.0f - 3.0f + pad * 2.0f, geo.rowH - 4.0f };
                else if (st.cursorCol <= 2) cr = { x0 + geo.instX + (st.cursorCol - 1) * geo.charW - pad * 0.5f, y + 2.0f, geo.charW, geo.rowH - 4.0f };
                else                        cr = { x0 + geo.fxX + (st.cursorCol - 3) * geo.charW - pad * 0.5f, y + 2.0f, geo.charW, geo.rowH - 4.0f };

                if (st.editMode)
                {
                    g.setColour (Theme::screenText);
                    g.fillRect (cr);
                    g.setColour (Theme::screen);
                    String s = st.cursorCol == 0 ? noteToString (c.note)
                             : st.cursorCol <= 2 ? hex2 (c.inst).substring (st.cursorCol - 1, st.cursorCol)
                                                 : fxToString (c.fx, c.param).substring (st.cursorCol - 3, st.cursorCol - 2);
                    PixelFont::drawAt (g, s, cr.getX() + (st.cursorCol == 0 ? pad : pad * 0.5f), ty, geo.textH, true);
                }
                else
                {
                    g.setColour (Theme::screenText);
                    g.drawRect (cr, 2.0f);
                }
            }
        }
    }
}

bool PatternView::cellAt (Point<float> p, int& row, int& ch, int& col) const
{
    const auto geo = geometry();
    const int v = jlimit (0, kVisibleChannels - 1, (int) (p.x / (geo.colW + geo.gap)));
    const float lx = p.x - v * (geo.colW + geo.gap);
    auto& es = const_cast<EditorState&> (st);
    ch = jlimit (0, es.numChannels() - 1, es.firstVisibleChannel() + v);
    auto& e = st.engine();
    auto& pat = const_cast<PatternView*> (this)->pattern();
    const int rows = pat.getNumRows();
    const int focusRow = e.playing ? e.playRow.load() : st.cursorRow;
    const int top = topRowFor (jlimit (0, rows - 1, focusRow), geo.visibleRows);
    row = jlimit (0, rows - 1, top + (int) ((p.y - 6.0f) / geo.rowH));

    if (lx < geo.instX - 10.0f)       col = 0;
    else if (lx < geo.fxX - 10.0f)    col = jlimit (1, 2, 1 + (int) ((lx - geo.instX) / geo.charW));
    else                              col = jlimit (3, 6, 3 + (int) ((lx - geo.fxX) / geo.charW));
    return true;
}

void PatternView::mouseDown (const MouseEvent& e)
{
    int r, c, col;
    if (! cellAt (e.position, r, c, col)) return;

    if (e.mods.isShiftDown())
    {
        if (! st.hasSelection) { st.anchorRow = st.cursorRow; st.anchorCh = st.cursorCh; }
        st.cursorRow = r; st.cursorCh = c; st.cursorCol = col;
        updateSelectionFromAnchor();
    }
    else
    {
        st.hasSelection = false;
        st.anchorRow = r; st.anchorCh = c;
        st.cursorRow = r; st.cursorCh = c; st.cursorCol = col;
    }
    st.changed();
    repaint();
}

void PatternView::mouseDrag (const MouseEvent& e)
{
    int r, c, col;
    if (! cellAt (e.position, r, c, col)) return;
    if (r == st.anchorRow && c == st.anchorCh && ! st.hasSelection) return;
    st.cursorRow = r; st.cursorCh = c;
    updateSelectionFromAnchor();
    repaint();
}

void PatternView::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    if (st.engine().playing) return;
    setCursorRow (st.cursorRow + (w.deltaY > 0 ? -4 : 4), false);
}

void PatternView::updateSelectionFromAnchor()
{
    st.hasSelection = true;
    st.selRow0 = st.anchorRow; st.selCh0 = st.anchorCh;
    st.selRow1 = st.cursorRow; st.selCh1 = st.cursorCh;
}

void PatternView::selectionBounds (int& r0, int& r1, int& c0, int& c1)
{
    r0 = jmin (st.selRow0, st.selRow1); r1 = jmax (st.selRow0, st.selRow1);
    c0 = jmin (st.selCh0, st.selCh1);   c1 = jmax (st.selCh0, st.selCh1);
    r1 = jmin (r1, numRows() - 1);
}

void PatternView::setCursorRow (int r, bool extend)
{
    const int rows = numRows();
    if (! extend && st.hasSelection) st.hasSelection = false;
    if (extend && ! st.hasSelection) { st.anchorRow = st.cursorRow; st.anchorCh = st.cursorCh; }
    st.cursorRow = ((r % rows) + rows) % rows;
    if (extend) updateSelectionFromAnchor();
    st.changed();
    repaint();
}

void PatternView::moveCursor (int dRow, int dCol, bool extend)
{
    if (dCol != 0)
    {
        if (extend)
        {
            if (! st.hasSelection) { st.anchorRow = st.cursorRow; st.anchorCh = st.cursorCh; }
            st.cursorCh = jlimit (0, st.numChannels() - 1, st.cursorCh + (dCol > 0 ? 1 : -1));
            updateSelectionFromAnchor();
        }
        else
        {
            st.hasSelection = false;
            int c = st.cursorCh * 7 + st.cursorCol + dCol;
            c = (c + st.numChannels() * 7) % (st.numChannels() * 7);
            st.cursorCh = c / 7;
            st.cursorCol = c % 7;
        }
        st.changed();
        repaint();
    }
    if (dRow != 0) setCursorRow (st.cursorRow + dRow, extend);
}

// ---------------------------------------------------------------------------
// keyboard
// ---------------------------------------------------------------------------

int PatternView::noteForKey (const KeyPress& k) const
{
    if (k.getModifiers().isCtrlDown() || k.getModifiers().isAltDown() || k.getModifiers().isCommandDown()) return -1;

    static const char lower[] = "ZSXDCVGBHNJM,L.;/";
    static const char upper[] = "Q2W3ER5T6Y7UI9O0P[=]";
    const int code = k.getKeyCode();
    const juce_wchar ch = (code >= 'a' && code <= 'z') ? (juce_wchar) (code - 32) : (juce_wchar) code;

    int semi = -1;
    for (int i = 0; lower[i] != 0; ++i) if ((juce_wchar) lower[i] == ch) { semi = i; break; }
    if (semi < 0) for (int i = 0; upper[i] != 0; ++i) if ((juce_wchar) upper[i] == ch) { semi = i + 12; break; }
    if (semi < 0) return -1;

    const int note = st.octave * 12 + semi + 1;
    return (note >= 1 && note <= kNoteMax) ? note : -1;
}

bool PatternView::handleKey (const KeyPress& k)
{
    const auto mods = k.getModifiers();
    const int code = k.getKeyCode();
    const bool shift = mods.isShiftDown();
    const bool ctrl = mods.isCtrlDown() || mods.isCommandDown();

    // up / down jump SPACING rows (Shift+arrows still select one row at a time)
    const int rowStep = shift ? 1 : jmax (1, st.editStep);
    if (code == KeyPress::upKey)       { moveCursor (-rowStep, 0, shift); return true; }
    if (code == KeyPress::downKey)     { moveCursor (rowStep, 0, shift);  return true; }
    if (code == KeyPress::leftKey)     { moveCursor (0, -1, shift); return true; }
    if (code == KeyPress::rightKey)    { moveCursor (0, 1, shift);  return true; }
    if (code == KeyPress::pageUpKey)   { setCursorRow (jmax (0, st.cursorRow - 16), shift); return true; }
    if (code == KeyPress::pageDownKey) { setCursorRow (jmin (numRows() - 1, st.cursorRow + 16), shift); return true; }
    if (code == KeyPress::homeKey)     { setCursorRow (0, shift); return true; }
    if (code == KeyPress::endKey)      { setCursorRow (numRows() - 1, shift); return true; }
    if (code == KeyPress::tabKey)
    {
        st.hasSelection = false;
        st.cursorCh = (st.cursorCh + (shift ? st.numChannels() - 1 : 1)) % st.numChannels();
        st.cursorCol = 0;
        st.changed(); repaint();
        return true;
    }

    if (ctrl && (code == 'Z' || code == 'z')) { if (st.undo()) { st.setStatus ("UNDO"); repaint(); } return true; }
    if (ctrl && (code == 'Y' || code == 'y')) { if (st.redo()) { st.setStatus ("REDO"); repaint(); } return true; }
    if (ctrl && (code == 'C' || code == 'c')) { copyBlock();  return true; }
    if (ctrl && (code == 'X' || code == 'x')) { cutBlock();   return true; }
    if (ctrl && (code == 'V' || code == 'v')) { pasteBlock(); return true; }
    if (ctrl && (code == 'A' || code == 'a'))
    {
        st.anchorRow = 0; st.anchorCh = 0; st.cursorRow = numRows() - 1; st.cursorCh = st.numChannels() - 1;
        updateSelectionFromAnchor(); repaint();
        return true;
    }

    // F3 = cut, F4 = copy, F5 = paste (ProTracker order).
    // Shift: one track (channel).  Ctrl: the whole block, all 4 channels.
    if (code == KeyPress::F3Key && (shift || ctrl)) { ctrl ? cutWholeBlock()   : cutTrack();   return true; }
    if (code == KeyPress::F4Key && (shift || ctrl)) { ctrl ? copyWholeBlock()  : copyTrack();  return true; }
    if (code == KeyPress::F5Key && (shift || ctrl)) { ctrl ? pasteWholeBlock() : pasteTrack(); return true; }

    if (! st.editMode) return false;

    if (code == KeyPress::deleteKey)    { clearField(); return true; }
    if (code == KeyPress::insertKey)    { insertRow();  return true; }
    if (code == KeyPress::backspaceKey) { deleteRow();  return true; }

    if (ctrl || mods.isAltDown()) return false;

    if (st.cursorCol == 0)
    {
        if (code == '1' || code == '`') { enterNote (kNoteOff); return true; }
        const int n = noteForKey (k);
        if (n > 0) { enterNote (n); return true; }
        return false;
    }

    const auto c = (juce_wchar) k.getTextCharacter();

    // X / Z / Y / V in the effect number: Four Tracker's sound commands
    // (0Xvv cutoff, 0Zvv resonance, 0Yvv distortion, 0Vvv accent)
    if (st.cursorCol == 3 || st.cursorCol == 4)
    {
        uint8_t cmd = 0;
        switch (CharacterFunctions::toUpperCase (c))
        {
            case 'X': cmd = fxFilterCutoff; break;
            case 'Z': cmd = fxFilterResonance; break;
            case 'Y': cmd = fxDistortion; break;
            case 'V': cmd = fxAccent; break;
            default: break;
        }
        if (cmd != 0)
        {
            st.pushUndo (st.viewPattern());
            pattern().at (st.cursorRow, st.cursorCh).fx = cmd;
            setCursorRow (st.cursorRow + st.editStep, false);
            return true;
        }
    }

    const int digit = CharacterFunctions::getHexDigitValue (c);
    if (digit >= 0) { enterHexDigit (digit); return true; }
    return false;
}

void PatternView::enterNote (int note)
{
    const int p = st.viewPattern();
    st.pushUndo (p);
    auto& c = pattern().at (st.cursorRow, st.cursorCh);
    c.note = (uint8_t) note;
    if (note != kNoteOff) c.inst = (uint8_t) st.selectedInstrument();
    else c.inst = 0;
    if (note != kNoteOff && ! st.engine().playing)
        st.engine().requestPreview (st.selectedInstrument(), note);
    setCursorRow (st.cursorRow + st.editStep, false);
}

void PatternView::enterHexDigit (int d)
{
    const int p = st.viewPattern();
    st.pushUndo (p);
    auto& c = pattern().at (st.cursorRow, st.cursorCh);
    switch (st.cursorCol)
    {
        case 1: c.inst = (uint8_t) jlimit (0, kNumInstruments - 1, (d << 4) | (c.inst & 15)); break;
        case 2: c.inst = (uint8_t) jlimit (0, kNumInstruments - 1, (c.inst & 0xf0) | d); break;
        case 3: c.fx = (uint8_t) ((d << 4) | (c.fx & 15)); break;
        case 4: c.fx = (uint8_t) ((c.fx & 0xf0) | d); break;
        case 5: c.param = (uint8_t) ((d << 4) | (c.param & 15)); break;
        case 6: c.param = (uint8_t) ((c.param & 0xf0) | d); break;
        default: break;
    }
    setCursorRow (st.cursorRow + st.editStep, false);
}

void PatternView::clearField()
{
    st.pushUndo (st.viewPattern());
    if (st.hasSelection)
    {
        int r0, r1, c0, c1; selectionBounds (r0, r1, c0, c1);
        for (int r = r0; r <= r1; ++r) for (int c = c0; c <= c1; ++c) pattern().at (r, c).clear();
        repaint();
        return;
    }
    auto& c = pattern().at (st.cursorRow, st.cursorCh);
    if (st.cursorCol == 0)      { c.note = 0; c.inst = 0; }
    else if (st.cursorCol <= 2) c.inst = 0;
    else                        { c.fx = 0; c.param = 0; }
    setCursorRow (st.cursorRow + st.editStep, false);
}

void PatternView::insertRow()
{
    st.pushUndo (st.viewPattern());
    const ScopedLock sl (st.engine().getDataLock());
    auto& pat = pattern();
    for (int r = pat.getNumRows() - 1; r > st.cursorRow; --r) pat.at (r, st.cursorCh) = pat.at (r - 1, st.cursorCh);
    pat.at (st.cursorRow, st.cursorCh).clear();
    repaint();
}

void PatternView::deleteRow()
{
    st.pushUndo (st.viewPattern());
    const ScopedLock sl (st.engine().getDataLock());
    auto& pat = pattern();
    const int rows = pat.getNumRows();
    for (int r = st.cursorRow; r < rows - 1; ++r) pat.at (r, st.cursorCh) = pat.at (r + 1, st.cursorCh);
    pat.at (rows - 1, st.cursorCh).clear();
    repaint();
}

// ---------------------------------------------------------------------------
// block / track operations
// ---------------------------------------------------------------------------

void PatternView::copyBlock()
{
    int r0, r1, c0, c1;
    if (st.hasSelection) selectionBounds (r0, r1, c0, c1);
    else { r0 = r1 = st.cursorRow; c0 = c1 = st.cursorCh; }

    st.blockClip.clear();
    for (int r = r0; r <= r1; ++r)
    {
        std::vector<Cell> line;
        for (int c = c0; c <= c1; ++c) line.push_back (pattern().at (r, c));
        st.blockClip.push_back (std::move (line));
    }
    st.setStatus ("COPIED BLOCK " + String (r1 - r0 + 1) + " ROWS X " + String (c1 - c0 + 1) + " CH");
    st.changed();
}

void PatternView::cutBlock()
{
    copyBlock();
    int r0, r1, c0, c1;
    if (st.hasSelection) selectionBounds (r0, r1, c0, c1);
    else { r0 = r1 = st.cursorRow; c0 = c1 = st.cursorCh; }
    st.pushUndo (st.viewPattern());
    for (int r = r0; r <= r1; ++r) for (int c = c0; c <= c1; ++c) pattern().at (r, c).clear();
    st.setStatus ("CUT BLOCK");
    repaint();
}

void PatternView::pasteBlock()
{
    if (st.blockClip.empty()) { st.setStatus ("BLOCK CLIPBOARD IS EMPTY"); st.changed(); return; }
    st.pushUndo (st.viewPattern());
    const ScopedLock sl (st.engine().getDataLock());
    auto& pat = pattern();
    for (size_t r = 0; r < st.blockClip.size(); ++r)
        for (size_t c = 0; c < st.blockClip[r].size(); ++c)
        {
            const int rr = st.cursorRow + (int) r, cc = st.cursorCh + (int) c;
            if (rr < pat.getNumRows() && cc < st.numChannels()) pat.at (rr, cc) = st.blockClip[r][c];
        }
    st.setStatus ("PASTED BLOCK");
    repaint();
}

void PatternView::copyWholeBlock()
{
    auto& pat = pattern();
    const int rows = pat.getNumRows();
    st.patternClip.assign (&pat.cells[0][0], &pat.cells[0][0] + rows * kNumChannels);
    st.patternClipRows = rows;
    st.setStatus ("COPIED BLOCK - PATTERN " + formatNumber (st.viewPattern(), 2, st.hex()) + ", " + String (rows) + " ROWS X " + String (st.numChannels()) + " CH");
    st.changed();
}

void PatternView::cutWholeBlock()
{
    copyWholeBlock();
    st.pushUndo (st.viewPattern());
    {
        const ScopedLock sl (st.engine().getDataLock());
        pattern().clear();
    }
    st.hasSelection = false;
    st.setStatus ("CUT BLOCK - PATTERN " + formatNumber (st.viewPattern(), 2, st.hex()));
    st.changed();
    repaint();
}

void PatternView::pasteWholeBlock()
{
    if (st.patternClipRows <= 0) { st.setStatus ("BLOCK CLIPBOARD IS EMPTY - USE CTRL+F3 FIRST"); st.changed(); return; }
    st.pushUndo (st.viewPattern());
    {
        const ScopedLock sl (st.engine().getDataLock());
        auto& pat = pattern();
        pat.clear();
        std::copy (st.patternClip.begin(), st.patternClip.end(), &pat.cells[0][0]);
        pat.numRows = st.patternClipRows;
    }
    if (st.cursorRow >= st.patternClipRows) st.cursorRow = st.patternClipRows - 1;
    st.hasSelection = false;
    st.setStatus ("PASTED BLOCK INTO PATTERN " + formatNumber (st.viewPattern(), 2, st.hex()));
    st.changed();
    repaint();
}

void PatternView::copyTrack()
{
    auto& pat = pattern();
    st.trackClip.clear();
    for (int r = 0; r < pat.getNumRows(); ++r) st.trackClip.push_back (pat.at (r, st.cursorCh));
    st.setStatus ("COPIED TRACK CH" + String (st.cursorCh + 1));
    st.changed();
}

void PatternView::cutTrack()
{
    copyTrack();
    st.pushUndo (st.viewPattern());
    auto& pat = pattern();
    for (int r = 0; r < pat.getNumRows(); ++r) pat.at (r, st.cursorCh).clear();
    st.setStatus ("CUT TRACK CH" + String (st.cursorCh + 1));
    repaint();
}

void PatternView::pasteTrack()
{
    if (st.trackClip.empty()) { st.setStatus ("TRACK CLIPBOARD IS EMPTY"); st.changed(); return; }
    st.pushUndo (st.viewPattern());
    const ScopedLock sl (st.engine().getDataLock());
    auto& pat = pattern();
    for (int r = 0; r < pat.getNumRows(); ++r)
        pat.at (r, st.cursorCh) = r < (int) st.trackClip.size() ? st.trackClip[(size_t) r] : Cell();
    st.setStatus ("PASTED TRACK TO CH" + String (st.cursorCh + 1));
    repaint();
}

void PatternView::clearPattern()
{
    st.pushUndo (st.viewPattern());
    const ScopedLock sl (st.engine().getDataLock());
    pattern().clear();
    repaint();
}

// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#include "HeaderBar.h"

using namespace juce;
using namespace ft;

// ===========================================================================
// TitleBar
// ===========================================================================

String TitleBar::buildStatus()
{
    auto& e = st.engine();
    const bool hex = st.hex();
    String s;

    if (e.playing)
    {
        s << "PLAY " << formatNumber (e.playPattern.load(), 2, hex) << ":" << formatNumber (e.playRow.load(), 3, hex);
        const int q = e.queuedPattern.load();
        if (q >= 0) s << ">" << formatNumber (q, 2, hex);
    }
    else
        s << "STOPPED";

    s << "  " << (st.editMode ? "EDIT" : "JAM") << " OCT" << st.octave << " STEP" << st.editStep;

    if (st.statusMessage.isNotEmpty() && Time::getMillisecondCounter() - st.statusTime < 3000)
        s = st.statusMessage;

    return s;
}

void TitleBar::refresh()
{
    auto s = buildStatus();
    const int view = st.numChannels() * 100 + st.firstVisibleChannel();
    if (s != lastStatus || view != lastView) { lastStatus = s; lastView = view; repaint(); }
}

void TitleBar::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::titleBar);
    g.fillRect (r);

    auto titleArea = r.removeFromLeft (r.getWidth() * 0.43f).reduced (4.0f);
    g.setColour (Theme::panelEdgeHi.darker (0.05f));
    g.fillRect (titleArea);
    g.setColour (Theme::ink);
    PixelFont::draw (g, "FOUR TRACKER", titleArea.reduced (40.0f, 0.0f), 42.0f, Justification::centredLeft, true, 1.4f);

    auto grid = r.removeFromRight (r.getHeight()).reduced (10.0f);
    Theme::iconGrid (g, grid, Theme::screenText);

    r.removeFromRight (rightReserve);
    auto statusArea = r.reduced (24.0f, 0.0f);
    const int n = st.numChannels(), first = st.firstVisibleChannel();
    const String what = n <= kVisibleChannels ? String (n) + " TRACK SAMPLE TRACKER"
                      : "TRACKS " + String (first + 1) + "-" + String (jmin (n, first + kVisibleChannels)) + " OF " + String (n);
    Theme::text (g, what, statusArea, 15.0f, Theme::screenText, Justification::centredLeft);
    g.reduceClipRegion (statusArea.withTrimmedLeft (PixelFont::getWidth (what, 15.0f) + 16.0f).toNearestInt());
    Theme::text (g, lastStatus, statusArea.withTrimmedRight (6.0f), 15.0f, Theme::screenDim.brighter (0.4f), Justification::centredRight);
}

void TitleBar::mouseDown (const MouseEvent& e)
{
    if (e.x > getWidth() - getHeight() && onGridClicked) onGridClicked();
}

// ===========================================================================
// HeaderControls
// ===========================================================================

HeaderControls::HeaderControls (EditorState& s) : st (s)
{
    for (auto* c : std::initializer_list<Component*> { &patternBox, &patPrev, &patNext, &instBox, &instPrev, &instNext,
                                                        &instName, &loop, &speedBox, &bpmBox, &octaveBox, &spacingBox, &editBtn, &hexBtn, &decBtn })
        addAndMakeVisible (c);

    auto hexFmt = [this] (int v) { return formatNumber (v, 2, st.hex()); };
    patternBox.formatter = hexFmt;
    instBox.formatter = hexFmt;
    auto hexParse = [this] (const String& t, bool& ok)
    {
        const auto txt = t.trim();
        ok = txt.isNotEmpty() && txt.containsOnly (st.hex() ? "0123456789abcdefABCDEF" : "0123456789");
        return st.hex() ? txt.getHexValue32() : txt.getIntValue();
    };
    patternBox.parser = hexParse;
    instBox.parser = hexParse;
    patternBox.setDragSensitivity (12.0f);
    instBox.setDragSensitivity (12.0f);

    patternBox.onChange = [this] (int v) { st.engine().requestPattern (v); };
    patPrev.onClick = [this] { st.engine().requestPattern (jmax (0, st.engine().selectedPattern.load() - 1)); };
    patNext.onClick = [this] { st.engine().requestPattern (jmin (kMaxPatterns - 1, st.engine().selectedPattern.load() + 1)); };

    instBox.onChange = [this] (int v) { selectInstrument (v); };
    instPrev.onClick = [this] { selectInstrument (st.proc.selectedInstrument - 1); };
    instNext.onClick = [this] { selectInstrument (st.proc.selectedInstrument + 1); };
    instName.editable = false;

    loop.onChange = [this] (bool on) { st.engine().loopPattern = on; };
    speedBox.onChange = [this] (int v) { st.engine().speed = v; };
    bpmBox.onChange = [this] (int v) { st.engine().bpm = v; };
    bpmBox.setDragSensitivity (2.0f);

    octaveBox.setShowSpinner (true);
    octaveBox.setDragSensitivity (14.0f);
    octaveBox.onChange = [this] (int v) { st.octave = v; st.changed(); };

    spacingBox.setShowSpinner (true);
    spacingBox.setDragSensitivity (10.0f);
    spacingBox.onChange = [this] (int v) { st.editStep = v; st.changed(); };

    editBtn.onClick = [this] { st.setEditMode (! st.editMode); };
    editBtn.setTooltip ("Edit mode (Space): when lit, note keys write into the pattern");

    hexBtn.onClick = [this] { st.proc.hexMode = true;  st.changed(); };
    decBtn.onClick = [this] { st.proc.hexMode = false; st.changed(); };

    refresh();
}

void HeaderControls::selectInstrument (int i)
{
    i = jlimit (1, kNumInstruments - 1, i);
    st.proc.selectedInstrument = i;
    refresh();
    if (onInstrumentSelected) onInstrumentSelected();
    st.changed();
}

void HeaderControls::refresh()
{
    auto& e = st.engine();
    patternBox.setValue (e.selectedPattern.load());
    instBox.setValue (st.proc.selectedInstrument);
    instName.setText (st.proc.getInstrument (st.proc.selectedInstrument).name);
    loop.setState (e.loopPattern.load());
    speedBox.setValue (e.speed.load());
    bpmBox.setValue (e.bpm.load());
    octaveBox.setValue (st.octave);
    spacingBox.setValue (st.editStep);
    editBtn.setToggleState (st.editMode, dontSendNotification);
    hexBtn.setToggleState (st.hex(), dontSendNotification);
    decBtn.setToggleState (! st.hex(), dontSendNotification);
    patternBox.repaint();
    instBox.repaint();
}

void HeaderControls::paint (Graphics& g)
{
    auto title = [&g] (Rectangle<int> p, const String& t)
    {
        Theme::drawPanel (g, p.toFloat());
        Theme::text (g, t, Rectangle<float> ((float) p.getX() + 14.0f, (float) p.getY() + 12.0f, 300.0f, 17.0f), 17.0f, Theme::ink, Justification::topLeft);
    };
    title (patPanel, "PATTERN");
    title (instPanel, "INSTRUMENT");
    title (loopPanel, "LOOP");
    title (speedPanel, "SPEED");
    title (bpmPanel, "BPM");
    title (octavePanel, "OCTAVE");
    Theme::drawPanel (g, spacingPanel.toFloat());
    Theme::text (g, "SPACING", Rectangle<float> ((float) spacingPanel.getX() + 10.0f, 13.0f, 90.0f, 15.0f), 14.0f, Theme::ink, Justification::topLeft);
    Theme::drawPanel (g, editPanel.toFloat());
    Theme::drawPanel (g, hexPanel.toFloat());
}

void HeaderControls::resized()
{
    // Coordinates follow the 1536x1024 mockup, relative to this component (x0 = 14, y0 = 84).
    patPanel     = { 0,    0, 300, 106 };
    instPanel    = { 304,  0, 404, 106 };
    loopPanel    = { 712,  0, 140, 106 };
    speedPanel   = { 856,  0, 104, 106 };
    bpmPanel     = { 964,  0, 124, 106 };
    octavePanel  = { 1092, 0, 104, 106 };
    spacingPanel = { 1200, 0, 104, 106 };
    editPanel    = { 1308, 0, 104, 106 };
    hexPanel     = { 1416, 0, 92, 106 };

    patternBox.setBounds (20, 36, 102, 54);
    patPrev.setBounds (130, 36, 74, 54);
    patNext.setBounds (210, 36, 76, 54);

    instBox.setBounds (instPanel.getX() + 16, 36, 92, 54);
    instPrev.setBounds (instPanel.getX() + 114, 36, 62, 54);
    instNext.setBounds (instPanel.getX() + 180, 36, 62, 54);
    instName.setBounds (instPanel.getX() + 248, 36, 144, 54);

    loop.setBounds (loopPanel.getX() + 12, 36, 116, 54);
    speedBox.setBounds (speedPanel.getX() + 12, 36, 80, 54);
    bpmBox.setBounds (bpmPanel.getX() + 12, 36, 100, 54);
    octaveBox.setBounds (octavePanel.getX() + 12, 36, 80, 54);
    spacingBox.setBounds (spacingPanel.getX() + 12, 36, 80, 54);
    editBtn.setBounds (editPanel.getX() + 10, 10, 84, 86);

    hexBtn.setBounds (hexPanel.getX() + 8, 10, 76, 42);
    decBtn.setBounds (hexPanel.getX() + 8, 56, 76, 40);
}

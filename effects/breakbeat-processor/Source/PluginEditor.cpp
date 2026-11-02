// SPDX-License-Identifier: AGPL-3.0-or-later
// Breakbeat Processor - Copyright (C) 2026 elcrispo76

#include "PluginEditor.h"
#include "PixelFont.h"     // the Amiga-style pixel font from Four Tracker

using namespace juce;

// ---------------------------------------------------------------------------
// look (the same grey panels, black screens and white keys as Four Tracker)
// ---------------------------------------------------------------------------
namespace Look
{
    const Colour background  { 0xffb3b7c0 };
    const Colour panel       { 0xffc4c7cf };
    const Colour edgeHi      { 0xffe4e6eb };
    const Colour edgeLo      { 0xff878b94 };
    const Colour ink         { 0xff0c0d0f };
    const Colour screen      { 0xff040405 };
    const Colour screenText  { 0xffeceef2 };
    const Colour screenDim   { 0xff8c9098 };
    const Colour key         { 0xfff7f8fa };
    const Colour red         { 0xffd9342b };
    const Colour redDark     { 0xff5a1a17 };

    void text (Graphics& g, const String& t, Rectangle<float> r, float h, Colour c,
               Justification j = Justification::centredLeft, bool bold = false)
    {
        g.setColour (c);
        PixelFont::draw (g, t, r, h, j, bold);
    }

    void panelBox (Graphics& g, Rectangle<float> r)
    {
        g.setColour (panel);
        g.fillRect (r);
        g.setColour (edgeHi);
        g.fillRect (r.removeFromTop (2.0f));
        g.fillRect (r.removeFromLeft (2.0f));
        g.setColour (edgeLo);
        g.fillRect (r.removeFromBottom (2.0f));
        g.fillRect (r.removeFromRight (2.0f));
    }

    void screenBox (Graphics& g, Rectangle<float> r)
    {
        g.setColour (edgeLo);
        g.fillRect (r.expanded (2.0f));
        g.setColour (screen);
        g.fillRect (r);
    }

    /** A rounded white key with a thick black rim, like the sketch. */
    void key3d (Graphics& g, Rectangle<float> r, Colour fill, bool down)
    {
        const float rad = jmin (10.0f, r.getHeight() * 0.18f);
        if (! down) { g.setColour (Colours::black.withAlpha (0.25f)); g.fillRoundedRectangle (r.translated (2.0f, 3.0f), rad); }
        else r = r.translated (1.0f, 2.0f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, rad);
        g.setColour (ink);
        g.drawRoundedRectangle (r.reduced (1.5f), rad, 3.0f);
    }

    void iconRev (Graphics& g, Rectangle<float> r, Colour c)
    {
        const float w = r.getWidth(), h = r.getHeight();
        Path p;
        p.startNewSubPath (r.getX(), r.getCentreY());
        p.lineTo (r.getX() + w * 0.45f, r.getY());
        p.lineTo (r.getX() + w * 0.45f, r.getY() + h * 0.28f);
        p.lineTo (r.getRight(), r.getY() + h * 0.28f);
        p.lineTo (r.getRight(), r.getBottom() - h * 0.28f);
        p.lineTo (r.getX() + w * 0.45f, r.getBottom() - h * 0.28f);
        p.lineTo (r.getX() + w * 0.45f, r.getBottom());
        p.closeSubPath();
        g.setColour (c);
        g.fillPath (p);
    }

    void iconStop (Graphics& g, Rectangle<float> r, Colour c)
    {
        g.setColour (c);
        g.fillRect (r);
    }

    /** What a step or a tool shows for an action. */
    void drawAction (Graphics& g, int action, Rectangle<float> r, float textH, Colour c)
    {
        if (action == bb::Rev)
        {
            const float s = jmin (r.getWidth() * 0.55f, r.getHeight() * 0.5f);
            iconRev (g, Rectangle<float> (s, s * 0.8f).withCentre (r.getCentre()), c);
        }
        else if (action == bb::Stop)
        {
            const float s = jmin (r.getWidth(), r.getHeight()) * 0.42f;
            iconStop (g, Rectangle<float> (s, s).withCentre (r.getCentre()), c);
        }
        else if (action != bb::Thru)
        {
            text (g, bb::actionName (action), r, textH, c, Justification::centred, true);
        }
    }
}

// ---------------------------------------------------------------------------
// a step pad: LED on top, the key, the number under
// ---------------------------------------------------------------------------
struct BreakbeatEditor::StepPad : public Component, public SettableTooltipClient
{
    StepPad (BreakbeatEditor& e, int i) : ed (e), index (i)
    {
        setTooltip ("Step " + String (i + 1) + ". Click with a tool to set it, or right-click for the menu.");
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const bool lit = ed.getPlayingStep() == index;
        const int action = ed.getStepAction (index);

        // LED
        auto led = r.removeFromTop (14.0f).withSizeKeepingCentre (r.getWidth() * 0.5f, 8.0f);
        g.setColour (Look::edgeLo);
        g.fillRect (led.expanded (1.5f));
        g.setColour (lit ? Look::red : Look::redDark);
        g.fillRect (led);
        if (lit) { g.setColour (Look::red.withAlpha (0.35f)); g.fillRect (led.expanded (4.0f, 3.0f)); }
        r.removeFromTop (8.0f);

        // number, and the pattern row the step starts on
        auto num = r.removeFromBottom (46.0f);
        Look::text (g, String (index + 1), num.withTrimmedTop (6.0f).removeFromTop (24.0f), 22.0f, Look::ink, Justification::centredTop, true);
        Look::text (g, "ROW " + String (index * ed.getRowsPerStep()).paddedLeft ('0', 2), num.removeFromBottom (12.0f), 9.0f,
                    Look::edgeLo.darker (0.5f), Justification::centredTop);

        // the key
        auto k = r.reduced (2.0f, 0.0f).withTrimmedBottom (4.0f);
        const Colour fill = lit ? Colour (0xffffe7e3) : (hover && ed.getBrush() >= 0 ? Colour (0xffeef2ff) : Look::key);
        Look::key3d (g, k, fill, down);
        if (down) k = k.translated (1.0f, 2.0f);
        Look::drawAction (g, action, k.reduced (8.0f), 22.0f, action == bb::Bit ? Look::red : Look::ink);
        if (action == bb::Thru)
            Look::text (g, "-", k, 14.0f, Look::edgeLo, Justification::centred);
    }

    void mouseEnter (const MouseEvent&) override { hover = true; repaint(); }
    void mouseExit  (const MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { ed.showStepMenu (index); return; }
        down = true; repaint();
        ed.stepPressed (index, e);
    }
    void mouseDrag (const MouseEvent& e) override { ed.stepDragged (e); }
    void mouseUp (const MouseEvent&) override { down = false; repaint(); }

    BreakbeatEditor& ed;
    const int index;
    bool hover = false, down = false;
};

// ---------------------------------------------------------------------------
// palette button
// ---------------------------------------------------------------------------
struct BreakbeatEditor::ToolButton : public Component, public SettableTooltipClient
{
    ToolButton (int a, String t = {}) : action (a), label (std::move (t)) {}

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f, 2.0f).withTrimmedBottom (2.0f).withTrimmedRight (2.0f);
        Look::key3d (g, r, selected ? Look::ink : Look::key, down || selected);
        if (down || selected) r = r.translated (1.0f, 2.0f);
        const Colour c = selected ? Look::screenText : Look::ink;
        if (label.isNotEmpty()) Look::text (g, label, r, 13.0f, c, Justification::centred, true);
        else Look::drawAction (g, action, r.reduced (8.0f, 6.0f), 15.0f, c);
    }

    void mouseDown (const MouseEvent&) override { down = true; repaint(); }
    void mouseUp (const MouseEvent& e) override
    {
        down = false; repaint();
        if (getLocalBounds().contains (e.getPosition()) && onClick) onClick();
    }

    void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }

    const int action;
    String label;
    bool selected = false, down = false;
    std::function<void()> onClick;
};

// ---------------------------------------------------------------------------
// knob, round disc with a line, value in a small screen (Four Tracker style)
// ---------------------------------------------------------------------------
struct BreakbeatEditor::Knob : public Component, public SettableTooltipClient
{
    Knob (RangedAudioParameter& p, String name, std::function<String (int)> fmt)
        : param (p), label (std::move (name)), format (std::move (fmt)),
          attach (p, [this] (float) { repaint(); }, nullptr)
    {
        attach.sendInitialUpdate();
    }

    int value() const { return roundToInt (param.convertFrom0to1 (param.getValue())); }
    int minV() const  { return roundToInt (param.getNormalisableRange().start); }
    int maxV() const  { return roundToInt (param.getNormalisableRange().end); }

    void set (int v)
    {
        v = jlimit (minV(), maxV(), v);
        if (v != value()) attach.setValueAsPartOfGesture ((float) v);
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        Look::text (g, label, r.removeFromTop (16.0f), 12.0f, Look::ink, Justification::centredTop, true);
        auto vr = r.removeFromBottom (20.0f).reduced (6.0f, 0.0f);
        Look::screenBox (g, vr);
        const String txt = format (value());
        float th = 11.0f;
        while (th > 7.0f && PixelFont::getWidth (txt, th) > vr.getWidth() - 6.0f) th -= 0.5f;   // shrink to fit
        Look::text (g, txt, vr, th, Look::screenText, Justification::centred);

        const float d = jmin (r.getWidth() - 14.0f, r.getHeight() - 8.0f);
        auto disc = Rectangle<float> (d, d).withCentre (r.getCentre());
        g.setColour (Look::edgeLo);
        g.fillEllipse (disc.translated (1.5f, 2.0f));
        g.setColour (Look::screen);
        g.fillEllipse (disc);
        g.setColour (Look::edgeHi.withAlpha (0.55f));
        g.drawEllipse (disc.reduced (1.0f), 1.5f);

        const float a0 = MathConstants<float>::pi * 1.25f, a1 = -MathConstants<float>::pi * 0.25f;
        auto at = [&] (float ang, float rad) { return disc.getCentre() + Point<float> (std::cos (ang) * rad, -std::sin (ang) * rad); };
        g.setColour (Look::ink.withAlpha (0.6f));
        for (float ang : { a0, a1 }) g.drawLine (Line<float> (at (ang, d * 0.52f), at (ang, d * 0.62f)), 2.0f);
        const float range = (float) jmax (1, maxV() - minV());
        const float ang = a0 + (a1 - a0) * (float) (value() - minV()) / range;
        g.setColour (Look::screenText);
        g.drawLine (Line<float> (at (ang, d * 0.08f), at (ang, d * 0.42f)), 3.0f);
    }

    void mouseDown (const MouseEvent& e) override
    {
        startY = e.position.y; startV = value();
        dragged = false;
        attach.beginGesture();
    }
    void mouseDrag (const MouseEvent& e) override
    {
        const float range = (float) (maxV() - minV());
        const float perPixel = range / (e.mods.isShiftDown() ? 600.0f : 150.0f);
        if (std::abs (startY - e.position.y) > 3.0f) dragged = true;
        set (startV + roundToInt ((startY - e.position.y) * perPixel));
    }
    void mouseUp (const MouseEvent&) override
    {
        if (! dragged && maxV() - minV() == 1) set (value() == minV() ? maxV() : minV());   // a two-way switch: click flips it
        attach.endGesture();
    }
    void mouseDoubleClick (const MouseEvent&) override
    {
        attach.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
    }
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override
    {
        wheel += w.deltaY * 8.0f;
        const int steps = (int) wheel;
        if (steps != 0) { wheel -= (float) steps; attach.setValueAsCompleteGesture ((float) jlimit (minV(), maxV(), value() + steps)); }
    }

    RangedAudioParameter& param;
    String label;
    std::function<String (int)> format;
    ParameterAttachment attach;
    float startY = 0.0f, wheel = 0.0f;
    int startV = 0;
    bool dragged = false;
};

// ---------------------------------------------------------------------------
// the editor
// ---------------------------------------------------------------------------
namespace
{
    constexpr int W = 940, H = 560;
    const Rectangle<int> stepsPanel  { 24, 100, 892, 196 };
    const Rectangle<int> toolsPanel  { 24, 312, 572, 224 };
    const Rectangle<int> knobsPanel  { 612, 312, 304, 224 };
}

BreakbeatEditor::BreakbeatEditor (BreakbeatProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    for (int i = 0; i < bb::kNumSteps; ++i)
    {
        auto* pad = pads.add (new StepPad (*this, i));
        addAndMakeVisible (pad);
        if (auto* param = proc.params.getParameter (BreakbeatProcessor::stepId (i)))
        {
            stepAttachments.push_back (std::make_unique<ParameterAttachment> (*param, [pad] (float) { pad->repaint(); }, nullptr));
            stepAttachments.back()->sendInitialUpdate();
        }
    }

    const char* tips[] = { "",
        "1/4: restart from the first quarter of the slice (it plays on after this step)",
        "2/4: restart from the second quarter of the slice",
        "3/4: restart from the third quarter of the slice",
        "4/4: restart from the last quarter of the slice",
        "REVERSE: this step plays backwards",
        "STOP: silence",
        "BIT: bit crusher (BITS and RATE knobs)" };
    for (int a = bb::Q1; a < bb::NumActions; ++a)
    {
        auto* t = tools.add (new ToolButton (a));
        t->setTooltip (String (tips[a]) + ". Click the tool, then click or drag over the steps.");
        t->onClick = [this, a] { setBrush (brush == a ? -1 : a); };
        addAndMakeVisible (t);
    }
    clearButton = std::make_unique<ToolButton> (bb::Thru, "CLEAR");
    clearButton->setTooltip ("Pick the eraser (click a step to empty it). Click it twice to empty every step.");
    clearButton->onClick = [this]
    {
        if (brush == bb::Thru) { proc.applyPattern ({}); setBrush (-1); }
        else setBrush (bb::Thru);
    };
    randomButton = std::make_unique<ToolButton> (-1, "RANDOM");
    randomButton->setTooltip ("A new random break");
    randomButton->onClick = [this] { randomise(); };
    patternButton = std::make_unique<ToolButton> (-1, "PATTERNS");
    patternButton->setTooltip ("Ready-made patterns");
    patternButton->onClick = [this] { showPatternMenu(); };
    aheadButton = std::make_unique<ToolButton> (-1, "READ AHEAD");
    aheadButton->setTooltip ("On: the plugin reads the whole SLICE of the track before it is heard (latency = SLICE; "
                             "for DAWs with plugin delay compensation). Off: no delay, parts not heard yet come from the last time round.");
    if (auto* ap = proc.params.getParameter ("ahead"))
    {
        aheadAttachment = std::make_unique<ParameterAttachment> (*ap, [this] (float v) { aheadButton->setSelected (v > 0.5f); repaint (statusArea); }, nullptr);
        aheadAttachment->sendInitialUpdate();
        aheadButton->onClick = [this] { aheadAttachment->setValueAsCompleteGesture (aheadButton->selected ? 0.0f : 1.0f); };
    }
    for (auto* b : { clearButton.get(), randomButton.get(), patternButton.get(), aheadButton.get() }) addAndMakeVisible (b);

    auto addKnob = [this] (const String& id, const String& name, std::function<String (int)> fmt, const String& tip)
    {
        if (auto* param = dynamic_cast<RangedAudioParameter*> (proc.params.getParameter (id)))
        {
            auto* k = knobs.add (new Knob (*param, name, std::move (fmt)));
            k->setTooltip (tip);
            addAndMakeVisible (k);
        }
    };
    auto rowsText = [] (int v) { return String (8 << jlimit (0, 3, v)) + " ROWS"; };
    addKnob ("length", "SLICE", rowsText,
             "How much of the channel's sound is cut into the quarters 1/4 - 4/4: 8, 16, 32 or 64 rows (1/2, 1, 2 or 4 bars in a DAW), "
             "whatever plays there: one long break, short samples, a bassline ...");
    addKnob ("steps", "STEPS", rowsText,
             "How much of the track the 8 steps cover: 8, 16, 32 or 64 rows (at most SLICE). They repeat over the slice.");
    addKnob ("smooth", "SMOOTH", [] (int v) { return String (v) + " MS"; }, "Cross-fade between steps (no clicks)");
    addKnob ("mix", "MIX", [] (int v) { return String (v) + "%"; }, "Dry / processed");
    addKnob ("bits", "BITS", [] (int v) { return String (v) + " BIT"; }, "BIT steps: bit depth (lower = dirtier)");
    addKnob ("rate", "RATE", [] (int v) { return v <= 1 ? String ("OFF") : "1/" + String (v); },
             "BIT steps: sample-rate reduction (higher = more aliasing)");
    addKnob ("tempo", "TEMPO", [] (int v) { return String (v) + " BPM"; },
             "Own tempo, used only when the host sends no tempo: then the steps start with the first sound (row 0) and stop after half a second of silence");
    addKnob ("sync", "SYNC", [] (int v) { return String (v == 0 ? "SONG" : "SOUND"); },
             "SONG: the steps follow the host's song position (DAWs). SOUND: they start with the first sound on the channel "
             "after a silence, at the host's tempo (trackers like MadTracker: row 0 = the first note). Click to switch.");

    setSize (W, H);
    startTimerHz (30);
}

BreakbeatEditor::~BreakbeatEditor() { stopTimer(); }

int BreakbeatEditor::getRowsPerStep() const
{
    auto* len = proc.params.getRawParameterValue ("length");
    auto* st  = proc.params.getRawParameterValue ("steps");
    if (len != nullptr && st != nullptr)
        return 1 << jlimit (0, 3, jmin (roundToInt (st->load()), roundToInt (len->load())));
    return 2;
}

int BreakbeatEditor::getStepAction (int step) const
{
    if (auto* v = proc.params.getRawParameterValue (BreakbeatProcessor::stepId (step)))
        return jlimit (0, (int) bb::NumActions - 1, roundToInt (v->load()));
    return 0;
}

void BreakbeatEditor::setStepAction (int step, int action)
{
    if (! isPositiveAndBelow (step, (int) stepAttachments.size()) || getStepAction (step) == action) return;
    stepAttachments[(size_t) step]->setValueAsCompleteGesture ((float) action);
}

void BreakbeatEditor::setBrush (int action)
{
    brush = action;
    for (auto* t : tools) t->setSelected (t->action == brush);
    clearButton->setSelected (brush == bb::Thru);
    for (auto* p : pads) p->setMouseCursor (brush >= 0 ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
    repaint (toolsPanel);
}

void BreakbeatEditor::stepPressed (int step, const MouseEvent&)
{
    if (brush < 0) { showStepMenu (step); return; }
    // clicking a step that already has the tool empties it again
    setStepAction (step, getStepAction (step) == brush ? (int) bb::Thru : brush);
}

void BreakbeatEditor::stepDragged (const MouseEvent& e)
{
    if (brush < 0) return;
    const auto pos = e.getEventRelativeTo (this).getPosition();
    for (auto* p : pads)
        if (p->getBounds().contains (pos) && p != e.eventComponent)
            setStepAction (p->index, brush);
}

void BreakbeatEditor::showStepMenu (int step)
{
    PopupMenu m;
    m.addSectionHeader ("STEP " + String (step + 1));
    const int now = getStepAction (step);
    const char* names[] = { "- (play on)", "1/4  restart from the first quarter", "2/4  restart from the second quarter",
                            "3/4  restart from the third quarter", "4/4  restart from the last quarter", "REVERSE", "STOP", "BITCRUSH" };
    for (int a = 0; a < bb::NumActions; ++a)
        m.addItem (a + 1, names[a], true, a == now);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (pads[step]),
                     [safe = Component::SafePointer<BreakbeatEditor> (this), step] (int r)
                     {
                         if (safe != nullptr && r > 0) safe->setStepAction (step, r - 1);
                     });
}

void BreakbeatEditor::showPatternMenu()
{
    PopupMenu m;
    const auto& list = BreakbeatProcessor::patterns();
    for (int i = 0; i < (int) list.size(); ++i) m.addItem (i + 1, list[(size_t) i].name);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (patternButton.get()),
                     [safe = Component::SafePointer<BreakbeatEditor> (this)] (int r)
                     {
                         if (safe != nullptr && r > 0) safe->proc.applyPattern (BreakbeatProcessor::patterns()[(size_t) r - 1].steps);
                     });
}

void BreakbeatEditor::randomise()
{
    // mostly slices, some straight steps, now and then a reverse, stop or crush
    std::array<int, bb::kNumSteps> s {};
    for (auto& a : s)
    {
        const int r = rng.nextInt (100);
        a = r < 22 ? (int) bb::Thru : r < 80 ? (int) bb::Q1 + rng.nextInt (4) : r < 89 ? (int) bb::Rev : r < 94 ? (int) bb::Stop : (int) bb::Bit;
    }
    s[0] = rng.nextBool() ? (int) bb::Thru : (int) bb::Q1;    // keep the downbeat
    proc.applyPattern (s);
}

void BreakbeatEditor::timerCallback()
{
    const int rps = getRowsPerStep();
    if (rps != shownRowsPerStep) { shownRowsPerStep = rps; for (auto* p : pads) p->repaint(); }
    const int now = proc.getCurrentStep();
    if (now != playing)
    {
        const int old = playing;
        playing = now;
        if (isPositiveAndBelow (old, pads.size())) pads[old]->repaint();
        if (isPositiveAndBelow (now, pads.size())) pads[now]->repaint();
        repaint (statusArea);
    }
    const double bpm = proc.getHostBpm();
    if (std::abs (bpm - shownBpm) > 0.05 || proc.hostIsPlaying() != shownPlaying || proc.isUsingOwnClock() != shownOwn)
    {
        shownOwn = proc.isUsingOwnClock();
        shownBpm = bpm;
        shownPlaying = proc.hostIsPlaying();
        repaint (statusArea);
    }
}

void BreakbeatEditor::paint (Graphics& g)
{
    g.fillAll (Look::background);

    // title
    Look::text (g, "BREAKBEAT PROCESSOR", { 30.0f, 26.0f, 600.0f, 36.0f }, 34.0f, Look::ink, Justification::centredLeft, true);
    Look::text (g, "8 STEP BREAKBEAT SLICER  -  VST3", { 32.0f, 70.0f, 600.0f, 14.0f }, 11.0f, Look::edgeLo.darker (0.4f));

    // status screen: tempo and step
    statusArea = { 652, 24, 264, 58 };
    auto st = statusArea.toFloat();
    Look::screenBox (g, st);
    auto top = st.reduced (12.0f, 9.0f);
    String bpmText = shownBpm > 0.0 ? String (shownBpm, 1) + " BPM" : String ("NO TEMPO");
    if (shownOwn)
    {
        auto* sync = proc.params.getRawParameterValue ("sync");
        bpmText << (sync != nullptr && sync->load() > 0.5f ? "  SOUND" : "  OWN");
    }
    if (aheadButton != nullptr && aheadButton->selected) bpmText << "  AHEAD";
    Look::text (g, bpmText, top.removeFromTop (18.0f), 16.0f, Look::screenText, Justification::centredLeft, true);
    const String stepText = shownPlaying && playing >= 0
                              ? "STEP " + String (playing + 1) + " / 8   ROW " + String (playing * getRowsPerStep()).paddedLeft ('0', 2)
                              : String (shownOwn ? "WAITING FOR SOUND" : "STOPPED - PRESS PLAY");
    Look::text (g, stepText, top.removeFromBottom (14.0f), 11.0f, shownPlaying ? Look::red.brighter (0.3f) : Look::screenDim);

    Look::panelBox (g, stepsPanel.toFloat());
    Look::panelBox (g, toolsPanel.toFloat());
    Look::panelBox (g, knobsPanel.toFloat());

    Look::text (g, "TOOLS", { (float) toolsPanel.getX() + 16.0f, (float) toolsPanel.getY() + 12.0f, 200.0f, 14.0f }, 12.0f, Look::ink, Justification::centredLeft, true);

    // help text, like the sketch
    const char* help[] = {
        "PICK A TOOL, THEN CLICK OR DRAG OVER THE STEPS (CLICK AGAIN TO EMPTY).",
        "RIGHT-CLICK A STEP FOR THE MENU. 1/4 - 4/4 RESTART FROM THAT QUARTER,",
        "ARROW = REVERSE, SQUARE = STOP, BIT = CRUSH, EMPTY = PLAY ON.",
        "SLICE + STEPS: 8 / 16 / 32 / 64 ROWS (1/2 - 4 BARS). RUNS WHILE IT PLAYS." };
    float y = (float) toolsPanel.getBottom() - 70.0f;
    for (auto* line : help)
    {
        Look::text (g, line, { (float) toolsPanel.getX() + 18.0f, y, (float) toolsPanel.getWidth() - 30.0f, 10.0f }, 8.0f, Look::ink.withAlpha (0.75f));
        y += 15.0f;
    }
}

void BreakbeatEditor::resized()
{
    // 8 steps
    const int padW = 96, gap = 12;
    const int x0 = stepsPanel.getX() + (stepsPanel.getWidth() - (8 * padW + 7 * gap)) / 2;
    for (int i = 0; i < pads.size(); ++i)
        pads[i]->setBounds (x0 + i * (padW + gap), stepsPanel.getY() + 16, padW, stepsPanel.getHeight() - 24);

    // tools
    int x = toolsPanel.getX() + 16;
    const int ty = toolsPanel.getY() + 36;
    for (auto* t : tools) { t->setBounds (x, ty, 70, 52); x += 77; }
    const int by = ty + 64;
    clearButton->setBounds   (toolsPanel.getX() + 16, by, 120, 42);
    randomButton->setBounds  (toolsPanel.getX() + 144, by, 120, 42);
    patternButton->setBounds (toolsPanel.getX() + 272, by, 130, 42);
    aheadButton->setBounds   (toolsPanel.getX() + 410, by, 146, 42);

    // knobs: SAMPLE STEPS SMOOTH MIX / BITS RATE TEMPO
    const int kw = 70, kh = 96;
    const int kx = knobsPanel.getX() + 10, ky = knobsPanel.getY() + 12;
    for (int i = 0; i < knobs.size(); ++i)
    {
        const int row = i < 4 ? 0 : 1, col = i < 4 ? i : i - 4;
        knobs[i]->setBounds (kx + col * (kw + 4), ky + row * (kh + 4), kw, kh);
    }
}

// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PixelFont.h"

namespace Theme
{
    const juce::Colour background   { 0xffb3b7c0 };
    const juce::Colour panel        { 0xffc4c7cf };
    const juce::Colour panelEdgeHi  { 0xffe4e6eb };
    const juce::Colour panelEdgeLo  { 0xff878b94 };
    const juce::Colour ink          { 0xff0c0d0f };
    const juce::Colour screen       { 0xff040405 };
    const juce::Colour screenText   { 0xffeceef2 };
    const juce::Colour screenDim    { 0xff8c9098 };
    const juce::Colour screenFaint  { 0xff1b1c20 };
    const juce::Colour buttonLight  { 0xffe4e6eb };
    const juce::Colour buttonDark   { 0xff5d6169 };
    const juce::Colour buttonDarkText { 0xffd5d8de };
    const juce::Colour titleBar     { 0xff1a1b1e };
    const juce::Colour accent       { 0xffffffff };

    void drawPanel  (juce::Graphics&, juce::Rectangle<float>, juce::Colour fill = panel);
    void drawRaised (juce::Graphics&, juce::Rectangle<float>, juce::Colour fill, bool pressed = false);
    void drawScreen (juce::Graphics&, juce::Rectangle<float>);
    void text (juce::Graphics&, const juce::String&, juce::Rectangle<float>, float h, juce::Colour,
               juce::Justification j = juce::Justification::centredLeft, bool bold = false);

    // Small vector icons used on buttons
    void iconWave   (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
    void iconList   (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
    void iconGrid   (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
    void iconTracks (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
    void iconMidi   (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
    void iconFolder (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
    void iconTrash  (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
    void iconSave   (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
    void iconPlay   (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
    void iconStop   (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
    void iconClip   (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
    void iconScissors (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
    void iconArrow  (juce::Graphics&, juce::Rectangle<float>, juce::Colour, bool right);
}

/** Static pixel-font text. */
class PixelLabel : public juce::Component
{
public:
    PixelLabel (juce::String t = {}, float h = 18.0f, juce::Justification j = juce::Justification::centredLeft)
        : text (std::move (t)), height (h), just (j) { setInterceptsMouseClicks (false, false); }

    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
    const juce::String& getText() const noexcept { return text; }
    void setColour (juce::Colour c) { colour = c; repaint(); }
    void setBold (bool b) { bold = b; repaint(); }
    void setScreen (bool s) { screen = s; colour = s ? Theme::screenText : Theme::ink; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (screen) { Theme::drawScreen (g, r); r = r.reduced (10.0f, 0.0f); }
        Theme::text (g, text, r, height, colour, just, bold);
    }

private:
    juce::String text;
    float height;
    juce::Justification just;
    juce::Colour colour { Theme::ink };
    bool bold = false, screen = false;
};

/** Tracker-style button. */
class PixelButton : public juce::Button
{
public:
    enum class Style { Raised, Dark, Segment, Tab, Record };   // Record: lights up red when toggled on

    PixelButton (const juce::String& label, Style s = Style::Raised, float textHeight = 16.0f);

    std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)> icon;
    juce::String subLabel;     // second line (e.g. "PATTERN" under "START")
    juce::String caption;      // shown under the button (e.g. "Shift+F3"), drawn by parent
    float iconWidth = 0.0f;    // > 0: icon occupies a left square of this width
    bool  iconDarkBox = false; // Tab style: icon sits in a black box

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Style style;
    float textH;
};

/** ON / OFF style pair (or any two labels). */
class SegmentPair : public juce::Component
{
public:
    SegmentPair (const juce::String& a = "ON", const juce::String& b = "OFF", float textH = 16.0f);

    void setState (bool firstSelected, bool notify = false);
    bool getState() const noexcept { return state; }
    std::function<void (bool)> onChange;
    void resized() override;

private:
    PixelButton first, second;
    bool state = true;
};

/** A black value box: drag up/down, mouse wheel, or double-click to type.
    Optional spin arrows on the right. */
/** Horizontal slider on a black screen strip with the value at the right.
    Click or drag to set, mouse wheel for small steps, double-click resets. */
class PixelSlider : public juce::Component
{
public:
    PixelSlider (int minValue, int maxValue, int initial, int defaultValue);

    void setValue (int v, bool notify = false);
    int  getValue() const noexcept { return value; }

    std::function<juce::String (int)> formatter;
    std::function<void (int)> onChange;
    std::function<void()> onRightClick;   // e.g. the MIDI LEARN menu
    float skew = 1.0f;   // > 1 gives more room to small values (times in ms)
    /** Small text in the slider, e.g. the MIDI knob ("CC 74"); learning = waiting for a knob (lit). */
    void setBadge (const juce::String& text, bool learning)
    {
        if (text == badge && learning == learn) return;
        badge = text; learn = learning; repaint();
    }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<float> track() const;
    void setFromX (float x);
    float toPos (int v) const;
    int fromPos (float pos) const;
    int minV, maxV, value, defV;
    juce::String badge;
    bool learn = false;
};

class ValueBox : public juce::Component, private juce::TextEditor::Listener
{
public:
    ValueBox (int minValue, int maxValue, int initial = 0, float textHeight = 24.0f);

    void setRange (int minValue, int maxValue);
    void setValue (int v, bool notify = false);
    int  getValue() const noexcept { return value; }
    void setShowSpinner (bool s) { spinner = s; resized(); repaint(); }
    void setDragSensitivity (float pixelsPerStep) { pxPerStep = pixelsPerStep; }
    void setLargeStep (int s) { largeStep = s; }
    void setJustification (juce::Justification j) { just = j; repaint(); }

    std::function<juce::String (int)> formatter;   // default: decimal
    std::function<int (const juce::String&, bool&)> parser;
    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;
    void textEditorFocusLost (juce::TextEditor&) override;
    void finishEditing (bool commit);
    void stepBy (int delta);
    juce::Rectangle<float> spinnerArea() const;

    int minV, maxV, value;
    float textH;
    bool spinner = false;
    float pxPerStep = 4.0f;
    int largeStep = 10;
    juce::Justification just { juce::Justification::centred };
    int dragStartValue = 0;
    bool draggingValue = false;
    std::unique_ptr<juce::TextEditor> editor;
    float wheelAccum = 0;
};

/** Inline text field shown in a black box (instrument names). */
class NameBox : public juce::Component, private juce::TextEditor::Listener
{
public:
    explicit NameBox (float textHeight = 20.0f);
    void setText (const juce::String& t);
    std::function<void (const juce::String&)> onChange;
    bool editable = true;

    void paint (juce::Graphics&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void resized() override;

private:
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;
    void textEditorFocusLost (juce::TextEditor&) override;
    void finish (bool commit);

    juce::String text;
    float textH;
    std::unique_ptr<juce::TextEditor> editor;
};

/** Raised light panel with an optional title. */
class PanelComponent : public juce::Component
{
public:
    explicit PanelComponent (juce::String t = {}, float th = 18.0f) : title (std::move (t)), titleH (th) {}
    void paint (juce::Graphics& g) override
    {
        Theme::drawPanel (g, getLocalBounds().toFloat());
        if (title.isNotEmpty())
            Theme::text (g, title, { 16.0f, 10.0f, (float) getWidth() - 20.0f, titleH }, titleH, Theme::ink, juce::Justification::topLeft);
    }
    juce::String title;
    float titleH;
};

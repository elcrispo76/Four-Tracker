// SPDX-License-Identifier: AGPL-3.0-or-later
// FX Processor - Copyright (C) 2026 elcrispo76

#pragma once

// The look shared with Four Tracker and the Breakbeat Processor: grey panels, black
// screens, white keys with a thick black rim, round knobs with a line, pixel font.

#include <juce_audio_processors/juce_audio_processors.h>
#include "PixelFont.h"

using namespace juce;

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

    inline void text (Graphics& g, const String& t, Rectangle<float> r, float h, Colour c,
               Justification j = Justification::centredLeft, bool bold = false)
    {
        g.setColour (c);
        PixelFont::draw (g, t, r, h, j, bold);
    }

    inline void panelBox (Graphics& g, Rectangle<float> r)
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

    inline void screenBox (Graphics& g, Rectangle<float> r)
    {
        g.setColour (edgeLo);
        g.fillRect (r.expanded (2.0f));
        g.setColour (screen);
        g.fillRect (r);
    }

    /** A rounded white key with a thick black rim, like the sketch. */
    inline void key3d (Graphics& g, Rectangle<float> r, Colour fill, bool down)
    {
        const float rad = jmin (10.0f, r.getHeight() * 0.18f);
        if (! down) { g.setColour (Colours::black.withAlpha (0.25f)); g.fillRoundedRectangle (r.translated (2.0f, 3.0f), rad); }
        else r = r.translated (1.0f, 2.0f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, rad);
        g.setColour (ink);
        g.drawRoundedRectangle (r.reduced (1.5f), rad, 3.0f);
    }

}

struct ToolButton : public juce::Component, public juce::SettableTooltipClient
{
    explicit ToolButton (String t) : label (std::move (t)) {}

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f, 2.0f).withTrimmedBottom (2.0f).withTrimmedRight (2.0f);
        Look::key3d (g, r, selected ? Look::ink : Look::key, down || selected);
        if (down || selected) r = r.translated (1.0f, 2.0f);
        const Colour c = selected ? Look::screenText : Look::ink;
        Look::text (g, label, r, textHeight, c, Justification::centred, true);
    }

    void mouseDown (const MouseEvent&) override { down = true; repaint(); }
    void mouseUp (const MouseEvent& e) override
    {
        down = false; repaint();
        if (getLocalBounds().contains (e.getPosition()) && onClick) onClick();
    }

    void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }

    String label;
    float textHeight = 13.0f;
    bool selected = false, down = false;
    std::function<void()> onClick;
};

// ---------------------------------------------------------------------------
// knob, round disc with a line, value in a small screen (Four Tracker style)
// ---------------------------------------------------------------------------
struct Knob : public juce::Component, public juce::SettableTooltipClient
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
        {
            float lh = 12.0f;
            while (lh > 7.0f && PixelFont::getWidth (label, lh) > r.getWidth() - 2.0f) lh -= 0.5f;   // shrink to fit
            Look::text (g, label, r.removeFromTop (16.0f), lh, Look::ink, Justification::centredTop, true);
        }
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

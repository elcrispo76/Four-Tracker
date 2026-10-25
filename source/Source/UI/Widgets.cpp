// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#include "Widgets.h"

using namespace juce;

// ===========================================================================
// Theme
// ===========================================================================

namespace Theme
{
void drawPanel (Graphics& g, Rectangle<float> r, Colour fill)
{
    g.setColour (fill);
    g.fillRect (r);
    g.setColour (panelEdgeHi);
    g.fillRect (r.removeFromTop (2.0f));
    g.fillRect (Rectangle<float> (r.getX(), r.getY(), 2.0f, r.getHeight()));
    g.setColour (panelEdgeLo);
    g.fillRect (Rectangle<float> (r.getX(), r.getBottom() - 2.0f, r.getWidth(), 2.0f));
    g.fillRect (Rectangle<float> (r.getRight() - 2.0f, r.getY() - 2.0f, 2.0f, r.getHeight() + 2.0f));
}

void drawRaised (Graphics& g, Rectangle<float> r, Colour fill, bool pressed)
{
    g.setColour (Colour (0xff2a2c30));
    g.fillRoundedRectangle (r, 3.0f);
    auto inner = r.reduced (2.0f);
    g.setColour (pressed ? fill.darker (0.15f) : fill);
    g.fillRoundedRectangle (inner, 2.0f);
    if (! pressed)
    {
        g.setColour (fill.brighter (0.25f));
        g.fillRect (inner.removeFromTop (2.0f).reduced (2.0f, 0.0f));
    }
}

void drawScreen (Graphics& g, Rectangle<float> r)
{
    g.setColour (Colour (0xff4a4d53));
    g.fillRect (r);
    g.setColour (screen);
    g.fillRect (r.reduced (2.0f));
}

void text (Graphics& g, const String& t, Rectangle<float> r, float h, Colour c, Justification j, bool bold)
{
    g.setColour (c);
    PixelFont::draw (g, t, r, h, j, bold);
}

void iconWave (Graphics& g, Rectangle<float> r, Colour c)
{
    Path p;
    const float y = r.getCentreY(), x0 = r.getX(), w = r.getWidth(), h = r.getHeight() * 0.42f;
    const float pts[] = { 0.0f, 0.0f, 0.14f, 0.0f, 0.2f, -0.4f, 0.26f, 0.5f, 0.32f, -0.9f, 0.4f, 1.0f, 0.48f, -1.0f,
                          0.56f, 0.8f, 0.63f, -0.6f, 0.7f, 0.4f, 0.76f, -0.2f, 0.82f, 0.0f, 1.0f, 0.0f };
    p.startNewSubPath (x0, y);
    for (size_t i = 2; i < std::size (pts); i += 2)
        p.lineTo (x0 + pts[i] * w, y + pts[i + 1] * h);
    g.setColour (c);
    g.strokePath (p, PathStrokeType (2.2f));
}

void iconList (Graphics& g, Rectangle<float> r, Colour c)
{
    g.setColour (c);
    const float rowH = r.getHeight() / 3.0f;
    for (int i = 0; i < 3; ++i)
    {
        auto row = Rectangle<float> (r.getX(), r.getY() + i * rowH + rowH * 0.3f, r.getWidth(), rowH * 0.4f);
        g.fillRect (row.removeFromLeft (row.getHeight()));
        g.fillRect (row.withTrimmedLeft (row.getHeight() * 0.8f));
    }
}

void iconGrid (Graphics& g, Rectangle<float> r, Colour c)
{
    g.setColour (c);
    const float cw = r.getWidth() / 3.0f, chh = r.getHeight() / 3.0f;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            g.drawRect (Rectangle<float> (r.getX() + x * cw + 1.5f, r.getY() + y * chh + 1.5f, cw - 3.0f, chh - 3.0f), 2.0f);
}

void iconTracks (Graphics& g, Rectangle<float> r, Colour c)
{
    g.setColour (c);
    const float cw = r.getWidth() / 4.0f;
    for (int x = 0; x < 4; ++x)
    {
        auto col = Rectangle<float> (r.getX() + x * cw + 2.0f, r.getY(), cw - 4.0f, r.getHeight());
        g.drawRect (col, 2.0f);
        for (int l = 1; l < 6; ++l)
            g.fillRect (col.getX() + 4.0f, col.getY() + l * col.getHeight() / 6.0f - 1.0f, col.getWidth() - 8.0f, 2.0f);
    }
}

void iconMidi (Graphics& g, Rectangle<float> r, Colour c)
{
    auto sq = r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight()), jmin (r.getWidth(), r.getHeight()));
    g.setColour (c);
    g.drawEllipse (sq.reduced (2.0f), 2.5f);
    const auto cen = sq.getCentre();
    const float rad = sq.getWidth() * 0.3f;
    for (int i = 0; i < 5; ++i)
    {
        const float a = MathConstants<float>::pi * (0.85f + i * 0.325f);
        g.fillEllipse (Rectangle<float> (4.0f, 4.0f).withCentre ({ cen.x + rad * std::cos (a), cen.y - rad * std::sin (a) }));
    }
    g.fillRect (Rectangle<float> (sq.getWidth() * 0.18f, 3.0f).withCentre ({ cen.x, sq.getBottom() - 5.0f }));
}

void iconFolder (Graphics& g, Rectangle<float> r, Colour c)
{
    Path p;
    const float x = r.getX(), y = r.getY() + r.getHeight() * 0.15f, w = r.getWidth(), h = r.getHeight() * 0.7f;
    p.startNewSubPath (x, y);
    p.lineTo (x + w * 0.38f, y);
    p.lineTo (x + w * 0.48f, y + h * 0.15f);
    p.lineTo (x + w, y + h * 0.15f);
    p.lineTo (x + w, y + h);
    p.lineTo (x, y + h);
    p.closeSubPath();
    g.setColour (c);
    g.fillPath (p);
}

void iconTrash (Graphics& g, Rectangle<float> r, Colour c)
{
    g.setColour (c);
    auto b = r.reduced (r.getWidth() * 0.15f, 0.0f);
    g.fillRect (b.getX(), b.getY() + 3.0f, b.getWidth(), 3.0f);
    g.fillRect (b.getCentreX() - 4.0f, b.getY(), 8.0f, 4.0f);
    auto can = Rectangle<float> (b.getX() + 2.0f, b.getY() + 8.0f, b.getWidth() - 4.0f, b.getHeight() - 8.0f);
    g.drawRect (can, 2.5f);
    for (int i = 1; i < 4; ++i)
        g.fillRect (can.getX() + i * can.getWidth() / 4.0f - 1.0f, can.getY() + 4.0f, 2.0f, can.getHeight() - 8.0f);
}

void iconSave (Graphics& g, Rectangle<float> r, Colour c)
{
    auto b = r.withSizeKeepingCentre (r.getWidth() * 0.75f, r.getHeight());
    g.setColour (c);
    g.drawRoundedRectangle (b.reduced (1.5f), 3.0f, 3.0f);
    g.fillRect (b.getX() + b.getWidth() * 0.3f, b.getY() + 4.0f, b.getWidth() * 0.4f, b.getHeight() * 0.28f);
    g.fillRect (b.getX() + 5.0f, b.getBottom() - b.getHeight() * 0.4f, b.getWidth() - 10.0f, b.getHeight() * 0.4f - 5.0f);
}

void iconPlay (Graphics& g, Rectangle<float> r, Colour c)
{
    Path p;
    p.addTriangle (r.getX(), r.getY(), r.getX(), r.getBottom(), r.getRight(), r.getCentreY());
    g.setColour (c);
    g.fillPath (p);
}

void iconStop (Graphics& g, Rectangle<float> r, Colour c)
{
    g.setColour (c);
    g.fillRoundedRectangle (r, 2.0f);
}

void iconClip (Graphics& g, Rectangle<float> r, Colour c)
{
    auto b = r.withSizeKeepingCentre (r.getWidth() * 0.62f, r.getHeight());
    g.setColour (c);
    g.drawRoundedRectangle (b.withTrimmedTop (5.0f).reduced (1.5f), 3.0f, 3.0f);
    g.fillRoundedRectangle (Rectangle<float> (b.getWidth() * 0.5f, 9.0f).withCentre ({ b.getCentreX(), b.getY() + 5.0f }), 2.0f);
}

void iconScissors (Graphics& g, Rectangle<float> r, Colour c)
{
    g.setColour (c);
    const float w = r.getWidth(), h = r.getHeight();
    g.drawLine (r.getX() + w * 0.2f, r.getY(), r.getX() + w * 0.72f, r.getY() + h * 0.65f, 2.5f);
    g.drawLine (r.getRight() - w * 0.2f, r.getY(), r.getX() + w * 0.28f, r.getY() + h * 0.65f, 2.5f);
    const float d = w * 0.36f;
    g.drawEllipse (r.getX() + w * 0.05f, r.getBottom() - d, d, d, 2.5f);
    g.drawEllipse (r.getRight() - w * 0.05f - d, r.getBottom() - d, d, d, 2.5f);
}

void iconArrow (Graphics& g, Rectangle<float> r, Colour c, bool right)
{
    Path p;
    if (right) p.addTriangle (r.getX(), r.getY(), r.getX(), r.getBottom(), r.getRight(), r.getCentreY());
    else       p.addTriangle (r.getRight(), r.getY(), r.getRight(), r.getBottom(), r.getX(), r.getCentreY());
    g.setColour (c);
    g.fillPath (p);
}
} // namespace Theme

// ===========================================================================
// PixelButton
// ===========================================================================

PixelButton::PixelButton (const String& label, Style s, float textHeight)
    : Button (label), style (s), textH (textHeight)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (false);
}

void PixelButton::paintButton (Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    Colour fill, ink;

    switch (style)
    {
        case Style::Raised:
            fill = Theme::buttonLight.darker (on ? 0.0f : 0.06f);
            if (highlighted) fill = fill.brighter (0.06f);
            ink = Theme::ink;
            Theme::drawRaised (g, r, fill, down);
            break;

        case Style::Dark:
            fill = down ? Colour (0xff222327) : (highlighted ? Colour (0xff151619) : Theme::screen);
            ink = Theme::screenText;
            g.setColour (Colour (0xff4a4d53));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (fill);
            g.fillRoundedRectangle (r.reduced (2.0f), 2.0f);
            break;

        case Style::Segment:
            fill = on ? Theme::buttonLight : Theme::buttonDark;
            if (highlighted && ! on) fill = fill.brighter (0.1f);
            ink = on ? Theme::ink : Theme::buttonDarkText;
            Theme::drawRaised (g, r, fill, down || ! on);
            break;

        case Style::Record:
            fill = on ? Colour (0xffd8322a) : Theme::buttonLight.darker (0.06f);
            if (highlighted) fill = fill.brighter (0.08f);
            ink = on ? Colours::white : Theme::ink;
            Theme::drawRaised (g, r, fill, down);
            break;

        case Style::Tab:
        {
            g.setColour (Colour (0xff2a2c30));
            g.fillRect (r);
            auto inner = r.reduced (2.0f);
            fill = on ? Theme::buttonLight : Theme::panel.darker (0.05f);
            if (highlighted && ! on) fill = fill.brighter (0.06f);
            g.setColour (fill);
            g.fillRect (inner);
            ink = Theme::ink;
            if (getButtonText().isEmpty() && icon != nullptr)
            {
                icon (g, inner.reduced (inner.getWidth() * 0.12f, inner.getHeight() * 0.2f), ink);
                return;
            }
            if (iconWidth > 0.0f)
            {
                auto box = inner.removeFromLeft (iconWidth);
                g.setColour (on ? Theme::ink : Theme::screen);
                g.fillRect (box);
                if (icon) icon (g, box.reduced (box.getWidth() * 0.18f, box.getHeight() * 0.22f), Theme::screenText);
            }
            Theme::text (g, getButtonText(), inner, textH, ink, Justification::centred);
            return;
        }
    }

    auto content = r.reduced (8.0f, 4.0f);
    if (icon != nullptr && iconWidth > 0.0f)
    {
        auto ib = content.removeFromLeft (iconWidth);
        icon (g, ib.withSizeKeepingCentre (ib.getWidth(), jmin (ib.getHeight(), iconWidth * 1.2f)), ink);
        content.removeFromLeft (8.0f);
    }
    else if (icon != nullptr)
    {
        icon (g, content.withSizeKeepingCentre (jmin (content.getWidth(), content.getHeight()) * 0.9f,
                                                jmin (content.getWidth(), content.getHeight()) * 0.9f), ink);
        return;
    }

    if (subLabel.isNotEmpty())
    {
        const float gap = textH * 0.55f;
        auto block = content.withSizeKeepingCentre (content.getWidth(), textH * 2.0f + gap);
        Theme::text (g, getButtonText(), block.removeFromTop (textH), textH, ink, Justification::centred);
        Theme::text (g, subLabel, block.removeFromBottom (textH), textH, ink, Justification::centred);
    }
    else
    {
        Theme::text (g, getButtonText(), content, textH, ink, Justification::centred);
    }
}

// ===========================================================================
// SegmentPair
// ===========================================================================

SegmentPair::SegmentPair (const String& a, const String& b, float textH)
    : first (a, PixelButton::Style::Segment, textH), second (b, PixelButton::Style::Segment, textH)
{
    addAndMakeVisible (first);
    addAndMakeVisible (second);
    first.onClick  = [this] { setState (true, true); };
    second.onClick = [this] { setState (false, true); };
    setState (true);
}

void SegmentPair::setState (bool s, bool notify)
{
    const bool changed = s != state;
    state = s;
    first.setToggleState (s, dontSendNotification);
    second.setToggleState (! s, dontSendNotification);
    if (notify && changed && onChange) onChange (s);
}

void SegmentPair::resized()
{
    auto r = getLocalBounds();
    const int gap = 6;
    first.setBounds (r.removeFromLeft ((r.getWidth() - gap) / 2));
    r.removeFromLeft (gap);
    second.setBounds (r);
}

// ===========================================================================
// ValueBox
// ===========================================================================

ValueBox::ValueBox (int minValue, int maxValue, int initial, float textHeight)
    : minV (minValue), maxV (maxValue), value (jlimit (minValue, maxValue, initial)), textH (textHeight)
{
    setMouseCursor (MouseCursor::UpDownResizeCursor);
    formatter = [] (int v) { return String (v); };
    parser = [] (const String& s, bool& ok)
    {
        auto t = s.trim();
        ok = t.containsOnly ("-+0123456789") && t.isNotEmpty();
        return t.getIntValue();
    };
}

void ValueBox::setRange (int minValue, int maxValue)
{
    minV = minValue; maxV = maxValue;
    setValue (value);
}

void ValueBox::setValue (int v, bool notify)
{
    v = jlimit (minV, maxV, v);
    if (v == value) return;
    value = v;
    repaint();
    if (notify && onChange) onChange (value);
}

Rectangle<float> ValueBox::spinnerArea() const
{
    if (! spinner) return {};
    return getLocalBounds().toFloat().removeFromRight (jmin (34.0f, getHeight() * 0.7f));
}

void ValueBox::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (spinner)
    {
        auto sp = spinnerArea();
        r.removeFromRight (sp.getWidth() + 6.0f);
        Theme::drawRaised (g, sp, Theme::buttonLight);
        auto top = sp.withHeight (sp.getHeight() * 0.5f).reduced (sp.getWidth() * 0.3f, sp.getHeight() * 0.17f);
        auto bot = top.withY (top.getY() + sp.getHeight() * 0.5f);
        Path up, dn;
        up.addTriangle (top.getX(), top.getBottom(), top.getRight(), top.getBottom(), top.getCentreX(), top.getY());
        dn.addTriangle (bot.getX(), bot.getY(), bot.getRight(), bot.getY(), bot.getCentreX(), bot.getBottom());
        g.setColour (Theme::ink);
        g.fillPath (up);
        g.fillPath (dn);
        g.setColour (Theme::panelEdgeLo);
        g.fillRect (sp.getX() + 4.0f, sp.getCentreY() - 1.0f, sp.getWidth() - 8.0f, 2.0f);
    }

    Theme::drawScreen (g, r);
    if (editor == nullptr)
        Theme::text (g, formatter ? formatter (value) : String (value), r.reduced (10.0f, 0.0f), textH, Theme::screenText, just);
}

void ValueBox::resized()
{
    if (editor != nullptr)
    {
        auto r = getLocalBounds();
        if (spinner) r.removeFromRight ((int) spinnerArea().getWidth() + 6);
        editor->setBounds (r.reduced (4));
    }
}

void ValueBox::stepBy (int delta) { setValue (value + delta, true); }

void ValueBox::mouseDown (const MouseEvent& e)
{
    draggingValue = false;
    if (spinner && spinnerArea().contains (e.position))
    {
        const int step = e.mods.isShiftDown() ? largeStep : 1;
        stepBy (e.position.y < spinnerArea().getCentreY() ? step : -step);
        return;
    }
    dragStartValue = value;
    draggingValue = true;
}

void ValueBox::mouseDrag (const MouseEvent& e)
{
    if (! draggingValue) return;
    const float sens = e.mods.isShiftDown() ? pxPerStep * 6.0f : pxPerStep;
    const int delta = (int) std::round (-e.getDistanceFromDragStartY() / sens);
    setValue (dragStartValue + delta, true);
}

void ValueBox::mouseUp (const MouseEvent&) { draggingValue = false; }

void ValueBox::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    wheelAccum += w.deltaY * (w.isReversed ? -1.0f : 1.0f);
    const int steps = (int) (wheelAccum / 0.1f);
    if (steps != 0)
    {
        wheelAccum -= steps * 0.1f;
        stepBy (steps * (e.mods.isShiftDown() ? largeStep : 1));
    }
}

void ValueBox::mouseDoubleClick (const MouseEvent& e)
{
    if (spinner && spinnerArea().contains (e.position)) return;
    editor = std::make_unique<TextEditor>();
    editor->setFont (FontOptions (Font::getDefaultMonospacedFontName(), textH * 1.15f, Font::bold));
    editor->setColour (TextEditor::backgroundColourId, Theme::screen);
    editor->setColour (TextEditor::textColourId, Theme::screenText);
    editor->setColour (TextEditor::highlightColourId, Theme::buttonDark);
    editor->setColour (TextEditor::outlineColourId, Theme::screenText);
    editor->setColour (TextEditor::focusedOutlineColourId, Theme::screenText);
    editor->setJustification (Justification::centred);
    editor->setText (formatter ? formatter (value) : String (value), false);
    editor->addListener (this);
    addAndMakeVisible (*editor);
    resized();
    editor->selectAll();
    editor->grabKeyboardFocus();
    repaint();
}

void ValueBox::finishEditing (bool commit)
{
    if (editor == nullptr) return;
    auto text = editor->getText();
    auto ed = std::move (editor);
    ed->removeListener (this);
    removeChildComponent (ed.get());
    if (commit)
    {
        bool ok = false;
        const int v = parser ? parser (text, ok) : text.getIntValue();
        if (ok) setValue (v, true);
    }
    repaint();
    auto* doomed = ed.release();
    MessageManager::callAsync ([doomed] { delete doomed; });
}

void ValueBox::textEditorReturnKeyPressed (TextEditor&) { finishEditing (true); }
void ValueBox::textEditorEscapeKeyPressed (TextEditor&) { finishEditing (false); }
void ValueBox::textEditorFocusLost (TextEditor&)         { finishEditing (true); }

// ===========================================================================
// NameBox
// ===========================================================================

NameBox::NameBox (float h) : textH (h) {}

void NameBox::setText (const String& t)
{
    if (t != text) { text = t; repaint(); }
}

void NameBox::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    Theme::drawScreen (g, r);
    g.reduceClipRegion (getLocalBounds().reduced (4));
    if (editor == nullptr)
        Theme::text (g, text.isEmpty() ? String ("---") : text, r.reduced (18.0f, 0.0f), textH, Theme::screenText, Justification::centredLeft);
}

void NameBox::resized()
{
    if (editor != nullptr) editor->setBounds (getLocalBounds().reduced (4));
}

void NameBox::mouseDoubleClick (const MouseEvent&)
{
    if (! editable) return;
    editor = std::make_unique<TextEditor>();
    editor->setFont (FontOptions (Font::getDefaultMonospacedFontName(), textH * 1.15f, Font::bold));
    editor->setColour (TextEditor::backgroundColourId, Theme::screen);
    editor->setColour (TextEditor::textColourId, Theme::screenText);
    editor->setColour (TextEditor::outlineColourId, Theme::screenText);
    editor->setColour (TextEditor::focusedOutlineColourId, Theme::screenText);
    editor->setInputRestrictions (22);
    editor->setText (text, false);
    editor->addListener (this);
    addAndMakeVisible (*editor);
    resized();
    editor->selectAll();
    editor->grabKeyboardFocus();
    repaint();
}

void NameBox::finish (bool commit)
{
    if (editor == nullptr) return;
    auto t = editor->getText().toUpperCase().trim();
    auto ed = std::move (editor);
    ed->removeListener (this);
    removeChildComponent (ed.get());
    if (commit && t != text)
    {
        text = t;
        if (onChange) onChange (t);
    }
    repaint();
    auto* doomed = ed.release();
    MessageManager::callAsync ([doomed] { delete doomed; });
}

void NameBox::textEditorReturnKeyPressed (TextEditor&) { finish (true); }
void NameBox::textEditorEscapeKeyPressed (TextEditor&) { finish (false); }
void NameBox::textEditorFocusLost (TextEditor&)         { finish (true); }

// ===========================================================================
// PixelSlider
// ===========================================================================

PixelSlider::PixelSlider (int minValue, int maxValue, int initial, int defaultValue)
    : minV (minValue), maxV (maxValue), value (jlimit (minValue, maxValue, initial)), defV (defaultValue)
{
}

void PixelSlider::setValue (int v, bool notify)
{
    v = jlimit (minV, maxV, v);
    if (v == value) return;
    value = v;
    repaint();
    if (notify && onChange) onChange (value);
}

Rectangle<float> PixelSlider::track() const
{
    return getLocalBounds().toFloat().withTrimmedRight (110.0f).reduced (8.0f, getHeight() * 0.28f);
}

void PixelSlider::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    Theme::drawScreen (g, r);

    const auto t = track();
    g.setColour (Theme::screenFaint);
    g.fillRect (t);
    const float frac = toPos (value);

    // chunky segments, like an old level meter
    const float seg = 8.0f, gap = 3.0f;
    const float filled = t.getWidth() * frac;
    g.setColour (Theme::screenText);
    for (float x = 0.0f; x < filled; x += seg + gap)
        g.fillRect (t.getX() + x, t.getY(), jmin (seg, filled - x), t.getHeight());

    // handle
    g.setColour (Theme::buttonLight);
    g.fillRect (t.getX() + filled - 3.0f, r.getY() + 6.0f, 6.0f, r.getHeight() - 12.0f);

    const String txt = formatter ? formatter (value) : String (value);
    Theme::text (g, txt, r.withTrimmedLeft (r.getWidth() - 104.0f).withTrimmedRight (10.0f), r.getHeight() * 0.42f,
                 Theme::screenText, Justification::centredRight);

    // MIDI knob badge on the track ("CC 74"), lit while waiting for a knob
    if (badge.isNotEmpty())
    {
        const float bh = jmin (t.getHeight() + 6.0f, r.getHeight() - 8.0f);
        auto b = Rectangle<float> (t.getRight() - 96.0f, t.getCentreY() - bh * 0.5f, 92.0f, bh);
        g.setColour (learn ? Colour (0xffd9342b) : Theme::screen.withAlpha (0.85f));
        g.fillRect (b);
        g.setColour (learn ? Colours::white : Theme::screenDim);
        g.drawRect (b, 1.0f);
        Theme::text (g, badge, b.reduced (4.0f, 0.0f), bh * 0.62f, learn ? Colours::white : Theme::screenText, Justification::centred);
    }
}

float PixelSlider::toPos (int v) const
{
    if (maxV <= minV) return 0.0f;
    const float n = (float) (v - minV) / (float) (maxV - minV);
    return skew == 1.0f ? n : std::pow (n, 1.0f / skew);
}

int PixelSlider::fromPos (float pos) const
{
    pos = jlimit (0.0f, 1.0f, pos);
    const float n = skew == 1.0f ? pos : std::pow (pos, skew);
    return minV + roundToInt (n * (float) (maxV - minV));
}

void PixelSlider::setFromX (float x)
{
    const auto t = track();
    setValue (fromPos ((x - t.getX()) / jmax (1.0f, t.getWidth())), true);
}

void PixelSlider::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onRightClick) { onRightClick(); return; }
    setFromX (e.position.x);
}
void PixelSlider::mouseDrag (const MouseEvent& e)  { if (! e.mods.isPopupMenu()) setFromX (e.position.x); }
void PixelSlider::mouseDoubleClick (const MouseEvent&) { setValue (defV, true); }

void PixelSlider::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    const float d = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    if (d == 0.0f) return;
    int nv = fromPos (toPos (value) + (d > 0 ? 0.01f : -0.01f));
    if (nv == value) nv += d > 0 ? 1 : -1;     // always move at least one step
    setValue (nv, true);
}

// ===========================================================================
// PixelKnob
// ===========================================================================

PixelKnob::PixelKnob (const String& l, int maxValue, int defaultValue)
    : label (l), maxV (maxValue), value (jlimit (0, maxValue, defaultValue)), defV (defaultValue)
{
    setRepaintsOnMouseActivity (false);
}

void PixelKnob::setValue (int v, bool notify)
{
    v = jlimit (0, maxV, v);
    if (v == value) return;
    value = v;
    repaint();
    if (notify && onChange) onChange (value);
}

void PixelKnob::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float labelH = 16.0f, valueH = 20.0f;

    // name above
    Theme::text (g, label, r.removeFromTop (labelH + 2.0f).reduced (1.0f, 0.0f), 12.0f, Theme::ink, Justification::centredTop);

    // value under, in a small black screen
    auto vr = r.removeFromBottom (valueH).reduced (2.0f, 0.0f);
    Theme::drawScreen (g, vr);
    const String txt = formatter ? formatter (value) : String (value);
    Theme::text (g, txt, vr.reduced (3.0f, 1.0f), 12.0f, Theme::screenText, Justification::centred);

    // the disc: a plain round shape with a line from the centre
    const float d = jmin (r.getWidth() - 8.0f, r.getHeight() - 6.0f);
    auto disc = Rectangle<float> (d, d).withCentre (r.getCentre());
    g.setColour (Theme::panelEdgeLo);
    g.fillEllipse (disc.translated (1.5f, 2.0f));                 // shadow
    g.setColour (learn ? Colour (0xffd9342b) : Theme::screen);
    g.fillEllipse (disc);
    g.setColour (Theme::panelEdgeHi.withAlpha (0.55f));
    g.drawEllipse (disc.reduced (1.0f), 1.5f);

    // range ticks at the two ends
    const float a0 = MathConstants<float>::pi * 1.25f, a1 = -MathConstants<float>::pi * 0.25f;
    auto pointAt = [&] (float ang, float rad) { return disc.getCentre() + Point<float> (std::cos (ang) * rad, -std::sin (ang) * rad); };
    g.setColour (Theme::ink.withAlpha (0.6f));
    for (float ang : { a0, a1 }) g.drawLine (Line<float> (pointAt (ang, d * 0.52f), pointAt (ang, d * 0.62f)), 2.0f);

    const float frac = maxV > 0 ? (float) value / (float) maxV : 0.0f;
    const float ang = a0 + (a1 - a0) * frac;
    g.setColour (Theme::screenText);
    g.drawLine (Line<float> (pointAt (ang, d * 0.08f), pointAt (ang, d * 0.42f)), 3.0f);

    if (badge.isNotEmpty())
    {
        auto b = Rectangle<float> (disc.getWidth() + 6.0f, 15.0f).withCentre ({ disc.getCentreX(), disc.getBottom() - 4.0f });
        g.setColour (learn ? Colours::white : Theme::buttonLight);
        g.fillRect (b);
        g.setColour (Theme::ink);
        g.drawRect (b, 1.0f);
        Theme::text (g, badge, b, 11.0f, learn ? Colour (0xffd9342b) : Theme::ink, Justification::centred);
    }
}

void PixelKnob::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
    dragStartY = e.position.y;
    dragStartValue = value;
}

void PixelKnob::mouseDrag (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    const float pxForFull = e.mods.isShiftDown() ? 800.0f : 180.0f;
    const float dv = (dragStartY - e.position.y) / pxForFull * (float) maxV;
    setValue (dragStartValue + roundToInt (dv), true);
}

void PixelKnob::mouseDoubleClick (const MouseEvent&) { setValue (defV, true); }

void PixelKnob::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    // small ranges (waveforms, sync choices): one step per notch; big ranges: a few
    wheelAcc += w.deltaY * (maxV <= 12 ? 4.0f : (float) maxV / 20.0f);
    const int steps = (int) wheelAcc;
    if (steps != 0) { wheelAcc -= (float) steps; setValue (value + steps, true); }
}

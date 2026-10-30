// SPDX-License-Identifier: AGPL-3.0-or-later
// Graphic EQ - Copyright (C) 2026 elcrispo76

#include "PluginEditor.h"
#include "PixelFont.h"

using namespace juce;

namespace
{
    // the look of the design: mid grey, black bars and ticks, light grey line, knobs and text
    const Colour bg      { 0xff7f7f7f };
    const Colour black   { 0xff000000 };
    const Colour lineCol { 0xffdddddd };
    const Colour knobCol { 0xffadadad };
    const Colour textCol { 0xffe6e6e6 };
    const Colour dimText { 0xff3a3a3a };

    constexpr int W = 1120, H = 462;
    constexpr float top = 76.0f;                 // the header above the design
    constexpr float zeroY = top + 172.0f;        // 0 dB
    constexpr float perDb = 152.0f / 12.0f;      // pixels per dB (+12 dB = 152 px up)
    constexpr float firstBar = 186.0f, barGap = 88.0f, outBar = 1066.0f;

    Font labelFont (float h) { return Font (FontOptions (h)); }

    String dbText (float v)
    {
        if (std::abs (v) < 0.05f) return "0.0 DB";
        return (v > 0.0f ? "+" : "") + String (v, 1) + " DB";
    }
    String hzText (int b)
    {
        const double f = eq::bandHz (b);
        return f >= 1000.0 ? String (f / 1000.0, std::fmod (f, 1000.0) == 0.0 ? 0 : 1) + " KHZ" : String ((int) f) + " HZ";
    }
}

EqEditor::EqEditor (EqProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    for (int b = 0; b <= eq::kBands; ++b)
        if (auto* prm = proc.params.getParameter (b == kOut ? String ("output") : EqProcessor::bandId (b)))
        {
            attach[(size_t) b] = std::make_unique<ParameterAttachment> (*prm, [this] (float) { repaint(); }, nullptr);
            attach[(size_t) b]->sendInitialUpdate();
        }

    buttons.push_back ({ "PRESETS", {}, [this] { showPresetMenu(); } });
    buttons.push_back ({ "FLAT", {}, [this] { proc.flat(); } });
    setSize (W, H);
}

EqEditor::~EqEditor() = default;

float EqEditor::barX (int b) const { return b == kOut ? outBar : firstBar + (float) b * barGap; }
float EqEditor::yOf (float db) const { return zeroY - db * perDb; }
float EqEditor::dbAt (float y) const { return jlimit (-eq::kRange, eq::kRange, std::round ((zeroY - y) / perDb * 10.0f) / 10.0f); }

float EqEditor::valueOf (int b) const
{
    if (auto* v = proc.params.getRawParameterValue (b == kOut ? String ("output") : EqProcessor::bandId (b))) return v->load();
    return 0.0f;
}

void EqEditor::set (int b, float db)
{
    if (! isPositiveAndBelow (b, eq::kBands + 1) || attach[(size_t) b] == nullptr) return;
    if (std::find (touched.begin(), touched.end(), b) == touched.end())
    {
        touched.push_back (b);
        attach[(size_t) b]->beginGesture();
    }
    attach[(size_t) b]->setValueAsPartOfGesture (jlimit (-eq::kRange, eq::kRange, db));
}

int EqEditor::barAt (float x) const
{
    if (std::abs (x - outBar) < 40.0f) return kOut;
    const int b = roundToInt ((x - firstBar) / barGap);
    if (b < 0 || b >= eq::kBands || std::abs (x - barX (b)) > barGap * 0.5f) return -1;
    return b;
}

// ---- mouse ---------------------------------------------------------------------------------
// Click or drag on a bar: the knob goes to the mouse. Drag sideways to draw over several bars.
// Shift: fine (the knob moves slowly from where it was). Double-click: 0 dB. Wheel: 0.5 dB steps.

void EqEditor::mouseDown (const MouseEvent& e)
{
    for (int i = 0; i < (int) buttons.size(); ++i)
        if (buttons[(size_t) i].r.contains (e.getPosition())) { pressedButton = i; repaint(); return; }
    if (e.position.y < top - 4.0f || e.position.y > yOf (-eq::kRange) + 26.0f) return;
    dragging = barAt (e.position.x);
    if (dragging < 0) return;
    fine = e.mods.isShiftDown();
    fineStartY = e.position.y;
    fineStartDb = valueOf (dragging);
    if (! fine) set (dragging, dbAt (e.position.y));
    repaint();
}

void EqEditor::mouseDrag (const MouseEvent& e)
{
    if (pressedButton >= 0 || dragging < 0) return;
    if (fine)
    {
        set (dragging, std::round ((fineStartDb + (fineStartY - e.position.y) / perDb * 0.25f) * 10.0f) / 10.0f);
        return;
    }
    // drawing: every band bar the mouse passes (not the OUT bar unless the drag started there)
    const int b = barAt (e.position.x);
    if (b >= 0 && (b == kOut) == (dragging == kOut)) { dragging = b; hover = b; }
    set (dragging, dbAt (e.position.y));
}

void EqEditor::mouseUp (const MouseEvent& e)
{
    if (pressedButton >= 0)
    {
        const auto& btn = buttons[(size_t) pressedButton];
        pressedButton = -1;
        repaint();
        if (btn.r.contains (e.getPosition()) && btn.click) btn.click();
        return;
    }
    for (int b : touched) attach[(size_t) b]->endGesture();
    touched.clear();
    dragging = -1;
    repaint();
}

void EqEditor::mouseMove (const MouseEvent& e)
{
    const int b = e.position.y >= top - 4.0f && e.position.y <= yOf (-eq::kRange) + 26.0f ? barAt (e.position.x) : -1;
    if (b != hover) { hover = b; repaint(); }
}

void EqEditor::mouseExit (const MouseEvent&)
{
    if (hover >= 0 && dragging < 0) { hover = -1; repaint(); }
}

void EqEditor::mouseDoubleClick (const MouseEvent& e)
{
    const int b = barAt (e.position.x);
    if (b >= 0 && attach[(size_t) b] != nullptr && e.position.y > top) attach[(size_t) b]->setValueAsCompleteGesture (0.0f);
}

void EqEditor::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const int b = barAt (e.position.x);
    if (b < 0 || attach[(size_t) b] == nullptr || w.deltaY == 0.0f) return;
    const float step = w.deltaY > 0.0f ? 0.5f : -0.5f;
    attach[(size_t) b]->setValueAsCompleteGesture (jlimit (-eq::kRange, eq::kRange, std::round ((valueOf (b) + step) * 2.0f) / 2.0f));
}

void EqEditor::showPresetMenu()
{
    PopupMenu menu;
    const auto& list = EqProcessor::presets();
    for (int i = 0; i < (int) list.size(); ++i) menu.addItem (i + 1, list[(size_t) i].name);
    menu.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (buttons[0].r)),
                        [safe = Component::SafePointer<EqEditor> (this)] (int r)
                        {
                            if (safe != nullptr && r > 0) safe->proc.applyPreset (r - 1);
                        });
}

// ---- drawing -------------------------------------------------------------------------------

void EqEditor::paint (Graphics& g)
{
    g.fillAll (bg);

    // header
    g.setColour (textCol);
    PixelFont::draw (g, "GRAPHIC EQ", { 24.0f, 14.0f, 400.0f, 32.0f }, 28.0f, Justification::centredLeft, true);
    g.setColour (dimText);
    PixelFont::draw (g, "10 BANDS  +12 / -12 DB  -  VST3", { 26.0f, 50.0f, 400.0f, 12.0f }, 10.0f, Justification::centredLeft);
    {
        // what the mouse is on
        const int b = dragging >= 0 ? dragging : hover;
        String t;
        if (b == kOut) t = "OUTPUT  " + dbText (valueOf (kOut));
        else if (b >= 0) t = hzText (b) + "  " + dbText (valueOf (b));
        if (t.isNotEmpty())
        {
            g.setColour (textCol);
            PixelFont::draw (g, t, { 440.0f, 20.0f, 340.0f, 24.0f }, 18.0f, Justification::centredLeft, true);
        }
    }
    for (int i = 0; i < (int) buttons.size(); ++i)
    {
        const auto& btn = buttons[(size_t) i];
        auto r = btn.r.toFloat();
        if (pressedButton == i) r = r.translated (1.0f, 1.0f);
        g.setColour (black);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (textCol);
        PixelFont::draw (g, btn.label, r, 13.0f, Justification::centred, true);
    }
    g.setColour (black.withAlpha (0.35f));
    g.fillRect (0.0f, top - 6.0f, (float) W, 2.0f);

    // dB ruler: labels, a thick tick every 6 dB, a thin one every 1 dB, and the long line with "db"
    const float rulerL = 80.0f, rulerR = 136.0f, minorL = 120.0f;
    g.setFont (labelFont (19.0f));
    for (int db = -12; db <= 12; ++db)
    {
        const float y = yOf ((float) db);
        g.setColour (black);
        if (db % 6 == 0)
        {
            g.fillRect (rulerL, y - 3.0f, rulerR - rulerL, 6.0f);
            g.setColour (textCol);
            const String t = db > 0 ? "+" + String (db) : String (db);
            g.drawText (t, Rectangle<float> (24.0f, y - 12.0f, 50.0f, 24.0f), Justification::centredRight, false);
        }
        else g.fillRect (minorL, y - 1.0f, rulerR - minorL, 2.0f);
    }
    g.setColour (black);
    g.fillRect (14.0f, yOf (12.0f) + 4.0f, 2.0f, zeroY - 24.0f - (yOf (12.0f) + 4.0f));
    g.fillRect (14.0f, zeroY + 22.0f, 2.0f, yOf (-12.0f) - 8.0f - (zeroY + 22.0f));
    g.setColour (textCol);
    g.drawText ("db", Rectangle<float> (0.0f, zeroY - 12.0f, 34.0f, 24.0f), Justification::centred, false);

    // the bars, with a short dash between each pair at +12, +6, -6 and -12
    const float barTop = yOf (12.0f) - 2.0f, barBottom = yOf (-12.0f) + 2.0f;
    for (int b = 0; b <= eq::kBands; ++b)
    {
        const float x = barX (b);
        g.setColour (black);
        g.fillRect (x - 3.0f, barTop, 6.0f, barBottom - barTop);
        if (b < eq::kBands - 1)
            for (int db : { 12, 6, -6, -12 })
                g.fillRect (x + barGap * 0.5f - 14.0f, yOf ((float) db) - 1.0f, 28.0f, 2.0f);
        g.setColour (b == hover || b == dragging ? textCol : textCol.withAlpha (0.9f));
        g.setFont (labelFont (19.0f));
        g.drawText (b == kOut ? String ("OUT") : String (eq::bandLabel (b)), Rectangle<float> (x - 44.0f, barBottom + 12.0f, 88.0f, 26.0f),
                    Justification::centred, false);
    }
    // the OUT bar stands apart
    g.setColour (black.withAlpha (0.35f));
    g.fillRect (1010.0f, barTop, 2.0f, barBottom - barTop);
    for (int db : { 12, 6, -6, -12 })
    {
        g.setColour (black);
        g.fillRect (outBar - 28.0f, yOf ((float) db) - 1.0f, 14.0f, 2.0f);
        g.fillRect (outBar + 14.0f, yOf ((float) db) - 1.0f, 14.0f, 2.0f);
    }

    // the white line: a smooth curve through the 10 knobs
    {
        std::array<Point<float>, eq::kBands> pts;
        for (int b = 0; b < eq::kBands; ++b) pts[(size_t) b] = { barX (b), yOf (valueOf (b)) };
        auto tangent = [&] (int i)
        {
            const int a = jmax (0, i - 1), c = jmin (eq::kBands - 1, i + 1);
            return (pts[(size_t) c].y - pts[(size_t) a].y) / (float) (c - a);   // per bar
        };
        Path curve;
        curve.startNewSubPath (pts[0]);
        for (int i = 0; i < eq::kBands - 1; ++i)
        {
            const float m0 = tangent (i), m1 = tangent (i + 1);
            const auto& p0 = pts[(size_t) i];
            const auto& p1 = pts[(size_t) i + 1];
            for (int k = 1; k <= 16; ++k)
            {
                const float t = (float) k / 16.0f, t2 = t * t, t3 = t2 * t;
                const float y = (2 * t3 - 3 * t2 + 1) * p0.y + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * p1.y + (t3 - t2) * m1;
                curve.lineTo (p0.x + (p1.x - p0.x) * t, jlimit (barTop, barBottom, y));
            }
        }
        g.setColour (lineCol);
        g.strokePath (curve, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    // OUT has its own short line
    g.setColour (lineCol);
    g.fillRect (outBar - 22.0f, yOf (valueOf (kOut)) - 2.0f, 44.0f, 4.0f);

    // the knobs
    for (int b = 0; b <= eq::kBands; ++b)
    {
        const bool hot = b == hover || b == dragging;
        const float d = hot ? 18.0f : 15.0f;
        auto r = Rectangle<float> (d, d).withCentre ({ barX (b), yOf (valueOf (b)) });
        g.setColour (black.withAlpha (0.3f));
        g.fillEllipse (r.translated (1.0f, 1.5f));
        g.setColour (hot ? lineCol : knobCol);
        g.fillEllipse (r);
        if (hot)
        {
            // the value next to the knob
            const String t = dbText (valueOf (b));
            const float tw = 78.0f;
            auto box = Rectangle<float> (tw, 22.0f).withCentre ({ barX (b) + (b >= eq::kBands - 1 ? -56.0f : 56.0f), r.getCentreY() });
            g.setColour (black);
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (textCol);
            PixelFont::draw (g, t, box, 11.0f, Justification::centred, true);
        }
    }
}

void EqEditor::resized()
{
    buttons[0].r = { W - 266, 18, 140, 40 };
    buttons[1].r = { W - 116, 18, 92, 40 };
}

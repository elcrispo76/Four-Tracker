// SPDX-License-Identifier: AGPL-3.0-or-later
// Dynamics Processor - Copyright (C) 2026 elcrispo76

#include "PluginEditor.h"

namespace
{
    constexpr int W = 1100, H = 690;
    constexpr int kw = 66, kh = 96;

    String db (int v)        { return String (v) + " DB"; }
    String dbSigned (int v)  { return (v > 0 ? "+" : "") + String (v) + " DB"; }
    String pctSigned (int v) { return (v > 0 ? "+" : "") + String (v) + "%"; }
    String ms (double t)
    {
        if (t >= 1000.0) return String (t / 1000.0, 2) + " S";
        if (t >= 10.0) return String (roundToInt (t)) + " MS";
        return String (t, t < 1.0 ? 2 : 1) + " MS";
    }

    /** Fits a text in a width: smaller and smaller, down to `minH`. */
    float fit (const String& t, float h, float minH, float width)
    {
        while (h > minH && PixelFont::getWidth (t, h) > width) h -= 0.5f;
        return h;
    }
}

Knob* DynEditor::knob (const char* id, const String& label, std::function<String (int)> fmt, const String& tip)
{
    auto* rp = dynamic_cast<RangedAudioParameter*> (proc.params.getParameter (id));
    jassert (rp != nullptr);
    auto* k = knobs.add (new Knob (*rp, label, std::move (fmt)));
    k->setTooltip (tip);
    addAndMakeVisible (k);
    return k;
}

DynEditor::Module& DynEditor::addModule (const String& name, const char* onId, std::vector<std::vector<Knob*>> rows)
{
    auto m = std::make_unique<Module>();
    m->name = name;
    m->onId = onId;
    m->rows = std::move (rows);
    auto* mod = m.get();
    modules.push_back (std::move (m));
    if (auto* p = proc.params.getParameter (onId))
    {
        mod->onButton = std::make_unique<ToolButton> ("ON");
        mod->onButton->textHeight = 11.0f;
        mod->onButton->setTooltip ("Switch the " + name + " on or off");
        mod->onAttach = std::make_unique<ParameterAttachment> (*p, [this, mod] (float v)
        {
            mod->onButton->setSelected (v > 0.5f);
            mod->onButton->label = v > 0.5f ? "ON" : "OFF";
            mod->onButton->repaint();
            updateDimming();
        }, nullptr);
        mod->onButton->onClick = [mod] { mod->onAttach->setValueAsCompleteGesture (mod->onButton->selected ? 0.0f : 1.0f); };
        addAndMakeVisible (*mod->onButton);
        mod->onAttach->sendInitialUpdate();
    }
    return *mod;
}

DynEditor::DynEditor (DynProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    static const char* ratios[] = { "1.5:1", "2:1", "3:1", "4:1", "6:1", "8:1", "12:1", "20:1", "LIMIT" };
    static const char* modes[] = { "TAPE", "TUBE", "CLIP" };

    addModule ("TRANSIENT", "tsOn", {
        { knob ("tsAttack", "ATTACK", pctSigned, "Turns the hits (the first milliseconds of every note or drum) up (+) or down (-)"),
          knob ("tsSustain", "SUSTAIN", pctSigned, "Turns the tail after each hit up (+, more room and body) or down (-, tighter)") } });

    addModule ("COMPRESSOR", "compOn", {
        { knob ("threshold", "THRESHOLD", db, "Above this level the sound is turned down (AUTO sets it)"),
          knob ("ratio", "RATIO", [] (int v) { return String (ratios[jlimit (0, 8, v)]); }, "How much: 4:1 = 4 dB over the threshold comes out as 1 dB. LIMIT = nothing over"),
          knob ("attack", "ATTACK", [] (int v) { return ms (dyn::attackMs (v)); }, "How fast it grabs (0.05 - 100 ms). Slow lets the click of a kick or snare through"),
          knob ("release", "RELEASE", [] (int v) { return ms (dyn::releaseMs (v)); }, "How fast it lets go (10 ms - 2 s). AUTO shortens it when the hits come fast") },
        { knob ("knee", "KNEE", db, "0 = hard corner, higher = it starts softly below the threshold"),
          knob ("makeup", "MAKEUP", dbSigned, "Turns the compressed sound up again (AUTO sets it to the same loudness)"),
          knob ("mix", "MIX", [] (int v) { return String (v) + "%"; }, "Dry / compressed. Less than 100 % = parallel compression (punch and air)"),
          knob ("detect", "DETECT", [] (int v) { return String (v == 1 ? "RMS" : "PEAK"); }, "PEAK reacts to every hit, RMS to the loudness (bass, pads, voice). Click to switch"),
          knob ("scFilter", "SC FILTER", [] (int v) { const double f = dyn::scFilterHz (v); return f <= 0.0 ? String ("OFF") : String (roundToInt (f)) + " HZ"; },
                "Sidechain filter: the compressor doesn't listen to the bass below this (e.g. a snare or voice ignores the kick)") } });

    addModule ("SATURATION", "satOn", {
        { knob ("satDrive", "DRIVE", [] (int v) { return "+" + String (dyn::driveDb (v), 1) + " DB"; }, "How hard the sound is pushed into the curve (0 - 24 dB). The level stays the same"),
          knob ("satMode", "MODE", [] (int v) { return String (modes[jlimit (0, 2, v)]); }, "TAPE = round and smooth, TUBE = warm and uneven, CLIP = harder and brighter") },
        { knob ("satTone", "TONE", [] (int v) { return v >= 100 ? String ("OPEN") : String (roundToInt (dyn::toneHz (v) / 100.0) / 10.0, 1) + " KHZ"; },
                "Softens the top of the saturated sound (2 - 20 kHz). OPEN = no filter"),
          knob ("satMix", "MIX", [] (int v) { return String (v) + "%"; }, "Dry / saturated") } });

    outputKnob = knob ("output", "OUTPUT", dbSigned, "Output level");

    for (int i = 0; i < dyn::numInstruments; ++i)
    {
        auto* b = instButtons.add (new ToolButton (dyn::instrumentName (i)));
        b->textHeight = 12.0f;
        b->setTooltip (String ("The track is ") + dyn::instrumentName (i) + ": sets all three for it. Then play the track and click AUTO");
        b->onClick = [this, i] { proc.selectInstrument (i); };
        addAndMakeVisible (b);
    }
    if (auto* ip = proc.params.getParameter ("instrument"))
    {
        instAttach = std::make_unique<ParameterAttachment> (*ip, [this] (float v)
        {
            for (int i = 0; i < instButtons.size(); ++i) instButtons[i]->setSelected (i == roundToInt (v));
        }, nullptr);
        instAttach->sendInitialUpdate();
    }
    // the curve is drawn again when one of its knobs moves
    for (auto* id : { "threshold", "ratio", "knee", "makeup", "mix", "compOn" })
        if (auto* cp = proc.params.getParameter (id))
            curveAttach.push_back (std::make_unique<ParameterAttachment> (*cp, [this] (float) { repaint (curveArea); }, nullptr));

    autoButton = std::make_unique<ToolButton> ("AUTO");
    autoButton->textHeight = 16.0f;
    autoButton->setTooltip ("AUTO listens to 4 seconds of the track and sets the transient shaper, the compressor and the drive for the instrument. Click again to stop");
    autoButton->onClick = [this] { proc.toggleAuto(); updateAutoButton(); repaint (statusArea); };
    addAndMakeVisible (*autoButton);

    presetButton = std::make_unique<ToolButton> ("PRESETS");
    presetButton->setTooltip ("Ready-made settings");
    presetButton->onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (*presetButton);

    updateDimming();
    setSize (W, H);
    startTimerHz (30);
}

DynEditor::~DynEditor() { stopTimer(); }

void DynEditor::updateDimming()
{
    for (auto& m : modules)
    {
        bool on = true;
        if (auto* v = proc.params.getRawParameterValue (m->onId)) on = v->load() > 0.5f;
        for (auto& row : m->rows)
            for (auto* k : row) k->setAlpha (on ? 1.0f : 0.4f);
    }
}

void DynEditor::showPresetMenu()
{
    PopupMenu menu;
    const auto& list = DynProcessor::presets();
    for (int i = 0; i < (int) list.size(); ++i) menu.addItem (i + 1, list[(size_t) i].name);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (presetButton.get()),
                        [safe = Component::SafePointer<DynEditor> (this)] (int r)
                        {
                            if (safe != nullptr && r > 0) safe->proc.applyPreset (r - 1);
                        });
}

void DynEditor::updateAutoButton()
{
    const bool on = proc.isListening();
    const int pct = roundToInt (proc.getListenProgress() * 100.0f);
    autoButton->setSelected (on);
    autoButton->label = on ? "LISTEN " + String (pct) + "%" : String ("AUTO");
    autoButton->textHeight = on ? 12.0f : 16.0f;
    autoButton->repaint();
}

void DynEditor::timerCallback()
{
    const bool on = proc.isListening();
    const int pct = roundToInt (proc.getListenProgress() * 100.0f);
    if (on != shownListening || (on && pct != shownProgress))
    {
        shownListening = on;
        shownProgress = pct;
        updateAutoButton();
        repaint (statusArea);
    }
    if (proc.getAutoMessage() != shownMessage) { shownMessage = proc.getAutoMessage(); repaint (statusArea); }

    // meters: up at once, down slowly
    auto& e = proc.getEngine();
    auto toDb = [] (float v) { return v > 1.0e-5f ? Decibels::gainToDecibels (v) : -90.0f; };
    meterIn  = jmax (toDb (e.takeInputPeak()),  meterIn - 1.2f);
    meterOut = jmax (toDb (e.takeOutputPeak()), meterOut - 1.2f);
    meterGr  = jmax (e.takeGainReduction(), meterGr - 0.8f);
    tsUp     = jmax (e.takeTransientUp(), tsUp - 0.6f);
    tsDown   = jmax (e.takeTransientDown(), tsDown - 0.6f);
    dotDb = jmax (e.getDetectorDb(), dotDb - 1.5f);
    repaint (meterArea);
    repaint (curveArea);
    repaint (tsMeterArea.expanded (2));
}

void DynEditor::paintCurve (Graphics& g, Rectangle<float> r)
{
    Look::screenBox (g, r);
    auto plot = r.reduced (10.0f);
    const auto s = proc.readSettings();
    const double thr = s.threshold, ratio = dyn::ratioValue (s.ratio), knee = s.knee;
    const double makeup = s.makeup, mix = s.mix / 100.0;
    constexpr double lo = -60.0, hi = 6.0;
    // the curve is wide: input along, output up
    auto px = [&] (double v) { return plot.getX() + (float) ((v - lo) / (hi - lo)) * plot.getWidth(); };
    auto py = [&] (double v) { return plot.getBottom() - (float) ((jlimit (lo, hi, v) - lo) / (hi - lo)) * plot.getHeight(); };
    auto outDb = [&] (double in)
    {
        if (! s.compOn) return in;
        const double gdb = -dyn::reductionDb (in, thr, ratio, knee) + makeup;
        const double gl = 1.0 + (std::pow (10.0, gdb / 20.0) - 1.0) * mix;
        return in + 20.0 * std::log10 (jmax (1e-6, gl));
    };

    g.setColour (Look::screenDim.withAlpha (0.25f));
    for (double v = -48.0; v <= 0.0; v += 12.0)
    {
        g.drawVerticalLine (roundToInt (px (v)), plot.getY(), plot.getBottom());
        g.drawHorizontalLine (roundToInt (py (v)), plot.getX(), plot.getRight());
    }
    g.setColour (Look::screenDim.withAlpha (0.5f));
    g.drawLine (px (lo), py (lo), px (hi), py (hi), 1.0f);

    const float tx = px (thr);
    if (s.compOn)
    {
        g.setColour (Look::red.withAlpha (0.7f));
        for (float y = plot.getY(); y < plot.getBottom(); y += 6.0f) g.drawVerticalLine (roundToInt (tx), y, jmin (y + 3.0f, plot.getBottom()));
    }

    Path curve;
    for (int i = 0; i <= 200; ++i)
    {
        const double in = lo + (hi - lo) * i / 200.0;
        const float x = px (in), y = py (outDb (in));
        if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());
    g.setColour (s.compOn ? Look::screenText : Look::screenDim);
    g.strokePath (curve, PathStrokeType (2.5f));
    if (s.compOn && dotDb > lo)
    {
        g.setColour (Look::red.brighter (0.3f));
        g.fillEllipse (Rectangle<float> (9.0f, 9.0f).withCentre ({ px (dotDb), py (outDb (dotDb)) }));
    }
    g.restoreState();

    Look::text (g, "OUT", { plot.getX() + 2.0f, plot.getY(), 60.0f, 12.0f }, 9.0f, Look::screenDim);
    Look::text (g, "IN", { plot.getRight() - 40.0f, plot.getBottom() - 12.0f, 38.0f, 12.0f }, 9.0f, Look::screenDim, Justification::centredRight);
    if (s.compOn)
        Look::text (g, "THR " + String (s.threshold), { tx + 4.0f, plot.getBottom() - 26.0f, 80.0f, 12.0f }, 9.0f, Look::red.brighter (0.3f));
    else
        Look::text (g, "COMPRESSOR OFF", plot, 11.0f, Look::screenDim, Justification::centred);
}

void DynEditor::paintMeters (Graphics& g, Rectangle<float> r)
{
    const float bw = 32.0f, gap = (r.getWidth() - 3.0f * bw) / 4.0f;
    const float top = r.getY() + 44.0f, bottom = r.getBottom() - 46.0f;
    const char* names[] = { "IN", "GR", "OUT" };
    for (int i = 0; i < 3; ++i)
    {
        auto bar = Rectangle<float> (r.getX() + gap + i * (bw + gap), top, bw, bottom - top);
        Look::screenBox (g, bar);
        auto inner = bar.reduced (4.0f);
        String val;
        if (i == 1)
        {
            const float frac = jlimit (0.0f, 1.0f, meterGr / 24.0f);
            g.setColour (Look::red);
            g.fillRect (inner.withHeight (inner.getHeight() * frac));
            val = meterGr >= 0.05f ? "-" + String (meterGr, 1) : String ("0.0");
        }
        else
        {
            const float v = i == 0 ? meterIn : meterOut;
            const float frac = jlimit (0.0f, 1.0f, (v + 60.0f) / 60.0f);
            g.setColour (Look::screenText);
            g.fillRect (inner.withTrimmedTop (inner.getHeight() * (1.0f - frac)));
            if (v > -1.0f) { g.setColour (Look::red); g.fillRect (inner.withHeight (6.0f)); }
            val = v > -60.0f ? String (v, 1) : String ("-INF");
        }
        Look::text (g, names[i], { bar.getX() - 10.0f, bar.getBottom() + 6.0f, bw + 20.0f, 14.0f }, 12.0f, Look::ink, Justification::centred, true);
        Look::text (g, val, { bar.getX() - 10.0f, bar.getBottom() + 22.0f, bw + 20.0f, 12.0f }, fit (val, 10.0f, 7.0f, bw + 18.0f), Look::ink, Justification::centred);
    }
    for (int dbv : { 0, -12, -24, -36, -48 })
    {
        const float y = top + 4.0f + (bottom - top - 8.0f) * (float) -dbv / 60.0f;
        Look::text (g, String (dbv), { r.getX() + 1.0f, y - 5.0f, gap - 3.0f, 10.0f }, 8.0f, Look::edgeLo.darker (0.4f), Justification::centredRight);
    }
}

void DynEditor::paintTransientMeter (Graphics& g, Rectangle<float> r)
{
    // how much the transient shaper turns up (white, right) and down (red, left) now, up to 12 dB
    Look::screenBox (g, r);
    auto in = r.reduced (3.0f);
    const float cx = in.getCentreX(), half = in.getWidth() * 0.5f;
    g.setColour (Look::screenText);
    g.fillRect (Rectangle<float> (cx, in.getY(), half * jlimit (0.0f, 1.0f, tsUp / 12.0f), in.getHeight()));
    g.setColour (Look::red);
    const float wd = half * jlimit (0.0f, 1.0f, tsDown / 12.0f);
    g.fillRect (Rectangle<float> (cx - wd, in.getY(), wd, in.getHeight()));
    g.setColour (Look::screenDim);
    g.fillRect (Rectangle<float> (cx - 0.5f, in.getY(), 1.0f, in.getHeight()));
    Look::text (g, "DOWN", { r.getX(), r.getBottom() + 3.0f, 60.0f, 11.0f }, 8.0f, Look::edgeLo.darker (0.4f));
    Look::text (g, "UP", { r.getRight() - 60.0f, r.getBottom() + 3.0f, 60.0f, 11.0f }, 8.0f, Look::edgeLo.darker (0.4f), Justification::centredRight);
}

void DynEditor::paint (Graphics& g)
{
    g.fillAll (Look::background);
    Look::text (g, "DYNAMICS PROCESSOR", { 30.0f, 26.0f, 560.0f, 36.0f }, 30.0f, Look::ink, Justification::centredLeft, true);
    Look::text (g, "TRANSIENT - COMPRESSOR - SATURATION - AUTO  -  VST3", { 32.0f, 70.0f, 560.0f, 14.0f }, 11.0f, Look::edgeLo.darker (0.4f));

    // the screen: what AUTO is doing
    {
        auto st = statusArea.toFloat();
        Look::screenBox (g, st);
        auto t = st.reduced (12.0f, 8.0f);
        const String head = proc.isListening()
            ? "AUTO LISTENING  " + String (roundToInt (proc.getListenProgress() * 100.0f)) + "%"
            : String (dyn::instrumentName (proc.getInstrument())) + "  -  " + (proc.getLastAutoResult().ok ? "AUTO SET" : "MANUAL");
        Look::text (g, head, t.removeFromTop (20.0f), fit (head, 16.0f, 10.0f, t.getWidth()), Look::screenText, Justification::centredLeft, true);
        const String msg = proc.getAutoMessage();
        Look::text (g, msg, t.removeFromBottom (16.0f), fit (msg, 11.0f, 6.5f, t.getWidth()),
                    proc.isListening() ? Look::red.brighter (0.3f) : Look::screenDim);
        if (proc.isListening())
        {
            auto bar = st.reduced (12.0f, 0.0f).removeFromBottom (5.0f).translated (0.0f, -3.0f);
            g.setColour (Look::redDark);
            g.fillRect (bar);
            g.setColour (Look::red);
            g.fillRect (bar.withWidth (bar.getWidth() * proc.getListenProgress()));
        }
    }

    Look::panelBox (g, instArea.toFloat());
    Look::text (g, "INSTRUMENT", { (float) instArea.getX() + 14.0f, (float) instArea.getY() + 20.0f, 140.0f, 16.0f }, 14.0f, Look::ink, Justification::centredLeft, true);
    Look::text (g, "ON THE TRACK", { (float) instArea.getX() + 14.0f, (float) instArea.getY() + 40.0f, 140.0f, 12.0f }, 10.0f, Look::edgeLo.darker (0.4f));

    for (auto& m : modules)
    {
        Look::panelBox (g, m->area.toFloat());
        Look::text (g, m->name, { (float) m->area.getX() + 14.0f, (float) m->area.getY() + 12.0f, 200.0f, 16.0f }, 14.0f, Look::ink, Justification::centredLeft, true);
    }
    for (auto [area, name] : { std::pair<Rectangle<int>, const char*> { curveArea, "COMPRESSOR CURVE" }, { meterArea, "METERS" }, { masterArea, "MASTER" } })
    {
        Look::panelBox (g, area.toFloat());
        Look::text (g, name, { (float) area.getX() + 14.0f, (float) area.getY() + 12.0f, 300.0f, 16.0f }, 14.0f, Look::ink, Justification::centredLeft, true);
    }
    paintCurve (g, curveArea.toFloat().withTrimmedTop (36.0f).reduced (12.0f, 0.0f).withTrimmedBottom (12.0f));
    paintMeters (g, meterArea.toFloat());
    paintTransientMeter (g, tsMeterArea.toFloat());

    // the order of the sound
    auto chain = masterArea.toFloat().reduced (16.0f).removeFromBottom (40.0f);
    Look::text (g, "THE SOUND GOES:", chain.removeFromTop (16.0f), 10.0f, Look::edgeLo.darker (0.4f));
    const String order = "TRANSIENT > COMPRESSOR > SATURATION > OUTPUT";
    Look::text (g, order, chain, fit (order, 11.0f, 7.0f, chain.getWidth()), Look::ink, Justification::centredLeft, true);
}

void DynEditor::resized()
{
    statusArea = { 600, 24, 476, 58 };
    instArea = { 24, 100, 1052, 76 };
    {
        int x = instArea.getX() + 150;
        for (auto* b : instButtons) { b->setBounds (x, instArea.getY() + 14, 92, 48); x += 96; }
        autoButton->setBounds (instArea.getRight() - 140, instArea.getY() + 10, 128, 56);
    }

    const int y0 = 188, ph = 270;
    modules[0]->area = { 24, y0, 200, ph };
    modules[1]->area = { 236, y0, 392, ph };
    modules[2]->area = { 640, y0, 250, ph };
    meterArea = { 902, y0, 174, ph };

    for (auto& mp : modules)
    {
        auto& m = *mp;
        if (m.onButton) m.onButton->setBounds (m.area.getRight() - 78, m.area.getY() + 6, 66, 32);
        const int rowGap = 16;
        const int rowsH = (int) m.rows.size() * kh + ((int) m.rows.size() - 1) * rowGap;
        int y = m.area.getY() + 40 + (m.area.getHeight() - 46 - rowsH) / 2;
        if (mp == modules[0]) y = m.area.getY() + 60;
        for (auto& row : m.rows)
        {
            const int rowW = (int) row.size() * kw + ((int) row.size() - 1) * 6;
            int x = m.area.getX() + (m.area.getWidth() - rowW) / 2;
            for (auto* k : row) { k->setBounds (x, y, kw, kh); x += kw + 6; }
            y += kh + rowGap;
        }
    }
    tsMeterArea = { modules[0]->area.getX() + 20, modules[0]->area.getY() + 190, modules[0]->area.getWidth() - 40, 16 };

    const int y1 = y0 + ph + 12, ph1 = 200;
    curveArea  = { 24, y1, 700, ph1 };
    masterArea = { 736, y1, 340, ph1 };
    presetButton->setBounds (masterArea.getX() + 14, masterArea.getY() + 44, 150, 44);
    outputKnob->setBounds (masterArea.getRight() - 110, masterArea.getY() + 30, 80, kh + 10);
}

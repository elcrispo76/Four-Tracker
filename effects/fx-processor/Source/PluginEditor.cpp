// SPDX-License-Identifier: AGPL-3.0-or-later
// FX Processor - Copyright (C) 2026 elcrispo76

#include "PluginEditor.h"

namespace
{
    constexpr int W = 1100, H = 648;
    constexpr int kw = 64, kh = 96;

    String hz (double f)
    {
        return f >= 1000.0 ? String (f / 1000.0, f >= 10000.0 ? 1 : 2) + " KHZ" : String (roundToInt (f)) + " HZ";
    }
    String hzSmall (double f) { return String (f, f < 1.0 ? 2 : 1) + " HZ"; }
    String percent (int v)    { return String (v) + "%"; }
    String plain (int v)      { return String (v); }
}

Knob* FxEditor::knob (const char* id, const String& label, std::function<String (int)> fmt, const String& tip)
{
    auto* rp = dynamic_cast<RangedAudioParameter*> (proc.params.getParameter (id));
    jassert (rp != nullptr);
    auto* k = knobs.add (new Knob (*rp, label, std::move (fmt)));
    k->setTooltip (tip);
    addAndMakeVisible (k);
    return k;
}

void FxEditor::addModule (const String& name, const char* onId, std::vector<std::vector<Knob*>> rows, int light)
{
    auto m = std::make_unique<Module>();
    m->name = name;
    m->onId = onId;
    m->rows = std::move (rows);
    m->light = light;
    if (onId != nullptr)
        if (auto* p = proc.params.getParameter (onId))
        {
            auto* mod = m.get();
            m->onButton = std::make_unique<ToolButton> ("OFF");
            m->onButton->textHeight = 11.0f;
            m->onButton->setTooltip ("Switch " + name + " on or off");
            m->onAttach = std::make_unique<ParameterAttachment> (*p, [this, mod] (float v)
            {
                mod->onButton->setSelected (v > 0.5f);
                mod->onButton->label = v > 0.5f ? "ON" : "OFF";
                mod->onButton->repaint();
                updateDimming();
            }, nullptr);
            m->onButton->onClick = [mod] { mod->onAttach->setValueAsCompleteGesture (mod->onButton->selected ? 0.0f : 1.0f); };
            addAndMakeVisible (*m->onButton);
            modules.push_back (std::move (m));
            modules.back()->onAttach->sendInitialUpdate();
            return;
        }
    modules.push_back (std::move (m));
}

FxEditor::FxEditor (FxProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    const char* modes[] = { "LP", "BP", "HP" };
    const char* pwRates[] = { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" };
    const char* shapes[] = { "SINE", "TRI", "SAW", "SQUARE", "S&H" };
    const char* syncs[] = { "FREE", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS" };
    const char* times[] = { "1/16", "1/8", "1/8 D", "1/4", "1/4 D", "1/2", "1 BAR" };

    addModule ("FILTER", "filterOn", { {
        knob ("filterMode", "MODE", [modes] (int v) { return String (modes[jlimit (0, 2, v)]); }, "Low-pass, band-pass or high-pass"),
        knob ("cutoff", "CUTOFF", [] (int v) { return hz (fx::cutoffHz (v)); }, "Cutoff frequency (the LFO can move it)"),
        knob ("reso", "RESO", plain, "Resonance") } });

    addModule ("PW", "pwOn", { {
        knob ("pwRate", "RATE", [pwRates] (int v) { return String (pwRates[jlimit (0, 5, v)]); }, "How often the gate opens, in time with the song"),
        knob ("pwWidth", "WIDTH", percent, "Pulse width: how much of each step the sound is on (the LFO can move it)"),
        knob ("pwDepth", "DEPTH", percent, "How far the sound goes down between the pulses") } }, 2);

    addModule ("LFO", nullptr, {
        { knob ("lfoShape", "SHAPE", [shapes] (int v) { return String (shapes[jlimit (0, 4, v)]); }, "The LFO's wave: sine, triangle, saw, square or sample & hold"),
          knob ("lfoSync", "SPEED", [syncs] (int v) { return String (syncs[jlimit (0, 7, v)]); },
                "How long one LFO wave lasts, in time with the song (1/16 to 4 bars), or FREE (then FREE HZ sets the speed)"),
          lfoFreeKnob = knob ("lfoRate", "FREE HZ", [] (int v) { return hzSmall (fx::lfoHz (v)); }, "The LFO's speed in Hz, only used when SPEED is FREE") },
        { knob ("lfoToFilter", "CUTOFF", [] (int v) { return (v > 0 ? "+" : "") + String (v); },
                "How much the LFO moves the FILTER's cutoff (up to 4 octaves; minus = the other way). Needs FILTER on."),
          knob ("lfoToPw", "PW WIDTH", percent, "How much the LFO moves the PW gate's WIDTH. Needs PW on.") } }, 1);

    addModule ("CHORUS", "chorusOn", { {
        knob ("chorusRate", "RATE", [] (int v) { return hzSmall (fx::chorusHz (v)); }, "Chorus speed"),
        knob ("chorusDepth", "DEPTH", plain, "Chorus depth"),
        knob ("chorusMix", "MIX", percent, "Dry / chorus") } });

    addModule ("DELAY", "delayOn", {
        { knob ("delayTime", "TIME", [times] (int v) { return String (times[jlimit (0, 6, v)]); }, "Delay time, in time with the song (D = dotted)"),
          knob ("delayFeedback", "FEEDBACK", percent, "How many repeats"),
          knob ("delayTone", "TONE", [] (int v) { return hz (fx::cutoffHz (v)); }, "Low-pass on the repeats (lower = darker, like tape)") },
        { knob ("delayMix", "MIX", percent, "How loud the repeats are"),
          knob ("delayPing", "PING-PONG", [] (int v) { return String (v ? "ON" : "OFF"); }, "Repeats bounce left and right. Click to switch.") } });

    addModule ("REVERB", "reverbOn", { {
        knob ("reverbSize", "SIZE", plain, "Room size"),
        knob ("reverbDamp", "DAMP", plain, "Damping (higher = darker)"),
        knob ("reverbWidth", "WIDTH", plain, "Stereo width"),
        knob ("reverbMix", "MIX", percent, "Dry / reverb") } });

    masterKnobs.push_back (knob ("output", "OUTPUT", [] (int v) { return (v > 0 ? "+" : "") + String (v) + " DB"; }, "Output level"));
    masterKnobs.push_back (knob ("tempo", "TEMPO", [] (int v) { return String (v) + " BPM"; },
                                 "Own tempo, used only when the host sends none (the display then says OWN)"));
    presetButton = std::make_unique<ToolButton> ("PRESETS");
    presetButton->setTooltip ("Ready-made settings");
    presetButton->onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (*presetButton);

    updateDimming();
    setSize (W, H);
    startTimerHz (30);
}

FxEditor::~FxEditor() { stopTimer(); }

void FxEditor::updateDimming()
{
    for (auto& m : modules)
    {
        bool on = true;
        if (m->onId != nullptr)
            if (auto* v = proc.params.getRawParameterValue (m->onId)) on = v->load() > 0.5f;
        for (auto& row : m->rows)
            for (auto* k : row) k->setAlpha (on ? 1.0f : 0.4f);
    }
}

void FxEditor::showPresetMenu()
{
    PopupMenu menu;
    const auto& list = FxProcessor::presets();
    for (int i = 0; i < (int) list.size(); ++i) menu.addItem (i + 1, list[(size_t) i].name);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (presetButton.get()),
                        [safe = Component::SafePointer<FxEditor> (this)] (int r)
                        {
                            if (safe != nullptr && r > 0) { safe->proc.applyPreset (r - 1); safe->updateDimming(); }
                        });
}

void FxEditor::timerCallback()
{
    // FREE HZ only counts when SPEED is FREE
    if (lfoFreeKnob != nullptr)
        if (auto* v = proc.params.getRawParameterValue ("lfoSync"))
        {
            const float a = v->load() < 0.5f ? 1.0f : 0.4f;
            if (lfoFreeKnob->getAlpha() != a) lfoFreeKnob->setAlpha (a);
        }
    const double bpm = proc.getShownBpm();
    if (std::abs (bpm - shownBpm) > 0.05 || proc.isHostTempo() != shownHost)
    {
        shownBpm = bpm;
        shownHost = proc.isHostTempo();
        repaint (statusArea);
    }
    const float lfo = proc.getLfo(), gate = proc.getGate();
    if (std::abs (lfo - shownLfo) > 0.02f || std::abs (gate - shownGate) > 0.02f)
    {
        shownLfo = lfo; shownGate = gate;
        for (auto& m : modules) if (m->light != 0) repaint (m->area.withHeight (40));
    }
}

void FxEditor::paint (Graphics& g)
{
    g.fillAll (Look::background);
    Look::text (g, "FX PROCESSOR", { 30.0f, 26.0f, 600.0f, 36.0f }, 34.0f, Look::ink, Justification::centredLeft, true);
    Look::text (g, "FILTER - PW - CHORUS - LFO - DELAY - REVERB  -  VST3", { 32.0f, 70.0f, 700.0f, 14.0f }, 11.0f, Look::edgeLo.darker (0.4f));

    // tempo screen
    auto st = statusArea.toFloat();
    Look::screenBox (g, st);
    auto top = st.reduced (12.0f, 9.0f);
    Look::text (g, shownBpm > 0.0 ? String (shownBpm, 1) + " BPM" : String ("NO TEMPO"), top.removeFromTop (18.0f), 16.0f,
                Look::screenText, Justification::centredLeft, true);
    Look::text (g, shownHost ? "FROM THE HOST" : "OWN TEMPO (NO HOST TEMPO)", top.removeFromBottom (14.0f), 11.0f,
                shownHost ? Look::red.brighter (0.3f) : Look::screenDim);

    for (auto& m : modules)
    {
        auto a = m->area.toFloat();
        Look::panelBox (g, a);
        Look::text (g, m->name, { a.getX() + 14.0f, a.getY() + 12.0f, 200.0f, 16.0f }, 14.0f, Look::ink, Justification::centredLeft, true);
        if (m->light == 1 && lfoCaptionY > 0)
        {
            // the second row are the LFO's targets: say so
            const String capText = "THE LFO MOVES:";
            const float th = 9.0f, tw = PixelFont::getWidth (capText, th);
            const float cy = (float) lfoCaptionY + 11.0f;
            Look::text (g, capText, { a.getCentreX() - tw * 0.5f, cy - th * 0.5f, tw + 2.0f, th }, th, Look::ink, Justification::centredLeft, true);
            g.setColour (Look::edgeLo);
            g.fillRect (Rectangle<float> (a.getX() + 14.0f, cy, a.getCentreX() - tw * 0.5f - 8.0f - (a.getX() + 14.0f), 1.0f));
            g.fillRect (Rectangle<float> (a.getCentreX() + tw * 0.5f + 8.0f, cy, a.getRight() - 14.0f - (a.getCentreX() + tw * 0.5f + 8.0f), 1.0f));
        }
        if (m->light != 0)
        {
            // a light that follows the LFO / the PW gate
            const float v = m->light == 1 ? 0.5f + 0.5f * shownLfo : shownGate;
            auto led = Rectangle<float> (26.0f, 10.0f).withPosition (a.getRight() - (m->onButton ? 112.0f : 44.0f), a.getY() + 15.0f);
            g.setColour (Look::edgeLo);
            g.fillRect (led.expanded (1.5f));
            g.setColour (Look::redDark.interpolatedWith (Look::red, jlimit (0.0f, 1.0f, v)));
            g.fillRect (led);
        }
    }

    Look::panelBox (g, masterArea.toFloat());
    Look::text (g, "MASTER", { (float) masterArea.getX() + 14.0f, (float) masterArea.getY() + 12.0f, 120.0f, 16.0f }, 14.0f, Look::ink, Justification::centredLeft, true);
}

void FxEditor::resized()
{
    statusArea = { 836, 24, 240, 58 };
    const int pw = 290, gap = 12, x0 = 24, y0 = 100, ph = 260;
    const int order[] = { 0, 1, 2, 3, 4, 5 };            // FILTER PW LFO / CHORUS DELAY REVERB
    for (int i = 0; i < (int) modules.size(); ++i)
    {
        auto& m = *modules[(size_t) order[i]];
        m.area = { x0 + (i % 3) * (pw + gap), y0 + (i / 3) * (ph + gap), pw, ph };
        if (m.onButton) m.onButton->setBounds (m.area.getRight() - 78, m.area.getY() + 6, 66, 32);

        const int rowGap = m.rows.size() > 1 ? 22 : 0;      // room for the "LFO MOVES:" line
        const int knobTop = m.area.getY() + 42, knobSpace = m.area.getHeight() - 46;
        const int rowsH = (int) m.rows.size() * kh + ((int) m.rows.size() - 1) * rowGap;
        int y = knobTop + (knobSpace - rowsH) / 2;
        for (auto& row : m.rows)
        {
            const int rowW = (int) row.size() * kw + ((int) row.size() - 1) * 4;
            int x = m.area.getX() + (m.area.getWidth() - rowW) / 2;
            for (auto* k : row) { k->setBounds (x, y, kw, kh); x += kw + 4; }
            if (m.light == 1 && &row == &m.rows.front()) lfoCaptionY = y + kh;
            y += kh + rowGap;
        }
    }
    masterArea = { x0 + 3 * (pw + gap), y0, W - 24 - (x0 + 3 * (pw + gap)), 2 * ph + gap };
    presetButton->setBounds (masterArea.getX() + 10, masterArea.getY() + 40, masterArea.getWidth() - 20, 42);
    int y = masterArea.getY() + 100;
    for (auto* k : masterKnobs)
    {
        k->setBounds (masterArea.getCentreX() - 40, y, 80, kh + 10);
        y += kh + 30;
    }
}

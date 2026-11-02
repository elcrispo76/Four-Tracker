// SPDX-License-Identifier: AGPL-3.0-or-later
// FX Processor 2 - Copyright (C) 2026 elcrispo76

#include "PluginEditor.h"

namespace
{
    constexpr int W = 1100, H = 414;
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
    static const char* syncs[] = { "FREE", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS" };
    static const char* stages[] = { "4", "6", "8", "12" };
    static const char* waves[] = { "SINE", "TRI", "SQUARE" };
    auto syncFmt = [] (int v) { return String (syncs[jlimit (0, 7, v)]); };
    const String speedTip = "How long one LFO wave lasts, in time with the song (1/16 to 4 bars), or FREE (then FREE HZ sets the speed)";

    Knob* phFree = nullptr;
    addModule ("PHASER", "phaserOn", {
        { knob ("phSync", "SPEED", syncFmt, speedTip),
          phFree = knob ("phRate", "FREE HZ", [] (int v) { return hzSmall (fx::lfoHz (v)); }, "Phaser speed in Hz, only when SPEED is FREE"),
          knob ("phDepth", "DEPTH", plain, "How far the notches sweep (up to 5 octaves)"),
          knob ("phCenter", "CENTER", [] (int v) { return hz (fx::phCenterHz (v)); }, "Where the sweep sits") },
        { knob ("phFeedback", "FEEDBACK", percent, "Stronger, ringing notches"),
          knob ("phStages", "STAGES", [] (int v) { return String (stages[jlimit (0, 3, v)]); }, "4, 6, 8 or 12 all-pass stages: more stages = more notches"),
          knob ("phMix", "MIX", percent, "Dry / phaser (100 % = the deepest notches)") } }, 1);
    modules.back()->freeKnob = phFree;  modules.back()->syncId = "phSync";

    Knob* flFree = nullptr;
    addModule ("FLANGER", "flangerOn", {
        { knob ("flSync", "SPEED", syncFmt, speedTip),
          flFree = knob ("flRate", "FREE HZ", [] (int v) { return hzSmall (fx::lfoHz (v)); }, "Flanger speed in Hz, only when SPEED is FREE"),
          knob ("flDepth", "DEPTH", plain, "How far the delay moves"),
          knob ("flDelay", "DELAY", [] (int v) { return String (fx::flDelayMs (v), 1) + " MS"; }, "The short delay (0.3 - 10 ms): short = jet, long = chorus") },
        { knob ("flFeedback", "FEEDBACK", [] (int v) { return (v > 0 ? "+" : "") + String (v) + "%"; }, "Plus = jet whoosh, minus = hollow metal"),
          knob ("flMix", "MIX", percent, "Dry / flanger") } }, 2);
    modules.back()->freeKnob = flFree;  modules.back()->syncId = "flSync";

    Knob* rmFree = nullptr;
    addModule ("RING MOD", "ringOn", {
        { knob ("rmFreq", "FREQ", [] (int v) { return hz (fx::rmFreqHz (v)); }, "Carrier frequency: low = tremolo / robot, high = bells and metal"),
          knob ("rmWave", "WAVE", [] (int v) { return String (waves[jlimit (0, 2, v)]); }, "Carrier wave: sine (smooth), triangle, square (buzzy)"),
          knob ("rmMix", "MIX", percent, "Dry / ring modulated") },
        { knob ("rmSync", "LFO SPEED", syncFmt, "The ring mod's LFO, which sweeps FREQ: in time with the song, or FREE"),
          rmFree = knob ("rmRate", "FREE HZ", [] (int v) { return hzSmall (fx::lfoHz (v)); }, "LFO speed in Hz, only when LFO SPEED is FREE"),
          knob ("rmLfoDepth", "LFO DEPTH", plain, "How far the LFO sweeps FREQ (up to 2 octaves each way). 0 = off") } }, 3);
    modules.back()->freeKnob = rmFree;  modules.back()->syncId = "rmSync";

    masterKnobs.push_back (knob ("output", "OUTPUT", [] (int v) { return (v > 0 ? "+" : "") + String (v) + " DB"; }, "Output level"));
    masterKnobs.push_back (knob ("tempo", "TEMPO", [] (int v) { return String (v) + " BPM"; },
                                 "Own tempo, used only when the host sends none (the display then says OWN TEMPO)"));
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
        bool free = true;
        if (m->syncId != nullptr)
            if (auto* v = proc.params.getRawParameterValue (m->syncId)) free = v->load() < 0.5f;
        for (auto& row : m->rows)
            for (auto* k : row)
                k->setAlpha (! on ? 0.4f : (k == m->freeKnob && ! free ? 0.4f : 1.0f));
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
    updateDimming();
    const double bpm = proc.getShownBpm();
    if (std::abs (bpm - shownBpm) > 0.05 || proc.isHostTempo() != shownHost)
    {
        shownBpm = bpm;
        shownHost = proc.isHostTempo();
        repaint (statusArea);
    }
    for (auto& m : modules)
        if (m->light > 0)
        {
            const float v = proc.getLfo (m->light - 1);
            auto& shown = shownLfo[(size_t) (m->light - 1)];
            if (std::abs (v - shown) > 0.02f) { shown = v; repaint (m->area.withHeight (40)); }
        }
}

void FxEditor::paint (Graphics& g)
{
    g.fillAll (Look::background);
    Look::text (g, "FX PROCESSOR 2", { 30.0f, 26.0f, 600.0f, 36.0f }, 34.0f, Look::ink, Justification::centredLeft, true);
    Look::text (g, "PHASER - FLANGER - RING MODULATOR  -  VST3", { 32.0f, 70.0f, 700.0f, 14.0f }, 11.0f, Look::edgeLo.darker (0.4f));

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
        if (m->light > 0)
        {
            // a light that follows the effect's LFO
            const float v = 0.5f + 0.5f * shownLfo[(size_t) (m->light - 1)];
            auto led = Rectangle<float> (26.0f, 10.0f).withPosition (a.getRight() - 112.0f, a.getY() + 15.0f);
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
    const int pw = 290, gap = 12, x0 = 24, y0 = 100, ph = 290;
    for (int i = 0; i < (int) modules.size(); ++i)
    {
        auto& m = *modules[(size_t) i];
        m.area = { x0 + i * (pw + gap), y0, pw, ph };
        if (m.onButton) m.onButton->setBounds (m.area.getRight() - 78, m.area.getY() + 6, 66, 32);
        const int rowGap = 14;
        const int knobTop = m.area.getY() + 44, knobSpace = m.area.getHeight() - 50;
        const int rowsH = (int) m.rows.size() * kh + ((int) m.rows.size() - 1) * rowGap;
        int y = knobTop + (knobSpace - rowsH) / 2;
        for (auto& row : m.rows)
        {
            const int rowW = (int) row.size() * kw + ((int) row.size() - 1) * 4;
            int x = m.area.getX() + (m.area.getWidth() - rowW) / 2;
            for (auto* k : row) { k->setBounds (x, y, kw, kh); x += kw + 4; }
            y += kh + rowGap;
        }
    }
    masterArea = { x0 + 3 * (pw + gap), y0, W - 24 - (x0 + 3 * (pw + gap)), ph };
    presetButton->setBounds (masterArea.getX() + 10, masterArea.getY() + 38, masterArea.getWidth() - 20, 36);
    int y = masterArea.getY() + 82;
    for (auto* k : masterKnobs)
    {
        k->setBounds (masterArea.getCentreX() - 34, y, 68, kh);
        y += kh + 6;
    }
}

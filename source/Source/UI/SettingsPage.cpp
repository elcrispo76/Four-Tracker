// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#include "SettingsPage.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

using namespace juce;
using namespace ft;

static const char* const kModeNames[4] = { "NEXT ROW (LEGATO)", "NEXT BEAT (LEGATO)", "END OF PATTERN", "RESTART AT ROW 0" };

SettingsPage::SettingsPage (EditorState& s) : st (s)
{
    addAndMakeVisible (baseNote);
    addAndMakeVisible (learn);
    addAndMakeVisible (channel);
    addAndMakeVisible (midiStartStop);
    addAndMakeVisible (master);

    baseNote.formatter = [] (int v) { return midiNoteName (v); };
    baseNote.parser = [] (const String& t, bool& ok)
    {
        // accepts "C-2", "C#2", "c2" or a MIDI number
        auto txt = t.trim().toUpperCase().removeCharacters (" ");
        ok = false;
        if (txt.containsOnly ("0123456789") && txt.isNotEmpty()) { ok = true; return txt.getIntValue(); }
        static const char* names = "C D EF G A B";
        if (txt.isEmpty()) return 0;
        const int idx = String (names).indexOfChar (txt[0]);
        if (idx < 0) return 0;
        int semi = idx;
        auto rest = txt.substring (1);
        if (rest.startsWithChar ('#')) { ++semi; rest = rest.substring (1); }
        else if (rest.startsWithChar ('-')) rest = rest.substring (1);   // "C-2": the dash is a separator
        const int oct = rest.getIntValue();
        ok = true;
        return (oct + 1) * 12 + semi;
    };
    baseNote.setDragSensitivity (6.0f);
    baseNote.onChange = [this] (int v) { st.engine().midiBaseNote = v; repaint(); };

    learn.setClickingTogglesState (false);
    learn.onClick = [this]
    {
        auto& e = st.engine();
        e.midiLearn = ! e.midiLearn.load();
        learn.setToggleState (e.midiLearn.load(), dontSendNotification);
    };

    channel.formatter = [] (int v) { return v == 0 ? String ("OMNI") : String (v); };
    channel.parser = [] (const String& t, bool& ok) { ok = true; return t.trim().equalsIgnoreCase ("OMNI") ? 0 : t.getIntValue(); };
    channel.setDragSensitivity (8.0f);
    channel.onChange = [this] (int v) { st.engine().midiChannel = v; };

    for (int i = 0; i < 4; ++i)
    {
        modeButtons[(size_t) i] = std::make_unique<PixelButton> (kModeNames[i], PixelButton::Style::Segment, 16.0f);
        addAndMakeVisible (*modeButtons[(size_t) i]);
        modeButtons[(size_t) i]->onClick = [this, i] { st.engine().switchMode = i; updateModeButtons(); };
    }

    midiStartStop.onChange = [this] (bool on) { st.engine().respondToMidiStartStop = on; };
    master.onChange = [this] (int v) { st.engine().masterVolume = v; };
    master.setDragSensitivity (2.0f);

   #if JucePlugin_Build_Standalone
    if (JUCEApplicationBase::isStandaloneApp())
        if (auto* holder = StandalonePluginHolder::getInstance())
        {
            isStandalone = true;
            deviceLnf = std::make_unique<LookAndFeel_V4> (LookAndFeel_V4::ColourScheme {
                Colour (0xffc4c7cf), Colour (0xffc4c7cf), Colour (0xffd8dade), Colour (0xff878b94), Colour (0xff0c0d0f),
                Colour (0xffe4e6eb), Colour (0xff0c0d0f), Colour (0xff5d6169), Colour (0xff0c0d0f) });
            deviceSelector = std::make_unique<AudioDeviceSelectorComponent> (holder->deviceManager, 0, 0, 2, 2, true, true, true, false);   // MIDI in + MIDI out
            deviceSelector->setLookAndFeel (deviceLnf.get());
            addAndMakeVisible (*deviceSelector);
        }
   #endif

    if (! isStandalone)
    {
        addAndMakeVisible (followHost);
        followHost.onChange = [this] (bool on) { st.engine().followHostTempo = on; };
    }

    refresh();
}

SettingsPage::~SettingsPage()
{
    if (deviceSelector != nullptr) deviceSelector->setLookAndFeel (nullptr);
}

void SettingsPage::updateModeButtons()
{
    const int m = st.engine().switchMode.load();
    for (int i = 0; i < 4; ++i) modeButtons[(size_t) i]->setToggleState (i == m, dontSendNotification);
}

void SettingsPage::refresh()
{
   #if JucePlugin_Build_Standalone
    // MIDI instruments: send to the MIDI output chosen here (it changes while the audio runs)
    if (isStandalone)
        if (auto* holder = StandalonePluginHolder::getInstance())
            holder->player.setMidiOutput (holder->deviceManager.getDefaultMidiOutput());
   #endif
    auto& e = st.engine();
    baseNote.setValue (e.midiBaseNote.load());
    channel.setValue (e.midiChannel.load());
    midiStartStop.setState (e.respondToMidiStartStop.load());
    master.setValue (e.masterVolume.load());
    followHost.setState (e.followHostTempo.load());
    updateModeButtons();

    const bool learning = e.midiLearn.load();
    learn.setToggleState (learning, dontSendNotification);

    const int n = e.lastMidiNote.load(), b = e.midiBaseNote.load();
    if (n != lastNote || b != lastBase || learning != lastLearn)
    {
        lastNote = n; lastBase = b; lastLearn = learning;
        repaint (midiPanel);
    }
}

void SettingsPage::paint (Graphics& g)
{
    auto& e = st.engine();
    const auto ink = Theme::ink;

    Theme::drawPanel (g, midiPanel.toFloat());
    Theme::drawPanel (g, audioPanel.toFloat());

    Theme::text (g, "MIDI IN", midiPanel.toFloat().translated (18.0f, 14.0f).withHeight (30.0f), 30.0f, ink, Justification::topLeft, true);
    Theme::text (g, "AUDIO", audioPanel.toFloat().translated (18.0f, 14.0f).withHeight (30.0f), 30.0f, ink, Justification::topLeft, true);

    auto label = [&] (const String& t, Component& c)
    {
        Theme::text (g, t, { (float) midiPanel.getX() + 20.0f, (float) c.getY(), 260.0f, (float) c.getHeight() }, 18.0f, ink, Justification::centredLeft);
    };
    label ("START/STOP KEY", baseNote);
    label ("MIDI CHANNEL", channel);
    label ("MIDI START/STOP", midiStartStop);
    Theme::text (g, "PATTERN SWITCH", { (float) midiPanel.getX() + 20.0f, (float) modeButtons[0]->getY() - 30.0f, 400.0f, 20.0f }, 18.0f, ink, Justification::centredLeft);

    if (e.midiLearn.load())
        Theme::text (g, "PRESS A KEY ON YOUR MIDI KEYBOARD...", { (float) midiPanel.getX() + 20.0f, (float) baseNote.getBottom() + 4.0f, 560.0f, 18.0f }, 15.0f, ink, Justification::centredLeft);

    // key map
    Theme::drawScreen (g, mapBox.toFloat());
    const int base = e.midiBaseNote.load();
    const int lastN = e.lastMidiNote.load();
    auto inner = mapBox.toFloat().reduced (16.0f, 12.0f);
    Theme::text (g, "LAST KEY: " + (lastN >= 0 ? midiNoteName (lastN) : String ("-")), inner.removeFromTop (22.0f), 18.0f, Theme::screenText, Justification::centredLeft);
    inner.removeFromTop (8.0f);

    const int perCol = 9;
    const float colW = inner.getWidth() / 3.0f;
    for (int i = 0; i < perCol * 3; ++i)
    {
        const int note = base + i;
        if (note > 127) break;
        const String what = i == 0 ? String ("START/STOP") : "PATTERN " + formatNumber (i - 1, 2, st.hex());
        const int col = i / perCol, row = i % perCol;
        auto r = Rectangle<float> (inner.getX() + col * colW, inner.getY() + row * 24.0f, colW, 22.0f);
        const auto c = note == lastN ? Theme::accent : (i == 0 ? Theme::screenText : Theme::screenDim);
        Theme::text (g, midiNoteName (note), r.withWidth (44.0f), 14.0f, c, Justification::centredLeft);
        Theme::text (g, what, r.withTrimmedLeft (50.0f), 14.0f, c, Justification::centredLeft);
    }
    Theme::text (g, "... UP TO PATTERN " + formatNumber (kMaxPatterns - 1, 2, st.hex()) + " ON " + midiNoteName (base + kMaxPatterns),
                 { inner.getX(), inner.getBottom() - 18.0f, inner.getWidth(), 18.0f }, 15.0f, Theme::screenDim, Justification::centredLeft);

    // audio side
    Theme::text (g, "MASTER", { (float) audioPanel.getX() + 20.0f, (float) master.getY(), 140.0f, (float) master.getHeight() }, 18.0f, ink, Justification::centredLeft);

    if (! isStandalone)
    {
        auto r = Rectangle<float> ((float) audioPanel.getX() + 20.0f, 150.0f, (float) audioPanel.getWidth() - 40.0f, 200.0f);
        Theme::text (g, "RUNNING AS A PLUGIN:", r.removeFromTop (24.0f), 18.0f, ink, Justification::centredLeft);
        Theme::text (g, "AUDIO DRIVER (ASIO), DEVICE AND BUFFER", r.removeFromTop (24.0f), 16.0f, ink, Justification::centredLeft);
        Theme::text (g, "SIZE ARE SET IN YOUR HOST (DAW).", r.removeFromTop (24.0f), 16.0f, ink, Justification::centredLeft);
        Theme::text (g, "ROUTE YOUR MIDI KEYBOARD TO THIS TRACK", r.removeFromTop (24.0f), 16.0f, ink, Justification::centredLeft);
        Theme::text (g, "TO USE THE LIVE KEYS.", r.removeFromTop (24.0f), 16.0f, ink, Justification::centredLeft);
        Theme::text (g, "FOLLOW HOST TEMPO", { (float) audioPanel.getX() + 20.0f, (float) followHost.getY(), 300.0f, (float) followHost.getHeight() }, 18.0f, ink, Justification::centredLeft);
    }
    else if (deviceSelector != nullptr)
    {
        Theme::drawScreen (g, deviceSelector->getBounds().toFloat().expanded (6.0f).withTrimmedTop (0));
        g.setColour (Theme::panel);
        g.fillRect (deviceSelector->getBounds());
    }
}

void SettingsPage::resized()
{
    midiPanel = { 0, 0, 640, getHeight() };
    audioPanel = { 648, 0, getWidth() - 648, getHeight() };

    const int x = 300;
    baseNote.setBounds (x, 66, 170, 46);
    learn.setBounds (x + 182, 66, 134, 46);
    channel.setBounds (x, 128, 170, 46);
    midiStartStop.setBounds (x, 190, 200, 46);

    for (int i = 0; i < 4; ++i)
        modeButtons[(size_t) i]->setBounds (20 + (i % 2) * 304, 284 + (i / 2) * 54, 296, 46);

    mapBox = { 14, 400, 612, getHeight() - 414 };

    master.setBounds (audioPanel.getX() + 170, 66, 140, 46);
    if (deviceSelector != nullptr)
        deviceSelector->setBounds (audioPanel.getX() + 20, 130, audioPanel.getWidth() - 40, getHeight() - 150);
    followHost.setBounds (audioPanel.getX() + 300, 340, 200, 46);
}

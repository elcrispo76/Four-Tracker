// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#include "Mixer.h"

using namespace juce;
using namespace ft;

namespace
{
    constexpr float kDbMin = -48.0f, kDbMax = 6.0f;
    const Colour kGreen  { 0xff52c46a }, kYellow { 0xffe6c23a }, kRed { 0xffe0342b }, kOff { 0xff26282d };
    constexpr int kStripW = 72, kMasterW = 96, kGap = 6, kTop = 52;
}

// ===========================================================================
// MixerStrip
// ===========================================================================

MixerStrip::MixerStrip (EditorState& s, int channel) : st (s), ch (channel)
{
    if (ch >= 0)
    {
        addAndMakeVisible (pan);
        addAndMakeVisible (muteBtn);
        addAndMakeVisible (soloBtn);
        pan.formatter = [] (int v) { return v == 0 ? String ("C") : (v < 0 ? "L" + String (-v) : "R" + String (v)); };
        pan.setDragSensitivity (2.0f);
        pan.setJustification (Justification::centred);
        pan.onChange = [this] (int v) { st.engine().mix[ch].pan = v; };
        muteBtn.setComponentID ("mixMute" + String (ch));
        soloBtn.setComponentID ("mixSolo" + String (ch));
        muteBtn.onClick = [this]
        {
            auto& m = st.engine().mix[ch].mute;
            m = ! m.load();
            st.setStatus ("CH" + String (ch + 1) + (m.load() ? " MUTED" : " UNMUTED"));
            st.changed();
        };
        soloBtn.onClick = [this]
        {
            auto& so = st.engine().mix[ch].solo;
            so = ! so.load();
            st.setStatus ("CH" + String (ch + 1) + (so.load() ? " SOLO" : " SOLO OFF"));
            st.changed();
        };
    }
    for (auto* b : { &muteBtn, &soloBtn }) { b->setWantsKeyboardFocus (false); b->setMouseClickGrabsKeyboardFocus (false); }
    setComponentID (ch < 0 ? "mixMaster" : "mixStrip" + String (ch));
}

float MixerStrip::toDb (float peak)
{
    return peak <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (peak);
}

float MixerStrip::meterPos (float db)
{
    return jlimit (0.0f, 1.0f, (db - kDbMin) / (kDbMax - kDbMin));
}

int MixerStrip::getVolume() const
{
    auto& e = st.engine();
    return ch < 0 ? e.masterVolume.load() : e.mix[ch].volume.load();
}

void MixerStrip::setVolume (int v)
{
    v = jlimit (0, maxVolume(), v);
    auto& e = st.engine();
    if (ch < 0) e.masterVolume = v; else e.mix[ch].volume = v;
    st.setStatus ((ch < 0 ? String ("MASTER") : "CH" + String (ch + 1)) + " VOLUME " + String (v));
    st.changed();
    repaint();
}

juce::Rectangle<int> MixerStrip::meterArea() const
{
    return { ch < 0 ? 10 : 8, 78, ch < 0 ? 30 : 18, jmax (40, getHeight() - (ch < 0 ? 78 + 40 : 78 + 118)) } ;
}

juce::Rectangle<int> MixerStrip::faderArea() const
{
    auto m = meterArea();
    return { m.getRight() + 8, m.getY() + 10, getWidth() - m.getRight() - 14, m.getHeight() - 20 };
}

juce::Rectangle<int> MixerStrip::lampArea() const
{
    return { 6, 52, getWidth() - 12, 20 };
}

void MixerStrip::refresh (float peak)
{
    auto& e = st.engine();
    if (peak > 1.0f) clipped = true;                       // the track went over 0 dBFS: it stays lit until RESET
    const float newLevel = jmax (peak, level * 0.86f);
    if (peak >= hold) { hold = peak; holdTicks = 30; }
    else if (holdTicks > 0) --holdTicks;
    else hold *= 0.9f;

    String n;
    if (ch >= 0)
    {
        const int inst = e.lastInstrumentOnChannel[ch].load();
        n = inst > 0 ? st.proc.getInstrument (inst).name : String();
        pan.setValue (e.mix[ch].pan.load());
        muteBtn.setToggleState (e.mix[ch].mute.load(), dontSendNotification);
        soloBtn.setToggleState (e.mix[ch].solo.load(), dontSendNotification);
    }
    if (std::abs (newLevel - level) > 0.002f || n != name || peak > 1.0f || holdTicks == 29)
    {
        level = newLevel;
        name = n;
        repaint();
    }
    else level = newLevel;
}

void MixerStrip::paint (Graphics& g)
{
    Theme::drawPanel (g, getLocalBounds().toFloat());
    const bool master = ch < 0;
    const bool silenced = ! master && st.engine().isChannelSilenced (ch);
    auto fit = [&] (const String& t, juce::Rectangle<float> r, float h, Colour c, bool bold = false)
    {
        Theme::text (g, t, r, jmin (h, h * (r.getWidth() - 2.0f) / jmax (1.0f, PixelFont::getWidth (t, h))), c, Justification::centred, bold);
    };
    fit (master ? String ("MASTER") : "CH" + String (ch + 1), { 4.0f, 6.0f, (float) getWidth() - 8.0f, 24.0f }, 20.0f,
         silenced ? Theme::panelEdgeLo : Theme::ink, true);
    fit (master ? String ("BEFORE LIMITER") : (name.isEmpty() ? String ("---") : name.upToLastOccurrenceOf (".", false, false)),
         { 4.0f, 30.0f, (float) getWidth() - 8.0f, 16.0f }, 12.0f, Theme::ink.withAlpha (0.7f));

    // CLIP (LIMIT on the master): lit red once the track went over 0 dBFS; click it to clear
    {
        auto r = lampArea().toFloat();
        g.setColour (clipped ? kRed : kOff);
        g.fillRect (r);
        fit (master ? String ("LIMIT") : String ("CLIP"), r, 13.0f, clipped ? Colours::white : Theme::screenDim, true);
    }

    // the meter: green, yellow from -6 dB, red from 0 dB
    {
        auto m = meterArea().toFloat();
        Theme::drawScreen (g, m.expanded (2.0f));
        const float db = toDb (level), top = meterPos (db);
        const int segs = jmax (8, (int) (m.getHeight() / 5.0f));
        const float sh = m.getHeight() / segs;
        for (int i = 0; i < segs; ++i)
        {
            const float pos = (i + 0.5f) / segs;                 // 0 bottom .. 1 top
            const float segDb = kDbMin + pos * (kDbMax - kDbMin);
            const Colour c = segDb >= 0.0f ? kRed : (segDb >= -6.0f ? kYellow : kGreen);
            const bool on = pos <= top && level > 1.0e-5f;
            g.setColour (on ? (silenced ? c.withSaturation (0.0f).darker (0.3f) : c) : c.withAlpha (0.13f));
            g.fillRect (m.getX(), m.getBottom() - (i + 1) * sh + 1.0f, m.getWidth(), sh - 1.5f);
        }
        if (hold > 1.0e-5f)
        {
            const float hy = m.getBottom() - meterPos (toDb (hold)) * m.getHeight();
            g.setColour (hold > 1.0f ? kRed : Colours::white);
            g.fillRect (m.getX(), hy - 1.0f, m.getWidth(), 2.0f);
        }
        // 0 dB mark
        const float zy = m.getBottom() - meterPos (0.0f) * m.getHeight();
        g.setColour (Theme::ink);
        g.fillRect (m.getX() - 5.0f, zy - 0.75f, 4.0f, 1.5f);
    }

    // the fader
    {
        auto f = faderArea().toFloat();
        g.setColour (Theme::panelEdgeLo);
        g.fillRect (f.getCentreX() - 2.0f, f.getY(), 4.0f, f.getHeight());
        g.setColour (Theme::ink.withAlpha (0.35f));
        for (int i = 0; i <= 4; ++i) g.fillRect (f.getX() + 2.0f, f.getY() + f.getHeight() * i / 4.0f - 0.5f, 6.0f, 1.0f);
        const float v = (float) getVolume() / (float) maxVolume();
        const float cy = f.getBottom() - v * f.getHeight();
        auto cap = juce::Rectangle<float> (f.getX(), cy - 9.0f, f.getWidth(), 18.0f);
        Theme::drawRaised (g, cap, Theme::buttonLight);
        g.setColour (Theme::ink);
        g.fillRect (cap.getX() + 3.0f, cap.getCentreY() - 1.0f, cap.getWidth() - 6.0f, 2.0f);
    }

    // the volume as a number, and the peak in dB
    const float ny = (float) meterArea().getBottom() + 6.0f;
    fit ("VOL " + String (getVolume()), { 4.0f, ny, (float) getWidth() - 8.0f, 16.0f }, 13.0f, Theme::ink, true);
    const float db = toDb (hold);
    fit (db < -60.0f ? String ("-INF") : (db > 0.0f ? "+" : "") + String (db, 1) + " DB", { 4.0f, ny + 16.0f, (float) getWidth() - 8.0f, 14.0f }, 11.0f,
         hold > 1.0f ? kRed : Theme::ink.withAlpha (0.65f));
}

void MixerStrip::resized()
{
    if (ch < 0) return;
    const int w = getWidth(), h = getHeight();
    pan.setBounds (4, h - 80, w - 8, 30);
    muteBtn.setBounds (4, h - 44, (w - 12) / 2, 36);
    soloBtn.setBounds (muteBtn.getRight() + 4, h - 44, w - 8 - (w - 12) / 2 - 4, 36);
}

void MixerStrip::setFromY (int y)
{
    auto f = faderArea();
    const float v = 1.0f - (float) (y - f.getY()) / (float) jmax (1, f.getHeight());
    setVolume (roundToInt (jlimit (0.0f, 1.0f, v) * maxVolume()));
}

void MixerStrip::mouseDown (const MouseEvent& e)
{
    if (lampArea().contains (e.getPosition())) { resetClip(); return; }
    if (faderArea().expanded (6, 10).contains (e.getPosition()) || meterArea().contains (e.getPosition())) setFromY (e.y);
}

void MixerStrip::mouseDrag (const MouseEvent& e)
{
    if (faderArea().expanded (40, 400).contains (e.getMouseDownPosition()) && ! lampArea().contains (e.getMouseDownPosition()))
        setFromY (e.y);
}

void MixerStrip::mouseDoubleClick (const MouseEvent& e)
{
    if (faderArea().expanded (6, 10).contains (e.getPosition())) setVolume (ch < 0 ? 80 : 64);
}

void MixerStrip::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    setVolume (getVolume() + (w.deltaY > 0 ? 1 : -1) * (w.isReversed ? -1 : 1));
}

// ===========================================================================
// MixerPanel
// ===========================================================================

MixerPanel::MixerPanel (EditorState& s) : st (s), master (s, -1)
{
    for (int c = 0; c < kNumChannels; ++c)
    {
        strips.push_back (std::make_unique<MixerStrip> (s, c));
        addChildComponent (*strips.back());
    }
    addAndMakeVisible (master);
    addAndMakeVisible (resetBtn);
    resetBtn.setComponentID ("mixReset");
    resetBtn.setWantsKeyboardFocus (false);
    resetBtn.onClick = [this] { resetClips(); };
    setWantsKeyboardFocus (true);
    shownChannels = st.numChannels();
    setSize (preferredWidth(), kHeight);
    startTimerHz (30);
}

int MixerPanel::preferredWidth() const
{
    const int n = jmax (1, shownChannels);
    return juce::jmax (760, 16 + n * (kStripW + kGap) + 10 + kMasterW + 10);
}

void MixerPanel::resetClips()
{
    for (auto& s : strips) s->resetClip();
    master.resetClip();
    st.setStatus ("MIXER: CLIP LAMPS CLEARED");
    st.changed();
}

void MixerPanel::refresh()
{
    auto& e = st.engine();
    if (const int n = st.numChannels(); n != shownChannels)
    {
        shownChannels = n;
        resized();
        repaint();
        if (onWidthChanged) onWidthChanged (preferredWidth());
    }
    for (int c = 0; c < kNumChannels; ++c)
    {
        const float pk = e.mixerPeak[c].exchange (0.0f);
        if (c < shownChannels) strips[(size_t) c]->refresh (pk);
    }
    master.refresh (e.masterPeak.exchange (0.0f));
}

void MixerPanel::paint (Graphics& g)
{
    g.fillAll (Theme::background);
    Theme::text (g, "MIXER", { 16.0f, 8.0f, 200.0f, 38.0f }, 28.0f, Theme::ink, Justification::centredLeft, true);
    bool any = false;
    for (int c = 0; c < shownChannels; ++c) any = any || strips[(size_t) c]->isClipped();
    const String help = any ? String ("A TRACK WENT INTO THE RED: TURN IT DOWN, THEN RESET CLIPS")
                            : String ("RED = OVER 0 DB, THE TRACK IS OVERDRIVEN.  DOUBLE-CLICK A FADER = 64.  SHIFT+M CLOSES");
    const float tw = (float) resetBtn.getX() - 156.0f;
    Theme::text (g, help, { 156.0f, 8.0f, tw - 20.0f, 38.0f }, jmin (14.0f, 14.0f * (tw - 4.0f) / jmax (1.0f, PixelFont::getWidth (help, 14.0f))),
                 any ? Colour (0xffd9342b) : Theme::ink.withAlpha (0.7f), Justification::centredLeft);
}

void MixerPanel::resized()
{
    const int h = getHeight() - kTop - 10;
    for (int c = 0; c < kNumChannels; ++c)
    {
        auto& s = *strips[(size_t) c];
        s.setVisible (c < shownChannels);
        s.setBounds (16 + c * (kStripW + kGap), kTop, kStripW, h);
    }
    master.setBounds (16 + shownChannels * (kStripW + kGap) + 10, kTop, kMasterW, h);
    resetBtn.setBounds (getWidth() - 16 - 150, 10, 150, 34);
}

bool MixerPanel::keyPressed (const KeyPress& k)
{
    const auto m = k.getModifiers();
    if ((k.getKeyCode() == 'M' || k.getKeyCode() == 'm') && m.isShiftDown() && ! m.isCommandDown())
    {
        if (onClose) onClose();
        return true;
    }
    return onKey != nullptr && onKey (k);
}

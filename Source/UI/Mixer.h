// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include "Widgets.h"
#include "EditorState.h"

/** One track in the MIXER: name, CLIP lamp, peak meter, volume fader, pan, MUTE / SOLO.
    The master strip (channel -1) has the MASTER volume and a LIMIT lamp. */
class MixerStrip : public juce::Component
{
public:
    MixerStrip (EditorState& s, int channel);
    void refresh (float peak);          // peak since the last call (1.0 = 0 dBFS)
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isClipped() const noexcept { return clipped; }
    void resetClip() { clipped = false; repaint(); }
    int  getVolume() const;
    void setVolume (int v);
    float getShownLevel() const noexcept { return level; }

    /** dB of a peak, and where it sits on the meter (0 bottom .. 1 top, -48 .. +6 dB). */
    static float toDb (float peak);
    static float meterPos (float db);

private:
    int maxVolume() const { return ch < 0 ? 100 : 64; }
    juce::Rectangle<int> faderArea() const;
    juce::Rectangle<int> meterArea() const;
    juce::Rectangle<int> lampArea() const;
    void setFromY (int y);

    EditorState& st;
    int ch;
    float level = 0.0f, hold = 0.0f;
    int holdTicks = 0;
    bool clipped = false;
    juce::String name;
    ValueBox pan { -64, 64, 0, 16.0f };
    PixelButton muteBtn { "M", PixelButton::Style::Record, 14.0f }, soloBtn { "S", PixelButton::Style::Record, 14.0f };
};

/** The MIXER window's content: a strip for every track in use (4, 8 or 16) and the master.
    Shift+M or the MIX button opens and closes it. */
class MixerPanel : public juce::Component, private juce::Timer
{
public:
    explicit MixerPanel (EditorState& s);
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool down) override { return onKeyState != nullptr && onKeyState (down); }
    void refresh();                       // read the meters (the timer does this 30 times a second)
    int  preferredWidth() const;
    static constexpr int kHeight = 560;

    std::function<bool (const juce::KeyPress&)> onKey;   // keys that aren't the mixer's go to the tracker
    std::function<void()> onClose;
    std::function<bool (bool)> onKeyState;            // key releases (held notes) go to the tracker too
    std::function<void (int)> onWidthChanged;

    MixerStrip* getStrip (int channel) { return juce::isPositiveAndBelow (channel, (int) strips.size()) ? strips[(size_t) channel].get() : nullptr; }
    MixerStrip& getMaster() { return master; }
    void resetClips();

private:
    void timerCallback() override { refresh(); }

    EditorState& st;
    std::vector<std::unique_ptr<MixerStrip>> strips;
    MixerStrip master;
    PixelButton resetBtn { "RESET CLIPS", PixelButton::Style::Raised, 14.0f };
    int shownChannels = -1;
};

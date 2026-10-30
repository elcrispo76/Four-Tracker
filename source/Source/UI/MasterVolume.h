// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include "Widgets.h"

/** "- MASTER 80 +" in the title bar: the volume of all tracks together (0..100), on every page.
    Click - / +, drag the number up or down, use the mouse wheel, or double-click for 80.
    The same value as MASTER on the MIDI / AUDIO page, and it is saved with the module. */
class MasterVolume : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    MasterVolume()
    {
        for (auto* b : { &minus, &plus })
        {
            b->setWantsKeyboardFocus (false);
            b->setMouseClickGrabsKeyboardFocus (false);
            addAndMakeVisible (b);
        }
        minus.onClick = [this] { set (value - 5); };
        plus.onClick  = [this] { set (value + 5); };
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
        setTooltip ("Master volume for all tracks. Drag the number, use the mouse wheel, or double-click for 80.");
        startTimerHz (15);
    }

    /** Reads and writes the engine's master volume. */
    std::function<int()> getValue;
    std::function<void (int)> setValue;
    /** How loud the fade is now (1 = full) and its state (0 none, 1 fading out, 2 faded out, 3 fading in). */
    std::function<float()> getFade;
    std::function<int()> getFadeState;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().withTrimmedLeft ((float) minus.getRight()).withRight ((float) plus.getX());
        const bool fading = fadeState != 0;
        const char* what = fadeState == 1 ? "FADE OUT" : fadeState == 2 ? "FADED OUT" : fadeState == 3 ? "FADE IN" : "MASTER";
        Theme::text (g, what, r.withHeight (r.getHeight() * 0.45f), 13.0f,
                     fading ? juce::Colour (0xffff6b5e) : Theme::screenDim, juce::Justification::centred);
        Theme::text (g, juce::String (fading ? juce::roundToInt (value * fade) : value), r.withTrimmedTop (r.getHeight() * 0.4f), 24.0f,
                     Theme::screenText, juce::Justification::centred, true);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        minus.setBounds (r.removeFromLeft (getHeight()));
        plus.setBounds (r.removeFromRight (getHeight()));
    }

    void mouseDown (const juce::MouseEvent& e) override { dragStartY = e.position.y; dragStartValue = value; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        set (dragStartValue + juce::roundToInt ((dragStartY - e.position.y) * (e.mods.isShiftDown() ? 0.1f : 0.5f)));
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { set (80); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        wheel += w.deltaY * 20.0f;
        const int steps = (int) wheel;
        if (steps != 0) { wheel -= (float) steps; set (value + steps); }
    }

    int getShownValue() const noexcept { return value; }

    void set (int v)
    {
        v = juce::jlimit (0, 100, v);
        if (setValue) setValue (v);
        if (v != value) { value = v; repaint(); }
    }

private:
    void timerCallback() override
    {
        // follow changes made elsewhere (MIDI / AUDIO page, loading a module)
        if (getValue)
        {
            const int v = juce::jlimit (0, 100, getValue());
            if (v != value) { value = v; repaint(); }
        }
        if (getFade)
        {
            const float f = getFade();
            if (std::abs (f - fade) > 0.005f) { fade = f; repaint(); }
        }
        if (getFadeState)
        {
            const int fs = getFadeState();
            if (fs != fadeState) { fadeState = fs; repaint(); }
        }
    }

    PixelButton minus { "-", PixelButton::Style::Raised, 26.0f }, plus { "+", PixelButton::Style::Raised, 26.0f };
    int value = 80;
    float fade = 1.0f;
    int fadeState = 0;
    float dragStartY = 0.0f, wheel = 0.0f;
    int dragStartValue = 80;
};

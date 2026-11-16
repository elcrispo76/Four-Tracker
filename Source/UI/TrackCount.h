// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include "Widgets.h"

/** "- TRACKS 8 +" in the title bar: adds or removes one track (channel) at a time, 1..16. */
class TrackCount : public juce::Component
{
public:
    TrackCount()
    {
        for (auto* b : { &minus, &plus })
        {
            b->setWantsKeyboardFocus (false);
            b->setMouseClickGrabsKeyboardFocus (false);
            addAndMakeVisible (b);
        }
        minus.onClick = [this] { if (onMinus) onMinus(); };
        plus.onClick  = [this] { if (onPlus) onPlus(); };
    }

    void setCount (int n) { if (n != count) { count = n; repaint(); } }
    std::function<void()> onMinus, onPlus;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().withTrimmedLeft ((float) minus.getRight()).withRight ((float) plus.getX());
        Theme::text (g, "TRACKS", r.withHeight (r.getHeight() * 0.45f), 13.0f, Theme::screenDim, juce::Justification::centred);
        Theme::text (g, juce::String (count), r.withTrimmedTop (r.getHeight() * 0.4f), 24.0f, Theme::screenText, juce::Justification::centred, true);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        minus.setBounds (r.removeFromLeft (getHeight()));
        plus.setBounds (r.removeFromRight (getHeight()));
    }

private:
    PixelButton minus { "-", PixelButton::Style::Raised, 26.0f }, plus { "+", PixelButton::Style::Raised, 26.0f };
    int count = 4;
};

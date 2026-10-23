// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include "Widgets.h"

/** Under the pattern when there are more than 4 tracks:
    [<] [TRACKS 1-4] [5-8] [9-12] [13-16] [>]  - click to see those 4 tracks. */
class TrackNav : public juce::Component
{
public:
    TrackNav()
    {
        prev.icon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { Theme::iconArrow (g, r.withSizeKeepingCentre (12.0f, 18.0f), c, false); };
        next.icon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { Theme::iconArrow (g, r.withSizeKeepingCentre (12.0f, 18.0f), c, true); };
        prev.onClick = [this] { if (onPage) onPage (page - 1); };
        next.onClick = [this] { if (onPage) onPage (page + 1); };
        for (int i = 0; i < 4; ++i)
        {
            auto& b = pages[(size_t) i];
            b = std::make_unique<PixelButton> ("", PixelButton::Style::Segment, 16.0f);
            b->onClick = [this, i] { if (onPage) onPage (i); };
            addChildComponent (*b);
        }
        for (auto* b : { &prev, &next }) addAndMakeVisible (b);
        for (auto* b : { &prev, &next }) { b->setWantsKeyboardFocus (false); b->setMouseClickGrabsKeyboardFocus (false); }
        for (auto& b : pages)            { b->setWantsKeyboardFocus (false); b->setMouseClickGrabsKeyboardFocus (false); }
    }

    /** page = which group of 4 is shown, tracks = how many tracks the song has. */
    void setState (int newPage, int tracks)
    {
        page = newPage;
        numPages = (tracks + 3) / 4;
        for (int i = 0; i < 4; ++i)
        {
            auto& b = *pages[(size_t) i];
            b.setVisible (i < numPages);
            const int a = i * 4 + 1, e = juce::jmin (tracks, i * 4 + 4);
            b.setButtonText ((i == 0 ? juce::String ("TRACKS ") : juce::String()) + juce::String (a) + (e > a ? "-" + juce::String (e) : juce::String()));
            b.setToggleState (i == page, juce::dontSendNotification);
        }
        prev.setEnabled (page > 0);
        next.setEnabled (page < numPages - 1);
        prev.setAlpha (page > 0 ? 1.0f : 0.35f);
        next.setAlpha (page < numPages - 1 ? 1.0f : 0.35f);
        resized();
    }

    std::function<void (int page)> onPage;

    void paint (juce::Graphics& g) override { Theme::drawPanel (g, getLocalBounds().toFloat()); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        prev.setBounds (r.removeFromLeft (70));
        next.setBounds (r.removeFromRight (70));
        r.reduce (8, 0);
        const int w = numPages > 0 ? (r.getWidth() - 8 * (numPages - 1)) / numPages : 0;
        for (int i = 0; i < numPages && i < 4; ++i)
            pages[(size_t) i]->setBounds (r.getX() + i * (w + 8), r.getY(), w, r.getHeight());
    }

private:
    PixelButton prev { "", PixelButton::Style::Raised }, next { "", PixelButton::Style::Raised };
    std::array<std::unique_ptr<PixelButton>, 4> pages;
    int page = 0, numPages = 1;
};

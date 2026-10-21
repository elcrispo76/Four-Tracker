// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include "Widgets.h"
#include "EditorState.h"

/** Title strip: "FOUR TRACKER" plus live status (play state, queue, edit mode). */
class TitleBar : public juce::Component
{
public:
    explicit TitleBar (EditorState& s) : st (s) {}
    void paint (juce::Graphics&) override;
    void refresh();
    std::function<void()> onGridClicked;
    void mouseDown (const juce::MouseEvent&) override;

private:
    EditorState& st;
    juce::String lastStatus;
    juce::String buildStatus();
};

/** PATTERN / INSTRUMENT / LOOP / SPEED / BPM / HEX-DEC row. */
class HeaderControls : public juce::Component
{
public:
    explicit HeaderControls (EditorState& s);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();     // pull values from the engine/processor

    std::function<void()> onInstrumentSelected;

private:
    EditorState& st;

    ValueBox patternBox { 0, ft::kMaxPatterns - 1, 0, 30.0f };
    PixelButton patPrev { "PREV" }, patNext { "NEXT" };
    ValueBox instBox { 1, ft::kNumInstruments - 1, 1, 30.0f };
    PixelButton instPrev { "PREV", PixelButton::Style::Raised, 14.0f }, instNext { "NEXT", PixelButton::Style::Raised, 14.0f };
    NameBox instName { 18.0f };
    SegmentPair loop { "ON", "OFF", 18.0f };
    ValueBox speedBox { 1, 31, 6, 30.0f };
    ValueBox bpmBox { 32, 255, 125, 30.0f };
    ValueBox octaveBox { 0, 7, 4, 30.0f };
    PixelButton editBtn { "EDIT", PixelButton::Style::Record, 18.0f };
    PixelButton hexBtn { "HEX", PixelButton::Style::Segment, 17.0f }, decBtn { "DEC", PixelButton::Style::Segment, 17.0f };

    juce::Rectangle<int> patPanel, instPanel, loopPanel, speedPanel, bpmPanel, octavePanel, editPanel, hexPanel;
    void selectInstrument (int i);
};

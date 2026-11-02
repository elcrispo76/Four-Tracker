// SPDX-License-Identifier: AGPL-3.0-or-later
// Breakbeat Processor - Copyright (C) 2026 elcrispo76

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class BreakbeatEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    explicit BreakbeatEditor (BreakbeatProcessor&);
    ~BreakbeatEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // used by the parts
    int  getStepAction (int step) const;
    void setStepAction (int step, int action);
    int  getBrush() const noexcept { return brush; }
    void setBrush (int action);
    void stepPressed (int step, const juce::MouseEvent&);
    void stepDragged (const juce::MouseEvent&);
    void showStepMenu (int step);
    int  getPlayingStep() const noexcept { return playing; }
    /** Rows (16th notes) per step: 1, 2, 4 or 8. */
    int  getRowsPerStep() const;

    struct StepPad;
    struct ToolButton;
    struct Knob;

private:
    void timerCallback() override;
    void randomise();
    void showPatternMenu();

    BreakbeatProcessor& proc;
    juce::OwnedArray<StepPad> pads;
    juce::OwnedArray<ToolButton> tools;
    std::unique_ptr<ToolButton> clearButton, randomButton, patternButton, aheadButton;
    std::unique_ptr<juce::ParameterAttachment> aheadAttachment;
    juce::OwnedArray<Knob> knobs;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> stepAttachments;
    int brush = -1;             // tool picked in the palette, -1 = none
    int playing = -1;
    double shownBpm = -1.0;
    bool shownPlaying = false;
    int shownRowsPerStep = 0;
    bool shownOwn = false;
    juce::Rectangle<int> statusArea;
    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakbeatEditor)
};

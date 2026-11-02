// SPDX-License-Identifier: AGPL-3.0-or-later
// FX Processor 2 - Copyright (C) 2026 elcrispo76

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PixelUI.h"

class FxEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    explicit FxEditor (FxProcessor&);
    ~FxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    struct Module
    {
        juce::String name;
        juce::Rectangle<int> area;
        const char* onId = nullptr;                    // nullptr = always on (the LFO)
        std::unique_ptr<ToolButton> onButton;
        std::unique_ptr<juce::ParameterAttachment> onAttach;
        std::vector<std::vector<Knob*>> rows;
        int light = 0;                                 // 0 none, 1..3 = the LFO of effect 0..2
        Knob* freeKnob = nullptr;                      // FREE HZ: only counts when the speed is FREE
        const char* syncId = nullptr;
    };

    Knob* knob (const char* id, const juce::String& label, std::function<juce::String (int)> fmt, const juce::String& tip);
    void addModule (const juce::String& name, const char* onId, std::vector<std::vector<Knob*>> rows, int light = 0);
    void updateDimming();
    void showPresetMenu();

    FxProcessor& proc;
    juce::OwnedArray<Knob> knobs;
    std::vector<std::unique_ptr<Module>> modules;
    std::unique_ptr<ToolButton> presetButton;
    std::vector<Knob*> masterKnobs;
    juce::Rectangle<int> statusArea, masterArea;
    double shownBpm = -1.0;
    bool shownHost = false;
    std::array<float, 3> shownLfo {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxEditor)
};

// SPDX-License-Identifier: AGPL-3.0-or-later
// Dynamics Processor - Copyright (C) 2026 elcrispo76

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PixelUI.h"

class DynEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    explicit DynEditor (DynProcessor&);
    ~DynEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    struct Module
    {
        juce::String name;
        juce::Rectangle<int> area;
        const char* onId = nullptr;
        std::unique_ptr<ToolButton> onButton;
        std::unique_ptr<juce::ParameterAttachment> onAttach;
        std::vector<std::vector<Knob*>> rows;
    };

    Knob* knob (const char* id, const juce::String& label, std::function<juce::String (int)> fmt, const juce::String& tip);
    Module& addModule (const juce::String& name, const char* onId, std::vector<std::vector<Knob*>> rows);
    void updateDimming();
    void showPresetMenu();
    void paintCurve (juce::Graphics&, juce::Rectangle<float>);
    void paintMeters (juce::Graphics&, juce::Rectangle<float>);
    void paintTransientMeter (juce::Graphics&, juce::Rectangle<float>);
    void updateAutoButton();

    DynProcessor& proc;
    juce::OwnedArray<Knob> knobs;
    std::vector<std::unique_ptr<Module>> modules;      // TRANSIENT, COMPRESSOR, SATURATION
    Knob* outputKnob = nullptr;
    juce::OwnedArray<ToolButton> instButtons;
    std::unique_ptr<ToolButton> autoButton, presetButton;
    std::unique_ptr<juce::ParameterAttachment> instAttach;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> curveAttach;

    juce::Rectangle<int> statusArea, instArea, curveArea, meterArea, masterArea, tsMeterArea;
    juce::String shownMessage;
    bool shownListening = false;
    int shownProgress = -1;
    float meterIn = -90.0f, meterOut = -90.0f, meterGr = 0.0f, dotDb = -120.0f, tsUp = 0.0f, tsDown = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DynEditor)
};

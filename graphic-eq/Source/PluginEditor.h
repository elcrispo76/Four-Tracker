// SPDX-License-Identifier: AGPL-3.0-or-later
// Graphic EQ - Copyright (C) 2026 elcrispo76

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

/** The window: a dB ruler, 10 black bars with a small round knob each (+12 .. -12 dB),
    and a white line through the knobs. A separate OUT bar on the right. */
class EqEditor : public juce::AudioProcessorEditor
{
public:
    explicit EqEditor (EqProcessor&);
    ~EqEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int kOut = eq::kBands;          // the OUT bar is "band" 10

    /** For the tests: where bar b is, and the y of a dB value. */
    float barX (int b) const;
    float yOf (float db) const;

private:
    struct Button { juce::String label; juce::Rectangle<int> r; std::function<void()> click; };

    float valueOf (int b) const;
    void set (int b, float db);
    int barAt (float x) const;
    float dbAt (float y) const;
    void showPresetMenu();

    EqProcessor& proc;
    std::array<std::unique_ptr<juce::ParameterAttachment>, eq::kBands + 1> attach;
    std::vector<Button> buttons;
    int pressedButton = -1;
    int hover = -1, dragging = -1;
    std::vector<int> touched;          // bars in the current drag (one gesture each)
    bool fine = false;
    float fineStartY = 0.0f, fineStartDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqEditor)
};

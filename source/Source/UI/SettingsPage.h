// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Pages.h"

/** The sixth page: MIDI input mapping and audio device settings (ASIO etc.). */
class SettingsPage : public juce::Component, public Refreshable
{
public:
    explicit SettingsPage (EditorState& s);
    ~SettingsPage() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override;

private:
    void updateModeButtons();

    EditorState& st;
    juce::Rectangle<int> midiPanel, audioPanel, mapBox;

    ValueBox baseNote { 0, 127 - ft::kMaxPatterns, 36, 24.0f };
    PixelButton learn { "LEARN", PixelButton::Style::Segment, 17.0f };
    ValueBox channel { 0, 16, 0, 24.0f };
    std::array<std::unique_ptr<PixelButton>, 4> modeButtons;
    SegmentPair midiStartStop { "ON", "OFF", 17.0f };
    ValueBox master { 0, 100, 80, 24.0f };
    SegmentPair followHost { "ON", "OFF", 17.0f };

    std::unique_ptr<juce::LookAndFeel_V4> deviceLnf;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;
    bool isStandalone = false;
    int lastNote = -2, lastBase = -1;
    bool lastLearn = false;
};

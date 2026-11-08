// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker Pattern Maker - Copyright (C) 2026 elcrispo76

#pragma once

#include "PatternGen.h"
#include "PluginProcessor.h"
#include "UI/Widgets.h"

/** Where the Four Tracker Sample Pack is, and which of its sounds each lane starts with. */
struct SoundLibrary
{
    juce::File pack;                                    // .../Four Tracker Sample Pack (may not exist)
    static juce::File findPack();
    bool isValid() const { return pack.isDirectory(); }
    /** The default sound for a lane in a style (a file in the pack), or an empty File. */
    juce::File defaultSound (pm::Style style, pm::LaneKind lane) const;
    /** The pack's folders that suit a lane (for the sound menu). */
    juce::Array<juce::File> foldersFor (pm::LaneKind lane) const;
};

/** One row of the grid: a lane's 16 steps of the bar in view. */
class StepGrid : public juce::Component
{
public:
    StepGrid (std::function<pm::Pattern&()> getPattern, int laneIndex);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    int bar = 0;                     // which bar is shown
    int playStep = -1;               // the step that plays now (-1: stopped)
    std::function<void()> onEdited;

private:
    int stepAt (int x) const;
    std::function<pm::Pattern&()> pattern;
    int lane;
};

/** The whole Pattern Maker: style, tempo, key, eight lanes, PLAY and SAVE. It owns no audio
    device; the processor it plays through is given to it (the app plays it, the test renders it). */
class MakerComponent : public juce::Component, private juce::Timer
{
public:
    MakerComponent (FourTrackerProcessor& processor, SoundLibrary library);
    ~MakerComponent() override;
    static constexpr int kWidth = 1400, kHeight = 900;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    void generateAll();                     // GENERATE: a new pattern for the settings
    void setStyle (pm::Style s, bool newTempo);
    void togglePlay();
    /** Writes the pattern into the processor (pattern 00) and loads the lanes' sounds. */
    void apply();
    /** SAVE: as a .ftpat for Four Tracker's LOAD PATTERN. Returns an error message or empty. */
    juce::String saveTo (const juce::File& file);
    juce::File suggestedFile() const;

    pm::Pattern& getPattern() { return pattern; }
    std::function<void()> onAudioSettings;  // the app opens its audio device settings

private:
    void timerCallback() override;
    void chooseSound (int lane);
    void refreshControls();
    void regenerateMelodic();               // new notes in the new scale (bass and arp)
    void setStatus (const juce::String& s) { status = s; repaint (statusArea); }

    FourTrackerProcessor& proc;
    SoundLibrary lib;
    pm::Pattern pattern;
    std::array<ft::Instrument, 10> builtIn;           // Four Tracker's own demo sounds, if the pack isn't there
    std::array<juce::String, pm::kNumLanes> loadedSound;
    int viewBar = 0;
    bool followPlay = true;
    bool volumesSet = false;                 // the bar in view follows the play position
    juce::String status;
    juce::Rectangle<int> topPanel, lanesPanel, statusArea;

    PixelButton stylePrev { "" }, styleNext { "" };
    PixelLabel styleLabel { "", 24.0f, juce::Justification::centred };
    ValueBox bpmBox { 60, 255, 124, 26.0f }, swingBox { 0, 3, 0, 26.0f }, keyBox { 0, 11, 0, 26.0f }, busyBox { 0, 100, 50, 26.0f };
    PixelButton scaleBtn { "MINOR", PixelButton::Style::Raised, 16.0f };
    std::array<std::unique_ptr<PixelButton>, 3> barsBtns;
    PixelButton fillBtn { "FILL", PixelButton::Style::Record, 17.0f };
    PixelButton generateBtn { "GENERATE", PixelButton::Style::Dark, 22.0f };
    PixelButton playBtn { "PLAY", PixelButton::Style::Record, 22.0f };
    PixelButton saveBtn { "SAVE PATTERN", PixelButton::Style::Raised, 20.0f };
    PixelButton audioBtn { "AUDIO", PixelButton::Style::Raised, 16.0f };
    std::array<std::unique_ptr<PixelButton>, 4> barTabs;
    PixelButton copyBarBtn { "BAR 1 TO ALL", PixelButton::Style::Raised, 14.0f };

    struct LaneRow
    {
        std::unique_ptr<PixelButton> onBtn, soundBtn, diceBtn;
        std::unique_ptr<ValueBox> vol;
        std::unique_ptr<StepGrid> grid;
    };
    std::array<LaneRow, pm::kNumLanes> rows;
    std::unique_ptr<juce::FileChooser> chooser;
};

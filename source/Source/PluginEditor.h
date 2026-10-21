// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <set>
#include "PluginProcessor.h"
#include "UI/EditorState.h"
#include "UI/HeaderBar.h"
#include "UI/Pages.h"
#include "UI/SettingsPage.h"
#include "UI/SampleBrowser.h"

/** Everything is laid out in the 1536 x 1024 coordinates of the mockups
    and scaled to the window size. */
class DesignRoot : public juce::Component
{
public:
    void paint (juce::Graphics& g) override { g.fillAll (Theme::background); }
};

class FourTrackerEditor : public juce::AudioProcessorEditor,
                          public juce::FileDragAndDropTarget,
                          private juce::Timer,
                          private juce::ChangeListener
{
public:
    explicit FourTrackerEditor (FourTrackerProcessor&);
    ~FourTrackerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    static constexpr int kDesignW = 1536, kDesignH = 1024;

private:
    enum class Page { Tracks, Instruments, SampleEdit, Song, Block, Settings };

    void showPage (Page p);
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshAll();

    FourTrackerProcessor& proc;
    EditorState state;
    DesignRoot root;

    TitleBar title;
    HeaderControls header;
    PatternView patternView;
    ChannelStripsPanel strips;
    SongPanel songPanel;
    BlockPanel blockPanel;
    InstrumentsPage instrumentsPage;
    SampleEditPage sampleEditPage;
    SettingsPage settingsPage;
    SampleBrowser sampleBrowser;

    PixelButton tabInstruments { "INSTRUMENTS", PixelButton::Style::Tab, 17.0f },
                tabSample      { "SAMPLE EDIT", PixelButton::Style::Tab, 17.0f },
                tabSong        { "SONG / PATTERN", PixelButton::Style::Tab, 17.0f },
                tabBlock       { "BLOCK", PixelButton::Style::Tab, 17.0f },
                tabSettings    { "MIDI / AUDIO", PixelButton::Style::Tab, 17.0f },
                tabTracks      { "", PixelButton::Style::Tab, 17.0f };

    Page page = Page::Tracks;
    bool jamNoteDown = false;
    int  jamInstrument = 0;
    std::set<int> heldNoteKeys;   // note keys currently held, so key auto-repeat doesn't retrigger

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FourTrackerEditor)
};

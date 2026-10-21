// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include "Widgets.h"
#include "WaveformView.h"
#include "EditorState.h"

/** Grey/black colours and the pixel font for the file list. */
struct BrowserLookAndFeel : public juce::LookAndFeel_V4
{
    BrowserLookAndFeel();
    void drawFileBrowserRow (juce::Graphics&, int width, int height, const juce::File&, const juce::String& filename,
                             juce::Image* icon, const juce::String& fileSizeDescription, const juce::String& fileTimeDescription,
                             bool isDirectory, bool isItemSelected, int itemIndex, juce::DirectoryContentsDisplayComponent&) override;
};

/** In-app sample browser (LOAD on the Instruments / Sample Edit pages).
    Clicking or arrowing onto a file plays it straight away, like the sample
    browsers in MadTracker and most DAWs, without loading it into a slot.
    Enter / double-click / LOAD puts the selected file(s) into the slots. */
class SampleBrowser : public juce::Component,
                      private juce::FileBrowserListener,
                      private juce::Timer
{
public:
    explicit SampleBrowser (EditorState&);
    ~SampleBrowser() override;

    void open();
    void close();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Folder the last sample was loaded from. Kept while the program runs,
        empty when it starts (the browser then opens in Documents/Four Tracker). */
    static juce::File& lastSampleFolder();

    std::function<void()> onClosed;

private:
    // FileBrowserListener
    void selectionChanged() override;
    void fileClicked (const juce::File&, const juce::MouseEvent&) override;
    void fileDoubleClicked (const juce::File&) override;
    void browserRootChanged (const juce::File&) override {}

    void timerCallback() override;

    void showFile (const juce::File&, bool play);
    void loadSelected();
    void focusList();

    EditorState& st;
    BrowserLookAndFeel lnf;
    juce::WildcardFileFilter filter;
    std::unique_ptr<juce::FileBrowserComponent> browser;
    juce::Component browserHolder;      // browser is drawn enlarged inside this

    WaveformView wave;
    juce::File current;
    juce::StringArray info;
    bool autoPlay = true;

    SegmentPair autoPair { "ON", "OFF", 18.0f };
    PixelButton play { "PLAY", PixelButton::Style::Raised, 19.0f },
                stop { "STOP", PixelButton::Style::Raised, 19.0f },
                load { "LOAD", PixelButton::Style::Raised, 22.0f },
                cancel { "CANCEL", PixelButton::Style::Raised, 22.0f };

    juce::Rectangle<float> listFrame, waveFrame, infoFrame;
    static constexpr float kBrowserScale = 1.6f;
};

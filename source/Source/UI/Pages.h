// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include "Widgets.h"
#include "WaveformView.h"
#include "PatternView.h"
#include "EditorState.h"

/** Interface for the bits of the editor that need refreshing on a timer. */
struct Refreshable
{
    virtual ~Refreshable() = default;
    virtual void refresh() = 0;
};

// ===========================================================================
// Track view top: four channel strips
// ===========================================================================

class ChannelStrip : public juce::Component, public Refreshable
{
public:
    ChannelStrip (EditorState& s, int channel);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh() override;

private:
    EditorState& st;
    int ch;
    WaveformView wave { false };
    ValueBox vol { 0, 64, 64, 24.0f }, pan { -64, 64, 0, 24.0f }, pitch { -24, 24, 0, 24.0f };
    int shownInst = -1;
    const void* shownData = nullptr;
    float level = 0.0f;
    bool muted = false;
    juce::String name;
};

class ChannelStripsPanel : public juce::Component, public Refreshable
{
public:
    explicit ChannelStripsPanel (EditorState& s);
    void resized() override;
    void refresh() override;
private:
    std::vector<std::unique_ptr<ChannelStrip>> strips;
};

// ===========================================================================
// Song / Pattern top panel
// ===========================================================================

class SongPanel : public juce::Component, public Refreshable
{
public:
    explicit SongPanel (EditorState& s);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override;
    void clearEverything (bool loadDemo);   // NEW MODULE, after the user chose Empty or Demo song

private:
    EditorState& st;
    PixelButton start { "START", PixelButton::Style::Raised, 16.0f }, stop { "STOP", PixelButton::Style::Raised, 16.0f },
                newSet { "NEW", PixelButton::Style::Raised, 16.0f },
                load { "LOAD", PixelButton::Style::Raised, 16.0f }, save { "SAVE", PixelButton::Style::Raised, 16.0f };
    ValueBox length { 1, ft::kMaxRows, ft::kDefaultRows, 32.0f };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastDir { FourTrackerProcessor::getUserFolder() };
    juce::File lastModule;
};

// ===========================================================================
// Block top panel
// ===========================================================================

class BlockPanel : public juce::Component
{
public:
    BlockPanel (EditorState& s, PatternView& pv);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    EditorState& st;
    PixelButton copyTrack { "COPY" }, cutTrack { "CUT" }, pasteTrack { "PASTE" },
                copyBlock { "COPY" }, cutBlock { "CUT" }, pasteBlock { "PASTE" };
    juce::Rectangle<int> trackArea, blockArea;
};

// ===========================================================================
// Instrument list (left side of Instruments and Sample Edit)
// ===========================================================================

class InstrumentList : public juce::Component
{
public:
    explicit InstrumentList (EditorState& s);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    std::function<void()> onSelectionChanged;

private:
    EditorState& st;
    PixelButton load { "LOAD", PixelButton::Style::Raised, 19.0f }, discard { "DISCARD", PixelButton::Style::Raised, 19.0f };
    juce::Rectangle<float> listArea;
    int rowAt (float y) const;
};

// ===========================================================================
// Instruments page
// ===========================================================================

class InstrumentsPage : public juce::Component, public Refreshable
{
public:
    explicit InstrumentsPage (EditorState& s);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh() override;
    void instrumentsChanged();

private:
    void push();   // UI -> instrument
    int slot() const { return st.proc.selectedInstrument; }

    EditorState& st;
    InstrumentList list;
    PixelLabel numberLabel { "01", 26.0f, juce::Justification::centred };
    NameBox name { 20.0f };
    PixelButton prev { "", PixelButton::Style::Raised }, next { "", PixelButton::Style::Raised };
    ValueBox volume { 0, 64, 64, 26.0f }, transpose { -48, 48, 0, 26.0f }, finetune { -128, 127, 0, 26.0f };
    SegmentPair loop, pingPong, reverse;
    WaveformView wave { false };
    juce::Rectangle<int> paramsPanel, togglesPanel, infoPanel, dropBar, slotPanel, slotGrid, header;

    // built-in synth
    SegmentPair source { "SAMPLE", "SYNTH", 17.0f };
    std::array<std::unique_ptr<PixelButton>, 5> waveButtons;
    PixelSlider attack { 0, 5000, 2, 2 }, decay { 0, 5000, 250, 250 }, sustain { 0, 64, 40, 40 }, release { 0, 5000, 200, 200 },
                cutoff { 0, 127, 127, 127 }, resonance { 0, 127, 0, 0 };
    bool synthMode = false;
    void setSource (bool synth);
    void pushSynth();
    void updateSynthVisibility();
};

// ===========================================================================
// Sample edit page
// ===========================================================================

class SampleEditPage : public juce::Component, public Refreshable
{
public:
    explicit SampleEditPage (EditorState& s);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override;
    void instrumentsChanged();

private:
    void pushPoints (const ft::InstrumentParams& p);
    void edit (std::function<bool (ft::Instrument&, int from, int to)> op, bool needsSelection, const juce::String& what);
    int slot() const { return st.proc.selectedInstrument; }

    EditorState& st;
    InstrumentList list;
    PixelLabel numberLabel { "01", 26.0f, juce::Justification::centred };
    NameBox name { 20.0f };
    PixelButton prev { "", PixelButton::Style::Raised }, next { "", PixelButton::Style::Raised };
    PixelLabel lengthVal { "", 22.0f, juce::Justification::centred }, rateVal { "", 22.0f, juce::Justification::centred },
               bitsVal { "", 22.0f, juce::Justification::centred }, chansVal { "", 22.0f, juce::Justification::centred };
    ValueBox curPos { 0, 0, 0, 19.0f };
    WaveformView wave { true };
    ValueBox startBox { 0, 0, 0, 19.0f }, endBox { 0, 0, 0, 19.0f }, loopStartBox { 0, 0, 0, 19.0f }, loopEndBox { 0, 0, 0, 19.0f };
    PixelButton setStart { "SET START", PixelButton::Style::Segment, 17.0f }, setEnd { "SET END", PixelButton::Style::Segment, 17.0f },
                setLoop { "SET LOOP", PixelButton::Style::Segment, 17.0f }, playLoop { "PLAY LOOP", PixelButton::Style::Dark, 17.0f };
    SegmentPair loopMode { "FORWARD", "REVERSE", 17.0f };
    PixelButton cut { "CUT" }, copy { "COPY" }, paste { "PASTE" }, fadeIn { "FADE IN" }, fadeOut { "FADE OUT" },
                normalize { "NORMALIZE" }, reverseBtn { "REVERSE" };
    PixelButton zoomIn { "" }, zoomOut { "" }, fit { "FIT", PixelButton::Style::Raised, 18.0f }, left { "" }, right { "" };

    juce::AudioBuffer<float> clip;
    juce::Rectangle<int> header, infoRow, pointsRow, buttonsRow, editPanel, zoomPanel;
    std::vector<std::pair<PixelButton*, juce::String>> captions;
    bool loopPreviewing = false;
};

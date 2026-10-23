// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include "Widgets.h"
#include "EditorState.h"
#include "SampleBrowser.h"

#if FOURTRACKER_FX_HOST
#include "../FxHost.h"

/** VST EFFECTS page (FX build, standalone): the plugin folders and the scan,
    and two effect slots per channel picked from drop-down lists. */
namespace fxui
{
    /** Fills an effect drop-down: "--- EMPTY ---" and every scanned effect, with the slot's effect selected. */
    void fillBox (juce::ComboBox&, FxHost&, const juce::Array<juce::PluginDescription>& effects, int channel, int slot, bool showFormat);
    /** Loads / clears the slot for the chosen drop-down item. Returns a status message. */
    juce::String applyChoice (FxHost&, const juce::Array<juce::PluginDescription>& effects, int channel, int slot, int itemId);
}

class FxPage : public juce::Component, private FxHost::Listener
{
public:
    FxPage (EditorState&, FxHost&);
    ~FxPage() override;

    void open();
    void close();
    std::function<void()> onClosed;
    void setView (int firstChannel, int usedChannels);   // shows the 4 channels in view

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct FxLookAndFeel : public BrowserLookAndFeel
    {
        juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (22.0f)); }
        juce::Font getPopupMenuFont() override { return juce::Font (juce::FontOptions (20.0f)); }
    };

    struct SlotUI
    {
        juce::ComboBox box;
        PixelButton edit { "EDIT", PixelButton::Style::Raised, 17.0f };
        PixelButton bypass { "BYPASS", PixelButton::Style::Segment, 15.0f };
    };

    void refillLists();
    void updateButtons();
    void chosen (int channel, int slot);
    void fxChanged() override;

    EditorState& st;
    FxHost& fx;
    FxLookAndFeel lnf;
    juce::Array<juce::PluginDescription> effects;
    std::array<std::array<std::unique_ptr<SlotUI>, FxHost::kSlotsPerChannel>, ft::kNumChannels> ui;

    PixelButton addFolder { "ADD FOLDER", PixelButton::Style::Raised, 18.0f },
                removeFolder { "REMOVE LAST", PixelButton::Style::Raised, 18.0f },
                scan { "SCAN", PixelButton::Style::Raised, 22.0f },
                closeBtn { "CLOSE", PixelButton::Style::Raised, 22.0f };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> folderBox, statusBox, rowsArea;
    juce::String message;
    bool refilling = false;
    int firstCh = 0, usedCh = ft::kVisibleChannels;
};

/** Two small effect drop-downs (+ E to open the effect's window) under each
    channel strip on the tracks page. */
class FxBar : public juce::Component, private FxHost::Listener
{
public:
    explicit FxBar (FxHost&);
    ~FxBar() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    /** Called after a choice, so Enter etc. go back to the tracker (the editor grabs the keyboard). */
    std::function<void()> onDone;
    void setView (int firstChannel, int usedChannels);   // follows the 4 channels in view

private:
    struct SmallLookAndFeel : public BrowserLookAndFeel
    {
        juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (17.0f)); }
        juce::Font getPopupMenuFont() override { return juce::Font (juce::FontOptions (20.0f)); }
    };
    void fxChanged() override;
    void refill();

    FxHost& fx;
    SmallLookAndFeel lnf;
    juce::Array<juce::PluginDescription> effects;
    std::array<std::array<std::unique_ptr<juce::ComboBox>, FxHost::kSlotsPerChannel>, ft::kNumChannels> boxes;
    std::array<std::array<std::unique_ptr<PixelButton>, FxHost::kSlotsPerChannel>, ft::kNumChannels> edits, bypasses;
    bool refilling = false;
    int firstCh = 0, usedCh = ft::kVisibleChannels;
    void done() { if (onDone) onDone(); }
};

/** Under the pattern on the tracks page: one drop-down per channel, where the channel's
    sound goes after its own effects - straight OUT, or on INTO another channel's effects
    (chaining, so one effect can be spread over several tracks). */
class RouteBar : public juce::Component, private FxHost::Listener
{
public:
    explicit RouteBar (FxHost&);
    ~RouteBar() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onDone;                          // after a choice: the tracker takes the keyboard back
    void setView (int firstChannel, int usedChannels);     // follows the 4 channels in view
    /** Column x positions inside this component, the same as the pattern's 4 columns. */
    std::function<juce::Rectangle<int> (int visibleIndex)> columnBounds;

private:
    struct SmallLookAndFeel : public BrowserLookAndFeel
    {
        juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (17.0f)); }
        juce::Font getPopupMenuFont() override { return juce::Font (juce::FontOptions (20.0f)); }
    };
    void fxChanged() override { if (! fx.isScanning()) refill(); }
    void refill();

    FxHost& fx;
    SmallLookAndFeel lnf;
    std::array<std::unique_ptr<juce::ComboBox>, ft::kNumChannels> boxes;
    bool refilling = false;
    int firstCh = 0, usedCh = ft::kVisibleChannels;
};

#endif

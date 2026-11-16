// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker Pattern Manager - Copyright (C) 2026 elcrispo76
//
// A program of its own that plays a whole song in Four Tracker: a list of patterns, sent as
// Four Tracker's pattern keys over a virtual MIDI cable (LoopBe1 or loopMIDI) - no DAW and
// no MIDI keyboard needed.

#include "ManagerComponent.h"

namespace
{
    class ScaledHolder : public juce::Component
    {
    public:
        explicit ScaledHolder (ManagerComponent& m) : comp (m) { addAndMakeVisible (comp); }
        void resized() override
        {
            const float s = juce::jmin (getWidth() / (float) ManagerComponent::kWidth, getHeight() / (float) ManagerComponent::kHeight);
            comp.setTransform (juce::AffineTransform::scale (s).translated ((getWidth() - ManagerComponent::kWidth * s) * 0.5f,
                                                                              (getHeight() - ManagerComponent::kHeight * s) * 0.5f));
        }
        void paint (juce::Graphics& g) override { g.fillAll (Theme::background); }
    private:
        ManagerComponent& comp;
    };

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow() : DocumentWindow ("Four Tracker Pattern Manager", Theme::background, DocumentWindow::allButtons)
        {
            holder = std::make_unique<ScaledHolder> (comp);
            setUsingNativeTitleBar (true);
            setContentNonOwned (holder.get(), false);
            setResizable (true, true);
            setResizeLimits (550, 380, 2200, 1520);
            const auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
            const float s = juce::jmin (1.0f, (area.getWidth() - 40) / (float) ManagerComponent::kWidth, (area.getHeight() - 60) / (float) ManagerComponent::kHeight);
            centreWithSize ((int) (ManagerComponent::kWidth * s), (int) (ManagerComponent::kHeight * s));
            setVisible (true);
            comp.grabKeyboardFocus();
        }
        ~MainWindow() override { setContentNonOwned (nullptr, false); }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    private:
        ManagerComponent comp;
        std::unique_ptr<ScaledHolder> holder;
    };
}

class PatternManagerApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Four Tracker Pattern Manager"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }
    void initialise (const juce::String&) override      { window = std::make_unique<MainWindow>(); }
    void shutdown() override                            { window.reset(); }
    void systemRequestedQuit() override                 { quit(); }
private:
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (PatternManagerApp)

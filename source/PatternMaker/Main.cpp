// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker Pattern Maker - Copyright (C) 2026 elcrispo76
//
// A program of its own that makes beats, basslines and arps for Four Tracker and saves them
// as .ftpat files (with their sounds), for SONG / PATTERN > LOAD PATTERN in Four Tracker.

#include <juce_audio_utils/juce_audio_utils.h>
#include "MakerComponent.h"

namespace
{
    /** Keeps the 1400 x 900 design and scales it to the window. */
    class ScaledHolder : public juce::Component
    {
    public:
        explicit ScaledHolder (MakerComponent& m) : maker (m) { addAndMakeVisible (maker); }
        void resized() override
        {
            const float s = juce::jmin (getWidth() / (float) MakerComponent::kWidth, getHeight() / (float) MakerComponent::kHeight);
            maker.setTransform (juce::AffineTransform::scale (s).translated ((getWidth() - MakerComponent::kWidth * s) * 0.5f,
                                                                               (getHeight() - MakerComponent::kHeight * s) * 0.5f));
        }
        void paint (juce::Graphics& g) override { g.fillAll (Theme::background); }
    private:
        MakerComponent& maker;
    };

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow() : DocumentWindow ("Four Tracker Pattern Maker", Theme::background, DocumentWindow::allButtons)
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
            processor = std::make_unique<FourTrackerProcessor>();
            devices.initialiseWithDefaultDevices (0, 2);
            player.setProcessor (processor.get());
            devices.addAudioCallback (&player);

            SoundLibrary lib;
            lib.pack = SoundLibrary::findPack();
            maker = std::make_unique<MakerComponent> (*processor, lib);
            maker->onAudioSettings = [this] { showAudioSettings(); };
            holder = std::make_unique<ScaledHolder> (*maker);

            setUsingNativeTitleBar (true);
            setContentNonOwned (holder.get(), false);
            setResizable (true, true);
            setResizeLimits (700, 450, 2800, 1800);
            const auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
            const float s = juce::jmin (1.0f, (area.getWidth() - 40) / (float) MakerComponent::kWidth, (area.getHeight() - 60) / (float) MakerComponent::kHeight);
            centreWithSize ((int) (MakerComponent::kWidth * s), (int) (MakerComponent::kHeight * s));
            setVisible (true);
            maker->grabKeyboardFocus();
        }

        ~MainWindow() override
        {
            devices.removeAudioCallback (&player);
            player.setProcessor (nullptr);
            setContentNonOwned (nullptr, false);
            holder.reset();
            maker.reset();
            processor.reset();
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        void showAudioSettings()
        {
            auto* sel = new juce::AudioDeviceSelectorComponent (devices, 0, 0, 2, 2, false, false, true, false);
            sel->setSize (560, 420);
            juce::DialogWindow::LaunchOptions o;
            o.content.setOwned (sel);
            o.dialogTitle = "Audio";
            o.dialogBackgroundColour = Theme::panel;
            o.escapeKeyTriggersCloseButton = true;
            o.useNativeTitleBar = true;
            o.resizable = false;
            o.launchAsync();
        }

        juce::AudioDeviceManager devices;
        juce::AudioProcessorPlayer player;
        std::unique_ptr<FourTrackerProcessor> processor;
        std::unique_ptr<MakerComponent> maker;
        std::unique_ptr<ScaledHolder> holder;
    };
}

class PatternMakerApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Four Tracker Pattern Maker"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }
    void initialise (const juce::String&) override      { window = std::make_unique<MainWindow>(); }
    void shutdown() override                            { window.reset(); }
    void systemRequestedQuit() override                 { quit(); }
private:
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (PatternMakerApp)

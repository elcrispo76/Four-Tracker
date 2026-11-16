// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

// The program (standalone app). It is JUCE's own standalone app, with one addition: closing the window
// (or Alt+F4 / Cmd+Q) asks first when the song has changes that aren't saved, or a REC take isn't saved.
// Built with JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1, so JUCE uses this class instead of its own.

#include <juce_core/system/juce_TargetPlatform.h>

#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "PluginProcessor.h"

namespace
{
using namespace juce;

class FourTrackerWindow final : public StandaloneFilterWindow
{
public:
    using StandaloneFilterWindow::StandaloneFilterWindow;

    void closeButtonPressed() override { askThenQuit(); }

    /** Quits at once when there is nothing to lose, otherwise asks: SAVE / DON'T SAVE / CANCEL. */
    void askThenQuit()
    {
        auto* proc = dynamic_cast<FourTrackerProcessor*> (getPluginHolder()->processor.get());
        if (asking) return;
        const bool song = proc != nullptr && proc->hasUnsavedChanges();
        const bool take = proc != nullptr && (proc->getRecorder().hasTake() || proc->getRecorder().isRecording());
        if (! song && ! take) { quitNow(); return; }

        String msg;
        if (song) msg << "Are you sure you don't want to save your progress?\n"
                         "The song has changes that aren't saved (patterns, instruments or samples).";
        if (take) msg << (song ? "\n\n" : "") << "A REC recording isn't saved. It will be kept as \"Unsaved jam\" in "
                                                 + String ("Documents/Four Tracker/Recordings.").replaceCharacter ('/', File::getSeparatorChar());
        asking = true;
        const auto opts = song ? MessageBoxOptions::makeOptionsYesNoCancel (MessageBoxIconType::WarningIcon, "Close Four Tracker", msg,
                                                                            "Save...", "Don't save", "Cancel", this)
                               : MessageBoxOptions::makeOptionsOkCancel (MessageBoxIconType::WarningIcon, "Close Four Tracker", msg,
                                                                         "Close", "Cancel", this);
        SafePointer<FourTrackerWindow> safe (this);
        AlertWindow::showAsync (opts, [safe, song] (int result)
        {
            if (safe == nullptr) return;
            safe->asking = false;
            if (! song) { if (result == 1) safe->quitNow(); return; }
            // Yes/No/Cancel: 1 = Save..., 2 = Don't save, 0 = Cancel
            if (result == 2) safe->quitNow();
            if (result == 1) safe->saveThenQuit();
        });
    }

private:
    void quitNow()
    {
        if (auto* proc = dynamic_cast<FourTrackerProcessor*> (getPluginHolder()->processor.get()))
            if (proc->getRecorder().isRecording()) proc->getRecorder().stop();
        getPluginHolder()->savePluginState();
        JUCEApplicationBase::quit();
    }

    void saveThenQuit()
    {
        auto* proc = dynamic_cast<FourTrackerProcessor*> (getPluginHolder()->processor.get());
        if (proc == nullptr) { quitNow(); return; }
        auto start = proc->currentModuleFile.existsAsFile() ? proc->currentModuleFile
                                                             : FourTrackerProcessor::getUserFolder().getChildFile ("my song.ftm");
        chooser = std::make_unique<FileChooser> ("Save the module before closing", start, "*.ftm");
        SafePointer<FourTrackerWindow> safe (this);
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
            [safe] (const FileChooser& fc)
            {
                if (safe == nullptr) return;
                auto f = fc.getResult();
                if (f == File()) return;                         // cancelled: stay open
                if (! f.hasFileExtension ("ftm")) f = f.withFileExtension ("ftm");
                auto* p = dynamic_cast<FourTrackerProcessor*> (safe->getPluginHolder()->processor.get());
                if (p != nullptr && p->saveModule (f)) { safe->quitNow(); return; }
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Close Four Tracker",
                                                  "Could not save " + f.getFullPathName() + ". The program stays open.");
            });
    }

    bool asking = false;
    std::unique_ptr<FileChooser> chooser;
};

class FourTrackerApp final : public JUCEApplication
{
public:
    FourTrackerApp()
    {
        PropertiesFile::Options options;
        options.applicationName     = CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName          = "~/.config";
       #else
        options.folderName          = "";
       #endif
        appProperties.setStorageParameters (options);
    }

    const String getApplicationName() override              { return CharPointer_UTF8 (JucePlugin_Name); }
    const String getApplicationVersion() override           { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override              { return true; }
    void anotherInstanceStarted (const String&) override    {}

    void initialise (const String&) override
    {
        if (Desktop::getInstance().getDisplays().displays.isEmpty()) { quit(); return; }
       #ifdef JucePlugin_PreferredChannelConfigurations
        constexpr StandalonePluginHolder::PluginInOuts channels[] { JucePlugin_PreferredChannelConfigurations };
        const Array<StandalonePluginHolder::PluginInOuts> channelConfig (channels, juce::numElementsInArray (channels));
       #else
        const Array<StandalonePluginHolder::PluginInOuts> channelConfig;
       #endif
        auto holder = std::make_unique<StandalonePluginHolder> (appProperties.getUserSettings(), false, String{}, nullptr, channelConfig, false);
        mainWindow = std::make_unique<FourTrackerWindow> (getApplicationName(),
                                                         LookAndFeel::getDefaultLookAndFeel().findColour (ResizableWindow::backgroundColourId),
                                                         std::move (holder));
        mainWindow->setVisible (true);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        // Alt+F4, Cmd+Q, logging off ...: ask the same way as the close button
        if (mainWindow != nullptr) mainWindow->askThenQuit();
        else quit();
    }

private:
    ApplicationProperties appProperties;
    std::unique_ptr<FourTrackerWindow> mainWindow;
};
} // namespace

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new FourTrackerApp(); }

#endif

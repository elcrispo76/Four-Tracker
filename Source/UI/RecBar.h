// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include "Widgets.h"
#include "../PluginProcessor.h"

/** REC and STOP in the title bar: records the jam (everything you hear) to a file.
    REC starts (asking first if the last take isn't saved). STOP stops and offers to save the take as
    MP3, FLAC or WAV, to listen to it, or to throw it away and record again. While a take waits, the STOP
    button says SAVE and opens the same menu. */
class RecBar : public juce::Component, private juce::Timer
{
public:
    explicit RecBar (FourTrackerProcessor& p) : proc (p)
    {
        for (auto* b : { &rec, &stop })
        {
            b->setWantsKeyboardFocus (false);
            b->setMouseClickGrabsKeyboardFocus (false);
            addAndMakeVisible (b);
        }
        rec.setTooltip ("REC (F3): records everything you hear while you jam. STOP ends it, then save it as MP3, FLAC or WAV.");
        stop.setTooltip ("STOP (F4): stops the recording and lets you save it (MP3, FLAC, WAV), listen to it, or record again.");
        rec.iconWidth = 24.0f;
        rec.icon = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour)
        {
            // a round dot: red, white while recording (and blinking)
            const bool on = proc.getRecorder().isRecording();
            if (on && blink) return;
            g.setColour (on ? juce::Colours::white : juce::Colour (0xffd8322a));
            g.fillEllipse (r.withSizeKeepingCentre (14.0f, 14.0f));
        };
        stop.iconWidth = 22.0f;
        stop.icon = Theme::iconStop;
        rec.onClick = [this] { recPressed(); };
        stop.onClick = [this] { stopPressed(); };
        startTimerHz (8);
        update();
    }
    ~RecBar() override { stopTimer(); }

    std::function<void (const juce::String&)> onStatus;

    // ---- for the tests and the menu ----
    void startRecording()
    {
        auto& r = proc.getRecorder();
        const auto err = r.start (proc.getSampleRate(), proc.getTotalNumOutputChannels());
        status (err.isEmpty() ? juce::String ("REC - RECORDING THE JAM. PRESS STOP WHEN YOU ARE DONE") : err);
        update();
    }
    void stopRecording (bool showMenu = true)
    {
        proc.getRecorder().stop();
        status ("STOPPED - " + timeText (proc.getRecorder().getSeconds()) + " RECORDED. SAVE IT, LISTEN, OR RECORD AGAIN");
        update();
        if (showMenu) showTakeMenu();
    }
    /** Saves the take in a background thread; afterwards the take is gone (it is in the file now). */
    void saveTake (const juce::File& dest, JamRecorder::Format format, bool wait = false)
    {
        if (saving.load()) return;
        saving = true;
        progress = 0.0f;
        update();
        juce::Component::SafePointer<RecBar> safe (this);
        auto job = [this, safe, dest, format]
        {
            const auto err = proc.getRecorder().save (dest, format, &progress);
            juce::MessageManager::callAsync ([safe, err, dest]
            {
                if (safe == nullptr) return;
                safe->saving = false;
                if (err.isEmpty()) safe->proc.getRecorder().discard();
                safe->status (err.isEmpty() ? "SAVED " + dest.getFileName().toUpperCase() + " IN " + dest.getParentDirectory().getFileName().toUpperCase() : err);
                safe->lastSaved = err.isEmpty() ? dest : juce::File();
                safe->update();
            });
        };
        if (wait) job();
        else juce::Thread::launch (job);
    }
    bool isSaving() const noexcept { return saving.load(); }
    /** The keys: F3 = REC, F4 = STOP (or SAVE when a take waits). */
    void pressRec()  { if (! saving.load()) recPressed(); }
    void pressStop() { if (! saving.load()) stopPressed(); }
    juce::File lastSaved;

    void paint (juce::Graphics&) override {}
    void resized() override
    {
        auto r = getLocalBounds();
        stop.setBounds (r.removeFromRight (100));
        r.removeFromRight (6);
        rec.setBounds (r);
    }

private:
    void status (const juce::String& s) { if (onStatus) onStatus (s); }
    static juce::String timeText (double s)
    {
        const int t = (int) s;
        return juce::String (t / 60).paddedLeft ('0', 2) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
    }

    void update()
    {
        auto& r = proc.getRecorder();
        const bool recNow = r.isRecording(), take = r.hasTake();
        rec.setToggleState (recNow, juce::dontSendNotification);
        rec.setButtonText (saving.load() ? "SAVING " + juce::String (juce::roundToInt (progress.load() * 100.0f)) + "%"
                                         : recNow ? "REC " + timeText (r.getSeconds()) : juce::String ("REC"));
        stop.setButtonText (! recNow && take ? "SAVE" : "STOP");
        stop.setEnabled ((recNow || take) && ! saving.load());
        rec.setEnabled (! saving.load());
        stop.setAlpha (stop.isEnabled() ? 1.0f : 0.45f);
    }

    void timerCallback() override
    {
        update();
        // blink the dot while recording
        if (proc.getRecorder().isRecording()) { blink = ! blink; rec.repaint(); }
    }

    void recPressed()
    {
        auto& r = proc.getRecorder();
        if (r.isRecording()) { stopRecording(); return; }
        if (r.hasTake())
        {
            juce::PopupMenu m;
            m.addSectionHeader ("THE LAST TAKE ISN'T SAVED");
            m.addItem (1, "THROW IT AWAY AND RECORD AGAIN");
            m.addItem (2, "SAVE IT FIRST...");
            if (! isShowing()) return;
            juce::Component::SafePointer<RecBar> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&rec), [safe] (int c)
            {
                if (safe == nullptr) return;
                if (c == 1) safe->startRecording();
                if (c == 2) safe->showTakeMenu();
            });
            return;
        }
        startRecording();
    }

    void stopPressed()
    {
        if (proc.getRecorder().isRecording()) stopRecording();
        else if (proc.getRecorder().hasTake()) showTakeMenu();
    }

    void showTakeMenu()
    {
        if (! proc.getRecorder().hasTake() || ! isShowing()) return;
        juce::PopupMenu m;
        m.addSectionHeader ("JAM " + timeText (proc.getRecorder().getSeconds()));
        m.addItem (1, "SAVE AS MP3 (320 KBPS)...");
        m.addItem (2, "SAVE AS FLAC (LOSSLESS)...");
        m.addItem (3, "SAVE AS WAV (24-BIT)...");
        m.addSeparator();
        m.addItem (4, "LISTEN (OPENS YOUR PLAYER)");
        m.addItem (5, "THROW AWAY AND RECORD AGAIN");
        m.addItem (6, "THROW AWAY");
        juce::Component::SafePointer<RecBar> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&stop), [safe] (int c)
        {
            if (safe == nullptr || c == 0) return;
            auto& self = *safe;
            if (c >= 1 && c <= 3)
            {
                const auto format = c == 1 ? JamRecorder::Format::mp3 : c == 2 ? JamRecorder::Format::flac : JamRecorder::Format::wav;
                JamRecorder::folder().createDirectory();
                const auto ext = JamRecorder::extensionFor (format);
                self.chooser = std::make_unique<juce::FileChooser> ("Save the jam",
                                   JamRecorder::folder().getNonexistentChildFile (JamRecorder::suggestedName(), ext, false), "*" + ext);
                self.chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                             | juce::FileBrowserComponent::warnAboutOverwriting,
                    [safe, format, ext] (const juce::FileChooser& fc)
                    {
                        if (safe == nullptr) return;
                        auto f = fc.getResult();
                        if (f == juce::File()) return;
                        if (! f.hasFileExtension (ext.substring (1))) f = f.withFileExtension (ext);
                        safe->saveTake (f, format);
                    });
            }
            if (c == 4) self.proc.getRecorder().getTakeFile().startAsProcess();
            if (c == 5) self.startRecording();
            if (c == 6) { self.proc.getRecorder().discard(); self.status ("THE TAKE WAS THROWN AWAY"); self.update(); }
        });
    }

    FourTrackerProcessor& proc;
    PixelButton rec { "REC", PixelButton::Style::Record, 17.0f }, stop { "STOP", PixelButton::Style::Raised, 17.0f };
    std::unique_ptr<juce::FileChooser> chooser;
    std::atomic<bool> saving { false };
    std::atomic<float> progress { 0.0f };
    bool blink = false;
};

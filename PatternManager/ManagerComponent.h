// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker Pattern Manager - Copyright (C) 2026 elcrispo76

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "SongList.h"
#include "UI/Widgets.h"

/** Sends the song's keys to Four Tracker on time, on its own thread. */
class SongPlayer : private juce::HighResolutionTimer
{
public:
    ~SongPlayer() override { stop(); }
    /** Where the keys go: a MIDI output (LoopBe1, loopMIDI ...). The test gives a function instead. */
    std::function<void (const juce::MidiMessage&)> send;

    void play (const pmgr::Song& song, int from);
    void stop();                              // also stops Four Tracker
    bool isPlaying() const noexcept { return playing.load(); }
    /** The position that plays now (-1 when stopped) and the seconds since PLAY. */
    int position() const noexcept { return pos.load(); }
    double elapsed() const;

private:
    void hiResTimerCallback() override;
    void sendKey (int note);

    juce::CriticalSection lock;
    pmgr::Song song;
    std::vector<pmgr::KeyEvent> events;
    std::vector<double> starts;               // when each played position starts (seconds)
    std::vector<int> order;                   // which list position that is
    size_t next = 0;
    double t0 = 0.0;
    std::atomic<bool> playing { false };
    std::atomic<int> pos { -1 };
    std::vector<std::pair<double, int>> noteOffs;   // (time, note)
};

/** The Pattern Manager window: the song list, its buttons, MIDI OUT, BPM / SPEED, PLAY / STOP. */
class ManagerComponent : public juce::Component, private juce::Timer
{
public:
    ManagerComponent();
    ~ManagerComponent() override;
    static constexpr int kWidth = 1100, kHeight = 760;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // the buttons (public for the tests)
    void moveSelection (int delta);
    void changePattern (int delta);
    void changeRows (int delta);            // - / +: the usual lengths (16, 32, 48, 64 ...)
    void setRows (int rows);                // any length 1 .. 256
    void insertHere();
    void insertAfter();
    void deleteSelected();
    void play();
    void stop();

    pmgr::Song& getSong() { return song; }
    int getSelected() const noexcept { return selected; }
    SongPlayer& getPlayer() { return player; }
    void setMidiOutput (const juce::String& identifier);
    juce::String saveTo (const juce::File&);
    juce::String loadFrom (const juce::File&);

private:
    void timerCallback() override;
    void refreshControls();
    void chooseMidiOut();
    juce::Rectangle<int> rowBounds (int index) const;
    int firstShown() const;
    void setStatus (const juce::String& s) { status = s; repaint(); }

    pmgr::Song song;
    int selected = 0;
    SongPlayer player;
    std::unique_ptr<juce::MidiOutput> midiOut;
    juce::String midiOutName;
    juce::String status;
    juce::Rectangle<int> topPanel, listArea, sidePanel, statusArea;
    std::unique_ptr<juce::FileChooser> chooser;
    int lastShownPos = -2;

    PixelButton midiBtn { "MIDI OUT: NONE", PixelButton::Style::Raised, 15.0f };
    ValueBox bpmBox { 32, 255, 125, 26.0f }, speedBox { 1, 31, 6, 26.0f }, chanBox { 1, 16, 1, 22.0f }, keyBox { 0, 62, 36, 22.0f };
    PixelButton loopBtn { "LOOP SONG", PixelButton::Style::Record, 16.0f };
    PixelButton up { "" }, down { "" }, patDown { "" }, patUp { "" };
    PixelButton rowsDown { "-" , PixelButton::Style::Raised, 22.0f }, rowsUp { "+", PixelButton::Style::Raised, 22.0f };
    ValueBox rowsBox { 1, 256, 64, 26.0f };   // type the exact length, drag it, or the mouse wheel
    PixelButton insHere { "INSERT HERE", PixelButton::Style::Raised, 13.0f }, insAfter { "INSERT AFTER", PixelButton::Style::Raised, 13.0f },
                delBtn { "DELETE", PixelButton::Style::Raised, 14.0f };
    PixelButton playBtn { "PLAY", PixelButton::Style::Record, 24.0f }, stopBtn { "STOP", PixelButton::Style::Raised, 24.0f },
                playHereBtn { "PLAY FROM HERE", PixelButton::Style::Raised, 12.0f };
    PixelButton newBtn { "NEW", PixelButton::Style::Raised, 16.0f }, loadBtn { "LOAD", PixelButton::Style::Raised, 16.0f },
                saveBtn { "SAVE", PixelButton::Style::Raised, 16.0f };
};

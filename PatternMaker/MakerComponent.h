// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker Pattern Maker - Copyright (C) 2026 elcrispo76

#pragma once

#include "PatternGen.h"
#include "PluginProcessor.h"
#include "UI/Widgets.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include "Metronome.h"

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
namespace pm { class SlicerComponent; }

class MakerComponent : public juce::Component, private juce::Timer, private juce::MidiInputCallback
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
    void apply() { apply (false); }
    /** SAVE: as a .ftpat for Four Tracker's LOAD PATTERN. Returns an error message or empty. */
    juce::String saveTo (const juce::File& file);
    juce::File suggestedFile() const;

    // ---- BLANK + REC: play the pattern in with a MIDI keyboard or drum pads, to a metronome ----
    void blank();                           // BLANK: every step off, the sounds stay
    void toggleRec();                       // REC: stopped = 1 bar count-in, then record; playing = punch in / out
    void undoTake();
    /** DOUBLE: 1 bar becomes 2, 2 become 4 - what is there is copied after itself (it can be undone). */
    void doubleBars();
    bool isRecording() const { return armed; }
    void setMetronome (bool on);
    void setCountIn (bool on) { countIn = on; refreshRecControls(); }
    /** A pad or key: plays the lane's sound now and, when recording, writes it on the nearest 16th.
        midiNote: the key (for the bass / arp the pitch), or -1 for the lane's root. timeMs: when it was hit
        (Time::getMillisecondCounterHiRes()). */
    void padDown (int lane, int velocity127, int midiNote, double timeMs);
    void padUp (int midiNote, double timeMs);
    /** A MIDI note from a controller: to its pad's lane, else to the KEYS lane, or learnt as a pad. */
    void midiNoteOn (int midiNote, int velocity127, double timeMs);
    /** The recording itself at a known place (pos = rows since row 0, as quantizeStep); for the test. */
    void recordAt (int lane, int velocity127, int midiNote, double pos);
    /** DRUM PADS: pads 1-8 play the 8 lanes, other notes are not used. KEYBOARD: the General MIDI drum
        keys play the drums, every other key plays the KEYS lane (bass or arp) at its pitch.
        Each has its own pads (LEARN changes the one in use). */
    void setPadMode (bool drumPads);
    bool isPadMode() const { return padMode; }
    pm::PadMap& getPads() { return padMode ? padPads : keyPads; }
    /** The lane a MIDI note plays in the mode in use (-1: none). */
    int laneForMidi (int midiNote) const;
    void learnPad (int lane, bool thenTheNext);   // the next note in becomes the lane's pad
    int  keysLane = 6;                            // the lane a keyboard plays (-1: off); 6 = BASS, 7 = ARP
    std::function<double()> outputLatencyMs;      // the app: how late the sound is heard
    /** Rows since row 0 of the pattern for a time (negative in the count-in), or NaN when stopped. */
    double positionAt (double timeMs) const;

    pm::Pattern& getPattern() { return pattern; }
    FourTrackerProcessor& getProcessor() { return proc; }

    // ---- 16 lanes and the SLICER ----
    /** A sound (or a slice from the SLICER) on a lane: it is turned on. */
    void setLaneSound (int lane, const juce::File& f, bool isSlice);
    void showStatus (const juce::String& s) { setStatus (s); }
    void setBpm (int bpm);                         // as the BPM box
    void setLaneView (int v);                      // 0: lanes 1-8, 1: lanes 9-16
    int  getLaneView() const { return laneView; }
    void openSlicer();
    void closeSlicer();
    pm::SlicerComponent* getSlicer() { return slicer.get(); }
    /** The app's audio: the metronome and the SLICER play on it, next to the engine. */
    void setAudioDevices (juce::AudioDeviceManager* d);
    pm::Metronome& getMetronome() { return metronomeOut; }
    std::function<void()> onAudioSettings;  // the app opens its audio device settings

private:
    void timerCallback() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void paintOverChildren (juce::Graphics&) override;
    void apply (bool forSave);
    void refreshRecControls();
    void openMidiInputs();
    void loadMidiSettings();
    void saveMidiSettings() const;
    void scheduleApply (double absRow);       // after the engine has passed that row (no double hit)
    int  jamChannel (int lane) const { return lane; }   // the jam voices are apart from the pattern's

    /** Follows the engine's row every millisecond, so a hit can be placed between two rows. */
    struct RowClock : public juce::HighResolutionTimer
    {
        explicit RowClock (ft::TrackerEngine& e) : engine (e) {}
        void hiResTimerCallback() override;
        ft::TrackerEngine& engine;
        std::atomic<bool>   running { false };
        std::atomic<double> absRow { 0.0 };         // rows played since PLAY (count-in included)
        std::atomic<double> changeMs { 0.0 };       // when the row last changed
        int lastPattern = -1, lastRow = -1;
    };

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

    // BLANK + REC
    RowClock clock;
    pm::Metronome metronomeOut;          // its own sound: no lane, no slot, never saved
    pm::PadMap padPads, keyPads = pm::PadMap::generalMidi();
    bool padMode = true;
    bool armed = false, countIn = true, metronome = false, useVelocity = true, countInRun = false;
    pm::Pattern undoPattern;
    bool hasUndo = false;
    int learnLane = -1;
    bool learnChain = false;
    double pendingApplyRow = -1.0;
    struct Held { int lane = -1, step = -1; double pos = 0.0; };
    std::array<Held, 128> held {};
    std::array<double, pm::kNumLanes> flash {};
    juce::String midiInName { "ALL" };
    std::vector<std::unique_ptr<juce::MidiInput>> inputs;
    juce::StringArray openInputNames;
    PixelButton blankBtn { "BLANK", PixelButton::Style::Raised, 16.0f };
    PixelButton recBtn { "REC", PixelButton::Style::Record, 18.0f };
    PixelButton countBtn { "COUNT-IN", PixelButton::Style::Record, 14.0f };
    PixelButton metroBtn { "METRO", PixelButton::Style::Record, 14.0f };
    PixelButton velBtn { "VELOCITY", PixelButton::Style::Record, 14.0f };
    PixelButton undoBtn { "UNDO", PixelButton::Style::Raised, 14.0f };
    PixelButton doubleBtn { "DOUBLE", PixelButton::Style::Raised, 14.0f };
    PixelButton modeBtn { "INPUT: DRUM PADS", PixelButton::Style::Dark, 13.0f };
    PixelButton padsBtn { "PADS", PixelButton::Style::Raised, 14.0f };
    PixelButton keysBtn { "KEYS: BASS", PixelButton::Style::Raised, 14.0f };
    PixelButton midiBtn { "MIDI IN: ALL", PixelButton::Style::Raised, 14.0f };
    juce::Rectangle<int> recStrip;
    juce::Component::SafePointer<MakerComponent> selfSafe;

    // 16 lanes, 8 in view; the SLICER
    int laneView = 0;
    std::array<std::unique_ptr<PixelButton>, 2> laneTabs;
    PixelButton slicerBtn { "SLICER", PixelButton::Style::Dark, 16.0f };
    std::unique_ptr<pm::SlicerComponent> slicer;
    std::unique_ptr<juce::DocumentWindow> slicerWindow;
    juce::AudioDeviceManager* audioDevices = nullptr;
};

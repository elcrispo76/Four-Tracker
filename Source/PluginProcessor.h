// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "TrackerEngine.h"
#include "Instrument.h"

class FxHost;   // VST effect hosting (FX build only)
#include "JamRecorder.h"

class FourTrackerProcessor : public juce::AudioProcessor,
                             public juce::ChangeBroadcaster,
                             private juce::Timer
{
public:
    FourTrackerProcessor();
    ~FourTrackerProcessor() override;

    // ---- AudioProcessor ------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Four Tracker"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }   // MIDI instruments
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- tracker API (message thread) ---------------------------------------
    ft::TrackerEngine& getEngine() noexcept { return engine; }

    ft::Instrument& getInstrument (int slot) { return instruments[(size_t) juce::jlimit (0, ft::kNumInstruments - 1, slot)]; }
    /** Call after changing an instrument: republishes it to the audio thread
        and notifies the UI. */
    void instrumentChanged (int slot);

    /** RE-SAMPLE: which pattern, which rows and which tracks to turn into a sample. */
    struct ResampleRequest
    {
        int pattern = 0;
        int fromRow = 0, toRow = 63;        // inclusive
        juce::uint32 tracks = 0xffffu;      // bit n = track n+1
        /** How the sample ends. LOOP: exactly as long as the rows, and what still sounds at the end is
            laid over the start (as when the pattern loops), so it loops without a seam; the loop is set.
            CUT: exactly as long as the rows, cut off. RING OUT: longer, the sound rings out (up to 4 s). */
        enum Ending { loopEnd = 0, cutEnd = 1, ringOut = 2 };
        int ending = loopEnd;
        bool normalise = false;             // loudest point to just under full scale
    };
    /** Plays the rows offline in a second engine (the song that plays now isn't disturbed) and puts
        the result into `slot` as a stereo sample. VST effects and MIDI instruments are not in it.
        Returns a message for the screen; `ok` says whether the slot got a sample. Message thread. */
    juce::String resampleInto (int slot, const ResampleRequest& request, bool& ok);

    // ---- synth MIDI LEARN: a knob on the MIDI keyboard moves one synth slider -------
    /** MIDI LEARN target for SYNTH 2's knob n: kSynth2Knob + n (SYNTH 1's sliders are 0..7). */
    static constexpr int kSynth2Knob = 100;
    /** The next knob moved becomes this slot's knob for that slider (ft::SynthParam) or SYNTH 2 knob. */
    void startSynthLearn (int slot, int param);
    void cancelSynthLearn();
    bool isSynthLearning (int slot, int param) const { return learnSlot == slot && learnParam == param; }
    void forgetSynthKnob (int slot, int param);
    /** Takes the knobs moved since last time and moves the synth sliders they belong to.
        Runs by itself ~60 times a second; public for the test program. */
    void applyMidiKnobs();
    /** Goes up each time a knob moved a slider or was learned (the UI follows it). */
    std::atomic<int> knobRevision { 0 };
    /** A message for the status line after a knob was learned ("CC 74 -> SLOT 09 CUTOFF"). */
    juce::String takeKnobMessage() { auto m = knobMessage; knobMessage = {}; return m; }

    juce::String loadInstrument (int slot, const juce::File& file);
    /** Writes the slot's sample to a WAV or AIFF file (by the extension; WAV if none), 16 or 24 bit
        like the sample, with the loop in the WAV 'smpl' chunk. The slot takes the file's name.
        Returns an error message, or an empty string. */
    juce::String saveInstrumentSample (int slot, const juce::File& file);
    /** Documents/Four Tracker/Samples: where SAVE suggests saving. */
    static juce::File getSavedSamplesFolder() { return getUserFolder().getChildFile ("Samples"); }
    void discardInstrument (int slot);
    juce::AudioFormatManager& getFormatManager() noexcept { return formats; }
    juce::String getSupportedAudioWildcard() const;

    /** Sample browser preview: plays a file once without loading it into a
        slot. Returns false if the file can't be read as a sample. */
    bool auditionFile (const juce::File& file, bool play = true);
    void stopAudition();
    /** The sound last read by auditionFile (for the browser's waveform). */
    const ft::Instrument& getAuditionSample() const noexcept { return audition; }

    /** Stops playback and clears every pattern and every instrument/sample,
        resetting speed, BPM, mixer and selections to their defaults. */
    void newPatternSet();

    /** Clears everything, then loads the built-in demo sounds and patterns. */
    void loadDemoSong();

    /** A module (.ftm) holds patterns, samples, instrument settings, speed/BPM
        and mixer in one compressed file. loadModule also reads old .ftps
        pattern sets (patterns only). Returns an error message or empty. */
    bool saveModule (const juce::File& file);
    juce::String loadModule (const juce::File& file);

    /** A single pattern (.ftpat): its rows and the instruments (with their samples) it uses, so it can be
        shared and put into another song. Returns an error message or empty. */
    juce::String savePattern (int pattern, const juce::File& file);
    /** Loads a .ftpat into `target`, which must be empty. Its instruments go into empty slots (one that is
        already loaded is used again) and the notes are renumbered to match. The song keeps its own
        SPEED and BPM: tempo commands (Fxx) are taken out. Adds tracks if the pattern has more.
        Returns a message for the screen. */
    juce::String loadPattern (const juce::File& file, int target, bool& ok);
    /** The first empty pattern from `from` on (wrapping round), or -1. */
    int findEmptyPattern (int from);
    /** How many tracks a pattern uses (the last track with anything in it). */
    int patternTracksUsed (int pattern);

    /** Documents/Four Tracker - default place for modules. */
    static juce::File getUserFolder();
    /** Creates Documents/Four Tracker/Demo with "Demo Song.ftm" and the demo
        sounds as WAV files, if they are not there yet. */
    static void ensureDemoFolder();

    /** VST effects per channel. nullptr unless this is the FX build running as the standalone app. */
    FxHost* getFxHost() const noexcept;
    /** Removes the VST effects from every channel (FX build; does nothing otherwise). */
    void clearAllEffects();
    /** After loadModule: a note for the title bar, e.g. effects in the module that aren't installed (empty = none). */
    juce::String takeLoadNote() { auto n = lastLoadNote; lastLoadNote = {}; return n; }

    /** Number of tracks (channels) in the song, 1..16 (+ / - TRACK). Fewer clears the ones beyond. */
    void setNumChannels (int n);
    int  getNumChannels() const noexcept { return engine.numChannels.load(); }
    /** True if a track has anything in any pattern. */
    bool trackHasNotes (int channel);

    /** Unsaved changes: the song (patterns, instruments, samples, tempo, tracks, mixer) is compared with how it
        was at the last SAVE / LOAD / NEW, so closing the program can ask first. */
    void markSaved();
    bool hasUnsavedChanges() const;
    /** The module file last saved or loaded (empty after NEW). */
    juce::File currentModuleFile;
    juce::String lastLoadNote;

    /** REC: records what you hear while you jam (see JamRecorder). */
    JamRecorder& getRecorder() noexcept { return recorder; }

    /** UI preferences kept with the session. */
    bool hexMode = true;
    bool chordMode = false;   // CHORD: held note keys go to the next tracks
    int  selectedInstrument = 1;

private:
    JamRecorder recorder;
    juce::uint64 songFingerprint() const;
    juce::uint64 savedFingerprint = 0;
    void timerCallback() override;

    void writeSong (juce::ValueTree& t) const;
    static int normaliseChannels (int n);
    void readSong (const juce::ValueTree& t);

    juce::ValueTree patternsToTree() const;
    void patternsFromTree (const juce::ValueTree& t);
    juce::ValueTree instrumentsToTree() const;
    void instrumentsFromTree (const juce::ValueTree& t);

    ft::TrackerEngine engine;
    std::array<ft::Instrument, ft::kNumInstruments> instruments;
    int learnSlot = -1, learnParam = -1;
    juce::String knobMessage;
    struct KnobPump : juce::Timer
    {
        std::function<void()> tick;
        void timerCallback() override { if (tick) tick(); }
    } knobPump;
    ft::Instrument audition;
   #if FOURTRACKER_FX_HOST
    std::unique_ptr<FxHost> fx;
   #endif
    juce::File auditionSource;
    juce::AudioFormatManager formats;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FourTrackerProcessor)
};

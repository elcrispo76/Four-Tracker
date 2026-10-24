// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "TrackerEngine.h"
#include "Instrument.h"

class FxHost;   // VST effect hosting (FX build only)

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

    // ---- synth MIDI LEARN: a knob on the MIDI keyboard moves one synth slider -------
    /** The next knob moved becomes this slot's knob for that slider (ft::SynthParam). */
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

    /** Documents/Four Tracker - default place for modules. */
    static juce::File getUserFolder();
    /** Creates Documents/Four Tracker/Demo with "Demo Song.ftm" and the demo
        sounds as WAV files, if they are not there yet. */
    static void ensureDemoFolder();

    /** VST effects per channel. nullptr unless this is the FX build running as the standalone app. */
    FxHost* getFxHost() const noexcept;
    /** Removes the VST effects from every channel (FX build; does nothing otherwise). */
    void clearAllEffects();

    /** Number of tracks (channels) in the song, 1..16 (+ / - TRACK). Fewer clears the ones beyond. */
    void setNumChannels (int n);
    int  getNumChannels() const noexcept { return engine.numChannels.load(); }
    /** True if a track has anything in any pattern. */
    bool trackHasNotes (int channel);

    /** UI preferences kept with the session. */
    bool hexMode = true;
    int  selectedInstrument = 1;

private:
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

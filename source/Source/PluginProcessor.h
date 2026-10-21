// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "TrackerEngine.h"
#include "Instrument.h"

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
    bool producesMidi() const override { return false; }
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

    /** UI preferences kept with the session. */
    bool hexMode = true;
    int  selectedInstrument = 1;

private:
    void timerCallback() override;

    void writeSong (juce::ValueTree& t) const;
    void readSong (const juce::ValueTree& t);

    juce::ValueTree patternsToTree() const;
    void patternsFromTree (const juce::ValueTree& t);
    juce::ValueTree instrumentsToTree() const;
    void instrumentsFromTree (const juce::ValueTree& t);

    ft::TrackerEngine engine;
    std::array<ft::Instrument, ft::kNumInstruments> instruments;
    ft::Instrument audition;
    juce::File auditionSource;
    juce::AudioFormatManager formats;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FourTrackerProcessor)
};

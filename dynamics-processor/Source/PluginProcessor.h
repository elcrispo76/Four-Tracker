// SPDX-License-Identifier: AGPL-3.0-or-later
// Dynamics Processor - Copyright (C) 2026 elcrispo76

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DynEngine.h"

class DynProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    DynProcessor();
    ~DynProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Dynamics Processor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState params;

    /** The instrument on the track: sets the instrument and every knob that suits it (transient
        shaper, compressor character, saturation) and switches all three on. Message thread. */
    void selectInstrument (int instrument);
    int  getInstrument() const;

    /** AUTO: listen to the track, then set the transient shaper, the compressor's threshold, release
        and makeup, and the drive, and switch all three on. A second click stops it. */
    void toggleAuto();
    bool isListening() const noexcept   { return engine.getListenState() != dyn::Engine::idle; }
    float getListenProgress() const noexcept { return engine.getListenProgress(); }
    /** The last thing AUTO said, for the screen (message thread). */
    juce::String getAutoMessage() const { return autoMessage; }
    const dyn::AutoResult& getLastAutoResult() const noexcept { return lastResult; }
    /** For the tests: run the analysis now if AUTO has heard enough. */
    void finishAutoNow() { handleUpdateNowIfNeeded(); }

    // meters, for the editor
    dyn::Engine& getEngine() noexcept { return engine; }

    struct Preset { const char* name; int instrument; std::vector<std::pair<const char*, int>> values; };
    static const std::vector<Preset>& presets();
    void applyPreset (int index);

    /** The engine settings from the parameters (also used by the tests). */
    dyn::Settings readSettings() const;

private:
    void handleAsyncUpdate() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    int get (const char* id) const;
    void setParam (const char* id, int value);

    dyn::Engine engine;
    dyn::AutoResult lastResult;
    juce::String autoMessage { "CHOOSE THE INSTRUMENT, PLAY THE TRACK AND CLICK AUTO" };
    std::atomic<bool> asked { false };
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DynProcessor)
};

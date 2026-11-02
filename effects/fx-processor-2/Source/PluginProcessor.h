// SPDX-License-Identifier: AGPL-3.0-or-later
// FX Processor 2 - Copyright (C) 2026 elcrispo76

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "FxEngine.h"

class FxProcessor : public juce::AudioProcessor
{
public:
    FxProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FX Processor 2"; }
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

    // for the editor
    double getShownBpm() const noexcept    { return shownBpm.load(); }
    bool   isHostTempo() const noexcept    { return hostTempo.load(); }
    /** -1..1: the LFO of effect 0 (phaser), 1 (flanger) or 2 (ring mod) now. */
    float  getLfo (int which) const noexcept { return lfos[(size_t) juce::jlimit (0, 2, which)].load(); }

    struct Preset { const char* name; std::vector<std::pair<const char*, int>> values; };
    static const std::vector<Preset>& presets();
    void applyPreset (int index);

    /** The engine settings from the parameters (also used by the tests). */
    fx::Settings readSettings() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    int get (const char* id) const;

    fx::Engine engine;
    std::atomic<double> shownBpm { 0.0 };
    std::atomic<bool> hostTempo { false };
    std::array<std::atomic<float>, 3> lfos {};
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxProcessor)
};

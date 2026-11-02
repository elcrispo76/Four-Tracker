// SPDX-License-Identifier: AGPL-3.0-or-later
// Graphic EQ - Copyright (C) 2026 elcrispo76

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "EqEngine.h"

class EqProcessor : public juce::AudioProcessor
{
public:
    EqProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Graphic EQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState params;
    static juce::String bandId (int band) { return "band" + juce::String (band); }

    std::array<float, eq::kBands> readKnobs() const;
    float readOutput() const;

    struct Preset { const char* name; std::array<float, eq::kBands> knobs; };
    static const std::vector<Preset>& presets();
    void applyPreset (int index);
    /** Every band back to 0 dB. */
    void flat();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    eq::Engine engine;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqProcessor)
};

// SPDX-License-Identifier: AGPL-3.0-or-later
// Breakbeat Processor - Copyright (C) 2026 elcrispo76

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "BreakEngine.h"

class BreakbeatProcessor : public juce::AudioProcessor
{
public:
    BreakbeatProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Breakbeat Processor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- for the editor ----------------------------------------------------------
    juce::AudioProcessorValueTreeState params;
    static juce::String stepId (int i) { return "step" + juce::String (i + 1); }

    /** Playing step (0..7) or -1 when the host is stopped. */
    int getCurrentStep() const noexcept { return currentStep.load(); }
    /** Tempo the host reported last (0 = no tempo yet). */
    double getHostBpm() const noexcept { return hostBpm.load(); }
    bool hostIsPlaying() const noexcept { return hostPlaying.load(); }
    /** True when the host sends no tempo, so the plugin runs on its own TEMPO. */
    bool isUsingOwnClock() const noexcept { return usingOwnClock.load(); }

    /** Ready-made step patterns. */
    struct Pattern { const char* name; std::array<int, bb::kNumSteps> steps; };
    static const std::vector<Pattern>& patterns();
    void applyPattern (const std::array<int, bb::kNumSteps>& steps);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    bb::Engine engine;
    std::array<std::atomic<float>*, bb::kNumSteps> stepParams {};
    std::atomic<float>* lengthParam = nullptr;
    std::atomic<float>* bitsParam = nullptr;
    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* smoothParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* aheadParam = nullptr;
    std::atomic<float>* tempoParam = nullptr;
    std::atomic<float>* stepsParam = nullptr;
    std::atomic<float>* syncParam = nullptr;
    double songPpq = 0.0;            // smoothed host position (SYNC SONG)
    bool songValid = false;
    juce::int64 lastHostSample = -1;  // host position in samples (to notice play / restart)
    bool sawHostPlaying = false;
    double lastCallMs = 0.0;

    // own clock, for hosts that send no tempo / song position
    bool ownRunning = false;
    double ownPpq = 0.0;
    int ownSilence = 0;
    std::atomic<bool> usingOwnClock { false };

    std::atomic<int> currentStep { -1 };
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<bool> hostPlaying { false };
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakbeatProcessor)
};

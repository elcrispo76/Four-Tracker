// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker Pattern Maker - Copyright (C) 2026 elcrispo76
//
// The metronome: its own sound next to the engine's, not a lane. It clicks on the rows the engine
// began in the same block, to the sample: in the count-in (an empty bar, pattern 01) always, in the
// pattern (00) when METRO is on. So it needs no lane, no instrument slot and is never saved.

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <cmath>
#include "TrackerEngine.h"

namespace pm
{

class Metronome : public juce::AudioIODeviceCallback
{
public:
    explicit Metronome (ft::TrackerEngine& e) : engine (e) {}

    std::atomic<bool> metroOn { false };     // METRO: on every beat of the pattern too
    std::atomic<int>  clicks { 0 };          // how many it has played (the test)

    /** Adds the clicks for the block the engine has just played (n samples, at sampleRate). */
    void render (float* const* out, int numChannels, int n, double sampleRate)
    {
        for (int c = 0; c < numChannels; ++c) if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);
        if (sampleRate <= 0.0) return;
        const int starts = juce::jmin ((int) ft::TrackerEngine::kMaxRowStarts, engine.numRowStarts.load (std::memory_order_acquire));
        int next = 0;
        for (int i = 0; i < n; ++i)
        {
            while (next < starts && engine.rowStarts[(size_t) next].offset <= i)
            {
                const auto rs = engine.rowStarts[(size_t) next++];
                const bool countIn = rs.pattern == 1, beat = rs.row % 4 == 0;
                if (beat && (countIn || (rs.pattern == 0 && metroOn.load())))
                {
                    phase = 0.0;
                    age = 0;
                    freq = (countIn ? rs.row == 0 : rs.row % 16 == 0) ? 2400.0 : 1600.0;   // the one higher
                    level = rs.row % 16 == 0 ? 0.6f : 0.45f;
                    ++clicks;
                }
            }
            if (age < 0) continue;
            const double t = age / sampleRate;
            const float v = level * (float) (std::sin (phase) * std::exp (-t * 90.0));
            phase += juce::MathConstants<double>::twoPi * freq / sampleRate;
            if (++age > (int) (sampleRate * 0.06)) age = -1;
            for (int c = 0; c < numChannels; ++c) if (out[c] != nullptr) out[c][i] = v;
        }
    }

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        render (out, numOut, n, rate);
    }
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override { rate = d->getCurrentSampleRate(); }
    void audioDeviceStopped() override {}

private:
    ft::TrackerEngine& engine;
    double rate = 44100.0, phase = 0.0, freq = 1600.0;
    float level = 0.5f;
    int age = -1;
};

} // namespace pm

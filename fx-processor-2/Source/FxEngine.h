// SPDX-License-Identifier: AGPL-3.0-or-later
// FX Processor 2 - Copyright (C) 2026 elcrispo76

#pragma once

// The DSP of FX Processor 2, kept free of plugin code so it can be tested.
// The sound goes  PHASER -> FLANGER -> RING MOD -> OUTPUT.
//
//   PHASER    4 to 12 all-pass stages swept by an LFO, with feedback (the classic notches)
//   FLANGER   a short modulated delay (0.3 - 10 ms) with feedback, + or - (jet / metal)
//   RING MOD  the sound times a carrier (sine, triangle or square, 10 Hz - 4 kHz); its own
//             LFO can sweep the carrier frequency
//
// Each effect has its own LFO: free (Hz) or in time with the song (1/16 .. 4 bars). The left
// and right LFOs of the phaser and flanger are a quarter turn apart, for wide stereo.
// Every knob is a whole number, so presets and automation stay simple.

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <vector>
#include <algorithm>

namespace fx
{

struct Settings
{
    // phaser
    bool phaserOn = false;
    int  phSync = 6;            // 0 FREE, 1 1/16, 2 1/8, 3 1/4, 4 1/2, 5 1 BAR, 6 2 BARS, 7 4 BARS
    int  phRate = 30;           // FREE: 0..127 -> 0.05 .. 20 Hz
    int  phDepth = 80;          // 0..127 (how far it sweeps)
    int  phCenter = 64;         // 0..127 where the sweep sits (200 Hz .. 3 kHz)
    int  phFeedback = 50;       // 0..95 %
    int  phStages = 1;          // 0 4, 1 6, 2 8, 3 12
    int  phMix = 100;           // 0..100
    // flanger
    bool flangerOn = false;
    int  flSync = 7;
    int  flRate = 20;
    int  flDepth = 90;          // 0..127
    int  flDelay = 30;          // 0..127 -> 0.3 .. 10 ms
    int  flFeedback = 60;       // -95..95 %
    int  flMix = 70;            // 0..100
    // ring mod
    bool ringOn = false;
    int  rmFreq = 60;           // 0..127 -> 10 Hz .. 4 kHz
    int  rmWave = 0;            // 0 sine, 1 triangle, 2 square
    int  rmSync = 0;
    int  rmRate = 30;
    int  rmLfoDepth = 0;        // 0..127 (up to +-2 octaves)
    int  rmMix = 100;           // 0..100
    // output
    int  outputDb = 0;          // -24..+6
};

struct Transport
{
    bool playing = false;
    double bpm = 120.0;
    double ppq = 0.0;
};

// ---- value mapping (also used for the knob texts) -------------------------------------
inline double lfoHz (int v)        { return 0.05 * std::pow (400.0, std::clamp (v, 0, 127) / 127.0); }
inline double syncBeats (int c)    { static const double b[] = { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 }; return b[std::clamp (c, 0, 7)]; }
inline double phCenterHz (int v)   { return 200.0 * std::pow (15.0, std::clamp (v, 0, 127) / 127.0); }
inline int    phStageCount (int c) { static const int n[] = { 4, 6, 8, 12 }; return n[std::clamp (c, 0, 3)]; }
inline double flDelayMs (int v)    { return 0.3 * std::pow (10.0 / 0.3, std::clamp (v, 0, 127) / 127.0); }
inline double rmFreqHz (int v)     { return 10.0 * std::pow (400.0, std::clamp (v, 0, 127) / 127.0); }

class Engine
{
public:
    static constexpr int kMaxStages = 12;

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        const int fsz = (int) (sr * 0.03) + 8;          // 30 ms, more than the flanger needs
        for (auto* b : { &flL, &flR }) b->assign ((size_t) fsz, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto* b : { &flL, &flR }) std::fill (b->begin(), b->end(), 0.0f);
        for (auto& ch : ap) for (auto& st : ch) st = {};
        phFb = { 0.0f, 0.0f };
        flPos = 0;
        flFbL = flFbR = 0.0f;
        phFree = flFree = rmFree = 0.0;
        carrierPhase = 0.0;
        ownBeats = 0.0;
        lfoPh = lfoFl = lfoRm = 0.0f;
    }

    // -1..1, the three LFOs now (for the lights on the panels)
    float getPhaserLfo() const noexcept  { return lfoPh; }
    float getFlangerLfo() const noexcept { return lfoFl; }
    float getRingLfo() const noexcept    { return lfoRm; }

    void process (float* L, float* R, int n, const Transport& t, const Settings& s) noexcept
    {
        if (sr <= 0.0 || n <= 0) return;
        const double bpm = std::clamp (t.bpm, 20.0, 999.0);
        const double beatsPerSample = bpm / 60.0 / sr;
        const double beats0 = t.playing ? t.ppq : ownBeats;
        ownBeats = beats0 + n * beatsPerSample;

        // phaser
        const int stages = phStageCount (s.phStages);
        const double phLo = phCenterHz (s.phCenter) / std::pow (2.0, s.phDepth / 127.0 * 2.5);
        const double phOct = s.phDepth / 127.0 * 5.0;               // sweep up to 5 octaves
        const float phFbAmt = std::clamp (s.phFeedback, 0, 95) / 100.0f;
        const float phMix = s.phMix / 100.0f;
        // flanger
        const double flBase = flDelayMs (s.flDelay) * 0.001 * sr;
        const double flSwing = flBase * (s.flDepth / 127.0) * 0.95;
        const float flFbAmt = std::clamp (s.flFeedback, -95, 95) / 100.0f;
        const float flMix = s.flMix / 100.0f;
        const int fsz = (int) flL.size();
        // ring mod
        const double rmBase = rmFreqHz (s.rmFreq);
        const double rmOct = s.rmLfoDepth / 127.0 * 2.0;
        const float rmMix = s.rmMix / 100.0f;
        const float outGain = juce::Decibels::decibelsToGain ((float) std::clamp (s.outputDb, -24, 6));

        for (int i = 0; i < n; ++i)
        {
            const double beats = beats0 + i * beatsPerSample;
            float l = L[i], r = R != nullptr ? R[i] : l;

            // ---- PHASER
            {
                const double ph = lfoPhase (s.phSync, s.phRate, beats, phFree);
                lfoPh = (float) std::sin (ph * juce::MathConstants<double>::twoPi);
                if (s.phaserOn)
                {
                    float* io[2] = { &l, &r };
                    for (int c = 0; c < 2; ++c)
                    {
                        const double phc = ph + (c == 1 ? 0.25 : 0.0);
                        const double sweep = 0.5 + 0.5 * std::sin (phc * juce::MathConstants<double>::twoPi);   // 0..1
                        const double hz = std::min (phLo * std::pow (2.0, phOct * sweep), sr * 0.45);
                        const double tn = std::tan (juce::MathConstants<double>::pi * hz / sr);
                        const float a = (float) ((tn - 1.0) / (tn + 1.0));
                        const float x = *io[c];
                        float y = x + phFb[(size_t) c] * phFbAmt;
                        for (int k = 0; k < stages; ++k)
                        {
                            auto& st = ap[(size_t) c][(size_t) k];
                            const float o = a * y + st.x1 - a * st.y1;
                            st.x1 = y; st.y1 = o;
                            y = o;
                        }
                        phFb[(size_t) c] = std::clamp (y, -2.0f, 2.0f);
                        *io[c] = x + (0.5f * (x + y) - x) * phMix;      // 100 % = dry + phased: the deep notches
                    }
                }
            }

            // ---- FLANGER (the line keeps running when it is off)
            {
                const double ph = lfoPhase (s.flSync, s.flRate, beats, flFree);
                lfoFl = (float) std::sin (ph * juce::MathConstants<double>::twoPi);
                const double swL = 0.5 + 0.5 * std::sin (ph * juce::MathConstants<double>::twoPi);
                const double swR = 0.5 + 0.5 * std::sin ((ph + 0.25) * juce::MathConstants<double>::twoPi);
                const float dl = tap (flL, flPos - (flBase - flSwing * 0.5 + flSwing * swL + 1.0), fsz);
                const float dr = tap (flR, flPos - (flBase - flSwing * 0.5 + flSwing * swR + 1.0), fsz);
                const float inL = s.flangerOn ? l : 0.0f, inR = s.flangerOn ? r : 0.0f;
                flL[(size_t) flPos] = inL + dl * flFbAmt;
                flR[(size_t) flPos] = inR + dr * flFbAmt;
                if (++flPos >= fsz) flPos = 0;
                if (s.flangerOn)
                {
                    l = (l + dl * flMix) / (1.0f + 0.5f * flMix);
                    r = (r + dr * flMix) / (1.0f + 0.5f * flMix);
                }
            }

            // ---- RING MOD
            {
                const double ph = lfoPhase (s.rmSync, s.rmRate, beats, rmFree);
                lfoRm = (float) std::sin (ph * juce::MathConstants<double>::twoPi);
                const double hz = rmBase * std::pow (2.0, rmOct * lfoRm);
                carrierPhase += hz / sr;
                carrierPhase -= std::floor (carrierPhase);
                if (s.ringOn)
                {
                    const float c = carrier (s.rmWave, carrierPhase);
                    l += (l * c - l) * rmMix;
                    r += (r * c - r) * rmMix;
                }
            }

            L[i] = soft (l * outGain);
            if (R != nullptr) R[i] = soft (r * outGain);
        }
    }

private:
    struct AllPass { float x1 = 0.0f, y1 = 0.0f; };

    /** 0..1: the LFO phase, in time with the song (sync > 0) or free running. */
    double lfoPhase (int sync, int rate, double beats, double& freePhase) const noexcept
    {
        const double b = syncBeats (sync);
        if (b > 0.0) { const double c = beats / b; return c - std::floor (c); }
        freePhase += lfoHz (rate) / sr;
        freePhase -= std::floor (freePhase);
        return freePhase;
    }

    static float carrier (int wave, double ph) noexcept
    {
        switch (wave)
        {
            case 1:  return (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);
            case 2:  return ph < 0.5 ? 1.0f : -1.0f;
            default: return (float) std::sin (ph * juce::MathConstants<double>::twoPi);
        }
    }

    static float tap (const std::vector<float>& b, double pos, int size) noexcept
    {
        while (pos < 0.0) pos += size;
        const int i0 = (int) pos % size;
        const int i1 = (i0 + 1) % size;
        const float f = (float) (pos - std::floor (pos));
        return b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * f;
    }

    static float soft (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= 0.9f) return x;
        return std::copysign (0.9f + 0.1f * std::tanh ((a - 0.9f) / 0.1f), x);
    }

    double sr = 0.0;
    std::array<std::array<AllPass, kMaxStages>, 2> ap {};
    std::array<float, 2> phFb { 0.0f, 0.0f };
    std::vector<float> flL, flR;
    int flPos = 0;
    float flFbL = 0.0f, flFbR = 0.0f;
    double phFree = 0.0, flFree = 0.0, rmFree = 0.0, carrierPhase = 0.0, ownBeats = 0.0;
    float lfoPh = 0.0f, lfoFl = 0.0f, lfoRm = 0.0f;
};

} // namespace fx

// SPDX-License-Identifier: AGPL-3.0-or-later
// FX Processor - Copyright (C) 2026 elcrispo76

#pragma once

// The DSP of the FX Processor, kept free of plugin code so it can be tested.
// The sound goes  FILTER -> PW -> CHORUS -> DELAY -> REVERB -> OUTPUT.
// The LFO moves the filter's cutoff and the PW gate's pulse width.
//
//   FILTER  state-variable filter (TPT): low-pass, band-pass or high-pass, cutoff, resonance
//   PW      a "pulse width" gate in time with the song: the sound is on for WIDTH of each
//           step (1/4 ... 1/32, triplets) and down by DEPTH for the rest (trance gate)
//   LFO     sine, triangle, saw, square or sample & hold, free (Hz) or in time (1/16 .. 4 bars)
//   CHORUS  two modulated delay lines (left/right a quarter turn apart)
//   DELAY   tempo-synced delay with feedback, tone (low-pass in the feedback) and ping-pong
//   REVERB  juce::Reverb (Freeverb): size, damping, width
//
// Every knob is a whole number (most 0..127, like Four Tracker's synths), so presets,
// automation and MIDI learn in hosts are simple.

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <vector>
#include <algorithm>

namespace fx
{

struct Settings
{
    // filter
    bool  filterOn = false;
    int   filterMode = 0;        // 0 LP, 1 BP, 2 HP
    int   cutoff = 100;          // 0..127 -> 20 Hz .. 20 kHz
    int   reso = 20;             // 0..127
    // pw gate
    bool  pwOn = false;
    int   pwRate = 2;            // 0 1/4, 1 1/8, 2 1/16, 3 1/32, 4 1/8T, 5 1/16T
    int   pwWidth = 50;          // 5..95 %
    int   pwDepth = 100;         // 0..100 %
    // lfo
    int   lfoShape = 0;          // 0 sine, 1 tri, 2 saw, 3 square, 4 S&H
    int   lfoSync = 5;           // 0 FREE, 1 1/16, 2 1/8, 3 1/4, 4 1/2, 5 1 BAR, 6 2 BARS, 7 4 BARS
    int   lfoRate = 40;          // FREE: 0..127 -> 0.05 .. 20 Hz
    int   lfoToFilter = 0;       // -64..63 (+-4 octaves)
    int   lfoToPw = 0;           // 0..100
    // chorus
    bool  chorusOn = false;
    int   chorusRate = 30;       // 0..127 -> 0.05 .. 5 Hz
    int   chorusDepth = 60;      // 0..127
    int   chorusMix = 50;        // 0..100
    // delay
    bool  delayOn = false;
    int   delayTime = 2;         // 0 1/16, 1 1/8, 2 1/8D, 3 1/4, 4 1/4D, 5 1/2, 6 1 BAR
    int   delayFeedback = 40;    // 0..95 %
    int   delayTone = 90;        // 0..127 (low-pass in the feedback)
    int   delayMix = 30;         // 0..100
    bool  delayPing = false;
    // reverb
    bool  reverbOn = false;
    int   reverbSize = 70;       // 0..127
    int   reverbDamp = 50;       // 0..127
    int   reverbWidth = 100;     // 0..127
    int   reverbMix = 25;        // 0..100
    // output
    int   outputDb = 0;          // -24..+6
};

struct Transport
{
    bool playing = false;        // with a song position (ppq)
    double bpm = 120.0;
    double ppq = 0.0;            // beats at the block's first sample
};

// ---- value mapping (also used for the knob texts) -------------------------------------
inline double cutoffHz (double v)  { return 20.0 * std::pow (2.0, std::clamp (v, 0.0, 127.0) / 127.0 * 10.0); }
inline double lfoHz (int v)        { return 0.05 * std::pow (400.0, std::clamp (v, 0, 127) / 127.0); }
inline double chorusHz (int v)     { return 0.05 * std::pow (100.0, std::clamp (v, 0, 127) / 127.0); }
inline double pwRateBeats (int c)  { static const double b[] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 }; return b[std::clamp (c, 0, 5)]; }
inline double lfoSyncBeats (int c) { static const double b[] = { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 }; return b[std::clamp (c, 0, 7)]; }
inline double delayBeats (int c)   { static const double b[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0 }; return b[std::clamp (c, 0, 6)]; }

class Engine
{
public:
    static constexpr double kMaxDelaySeconds = 4.0;

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        const int dsz = (int) (sr * kMaxDelaySeconds) + 4;
        for (auto* b : { &dlL, &dlR }) b->assign ((size_t) dsz, 0.0f);
        const int csz = (int) (sr * 0.05) + 4;
        for (auto* b : { &chL, &chR }) b->assign ((size_t) csz, 0.0f);
        reverb.setSampleRate (sr);
        tmpL.assign ((size_t) std::max (1, maxBlock), 0.0f);
        tmpR.assign ((size_t) std::max (1, maxBlock), 0.0f);
        reset();
    }

    void reset()
    {
        for (auto* b : { &dlL, &dlR, &chL, &chR }) std::fill (b->begin(), b->end(), 0.0f);
        s1 = s2 = {};
        dlPos = chPos = 0;
        dlSmooth = -1.0;
        toneL = toneR = 0.0f;
        chPhase = 0.0;
        freePhase = 0.0;
        gate = 1.0f;
        lfoValue = 0.0f;
        shValue = 0.0f;
        lastShCycle = -1;
        ownBeats = 0.0;
        reverb.reset();
    }

    /** -1..1, the LFO now (for the light on the panel). */
    float getLfo() const noexcept   { return lfoValue; }
    /** 0..1, the PW gate now. */
    float getGate() const noexcept  { return gate; }

    void process (float* L, float* R, int n, const Transport& t, const Settings& s) noexcept
    {
        if (sr <= 0.0 || n <= 0) return;
        const double bpm = std::clamp (t.bpm, 20.0, 999.0);
        const double beatsPerSample = bpm / 60.0 / sr;

        // a beat position for the tempo-synced parts: the host's song position while it plays,
        // otherwise our own clock running at the same tempo
        double beats0 = t.playing ? t.ppq : ownBeats;
        ownBeats = beats0 + n * beatsPerSample;

        // ---- filter coefficients change with the LFO, so per sample; smoothed cutoff
        const double baseCut = (double) s.cutoff;
        const double lfoOct = s.lfoToFilter / 64.0 * 4.0;
        const double k = 2.0 - 1.96 * std::clamp (s.reso, 0, 127) / 127.0;

        // delay time, glided so a change doesn't click
        const double dTarget = std::min (delayBeats (s.delayTime) * 60.0 / bpm * sr, (double) dlL.size() - 4.0);
        if (dlSmooth < 0.0) dlSmooth = dTarget;
        const float fb = std::clamp (s.delayFeedback, 0, 95) / 100.0f;
        const float toneCoef = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * cutoffHz (s.delayTone) / sr));
        const float dMix = s.delayMix / 100.0f;

        const double chInc = chorusHz (s.chorusRate) / sr;
        const double chDepth = s.chorusDepth / 127.0 * 0.006 * sr;      // up to 6 ms
        const double chBase = 0.012 * sr;
        const float chMix = s.chorusMix / 100.0f;

        const double pwBeats = pwRateBeats (s.pwRate);
        const float depth = std::clamp (s.pwDepth, 0, 100) / 100.0f;
        const float gateCoef = (float) (1.0 - std::exp (-1.0 / (0.002 * sr)));   // 2 ms edges

        const double lfoBeats = lfoSyncBeats (s.lfoSync);
        const double freeInc = lfoHz (s.lfoRate) / sr;
        const float outGain = juce::Decibels::decibelsToGain ((float) std::clamp (s.outputDb, -24, 6));

        for (int i = 0; i < n; ++i)
        {
            const double beats = beats0 + i * beatsPerSample;
            float l = L[i], r = R != nullptr ? R[i] : l;

            // ---- LFO
            double ph;
            long long cycle;
            if (lfoBeats > 0.0) { const double c = beats / lfoBeats; cycle = (long long) std::floor (c); ph = c - (double) cycle; }
            else
            {
                freePhase += freeInc;
                cycle = lastShCycle;
                if (freePhase >= 1.0) { freePhase -= std::floor (freePhase); ++cycle; }
                ph = freePhase;
            }
            if (cycle != lastShCycle) { lastShCycle = cycle; shValue = rng.nextFloat() * 2.0f - 1.0f; }
            lfoValue = lfoShape (s.lfoShape, ph);

            // ---- FILTER
            if (s.filterOn)
            {
                const double hz = std::min (cutoffHz (baseCut) * std::pow (2.0, lfoOct * lfoValue), sr * 0.45);
                const double g = std::tan (juce::MathConstants<double>::pi * hz / sr);
                const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
                l = svf (s1, l, a1, a2, a3, k, s.filterMode);
                r = svf (s2, r, a1, a2, a3, k, s.filterMode);
            }

            // ---- PW gate
            if (s.pwOn)
            {
                const double gph = beats / pwBeats - std::floor (beats / pwBeats);
                const double w = std::clamp (s.pwWidth / 100.0 + s.lfoToPw / 100.0 * 0.45 * lfoValue, 0.02, 0.98);
                const float target = gph < w ? 1.0f : 1.0f - depth;
                gate += (target - gate) * gateCoef;
                l *= gate; r *= gate;
            }
            else gate = 1.0f;

            // ---- CHORUS
            if (s.chorusOn)
            {
                const int csz = (int) chL.size();
                chL[(size_t) chPos] = l; chR[(size_t) chPos] = r;
                const double dl = chBase + chDepth * (0.5 + 0.5 * std::sin (chPhase * juce::MathConstants<double>::twoPi));
                const double dr = chBase + chDepth * (0.5 + 0.5 * std::cos (chPhase * juce::MathConstants<double>::twoPi));
                const float wl = tap (chL, chPos - dl, csz), wr = tap (chR, chPos - dr, csz);
                l += (wl - l) * chMix;
                r += (wr - r) * chMix;
                chPhase += chInc;
                if (chPhase >= 1.0) chPhase -= 1.0;
                if (++chPos >= csz) chPos = 0;
            }

            // ---- DELAY (the line keeps running when it is off, so switching it on has no old sound)
            {
                const int dsz = (int) dlL.size();
                dlSmooth += (dTarget - dlSmooth) * 0.0005;
                const float yl = tap (dlL, dlPos - dlSmooth, dsz), yr = tap (dlR, dlPos - dlSmooth, dsz);
                toneL += (yl - toneL) * toneCoef;
                toneR += (yr - toneR) * toneCoef;
                const float inL = s.delayOn ? l : 0.0f, inR = s.delayOn ? r : 0.0f;
                if (s.delayPing)
                {
                    dlL[(size_t) dlPos] = 0.5f * (inL + inR) + toneR * fb;   // into the left, then across
                    dlR[(size_t) dlPos] = toneL * fb;
                }
                else
                {
                    dlL[(size_t) dlPos] = inL + toneL * fb;
                    dlR[(size_t) dlPos] = inR + toneR * fb;
                }
                if (++dlPos >= dsz) dlPos = 0;
                if (s.delayOn) { l += toneL * dMix * 1.2f; r += toneR * dMix * 1.2f; }
                else { toneL *= 0.999f; toneR *= 0.999f; }
            }

            L[i] = l;
            if (R != nullptr) R[i] = r;
        }

        // ---- REVERB (whole block)
        if (s.reverbOn)
        {
            juce::Reverb::Parameters p;
            p.roomSize = s.reverbSize / 127.0f;
            p.damping = s.reverbDamp / 127.0f;
            p.width = s.reverbWidth / 127.0f;
            const float mix = s.reverbMix / 100.0f;
            p.wetLevel = mix;
            p.dryLevel = 1.0f - mix * 0.5f;
            p.freezeMode = 0.0f;
            reverb.setParameters (p);
            if (R != nullptr) reverb.processStereo (L, R, n);
            else reverb.processMono (L, n);
        }

        for (int i = 0; i < n; ++i)
        {
            L[i] = soft (L[i] * outGain);
            if (R != nullptr) R[i] = soft (R[i] * outGain);
        }
    }

private:
    struct SvfState { double ic1 = 0.0, ic2 = 0.0; };

    static float svf (SvfState& st, float x, double a1, double a2, double a3, double k, int mode) noexcept
    {
        const double v3 = x - st.ic2;
        const double v1 = a1 * st.ic1 + a2 * v3;
        const double v2 = st.ic2 + a2 * st.ic1 + a3 * v3;
        st.ic1 = 2.0 * v1 - st.ic1;
        st.ic2 = 2.0 * v2 - st.ic2;
        switch (mode)
        {
            case 1:  return (float) v1;                       // band-pass
            case 2:  return (float) (x - k * v1 - v2);        // high-pass
            default: return (float) v2;                       // low-pass
        }
    }

    float lfoShape (int shape, double ph) const noexcept
    {
        switch (shape)
        {
            case 1:  return (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);
            case 2:  return (float) (1.0 - 2.0 * ph);
            case 3:  return ph < 0.5 ? 1.0f : -1.0f;
            case 4:  return shValue;
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

    /** gentle limiter, so a loud resonance or feedback never clips hard */
    static float soft (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= 0.9f) return x;
        return std::copysign (0.9f + 0.1f * std::tanh ((a - 0.9f) / 0.1f), x);
    }

    double sr = 0.0;
    SvfState s1, s2;
    std::vector<float> dlL, dlR, chL, chR, tmpL, tmpR;
    int dlPos = 0, chPos = 0;
    double dlSmooth = -1.0;
    float toneL = 0.0f, toneR = 0.0f;
    double chPhase = 0.0, freePhase = 0.0, ownBeats = 0.0;
    float gate = 1.0f, lfoValue = 0.0f, shValue = 0.0f;
    long long lastShCycle = -1;
    juce::Random rng { 1234 };
    juce::Reverb reverb;
};

} // namespace fx

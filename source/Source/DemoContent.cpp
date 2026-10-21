// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#include "DemoContent.h"

namespace ft
{

namespace
{
    constexpr double kRate = 44100.0;
    constexpr double kTwoPi = juce::MathConstants<double>::twoPi;

    SampleData::Ptr makeData (int frames, const std::function<float (int)>& fn)
    {
        SampleData::Ptr d = new SampleData();
        d->sampleRate = kRate;
        d->bitDepth = 16;
        d->buffer.setSize (1, frames);
        auto* w = d->buffer.getWritePointer (0);
        for (int i = 0; i < frames; ++i) w[i] = juce::jlimit (-1.0f, 1.0f, fn (i));
        return d;
    }

    void set (Instrument& inst, const char* name, SampleData::Ptr d, int volume = 64)
    {
        inst.name = name;
        inst.sourcePath = {};
        inst.data = d;
        inst.params = InstrumentParams();
        inst.params.volume = volume;
        inst.resetPoints();
    }

    void setLoop (Instrument& inst, int loopStart, int loopEnd)
    {
        inst.params.loop = true;
        inst.params.loopStart = loopStart;
        inst.params.loopEnd = loopEnd;
        inst.sanitise();
    }
}

void createDemoInstruments (std::array<Instrument, kNumInstruments>& in)
{
    juce::Random rng (1234);

    // KICK: pitch-swept sine, 0.5 s (22050 frames, as in the mockup)
    {
        double phase = 0;
        set (in[1], "KICK.WAV", makeData (22050, [&] (int i)
        {
            const double t = i / kRate;
            const double f = 45.0 + 110.0 * std::exp (-t * 30.0);
            phase += kTwoPi * f / kRate;
            return (float) (std::sin (phase) * std::exp (-t * 7.0) * 0.95);
        }));
    }

    // SNARE: tone + noise
    set (in[2], "SNARE.WAV", makeData (13000, [&] (int i)
    {
        const double t = i / kRate;
        const double tone = std::sin (kTwoPi * 190.0 * t) * std::exp (-t * 30.0) * 0.5;
        const double noise = (rng.nextDouble() * 2.0 - 1.0) * std::exp (-t * 14.0) * 0.6;
        return (float) (tone + noise);
    }));

    // HAT: high-passed noise burst
    {
        float prev = 0;
        set (in[3], "HAT.WAV", makeData (4000, [&] (int i)
        {
            const double t = i / kRate;
            const float n = rng.nextFloat() * 2.0f - 1.0f;
            const float hp = n - prev; prev = n;
            return (float) (hp * std::exp (-t * 55.0) * 0.45);
        }), 52);
    }

    // PERC: woody click
    set (in[4], "PERC.WAV", makeData (6000, [&] (int i)
    {
        const double t = i / kRate;
        return (float) ((std::sin (kTwoPi * 820.0 * t) + 0.5 * std::sin (kTwoPi * 1370.0 * t)) * std::exp (-t * 45.0) * 0.6);
    }), 48);

    // Looped single-cycle style waves, pitched so C-4 = middle C (261.63 Hz).
    const int cycle = (int) std::round (kRate / 261.6256);   // ~169 frames

    // BASS: saw, looped
    set (in[5], "BASS.WAV", makeData (cycle * 64, [&] (int i)
    {
        const double ph = (double) (i % cycle) / cycle;
        return (float) ((2.0 * ph - 1.0) * 0.6);
    }), 56);
    setLoop (in[5], cycle * 32, cycle * 64 - 1);
    in[5].params.transpose = -24;

    // CHORD: minor triad of sines
    set (in[6], "CHORD.WAV", makeData (cycle * 240, [&] (int i)
    {
        const double t = i / kRate;
        const double f = 261.6256;
        return (float) ((std::sin (kTwoPi * f * t) + std::sin (kTwoPi * f * 1.189207 * t) + std::sin (kTwoPi * f * 1.498307 * t))
                        * 0.25 * std::exp (-t * 2.5));
    }), 50);

    // LEAD: square, looped
    set (in[7], "LEAD.WAV", makeData (cycle * 64, [&] (int i)
    {
        return (i % cycle) < cycle / 2 ? 0.35f : -0.35f;
    }), 44);
    setLoop (in[7], 0, cycle * 64 - 1);

    // PAD: two detuned saws, ping-pong loop
    set (in[8], "PAD.WAV", makeData (44100, [&] (int i)
    {
        const double t = i / kRate;
        const double a = std::fmod (t * 261.6256, 1.0), b = std::fmod (t * 263.1, 1.0);
        const double env = juce::jmin (1.0, t * 4.0);
        return (float) ((a + b - 1.0) * 0.35 * env);
    }), 40);
    setLoop (in[8], 11025, 44099);
    in[8].params.pingPong = true;

    // FX: falling noise sweep
    {
        float lp = 0;
        set (in[9], "FX.WAV", makeData (44100, [&] (int i)
        {
            const double t = i / kRate;
            const float k = (float) juce::jlimit (0.01, 0.9, 0.9 - t * 0.85);
            lp += k * ((rng.nextFloat() * 2.0f - 1.0f) - lp);
            return (float) (lp * 0.7 * std::exp (-t * 1.5));
        }), 44);
    }

    for (auto& i : in) i.sanitise();
}

void createDemoPatterns (TrackerEngine& engine)
{
    auto note = [] (int semitoneFromC0) { return (uint8_t) (semitoneFromC0 + 1); };
    const uint8_t C3 = note (36), C4 = note (48), D4 = note (50), F4 = note (53), Fs4 = note (54),
                  G4 = note (55), A3 = note (45), F3 = note (41), G3 = note (43), C5 = note (60), Ds4 = note (51);

    auto put = [&engine] (int p, int row, int ch, uint8_t n, uint8_t inst, uint8_t fx = 0, uint8_t prm = 0)
    {
        auto& c = engine.getPattern (p).at (row, ch);
        c.note = n; c.inst = inst; c.fx = fx; c.param = prm;
    };

    for (int p = 0; p < kMaxPatterns; ++p)
    {
        engine.getPattern (p).clear();
        engine.getPattern (p).numRows = kDefaultRows;
    }

    // 00: the groove from the mockup, repeated over 64 rows
    for (int bar = 0; bar < 4; ++bar)
    {
        const int r = bar * 16;
        put (0, r + 0,  0, C4, 1);  put (0, r + 3,  0, D4, 1, 0x0C, 0x30);
        put (0, r + 5,  0, C4, 1);  put (0, r + 8,  0, D4, 1);
        put (0, r + 12, 0, D4, 1, 0x0C, 0x28);
        if (bar == 3) put (0, r + 15, 0, C4, 1, 0x0C, 0x20);

        put (0, r + 4,  1, C4, 2);  put (0, r + 12, 1, C4, 2);
        for (int h = 0; h < 16; h += 2) put (0, r + h, 2, Fs4, 3, 0x0C, (uint8_t) (h % 4 == 0 ? 0x40 : 0x20));
        put (0, r + 6, 3, C4, 4);   put (0, r + 10, 3, D4, 4);  put (0, r + 14, 3, C4, 4, 0x08, 0xC0);
    }

    // 01: same drums, bass line instead of perc
    const uint8_t bassLine[16] = { C3, 0, C3, 0, 0, Ds4 - 12, 0, C3, 0, 0, G3, 0, F3, 0, Ds4 - 12, 0 };
    for (int bar = 0; bar < 4; ++bar)
    {
        const int r = bar * 16;
        put (1, r + 0, 0, C4, 1);  put (1, r + 5, 0, C4, 1);  put (1, r + 8, 0, D4, 1);  put (1, r + 10, 0, C4, 1, 0x0C, 0x30);
        put (1, r + 4, 1, C4, 2);  put (1, r + 12, 1, C4, 2);
        for (int h = 0; h < 16; ++h) put (1, r + h, 2, Fs4, 3, 0x0C, (uint8_t) (h % 2 == 0 ? 0x38 : 0x18));
        for (int s = 0; s < 16; ++s)
        {
            const uint8_t n = bassLine[s];
            if (n) put (1, r + s, 3, (uint8_t) (n + 24), 5);
            else if (s % 4 == 1) put (1, r + s, 3, kNoteOff, 0);
        }
    }

    // 02: breakdown - pad chords, lead arpeggio, sparse kick
    const uint8_t chordRoots[4] = { C4, A3, F3, G3 };
    for (int bar = 0; bar < 4; ++bar)
    {
        const int r = bar * 16;
        put (2, r, 0, C4, 1, 0x0C, 0x28);
        put (2, r, 1, chordRoots[bar], 8);
        put (2, r, 2, chordRoots[bar], 6, 0x00, 0x37);
        for (int s = 2; s < 16; s += 3) put (2, r + s, 3, (uint8_t) (chordRoots[bar] + 12 + (s % 2) * 7), 7, 0x04, 0x46);
    }

    // 03: fill - snare rolls with retrig, rising pitch, FX sweep
    for (int r = 0; r < 64; r += 4) put (3, r, 0, C4, 1);
    for (int r = 0; r < 64; r += 2) put (3, r, 1, (uint8_t) (C4 + r / 8), 2, (uint8_t) (r >= 48 ? 0x0E : 0x00), (uint8_t) (r >= 48 ? 0x93 : 0x00));
    put (3, 0, 2, C4, 9);
    put (3, 32, 2, G4, 9, 0x01, 0x04);
    for (int r = 48; r < 64; ++r) put (3, r, 3, F4, 4, 0x1B, 0x82);
    put (3, 63, 3, C5, 4, 0x0C, 0x40);
}

} // namespace ft

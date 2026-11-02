// SPDX-License-Identifier: AGPL-3.0-or-later
// Graphic EQ - Copyright (C) 2026 elcrispo76

#pragma once

// The DSP of the Graphic EQ, kept free of plugin code so it can be tested.
//
// 10 peaking filters at 60, 170, 310, 600, 800, 1000, 3000, 6000, 13000 and 16000 Hz, each +-12 dB.
// Each filter is as wide as the gap to its neighbours, so together they cover the whole range.
//
// "What you see is what you hear": on an ordinary graphic EQ neighbouring bands add up, so with
// 600, 800 and 1000 Hz all at +6 you get +10 dB, and the sound doesn't follow the knobs. This EQ
// works out (a few rounds of correction) how much each filter must really do so that the sound at
// every band's frequency is exactly what its knob says. Gains glide, so moving a knob never clicks.
// When every knob is at 0 the sound passes through untouched.

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <complex>
#include <algorithm>

namespace eq
{

constexpr int kBands = 10;
inline double bandHz (int i)
{
    static const double f[kBands] = { 60, 170, 310, 600, 800, 1000, 3000, 6000, 13000, 16000 };
    return f[std::clamp (i, 0, kBands - 1)];
}
inline const char* bandLabel (int i)
{
    static const char* t[kBands] = { "60", "170", "310", "600", "800", "1000", "3000", "6000", "13000", "16000" };
    return t[std::clamp (i, 0, kBands - 1)];
}
constexpr float kRange = 12.0f;   // +-12 dB on the knobs

/** How wide band i is, in octaves: the average gap to its neighbours (at least 2/3 octave). */
inline double bandOctaves (int i)
{
    auto gap = [] (int a, int b) { return std::log2 (bandHz (b) / bandHz (a)); };
    double w;
    if (i == 0)              w = gap (0, 1);
    else if (i == kBands - 1) w = gap (kBands - 2, kBands - 1);
    else                     w = 0.5 * (gap (i - 1, i) + gap (i, i + 1));
    return std::clamp (w, 0.66, 2.0);
}

struct Coefs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

/** RBJ peaking filter, bandwidth in octaves. */
inline Coefs peaking (double hz, double gainDb, double octaves, double sr)
{
    Coefs c;
    if (std::abs (gainDb) < 1.0e-4 || hz >= sr * 0.47) return c;
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * juce::MathConstants<double>::pi * hz / sr;
    const double sn = std::sin (w0), cs = std::cos (w0);
    const double alpha = sn * std::sinh (std::log (2.0) / 2.0 * octaves * w0 / sn);
    const double a0 = 1.0 + alpha / A;
    c.b0 = (1.0 + alpha * A) / a0;
    c.b1 = -2.0 * cs / a0;
    c.b2 = (1.0 - alpha * A) / a0;
    c.a1 = -2.0 * cs / a0;
    c.a2 = (1.0 - alpha / A) / a0;
    return c;
}

/** dB of a filter at a frequency. */
inline double responseDb (const Coefs& c, double hz, double sr)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * hz / sr;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    const auto h = (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
    return 20.0 * std::log10 (std::max (1.0e-12, std::abs (h)));
}

/** The gains the 10 filters really need so that the sound at each band's frequency is what its
    knob says (the bands overlap, so they help each other). */
inline std::array<double, kBands> solveGains (const std::array<float, kBands>& knobs, double sr)
{
    std::array<double, kBands> g {};
    for (int i = 0; i < kBands; ++i) g[(size_t) i] = knobs[(size_t) i];
    for (int round = 0; round < 12; ++round)
    {
        std::array<Coefs, kBands> c;
        for (int j = 0; j < kBands; ++j) c[(size_t) j] = peaking (bandHz (j), g[(size_t) j], bandOctaves (j), sr);
        double worst = 0.0;
        for (int i = 0; i < kBands; ++i)
        {
            if (bandHz (i) >= sr * 0.47) continue;
            double r = 0.0;
            for (int j = 0; j < kBands; ++j) r += responseDb (c[(size_t) j], bandHz (i), sr);
            const double err = knobs[(size_t) i] - r;
            worst = std::max (worst, std::abs (err));
            g[(size_t) i] = std::clamp (g[(size_t) i] + 0.75 * err, -24.0, 24.0);
        }
        if (worst < 0.02) break;
    }
    return g;
}

class Engine
{
public:
    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        current.fill (0.0);
        target.fill (0.0);
        lastKnobs.fill (1.0e9f);
        for (auto& b : coefs) b = {};
        reset();
    }

    void reset()
    {
        for (auto& ch : state) for (auto& s : ch) s = {};
    }

    /** Audio thread, once per block: the knobs (-12..+12 dB) and OUTPUT (dB). */
    void setKnobs (const std::array<float, kBands>& knobs, float outputDb) noexcept
    {
        if (knobs != lastKnobs)
        {
            lastKnobs = knobs;
            target = solveGains (knobs, sr);
            allFlat = std::all_of (knobs.begin(), knobs.end(), [] (float k) { return k == 0.0f; });
        }
        outTarget = juce::Decibels::decibelsToGain (outputDb);
    }

    /** The gain of each filter now (after the correction), for the tests. */
    const std::array<double, kBands>& getFilterGains() const noexcept { return target; }

    void process (float* L, float* R, int n) noexcept
    {
        if (sr <= 0.0 || n <= 0) return;
        for (int start = 0; start < n; start += kSub)
        {
            const int len = std::min (kSub, n - start);
            glide();
            const bool bypass = settled && allFlat;
            const float g0 = outGain;
            outGain += (outTarget - outGain) * 0.15f;
            if (std::abs (outGain - outTarget) < 1.0e-5f) outGain = outTarget;
            for (int c = 0; c < 2; ++c)
            {
                float* x = c == 0 ? L : R;
                if (x == nullptr) continue;
                x += start;
                if (! bypass)
                    for (int b = 0; b < kBands; ++b)
                    {
                        if (std::abs (current[(size_t) b]) < 1.0e-4) continue;
                        const auto& k = coefs[(size_t) b];
                        auto& s = state[(size_t) c][(size_t) b];
                        for (int i = 0; i < len; ++i)
                        {
                            const double in = x[i];
                            const double out = k.b0 * in + s.z1;
                            s.z1 = k.b1 * in - k.a1 * out + s.z2;
                            s.z2 = k.b2 * in - k.a2 * out;
                            x[i] = (float) out;
                        }
                    }
                if (g0 != 1.0f || outGain != 1.0f)
                    for (int i = 0; i < len; ++i)
                    {
                        const float g = g0 + (outGain - g0) * (float) i / (float) len;
                        x[i] *= g;
                    }
            }
        }
    }

private:
    static constexpr int kSub = 32;
    struct State { double z1 = 0.0, z2 = 0.0; };

    /** The filter gains glide to where they should be (about 20 ms), coefficients follow. */
    void glide() noexcept
    {
        settled = true;
        for (int b = 0; b < kBands; ++b)
        {
            double& g = current[(size_t) b];
            const double t = target[(size_t) b];
            if (g == t) continue;
            settled = false;
            g += (t - g) * 0.12;
            if (std::abs (g - t) < 0.005) g = t;
            coefs[(size_t) b] = peaking (bandHz (b), g, bandOctaves (b), sr);
            if (std::abs (g) < 1.0e-4) state[0][(size_t) b] = state[1][(size_t) b] = {};
        }
    }

    double sr = 0.0;
    std::array<double, kBands> current {}, target {};
    std::array<float, kBands> lastKnobs {};
    std::array<Coefs, kBands> coefs {};
    std::array<std::array<State, kBands>, 2> state {};
    bool allFlat = true, settled = true;
    float outGain = 1.0f, outTarget = 1.0f;
};

} // namespace eq

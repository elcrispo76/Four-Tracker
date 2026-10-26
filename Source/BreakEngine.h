// SPDX-License-Identifier: AGPL-3.0-or-later
// Breakbeat Processor - Copyright (C) 2026 elcrispo76

#pragma once

// The DSP of the Breakbeat Processor, kept free of plugin code so it can be tested.
//
// The sound coming through the channel is recorded all the time into a loop that is
// SLICE long (8, 16, 32 or 64 rows = 1/2, 1, 2 or 4 bars), cut into four quarters. The
// eight steps cover STEPS (at most SLICE) and repeat inside the slice.
//
// It works like a tracker playing a sample: there is a play head that runs through the
// slice. Normally it runs along with the song, so you hear the channel as it is.
//
//   -     nothing new: whatever is playing goes on
//   1/4.. 4/4   RETRIGGER: the play head jumps to the start of that quarter and plays on
//               from there (like putting the sample in again with 9xx offset), through
//               the empty steps after it, until the next retrigger or the next slice
//   REV   this step plays backwards what would have played
//   STOP  silence for this step
//   BIT   bit crusher on what is playing, for this step
//
// At the start of every slice the play head is back with the song. A part of the slice
// that hasn't been heard yet this time round is taken from the last time round.
//
// READ AHEAD: the plugin reports SLICE as latency, so a DAW that compensates for plugin
// latency hands it the whole slice before it is heard; every step then plays from the same,
// complete time round, also the first time.
//
// Every jump is cross-faded over SMOOTH ms, so there are no clicks. When the host is
// stopped the input passes straight through.

#include <array>
#include <cmath>
#include <vector>
#include <algorithm>

namespace bb
{

enum Action { Thru = 0, Q1, Q2, Q3, Q4, Rev, Stop, Bit, NumActions };
constexpr int kNumSteps = 8;

inline const char* actionName (int a)
{
    static const char* names[] = { "-", "1/4", "2/4", "3/4", "4/4", "REV", "STOP", "BIT" };
    return a >= 0 && a < NumActions ? names[a] : "-";
}

struct Settings
{
    std::array<int, kNumSteps> steps {};   // Action per step
    double lengthBeats = 4.0;              // SLICE: 2, 4, 8 or 16 beats
    double stepsBeats = 4.0;               // what the 8 steps cover (at most lengthBeats)
    int bits = 6;                          // BIT: 1..16
    int rate = 6;                          // BIT: hold every sample this many samples (1 = off)
    double smoothMs = 3.0;                 // cross-fade
    float mix = 1.0f;                      // 0..1
    bool readAhead = false;                // latency = SLICE, read the whole slice first
};

struct Transport
{
    bool playing = false;
    double bpm = 120.0;
    double ppq = 0.0;                      // quarter notes since the song start, at the block's first sample
    double beatsPerSample = 0.0;           // how fast ppq moves (0 = from bpm); lets a host clock be followed smoothly
};

class Engine
{
public:
    static constexpr double kMinBpm = 50.0;
    static constexpr double kMaxBeats = 16.0;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // four bars (16 beats) at the slowest tempo, plus room
        cap = (int) std::ceil (sr * 60.0 / kMinBpm * kMaxBeats) + 16;
        for (auto& b : bufL) b.assign ((size_t) cap, 0.0f);
        for (auto& b : bufR) b.assign ((size_t) cap, 0.0f);
        dlyL.assign ((size_t) cap, 0.0f);
        dlyR.assign ((size_t) cap, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& b : bufL) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& b : bufR) std::fill (b.begin(), b.end(), 0.0f);
        std::fill (dlyL.begin(), dlyL.end(), 0.0f);
        std::fill (dlyR.begin(), dlyR.end(), 0.0f);
        dlyPos = 0;
        rec = 0;
        recCycle = -1;
        lastWrite = -1;
        cur = {};
        fade = {};
        fadeLeft = 0;
        holdCount = 0;
        holdL = holdR = 0.0f;
        step = -1;
    }

    /** Samples of latency READ AHEAD needs at this tempo and SLICE (0 when it is off). */
    int latencyFor (double bpm, const Settings& s) const noexcept
    {
        if (! s.readAhead || cap <= 0) return 0;
        const double spb = sr * 60.0 / std::max (kMinBpm, bpm > 1.0 ? bpm : 120.0);
        return std::max (8, (int) std::min ((double) cap - 2.0, std::clamp (s.lengthBeats, 1.0, kMaxBeats) * spb));
    }

    /** The step that is playing now, or -1 when the host is stopped. */
    int currentStep() const noexcept { return step; }

    void process (float* L, float* R, int n, const Transport& t, const Settings& s) noexcept
    {
        if (cap <= 0) return;
        const int fadeLen = std::max (1, (int) (s.smoothMs * 0.001 * sr));
        const bool run = t.playing && t.bpm >= 1.0;
        const double lenBeats = std::clamp (s.lengthBeats, 1.0, kMaxBeats);
        const double spanBeats = std::clamp (s.stepsBeats, 0.5, lenBeats);

        double spb = 0.0, q = 0.0, st = 1.0, stepBeats = 1.0;
        int loopLen = 1;
        if (run)
        {
            spb = sr * 60.0 / std::max (kMinBpm, t.bpm);                      // samples per beat
            loopLen = std::max (8, (int) std::min ((double) cap - 2.0, lenBeats * spb));
            q = loopLen / 4.0;
            stepBeats = spanBeats / kNumSteps;
            st = stepBeats / lenBeats * loopLen;                             // one step, in loop samples
            lastQ = q; lastLoopLen = loopLen; lastSt = st;
        }
        else
        {
            q = lastQ; loopLen = lastLoopLen; st = lastSt;    // so a fade-out after the host stops reads the right place
        }

        ahead = s.readAhead;
        const int delay = ahead ? std::min (cap - 1, loopLen) : 0;
        const double rate = t.beatsPerSample > 0.0 ? t.beatsPerSample : (run ? 1.0 / spb : 0.0);

        for (int i = 0; i < n; ++i)
        {
            const float rawL = L[i], rawR = R != nullptr ? R[i] : rawL;
            // READ AHEAD: what is heard is the input from one SLICE ago
            dlyL[(size_t) dlyPos] = rawL;
            dlyR[(size_t) dlyPos] = rawR;
            int rp = dlyPos - delay;
            if (rp < 0) rp += cap;
            if (++dlyPos >= cap) dlyPos = 0;
            const float inL = ahead ? dlyL[(size_t) rp] : rawL;
            const float inR = ahead ? dlyR[(size_t) rp] : rawR;

            Voice v;
            if (run)
            {
                const double ppq = t.ppq + i * rate;
                // record; a new time round swaps the two buffers, so the last round stays whole
                const long long cycle = (long long) std::floor (ppq / lenBeats);
                if (cycle != recCycle) { rec ^= 1; recCycle = cycle; lastWrite = -1; }
                const double cyc = wrap (ppq, lenBeats);
                const int w = std::min (loopLen - 1, (int) (cyc / lenBeats * loopLen + 1.0e-6));
                // rounding can skip an index now and then: fill the gap so no old sample is left in it
                int from = w;
                if (lastWrite >= 0 && w > lastWrite + 1 && w - lastWrite < 64) from = lastWrite + 1;
                for (int k = from; k <= w; ++k)
                {
                    bufL[(size_t) rec][(size_t) k] = rawL;
                    bufR[(size_t) rec][(size_t) k] = rawR;
                }
                lastWrite = w;
                writePos = w;

                const double sp = wrap (ppq, spanBeats) / stepBeats;
                v.step = std::min (kNumSteps - 1, (int) sp);
                v.action = s.steps[(size_t) v.step];
                v.cycle = cycle;
                // where (in the slice) this step began: worked out from the step itself, so it is the
                // same number for every sample of the step
                const double stepStart = cyc - (sp - v.step) * stepBeats;
                v.startPos = std::clamp ((int) (stepStart / lenBeats * loopLen + 1.0e-6), 0, w);

                // the play head at the start of this step
                const bool newStep = v.step != cur.step || v.cycle != cur.cycle || v.startPos != cur.startPos;
                if (v.action >= Q1 && v.action <= Q4)
                {
                    v.head = (v.action - Q1) * q;                           // retrigger
                    if (! newStep && cur.action == v.action) v.head = cur.head;
                }
                else if (! newStep)
                    v.head = cur.head;                                       // same step, other action: keep playing
                else if (v.startPos < 1 || cur.step < 0)
                    v.head = v.startPos;                                     // a new slice: back with the song
                else if (v.step == (cur.step + 1) % kNumSteps)
                    v.head = cur.head + (v.startPos - cur.startPos + (v.startPos < cur.startPos ? loopLen : 0));  // plays on
                else
                    v.head = v.startPos;                                     // the host jumped
            }
            else
            {
                v.step = -1;
                v.action = Thru;
                writePos = -1;
            }

            if (v.step != cur.step || v.action != cur.action || v.head != cur.head || v.startPos != cur.startPos || v.cycle != cur.cycle)
            {
                // fade over from what was heard, unless it simply plays on
                const bool continues = v.step >= 0 && cur.step >= 0 && v.action == Thru && cur.action == Thru
                                       && std::abs ((v.head - v.startPos) - (cur.head - cur.startPos)) < 0.5;
                const bool continuesHead = v.step >= 0 && cur.step >= 0 && (v.action == Thru || v.action == cur.action)
                                           && (cur.action == Thru || (cur.action >= Q1 && cur.action <= Q4))
                                           && std::abs (v.head - (cur.head + offsetIn (cur, loopLen))) < 0.5;
                if (! (continues || continuesHead || (cur.step < 0 && v.step < 0)))
                {
                    fade = cur;
                    fadeLeft = fadeLen;
                }
                cur = v;
            }

            float wl, wr;
            render (cur, loopLen, st, inL, inR, wl, wr);
            if (cur.action == Bit)
            {
                if (s.rate <= 1 || holdCount == 0) { holdL = crush (wl, s.bits); holdR = crush (wr, s.bits); }
                wl = holdL; wr = holdR;
                if (++holdCount >= std::max (1, s.rate)) holdCount = 0;
            }
            if (fadeLeft > 0)
            {
                float fl, fr;
                render (fade, loopLen, st, inL, inR, fl, fr);
                if (fade.action == Bit) { fl = crush (fl, s.bits); fr = crush (fr, s.bits); }
                const float g = (float) fadeLeft / (float) fadeLen;   // 1 -> 0
                wl = wl + (fl - wl) * g;
                wr = wr + (fr - wr) * g;
                --fadeLeft;
            }

            if (s.mix >= 1.0f) { L[i] = wl; if (R != nullptr) R[i] = wr; }
            else
            {
                L[i] = inL + (wl - inL) * s.mix;
                if (R != nullptr) R[i] = inR + (wr - inR) * s.mix;
            }
        }
        step = run ? cur.step : -1;
    }

private:
    struct Voice
    {
        int step = -1;
        int action = Thru;
        long long cycle = 0;
        int startPos = 0;        // slice position (samples) where the step began
        double head = 0.0;       // play head (slice samples) at the start of the step
    };

    static double wrap (double x, double len) noexcept
    {
        double r = std::fmod (x, len);
        return r < 0.0 ? r + len : r;
    }

    /** How far into its step a voice is now. */
    int offsetIn (const Voice& v, int loopLen) const noexcept
    {
        if (writePos < 0) return 0;
        int o = writePos - v.startPos;
        if (o < 0) o += loopLen;
        return o;
    }

    /** Reads the slice at `pos`: this round if that part has been recorded already, else the last round. */
    void readLoop (const Voice& v, double pos, int loopLen, float& l, float& r) const noexcept
    {
        long long p = (long long) std::floor (pos) % loopLen;
        if (p < 0) p += loopLen;
        // (READ AHEAD: everything heard is one slice late, so the last round is complete)
        const bool fresh = ! ahead && v.cycle == recCycle && writePos >= 0 && p <= writePos;
        const int b = fresh ? rec : rec ^ 1;
        l = bufL[(size_t) b][(size_t) p];
        r = bufR[(size_t) b][(size_t) p];
    }

    float crush (float x, int bits) const noexcept
    {
        const float levels = (float) (1 << (std::clamp (bits, 1, 16) - 1));
        return std::round (std::clamp (x, -1.0f, 1.0f) * levels) / levels;
    }

    void render (const Voice& v, int loopLen, double st, float inL, float inR, float& l, float& r) const noexcept
    {
        if (v.step < 0) { l = inL; r = inR; return; }               // host stopped: the input as it is
        const int o = offsetIn (v, loopLen);
        switch (v.action)
        {
            case Stop: l = r = 0.0f; return;
            case Rev:  readLoop (v, v.head + st - 1.0 - o, loopLen, l, r); return;
            default:
            {
                const double pos = v.head + o;
                // playing along with the song: the input itself (bit-exact, and READ AHEAD's delayed input)
                if (std::abs (v.head - v.startPos) < 0.5) { l = inL; r = inR; return; }
                readLoop (v, pos, loopLen, l, r);
                return;
            }
        }
    }

    double sr = 44100.0;
    int cap = 0;
    std::array<std::vector<float>, 2> bufL, bufR;   // [rec] = this round, [rec ^ 1] = the last round
    int rec = 0;
    long long recCycle = -1;
    int lastWrite = -1;
    int writePos = -1;
    Voice cur, fade;
    int fadeLeft = 0;
    int holdCount = 0;
    float holdL = 0.0f, holdR = 0.0f;
    int step = -1;
    double lastQ = 1.0;
    int lastLoopLen = 4;
    double lastSt = 1.0;
    bool ahead = false;
    std::vector<float> dlyL, dlyR;
    int dlyPos = 0;
};

} // namespace bb

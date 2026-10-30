// SPDX-License-Identifier: AGPL-3.0-or-later
// Dynamics Processor - Copyright (C) 2026 elcrispo76

#pragma once

// The DSP of the Dynamics Processor, kept free of plugin code so it can be tested.
// The sound goes  TRANSIENT SHAPER -> COMPRESSOR -> SATURATION -> OUTPUT.
//
//   TRANSIENT  two pairs of envelopes: a fast and a slow one find the hits (ATTACK turns them up or
//              down), a short and a long release find the tail (SUSTAIN turns it up or down).
//              It works on the shape of the sound only, not on its level.
//   COMPRESSOR sidechain filter -> detector (PEAK or RMS) -> threshold, ratio, knee -> attack and
//              release -> makeup -> mix (parallel compression). Left and right together.
//   SATURATION DRIVE into TAPE (round), TUBE (warm, uneven) or CLIP (harder), with 2 x oversampling so
//              it doesn't sound digital, TONE (a low-pass on the result) and MIX. The level is kept:
//              the loudness going in is measured (300 ms) and the result is set to the same.
//
// AUTO: the engine listens to the track (4 seconds of sound, silence doesn't count) and stores a
// small picture of it (the level of every 32 samples). analyse() then sets all three for the
// instrument: the transient shaper from the instrument and how punchy the sound already is (crest
// factor), the compressor's threshold (so the loud parts are squeezed the right amount, with the
// transient shaper in front taken into account), the release (open before the next hit), the
// makeup (the same loudness, peaks under -1 dB), and the drive. Every knob is a whole number.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>
#include <algorithm>

namespace dyn
{

enum Instrument { kick, snare, hihats, percussion, bassline, synthArp, pads, voice, numInstruments };

inline const char* instrumentName (int i)
{
    static const char* n[] = { "KICK", "SNARE", "HIHATS", "PERC", "BASS", "SYNTH/ARP", "PADS", "VOICE" };
    return n[std::clamp (i, 0, (int) numInstruments - 1)];
}

struct Settings
{
    // transient shaper
    bool tsOn = false;
    int tsAttack = 0;        // -100..100 %
    int tsSustain = 0;       // -100..100 %
    // compressor
    bool compOn = true;
    int threshold = -18;     // -60..0 dB
    int ratio = 3;           // index into ratioValue: 1.5 2 3 4 6 8 12 20 LIMIT
    int attack = 89;         // 0..127 -> 0.05 .. 100 ms
    int release = 55;        // 0..127 -> 10 .. 2000 ms
    int knee = 6;            // 0..24 dB
    int makeup = 0;          // -12..+24 dB
    int mix = 100;           // 0..100 % (less = parallel compression)
    int scFilter = 0;        // index into scFilterHz: OFF 40 60 80 100 150 250 400 Hz
    int detect = 0;          // 0 PEAK, 1 RMS
    // saturation
    bool satOn = false;
    int satDrive = 30;       // 0..100 -> 0 .. +24 dB into the curve
    int satMode = 0;         // 0 TAPE, 1 TUBE, 2 CLIP
    int satTone = 100;       // 0..100 -> low-pass 2 kHz .. 20 kHz (100 = open)
    int satMix = 100;        // 0..100 %
    // output
    int outputDb = 0;        // -24..+6
};

// ---- value mapping (also used for the knob texts) -------------------------------------
constexpr int kNumRatios = 9, kNumScFilters = 8;
inline double ratioValue (int i)  { static const double r[] = { 1.5, 2, 3, 4, 6, 8, 12, 20, 100 }; return r[std::clamp (i, 0, kNumRatios - 1)]; }
inline double scFilterHz (int i)  { static const double f[] = { 0, 40, 60, 80, 100, 150, 250, 400 }; return f[std::clamp (i, 0, kNumScFilters - 1)]; }
inline double attackMs (int v)    { return 0.05 * std::pow (2000.0, std::clamp (v, 0, 127) / 127.0); }
inline double releaseMs (int v)   { return 10.0 * std::pow (200.0, std::clamp (v, 0, 127) / 127.0); }
inline int    attackParam (double ms)  { return std::clamp ((int) std::lround (127.0 * std::log (ms / 0.05) / std::log (2000.0)), 0, 127); }
inline int    releaseParam (double ms) { return std::clamp ((int) std::lround (127.0 * std::log (ms / 10.0) / std::log (200.0)), 0, 127); }
inline double driveDb (int v)     { return std::clamp (v, 0, 100) * 0.24; }
inline double toneHz (int v)      { return 2000.0 * std::pow (10.0, std::clamp (v, 0, 100) / 100.0); }

/** dB of gain reduction (0 or more) for a level in dB: the classic soft-knee curve. */
inline double reductionDb (double levelDb, double threshold, double ratio, double knee) noexcept
{
    const double over = levelDb - threshold;
    const double slope = 1.0 - 1.0 / ratio;
    if (knee > 0.0 && 2.0 * std::abs (over) <= knee)
        return slope * (over + knee * 0.5) * (over + knee * 0.5) / (2.0 * knee);
    return over > 0.0 ? slope * over : 0.0;
}

/** The saturation curves: slope 1 at 0, so quiet sound goes through at the same level. */
inline double shape (int mode, double x) noexcept
{
    switch (mode)
    {
        case 1:   // TUBE: uneven, the top bends earlier than the bottom (even harmonics, warm)
        {
            static const double t = std::tanh (0.3), d = 1.0 - t * t;
            return (std::tanh (x + 0.3) - t) / d;
        }
        case 2:   // CLIP: a cubic, flat from 1.5 up
        {
            const double u = std::clamp (x / 1.5, -1.0, 1.0);
            return 1.5 * (u - u * u * u / 3.0);
        }
        default:  return std::tanh (x);   // TAPE
    }
}

/** What each instrument wants: the starting point of every knob, and how hard AUTO squeezes. */
struct Profile
{
    int ratio; double attackMs, releaseMs; int knee, scFilter, detect;
    double targetGr;         // dB of gain reduction on the loud parts
    bool fastRelease;        // AUTO may shorten the release when the hits come fast
    int tsAttack, tsSustain; // transient shaper
    int satMode, satDrive, satTone;
};

inline const Profile& profile (int instrument)
{
    static const Profile p[] = {
        //  ratio     att  rel   knee sc det  GR   fast   ts att sus   sat mode drive tone
        { 3 /*4:1*/,  15,   80,  3,  0, 0,  6.0, true,    30, -10,     0, 25,  80 },   // KICK: click up, short tail, tape
        { 3 /*4:1*/,   5,  120,  4,  4, 0,  5.0, true,    40,  10,     1, 20,  90 },   // SNARE: crack, a bit more body, tube
        { 2 /*3:1*/,   1,   50,  6,  6, 0,  3.0, true,    15, -25,     0, 10, 100 },   // HIHATS: tight and gentle
        { 3 /*4:1*/,   3,   90,  4,  4, 0,  4.0, true,    30, -15,     0, 15, 100 },   // PERCUSSION
        { 3 /*4:1*/,  10,  150,  6,  0, 1,  5.0, true,    10,  10,     1, 35,  70 },   // BASSLINE: even, warm and growling
        { 2 /*3:1*/,   5,  100,  6,  3, 0,  4.0, true,    15,   0,     0, 20,  90 },   // SYNTH / ARP
        { 1 /*2:1*/,  30,  400, 10,  3, 1,  3.0, false,  -20,  20,     0, 15,  80 },   // PADS: softer start, longer tail
        { 2 /*3:1*/,   5,  200,  8,  4, 1,  6.0, false,    0,   0,     1, 20,  90 },   // VOICE: even, a little tube
    };
    return p[std::clamp (instrument, 0, (int) numInstruments - 1)];
}

struct AutoResult
{
    bool ok = false;
    int threshold = -18, makeup = 0, release = 55;
    int tsAttack = 0, tsSustain = 0, satDrive = 30;
    double gainReduction = 0.0;   // dB on the loud parts with the new settings
    double crestDb = 0.0;         // peak to RMS of the track
    int hits = 0;                 // hits (onsets) heard
    bool peakLimited = false;     // makeup held back so the peaks stay under -1 dB
    double seconds = 0.0;         // seconds of sound heard
};

// ---------------------------------------------------------------------------------------
/** The transient shaper's envelopes, the same at the sample rate and in AUTO's picture. */
struct TransientDetector
{
    struct Coefs { double levelR, slowA, slowR, longA, longR; };
    static Coefs coefsFor (double stepSeconds)
    {
        auto c = [stepSeconds] (double ms) { return std::exp (-stepSeconds / (ms * 0.001)); };
        return { c (50.0), c (15.0), c (50.0), c (1.0), c (400.0) };
    }

    double level = 0.0, slow = 0.0, longE = 0.0;

    /** Gain in dB for a level d (linear, 0 or more). The level follows the peaks at once and
        falls in 50 ms (so the wiggles of a wave don't count as hits); the slow envelope is 15 ms
        behind it going up (the difference = the hit), the long one holds 400 ms (the difference
        to the level = the tail). */
    double gainDb (double d, const Coefs& k, double attackPct, double sustainPct) noexcept
    {
        level = d > level ? d : d + (level - d) * k.levelR;
        auto follow = [this] (double& e, double a, double r) { e = level + (e - level) * (level > e ? a : r); };
        follow (slow, k.slowA, k.slowR);
        follow (longE, k.longA, k.longR);
        auto db = [] (double v) { return 20.0 * std::log10 (v + 1.0e-9); };
        const double hit = std::clamp (db (level) - db (slow), 0.0, 18.0);
        const double tail = std::clamp (db (longE) - db (level), 0.0, 24.0);
        const double gate = std::clamp ((db (longE) + 60.0) / 12.0, 0.0, 1.0);   // don't lift the noise floor
        return std::clamp (attackPct * 0.01 * hit + sustainPct * 0.01 * tail * 0.5 * gate, -18.0, 18.0);
    }
};

// ---------------------------------------------------------------------------------------
class Saturator
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        blockMax = std::max (32, maxBlock);
        os = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        os->initProcessing ((size_t) blockMax);
        inLevel.assign ((size_t) blockMax, 0.0);
        levelCoef = std::exp (-1.0 / (0.3 * sr));
        reset();
    }
    void reset()
    {
        if (os != nullptr) os->reset();
        lp = {}; lp2 = {}; dcX = {}; dcY = {};
        msIn = msOut = 0.0;
        lastDrive = -1; lastMode = -1;
    }
    float getLatency() const { return os != nullptr ? os->getLatencyInSamples() : 0.0f; }

    void process (float* L, float* R, int n, const Settings& s) noexcept
    {
        if (os == nullptr) return;
        if (s.satDrive != lastDrive || s.satMode != lastMode)
        {
            lastDrive = s.satDrive; lastMode = s.satMode;
            g = std::pow (10.0, driveDb (s.satDrive) / 20.0);
            norm = 1.0 / g;                                    // quiet sound: the same level (the curve's slope is 1 at 0)
        }
        const bool toneOn = s.satTone < 100;
        const double a = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * toneHz (s.satTone) / (2.0 * sr));
        const double mix = std::clamp (s.satMix, 0, 100) / 100.0;
        const bool tube = s.satMode == 1;

        for (int start = 0; start < n; start += blockMax)
        {
            const int len = std::min (blockMax, n - start);
            float* ch[2] = { L + start, R != nullptr ? R + start : nullptr };
            const int nch = R != nullptr ? 2 : 1;
            // the level going in (300 ms), so the level coming out can be made the same
            for (int i = 0; i < len; ++i)
            {
                double e = 0.0;
                for (int c = 0; c < nch; ++c) e += (double) ch[c][i] * ch[c][i];
                e /= nch;
                msIn = e + (msIn - e) * levelCoef;
                inLevel[(size_t) i] = msIn;
            }
            juce::dsp::AudioBlock<float> block (ch, (size_t) nch, (size_t) len);
            auto up = os->processSamplesUp (block);
            for (size_t c = 0; c < up.getNumChannels(); ++c)
            {
                float* x = up.getChannelPointer (c);
                double& lpc = lp[c];
                double& lpc2 = lp2[c];
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                {
                    const double in = x[i];
                    double y = shape (s.satMode, in * g) * norm;
                    if (toneOn) { lpc += (y - lpc) * a; lpc2 += (lpc - lpc2) * a; y = lpc2; }
                    x[i] = (float) (in + (y - in) * mix);
                }
            }
            os->processSamplesDown (block);
            if (tube)   // the uneven curve makes a little DC: take it away
                for (int c = 0; c < nch; ++c)
                {
                    float* x = ch[c];
                    for (int i = 0; i < len; ++i)
                    {
                        const double y = x[i] - dcX[(size_t) c] + 0.9995 * dcY[(size_t) c];
                        dcX[(size_t) c] = x[i]; dcY[(size_t) c] = y;
                        x[i] = (float) y;
                    }
                }
            // the same level out as in: the saturation changes the sound, not how loud it is
            for (int i = 0; i < len; ++i)
            {
                double e = 0.0;
                for (int c = 0; c < nch; ++c) e += (double) ch[c][i] * ch[c][i];
                e /= nch;
                msOut = e + (msOut - e) * levelCoef;
                const double gain = std::clamp (std::sqrt ((inLevel[(size_t) i] + 1.0e-10) / (msOut + 1.0e-10)), 0.25, 4.0);
                for (int c = 0; c < nch; ++c) ch[c][i] = (float) (ch[c][i] * gain);
            }
        }
    }

private:
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    double sr = 44100.0;
    int blockMax = 512;
    int lastDrive = -1, lastMode = -1;
    double g = 1.0, norm = 1.0;
    std::array<double, 2> lp {}, lp2 {}, dcX {}, dcY {};
    std::vector<double> inLevel;
    double msIn = 0.0, msOut = 0.0, levelCoef = 0.999;
};

// ---------------------------------------------------------------------------------------
class Engine
{
public:
    static constexpr int kChunk = 32;               // AUTO stores one level per 32 samples
    static constexpr double kListenSeconds = 4.0;   // seconds of sound AUTO wants
    static constexpr double kMaxListenSeconds = 30.0;

    enum ListenState { idle = 0, listening = 1, heard = 2 };

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        chunks.assign ((size_t) (kMaxListenSeconds * sr / kChunk) + 4, {});
        tsCoefs = TransientDetector::coefsFor (1.0 / sr);
        sat.prepare (sampleRate, maxBlock);
        reset();
    }

    void reset()
    {
        hp = {}; hpA = {};
        ts = {};
        msEnv = 0.0;
        grDb = 0.0;
        lastFilter = -1;
        chunkN = 0;
        cur = {};
        sat.reset();
    }

    float getLatency() const { return sat.getLatency(); }

    // ---- AUTO ---------------------------------------------------------------------------
    /** Start listening (from any thread). The audio thread fills the picture; when it has
        enough, getListenState() says heard and analyse() can be called on another thread. */
    void startListening() noexcept   { pendingStart = true; listenState = listening; }
    void cancelListening() noexcept  { listenState = idle; }
    int  getListenState() const noexcept { return listenState.load(); }
    /** 0..1 how much of the sound AUTO wants it has heard. */
    float getListenProgress() const noexcept { return listenProgress.load(); }
    /** After analyse(): back to idle. */
    void finishListening() noexcept  { listenState = idle; }

    // ---- meters (the highest since the last call) -----------------------------------------
    float takeInputPeak() noexcept   { return inPeak.exchange (0.0f); }
    float takeOutputPeak() noexcept  { return outPeak.exchange (0.0f); }
    float takeGainReduction() noexcept { return grMeter.exchange (0.0f); }    // dB, 0 or more
    /** The transient shaper's gain: the most up and the most down since the last call (dB). */
    float takeTransientUp() noexcept   { return tsUp.exchange (0.0f); }
    float takeTransientDown() noexcept { return tsDown.exchange (0.0f); }
    /** The detector level now, in dB (for the dot on the curve). */
    float getDetectorDb() const noexcept { return detDb.load(); }

    void process (float* L, float* R, int n, const Settings& s) noexcept
    {
        if (sr <= 0.0 || n <= 0) return;
        if (s.scFilter != lastFilter) setFilter (s.scFilter);

        const double thr = std::clamp (s.threshold, -60, 0);
        const double ratio = ratioValue (s.ratio);
        const double knee = std::clamp (s.knee, 0, 24);
        const double aCoef = coef (attackMs (s.attack));
        const double rCoef = coef (releaseMs (s.release));
        const double rmsCoef = coef (10.0);
        const bool rms = s.detect == 1;
        const bool filtered = scFilterHz (s.scFilter) > 0.0;
        const float makeup = juce::Decibels::decibelsToGain ((float) std::clamp (s.makeup, -12, 24));
        const float mix = std::clamp (s.mix, 0, 100) / 100.0f;
        const double tsA = std::clamp (s.tsAttack, -100, 100), tsS = std::clamp (s.tsSustain, -100, 100);
        if (pendingStart.exchange (false)) clearPicture();
        const bool recording = listenState.load() == listening && ! chunks.empty();

        float pkIn = 0.0f;
        double grMax = 0.0, levelDb = -120.0, tsMax = 0.0, tsMin = 0.0;

        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R != nullptr ? R[i] : l;
            const float rawPeak = std::max (std::abs (l), std::abs (r));
            pkIn = std::max (pkIn, rawPeak);

            // AUTO's picture is of the sound as it comes in
            if (recording)
            {
                double al = l, ar = r;
                if (filtered) { al = hpA[0].process (l, g, k); ar = hpA[1].process (r, g, k); }
                record (std::max (std::abs (al), std::abs (ar)), 0.5 * ((double) l * l + (double) r * r), rawPeak);
            }

            // ---- TRANSIENT (the envelopes keep running when it is off, so switching on is smooth)
            {
                const double gdb = ts.gainDb (rawPeak, tsCoefs, tsA, tsS);
                if (s.tsOn)
                {
                    const float tg = (float) std::pow (10.0, gdb / 20.0);
                    l *= tg; r *= tg;
                    tsMax = std::max (tsMax, gdb); tsMin = std::min (tsMin, gdb);
                }
            }

            // ---- COMPRESSOR
            if (s.compOn)
            {
                double sl = l, sr2 = r;
                if (filtered) { sl = hp[0].process (l, g, k); sr2 = hp[1].process (r, g, k); }
                const double d = std::max (std::abs (sl), std::abs (sr2));
                msEnv = d * d + (msEnv - d * d) * rmsCoef;
                levelDb = rms ? 10.0 * std::log10 (msEnv + 1e-12) : 20.0 * std::log10 (d + 1e-9);
                const double want = reductionDb (levelDb, thr, ratio, knee);
                grDb = want + (grDb - want) * (want > grDb ? aCoef : rCoef);
                grMax = std::max (grMax, grDb);
                const float gain = (float) std::pow (10.0, -grDb / 20.0) * makeup;
                l += (l * gain - l) * mix;
                r += (r * gain - r) * mix;
            }
            else grDb = 0.0;

            L[i] = l;
            if (R != nullptr) R[i] = r;
        }

        // ---- SATURATION
        if (s.satOn) sat.process (L, R, n, s);

        // ---- OUTPUT
        const float outGain = juce::Decibels::decibelsToGain ((float) std::clamp (s.outputDb, -24, 6));
        float pkOut = 0.0f;
        for (int c = 0; c < (R != nullptr ? 2 : 1); ++c)
        {
            float* x = c == 0 ? L : R;
            for (int i = 0; i < n; ++i)
            {
                x[i] = safe (x[i] * outGain);
                pkOut = std::max (pkOut, std::abs (x[i]));
            }
        }

        raise (inPeak, pkIn);
        raise (outPeak, pkOut);
        raise (grMeter, (float) grMax);
        raise (tsUp, (float) tsMax);
        raise (tsDown, (float) -tsMin);
        detDb = (float) levelDb;
    }

    /** Works out all three for the instrument from what was heard. Not on the audio thread.
        Uses the compressor's ratio, knee, attack, detector and mix from s, and the instrument's
        transient and saturation starting points; finds the rest. */
    AutoResult analyse (const Settings& s, int instrument) const
    {
        AutoResult res;
        const auto& pr = profile (instrument);
        const int total = std::min (chunkN, (int) chunks.size());
        const double chunkSec = kChunk / sr;

        // the parts with sound
        std::vector<char> active ((size_t) total, 0);
        int activeN = 0;
        double sumMs = 0.0, peakMax = 0.0;
        for (int i = 0; i < total; ++i)
            if (chunks[(size_t) i].msIn > 1e-6)                     // above -60 dB
            {
                active[(size_t) i] = 1; ++activeN;
                sumMs += chunks[(size_t) i].msIn;
                peakMax = std::max (peakMax, (double) chunks[(size_t) i].peakIn);
            }
        res.seconds = activeN * chunkSec;
        res.release = s.release;
        res.tsAttack = pr.tsAttack; res.tsSustain = pr.tsSustain; res.satDrive = pr.satDrive;
        if (activeN * chunkSec < 0.5) return res;                     // no sound to go by

        res.crestDb = 20.0 * std::log10 (peakMax + 1e-9) - 10.0 * std::log10 (sumMs / activeN + 1e-12);

        // hits: the level (held over the wiggles of the wave) jumps well above where it was just before
        {
            double held = 0.0, slow = -120.0, sinceLast = 1e9;
            const double holdCoef = std::exp (-chunkSec / 0.04), slowCoef = std::exp (-chunkSec / 0.03);
            for (int i = 0; i < total; ++i)
            {
                held = std::max ((double) chunks[(size_t) i].peakSc, held * holdCoef);
                const double lv = 20.0 * std::log10 (held + 1e-9);
                sinceLast += chunkSec;
                if (active[(size_t) i] && lv > slow + 6.0 && lv > -50.0 && sinceLast > 0.05) { ++res.hits; sinceLast = 0.0; }
                slow = lv + (slow - lv) * slowCoef;
            }
        }
        const double gapMs = res.hits >= 3 ? 1000.0 * res.seconds / res.hits : 1.0e9;

        // ---- the transient shaper: a beat that is already squashed gets more punch, a very spiky
        //      one less; fast hits get a shorter tail so they don't run into each other
        const bool rhythmic = instrument <= percussion || instrument == synthArp || instrument == bassline;
        {
            double a = pr.tsAttack, t = pr.tsSustain;
            if (rhythmic)
            {
                if (res.crestDb < 10.0) a += std::min (30.0, (10.0 - res.crestDb) * 4.0);
                if (res.crestDb > 16.0) a -= std::min (20.0, (res.crestDb - 16.0) * 3.0);
                if (gapMs < 150.0) t -= 10.0;
            }
            res.tsAttack = std::clamp ((int) std::lround (a), -100, 100);
            res.tsSustain = std::clamp ((int) std::lround (t), -100, 100);
            // an already dense sound needs less drive
            res.satDrive = std::clamp (pr.satDrive - (int) std::lround (std::max (0.0, 10.0 - res.crestDb) * 2.0), 0, 100);
        }

        // the transient shaper run over the picture: its gain per chunk
        std::vector<double> tsg ((size_t) total, 1.0);
        {
            TransientDetector det;
            const auto kc = TransientDetector::coefsFor (chunkSec);
            for (int i = 0; i < total; ++i)
                tsg[(size_t) i] = std::pow (10.0, det.gainDb (chunks[(size_t) i].peakIn, kc, res.tsAttack, res.tsSustain) / 20.0);
        }

        // the release: open again before the next hit
        double relMs = releaseMs (s.release);
        if (pr.fastRelease && res.hits >= 3)
        {
            relMs = std::clamp (std::min (pr.releaseMs, 0.6 * gapMs), 25.0, pr.releaseMs);
            res.release = releaseParam (relMs);
            relMs = releaseMs (res.release);
        }

        // a sound that is already squashed (low crest) needs less
        double target = pr.targetGr;
        if (instrument <= percussion) target -= std::clamp ((10.0 - res.crestDb) * 0.5, 0.0, 2.5);

        const double ratio = ratioValue (s.ratio), knee = std::clamp (s.knee, 0, 24);
        const double aCoef = std::exp (-chunkSec / (attackMs (s.attack) * 0.001));
        const double rCoef = std::exp (-chunkSec / (relMs * 0.001));
        const double rmsCoefC = std::exp (-chunkSec / 0.01);
        const bool rms = s.detect == 1;
        const double mix = std::clamp (s.mix, 0, 100) / 100.0;

        // the compressor run over the picture (after the transient shaper): gain reduction per chunk
        std::vector<double> gr ((size_t) total), loud;
        auto simulate = [&] (double thr)
        {
            double y = 0.0, ms = 0.0;
            for (int i = 0; i < total; ++i)
            {
                const auto& c = chunks[(size_t) i];
                const double t2 = tsg[(size_t) i] * tsg[(size_t) i];
                ms = c.msSc * t2 + (ms - c.msSc * t2) * rmsCoefC;
                const double lv = rms ? 10.0 * std::log10 (ms + 1e-12) : 20.0 * std::log10 (c.peakSc * tsg[(size_t) i] + 1e-9);
                const double want = reductionDb (lv, thr, ratio, knee);
                y = want + (y - want) * (want > y ? aCoef : rCoef);
                gr[(size_t) i] = y;
            }
        };
        // "how hard it squeezes": the gain reduction that only the loudest 10 % of the sound gets more than
        auto squeeze = [&]
        {
            loud.clear();
            for (int i = 0; i < total; ++i) if (active[(size_t) i]) loud.push_back (gr[(size_t) i]);
            const size_t at = (size_t) ((double) loud.size() * 0.9);
            std::nth_element (loud.begin(), loud.begin() + (std::ptrdiff_t) std::min (at, loud.size() - 1), loud.end());
            return loud[std::min (at, loud.size() - 1)];
        };

        double lo = -60.0, hi = 0.0;                 // more squeeze at lo
        simulate (hi);
        if (squeeze() >= target) lo = hi;            // even at 0 dB it is squeezed enough
        else
        {
            simulate (lo);
            if (squeeze() <= target) hi = lo;        // very quiet: as low as it goes
            else
                for (int it = 0; it < 24; ++it)
                {
                    const double mid = 0.5 * (lo + hi);
                    simulate (mid);
                    if (squeeze() > target) lo = mid; else hi = mid;
                }
        }
        res.threshold = std::clamp ((int) std::lround (0.5 * (lo + hi)), -60, 0);
        simulate (res.threshold);
        res.gainReduction = squeeze();

        // makeup: the same loudness out as in (with the transient shaper and MIX taken into account)
        auto chunkGain = [&] (int i, double makeupDb, double grDb)
        {
            return tsg[(size_t) i] * (1.0 + (std::pow (10.0, (makeupDb - grDb) / 20.0) - 1.0) * mix);
        };
        auto energyOut = [&] (double makeupDb)
        {
            double e = 0.0;
            for (int i = 0; i < total; ++i)
                if (active[(size_t) i])
                {
                    const double gl = chunkGain (i, makeupDb, gr[(size_t) i]);
                    e += chunks[(size_t) i].msIn * gl * gl;
                }
            return e;
        };
        double mlo = -12.0, mhi = 24.0;
        for (int it = 0; it < 30; ++it)
        {
            const double mid = 0.5 * (mlo + mhi);
            if (energyOut (mid) < sumMs) mlo = mid; else mhi = mid;
        }
        double makeupDb = 0.5 * (mlo + mhi);

        // but the peaks may not go over -1 dB (or over where they were, when they were already hotter)
        {
            double peakOut = 0.0, peakIn = 0.0;
            for (int i = 0; i < total; ++i)
                if (active[(size_t) i])
                {
                    const double pin = chunks[(size_t) i].peakIn;
                    peakIn = std::max (peakIn, pin);
                    const double grStart = std::min (gr[(size_t) i], i > 0 ? gr[(size_t) i - 1] : 0.0);   // the least squeeze in the chunk
                    const double tsMaxG = std::max (tsg[(size_t) i], i > 0 ? tsg[(size_t) i - 1] : 1.0);
                    peakOut = std::max (peakOut, pin * tsMaxG / tsg[(size_t) i] * chunkGain (i, makeupDb, grStart));
                }
            const double ceiling = std::max (20.0 * std::log10 (peakIn + 1e-9), -1.0);
            const double over = 20.0 * std::log10 (peakOut + 1e-9) - ceiling;
            if (over > 0.0) { makeupDb = std::floor (makeupDb - over / std::max (mix, 0.05)); res.peakLimited = true; }
        }
        res.makeup = std::clamp ((int) std::floor (makeupDb + 0.5), -12, 24);
        res.ok = true;
        return res;
    }

private:
    struct Chunk { float peakSc = 0.0f, peakIn = 0.0f; double msSc = 0.0, msIn = 0.0; };
    struct HighPass   // 2-pole state variable filter, high-pass out
    {
        double s1 = 0.0, s2 = 0.0;
        double process (double x, double g, double k) noexcept
        {
            const double hpv = (x - (k + g) * s1 - s2) / (1.0 + g * (k + g));
            const double bp = g * hpv + s1;  s1 = g * hpv + bp;
            const double lp = g * bp + s2;   s2 = g * bp + lp;
            return hpv;
        }
    };

    double coef (double ms) const noexcept { return std::exp (-1.0 / (std::max (ms, 0.01) * 0.001 * sr)); }

    void setFilter (int idx) noexcept
    {
        lastFilter = idx;
        const double f = scFilterHz (idx);
        g = std::tan (juce::MathConstants<double>::pi * std::max (f, 10.0) / sr);
        k = std::sqrt (2.0);   // Q 0.707
        hp = {}; hpA = {};
    }

    void record (double d, double msIn, float peakIn) noexcept
    {
        cur.peakSc = std::max (cur.peakSc, (float) d);
        cur.peakIn = std::max (cur.peakIn, peakIn);
        cur.msSc += d * d;
        cur.msIn += msIn;
        if (++curN < kChunk) return;
        cur.msSc /= kChunk;  cur.msIn /= kChunk;
        if (chunkN < (int) chunks.size()) chunks[(size_t) chunkN++] = cur;
        if (cur.msIn > 1e-6) ++activeChunks;
        cur = {};  curN = 0;
        const int need = (int) (kListenSeconds * sr / kChunk);
        listenProgress = std::min (1.0f, (float) activeChunks / (float) need);
        if (activeChunks >= need || chunkN >= (int) chunks.size()) listenState = heard;
    }

    static void raise (std::atomic<float>& a, float v) noexcept
    {
        float old = a.load();
        while (v > old && ! a.compare_exchange_weak (old, v)) {}
    }

    /** Nothing above full scale: a soft corner over 0.98, nothing touched below it. */
    static float safe (float x) noexcept
    {
        const float a = std::abs (x);
        if (! std::isfinite (a)) return 0.0f;
        if (a <= 0.98f) return x;
        return std::copysign (0.98f + 0.02f * std::tanh ((a - 0.98f) / 0.02f), x);
    }

    void clearPicture() noexcept { chunkN = 0; activeChunks = 0; curN = 0; cur = {}; listenProgress = 0.0f; hpA = {}; }

    double sr = 0.0;
    std::array<HighPass, 2> hp {}, hpA {};
    double g = 0.0, k = 1.414;
    int lastFilter = -1;
    double msEnv = 0.0, grDb = 0.0;
    TransientDetector ts;
    TransientDetector::Coefs tsCoefs {};
    Saturator sat;

    std::vector<Chunk> chunks;
    Chunk cur;
    int curN = 0, chunkN = 0, activeChunks = 0;
    std::atomic<bool> pendingStart { false };
    std::atomic<int> listenState { idle };
    std::atomic<float> listenProgress { 0.0f };

    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, grMeter { 0.0f }, detDb { -120.0f }, tsUp { 0.0f }, tsDown { 0.0f };
};

} // namespace dyn

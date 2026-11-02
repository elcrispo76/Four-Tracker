// SPDX-License-Identifier: AGPL-3.0-or-later
// Dynamics Processor - tests (run on Linux / CI)

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>
#include "DynEngine.h"
#include "PluginProcessor.h"

using namespace juce;

static int failures = 0;
static void CHECK (bool ok, const String& what)
{
    std::cout << (ok ? "ok:   " : "FAIL: ") << what << "\n";
    if (! ok) ++failures;
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    const File outDir = argc > 1 ? File (argv[1]) : File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    constexpr double sr = 48000.0;
    constexpr int block = 480;
    using Signal = std::function<float (long long)>;

    auto run = [&] (dyn::Engine& e, const dyn::Settings& s, double seconds, Signal in)
    {
        const long long total = (long long) (seconds * sr);
        AudioBuffer<float> out (2, (int) total);
        std::vector<float> L ((size_t) block), R ((size_t) block);
        for (long long i = 0; i < total; i += block)
        {
            const int n = (int) std::min<long long> (block, total - i);
            for (int k = 0; k < n; ++k) L[(size_t) k] = R[(size_t) k] = in (i + k);
            e.process (L.data(), R.data(), n, s);
            out.copyFrom (0, (int) i, L.data(), n);
            out.copyFrom (1, (int) i, R.data(), n);
        }
        return out;
    };
    auto sine = [] (double hz, double dbfs) -> Signal
    {
        const float amp = (float) std::pow (10.0, dbfs / 20.0);
        return [hz, amp] (long long i) { return amp * (float) std::sin (2.0 * MathConstants<double>::pi * hz * (double) i / sr); };
    };
    auto peakDb = [] (const AudioBuffer<float>& b, double from, double to)
    {
        float pk = 0.0f;
        for (int i = (int) (from * sr); i < jmin ((int) (to * sr), b.getNumSamples()); ++i) pk = jmax (pk, std::abs (b.getSample (0, i)));
        return 20.0 * std::log10 (pk + 1e-9);
    };
    auto rmsDb = [] (const AudioBuffer<float>& b, double from, double to)
    {
        double e = 0; int n = 0;
        for (int i = (int) (from * sr); i < jmin ((int) (to * sr), b.getNumSamples()); ++i, ++n) e += (double) b.getSample (0, i) * b.getSample (0, i);
        return 10.0 * std::log10 (e / jmax (1, n) + 1e-12);
    };
    // a kick drum: a falling sine with a fast decay, every `gap` seconds, at `dbfs`
    auto kickAt = [] (double dbfs, double gap) -> Signal
    {
        const double amp = std::pow (10.0, dbfs / 20.0);
        return [amp, gap] (long long i)
        {
            const double t = std::fmod ((double) i / sr, gap);
            const double f = 50.0 + 120.0 * std::exp (-t * 40.0);
            const double ph = 2.0 * MathConstants<double>::pi * (50.0 * t + 120.0 / 40.0 * (1.0 - std::exp (-t * 40.0)));
            ignoreUnused (f);
            return (float) (amp * std::sin (ph) * std::exp (-t * 9.0));
        };
    };

    // ---- the compressor -------------------------------------------------------------------
    {
        dyn::Engine e; e.prepare (sr, block);
        dyn::Settings s; s.mix = 0;
        auto in = sine (440.0, -6.0);
        auto o = run (e, s, 0.5, in);
        bool same = true;
        for (int i = 0; i < o.getNumSamples(); ++i) same = same && std::abs (o.getSample (0, i) - in (i)) < 1e-6f;
        CHECK (same, "MIX 0 %: the sound passes through untouched");
    }
    {
        dyn::Engine e; e.prepare (sr, block);
        dyn::Settings s; s.threshold = -20; s.knee = 0;
        auto o = run (e, s, 1.0, sine (440.0, -30.0));
        CHECK (std::abs (peakDb (o, 0.5, 1.0) + 30.0) < 0.05, "under the threshold nothing changes (" + String (peakDb (o, 0.5, 1.0), 2) + " dB)");
    }
    {
        dyn::Engine e; e.prepare (sr, block);
        dyn::Settings s; s.threshold = -20; s.ratio = 3 /*4:1*/; s.knee = 0; s.attack = dyn::attackParam (1.0); s.release = dyn::releaseParam (300.0);
        auto o = run (e, s, 1.5, sine (440.0, -6.0));
        const double outDb = peakDb (o, 1.0, 1.5), want = -20.0 + 14.0 / 4.0;
        CHECK (std::abs (outDb - want) < 1.0, "4:1, 14 dB over -20: out at " + String (outDb, 2) + " dB (should be " + String (want, 1) + ")");
        s.makeup = 6;
        e.reset();
        auto o2 = run (e, s, 1.5, sine (440.0, -6.0));
        CHECK (std::abs (peakDb (o2, 1.0, 1.5) - outDb - 6.0) < 0.2, "MAKEUP +6 dB turns it up 6 dB");
        s.makeup = 0; s.ratio = 8; /*LIMIT*/
        e.reset();
        auto o3 = run (e, s, 1.5, sine (440.0, -6.0));
        CHECK (peakDb (o3, 1.0, 1.5) < -19.0, "LIMIT holds it at the threshold (" + String (peakDb (o3, 1.0, 1.5), 2) + " dB)");
    }
    {
        // attack: a slow attack lets the start of a hit through
        dyn::Engine e; e.prepare (sr, block);
        dyn::Settings s; s.threshold = -30; s.ratio = 7; s.knee = 0; s.attack = dyn::attackParam (20.0); s.release = dyn::releaseParam (50.0);
        Signal burst = [] (long long i) { return i > (long long) (0.2 * sr) ? 0.5f * (float) std::sin (i * 0.05) : 0.0f; };
        auto o = run (e, s, 0.5, burst);
        const double start = peakDb (o, 0.2, 0.202), later = peakDb (o, 0.35, 0.45);
        CHECK (start > later + 10.0, "ATTACK 20 ms: the start of the hit comes through (" + String (start, 1) + " dB), then it is held (" + String (later, 1) + " dB)");
        s.attack = 0;
        e.reset();
        auto o2 = run (e, s, 0.5, burst);
        CHECK (peakDb (o2, 0.2005, 0.205) < start - 10.0, "ATTACK 0.05 ms grabs at once");
    }
    {
        // sidechain filter: a deep bass doesn't make it compress
        dyn::Engine e; e.prepare (sr, block);
        dyn::Settings s; s.threshold = -24; s.ratio = 5; s.knee = 0; s.attack = 10;
        auto o = run (e, s, 1.0, sine (40.0, -6.0));
        s.scFilter = 7; // 400 Hz
        e.reset();
        auto o2 = run (e, s, 1.0, sine (40.0, -6.0));
        CHECK (peakDb (o2, 0.5, 1.0) > peakDb (o, 0.5, 1.0) + 8.0, "SC FILTER 400 Hz: a 40 Hz bass is hardly compressed ("
                 + String (peakDb (o, 0.5, 1.0), 1) + " -> " + String (peakDb (o2, 0.5, 1.0), 1) + " dB)");
    }
    {
        // RMS reacts to loudness: a sine at -6 dB peak is -9 dB RMS, so it squeezes less
        dyn::Engine e; e.prepare (sr, block);
        dyn::Settings s; s.threshold = -20; s.ratio = 8; s.knee = 0; s.attack = 40; s.release = 60;
        auto pk = run (e, s, 1.0, sine (220.0, -6.0));
        s.detect = 1;
        e.reset();
        auto rm = run (e, s, 1.0, sine (220.0, -6.0));
        CHECK (peakDb (rm, 0.5, 1.0) > peakDb (pk, 0.5, 1.0) + 2.0, "RMS detector: less squeeze than PEAK on the same sine");
    }
    {
        // extreme: no NaN, never above full scale
        dyn::Engine e; e.prepare (sr, block);
        dyn::Settings s; s.threshold = 0; s.ratio = 0; s.makeup = 24; s.outputDb = 6; s.attack = 127;
        auto o = run (e, s, 1.0, sine (100.0, 0.0));
        bool ok = true; float pk = 0.0f;
        for (int i = 0; i < o.getNumSamples(); ++i) { const float v = o.getSample (0, i); ok = ok && std::isfinite (v); pk = jmax (pk, std::abs (v)); }
        CHECK (ok && pk <= 1.0f, "+30 dB of gain: no NaN, peak " + String (pk, 3));
    }

    // ---- the transient shaper --------------------------------------------------------------
    {
        auto shaped = [&] (int att, int sus)
        {
            dyn::Engine e; e.prepare (sr, block);
            dyn::Settings s; s.compOn = false; s.tsOn = true; s.tsAttack = att; s.tsSustain = sus;
            return run (e, s, 2.0, kickAt (-12.0, 0.5));
        };
        AudioBuffer<float> dry (2, (int) (2.0 * sr));
        { auto k = kickAt (-12.0, 0.5); for (int i = 0; i < dry.getNumSamples(); ++i) dry.setSample (0, i, k (i)); }
        auto hit  = [&] (const AudioBuffer<float>& b) { return peakDb (b, 1.0, 1.015); };          // the first 15 ms of a kick
        auto tail = [&] (const AudioBuffer<float>& b) { return rmsDb (b, 1.2, 1.45); };           // 200 - 450 ms after it
        const auto up = shaped (100, 0), down = shaped (-100, 0), longer = shaped (0, 100), shorter = shaped (0, -100);
        CHECK (hit (up) > hit (dry) + 3.0 && std::abs (tail (up) - tail (dry)) < 1.5, "TRANSIENT ATTACK +100: the hit up " + String (hit (up) - hit (dry), 1) + " dB, the tail stays");
        CHECK (hit (down) < hit (dry) - 3.0, "TRANSIENT ATTACK -100: the hit down " + String (hit (dry) - hit (down), 1) + " dB");
        CHECK (tail (longer) > tail (dry) + 3.0 && tail (shorter) < tail (dry) - 3.0, "TRANSIENT SUSTAIN +100 / -100: the tail "
                 + String (tail (longer) - tail (dry), 1) + " / " + String (tail (shorter) - tail (dry), 1) + " dB");
        dyn::Engine e; e.prepare (sr, block);
        dyn::Settings s; s.compOn = false; s.tsOn = true; s.tsAttack = 100; s.tsSustain = 100;
        auto o = run (e, s, 1.0, sine (300.0, -12.0));
        CHECK (std::abs (peakDb (o, 0.6, 1.0) + 12.0) < 0.6, "TRANSIENT on a steady tone does nothing once it runs (" + String (peakDb (o, 0.6, 1.0), 2) + " dB)");
    }

    // ---- the saturation -------------------------------------------------------------------
    {
        auto goertzel = [&] (const AudioBuffer<float>& b, double hz, double from, double to)
        {
            const int i0 = (int) (from * sr), i1 = jmin ((int) (to * sr), b.getNumSamples());
            const double w = 2.0 * MathConstants<double>::pi * hz / sr, c = 2.0 * std::cos (w);
            double s1 = 0, s2 = 0;
            for (int i = i0; i < i1; ++i) { const double s0 = b.getSample (0, i) + c * s1 - s2; s2 = s1; s1 = s0; }
            return 2.0 * std::sqrt (jmax (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (i1 - i0);
        };
        auto sat = [&] (int mode, int drive, int tone, double dbfs)
        {
            dyn::Engine e; e.prepare (sr, block);
            dyn::Settings s; s.compOn = false; s.satOn = true; s.satMode = mode; s.satDrive = drive; s.satTone = tone;
            return run (e, s, 1.0, sine (200.0, dbfs));
        };
        const auto tape = sat (0, 60, 100, -10.0), tube = sat (1, 60, 100, -10.0);
        const double t1 = goertzel (tape, 200, 0.3, 1.0), t2 = goertzel (tape, 400, 0.3, 1.0), t3 = goertzel (tape, 600, 0.3, 1.0);
        const double u2 = goertzel (tube, 400, 0.3, 1.0);
        CHECK (t3 > t1 * 0.03 && t2 < t1 * 0.003, "TAPE: odd harmonics (3rd " + String (20.0 * std::log10 (t3 / t1), 1) + " dB), no 2nd");
        CHECK (u2 > goertzel (tube, 200, 0.3, 1.0) * 0.02, "TUBE: uneven, a 2nd harmonic (" + String (20.0 * std::log10 (u2 / goertzel (tube, 200, 0.3, 1.0)), 1) + " dB)");
        CHECK (std::abs (rmsDb (sat (0, 100, 100, -24.0), 0.3, 1.0) - (-27.0)) < 1.0, "SATURATION keeps the level of a quiet sound at full DRIVE");
        const auto dark = sat (2, 80, 0, -10.0), open = sat (2, 80, 100, -10.0);
        CHECK (goertzel (dark, 5000, 0.3, 1.0) < goertzel (open, 5000, 0.3, 1.0) * 0.3, "TONE takes the top off the saturated sound");
        // a loud sine at full drive: no NaN, nothing over full scale
        const auto hot = sat (2, 100, 100, 0.0);
        bool ok = true; float pk = 0.0f;
        for (int i = 0; i < hot.getNumSamples(); ++i) { const float v = hot.getSample (0, i); ok = ok && std::isfinite (v); pk = jmax (pk, std::abs (v)); }
        CHECK (ok && pk <= 1.0f, "CLIP at full drive: no NaN, peak " + String (pk, 3));
        dyn::Engine e; e.prepare (sr, block);
        CHECK (e.getLatency() < 4.0f, "oversampling latency " + String (e.getLatency(), 2) + " samples");
    }

    // ---- AUTO ------------------------------------------------------------------------------
    auto listen = [&] (dyn::Engine& e, const dyn::Settings& s, Signal in, double maxSeconds = 10.0)
    {
        e.startListening();
        std::vector<float> L ((size_t) block), R ((size_t) block);
        long long i = 0;
        while (e.getListenState() == dyn::Engine::listening && i < (long long) (maxSeconds * sr))
        {
            for (int k = 0; k < block; ++k) L[(size_t) k] = R[(size_t) k] = in (i + k);
            e.process (L.data(), R.data(), block, s);
            i += block;
        }
        return (double) i / sr;
    };
    auto withProfile = [] (int inst)
    {
        dyn::Settings s;
        const auto& p = dyn::profile (inst);
        s.ratio = p.ratio; s.attack = dyn::attackParam (p.attackMs); s.release = dyn::releaseParam (p.releaseMs);
        s.knee = p.knee; s.scFilter = p.scFilter; s.detect = p.detect;
        return s;
    };
    {
        dyn::Engine e; e.prepare (sr, block);
        auto s = withProfile (dyn::kick);
        const double took = listen (e, s, kickAt (-3.0, 0.5));
        CHECK (e.getListenState() == dyn::Engine::heard && took < 8.0, "AUTO hears enough of a kick in " + String (took, 1) + " s");
        auto r = e.analyse (s, dyn::kick);
        e.finishListening();
        CHECK (r.ok && r.hits >= 6, "AUTO: " + String (r.hits) + " kicks heard in " + String (r.seconds, 1) + " s, crest " + String (r.crestDb, 1) + " dB");
        CHECK (std::abs (r.gainReduction - dyn::profile (dyn::kick).targetGr) < 3.0, "AUTO KICK: threshold " + String (r.threshold) + " dB, squeeze "
                 + String (r.gainReduction, 1) + " dB, makeup +" + String (r.makeup) + " dB");
        CHECK (r.makeup >= -6 && r.makeup <= 12, "AUTO makeup " + String (r.makeup) + " dB (the transient shaper adds punch, the compressor takes it)");

        // the same kick 12 dB quieter: the threshold follows it down
        dyn::Engine e2; e2.prepare (sr, block);
        listen (e2, s, kickAt (-15.0, 0.5));
        auto r2 = e2.analyse (s, dyn::kick);
        CHECK (r2.ok && std::abs ((r.threshold - r2.threshold) - 12) <= 2, "12 dB quieter kick: threshold " + String (r2.threshold) + " (was " + String (r.threshold) + ")");

        // a kick with headroom: with the new settings the loudness stays about the same
        auto loudness = [&] (double kickDb)
        {
            auto in = kickAt (kickDb, 0.5);
            auto sk = withProfile (dyn::kick);
            dyn::Engine ea; ea.prepare (sr, block);
            listen (ea, sk, in);
            auto ra = ea.analyse (sk, dyn::kick);
            sk.threshold = ra.threshold; sk.makeup = ra.makeup; sk.release = ra.release;
            sk.tsOn = sk.satOn = true; sk.tsAttack = ra.tsAttack; sk.tsSustain = ra.tsSustain;
            sk.satDrive = ra.satDrive; sk.satMode = dyn::profile (dyn::kick).satMode; sk.satTone = dyn::profile (dyn::kick).satTone;
            dyn::Engine eb; eb.prepare (sr, block);
            auto o = run (eb, sk, 4.0, in);
            AudioBuffer<float> dry (1, (int) (4.0 * sr));
            for (int i = 0; i < dry.getNumSamples(); ++i) dry.setSample (0, i, in (i));
            return std::array<double, 5> { rmsDb (dry, 1.0, 4.0), rmsDb (o, 1.0, 4.0), peakDb (dry, 1.0, 4.0), peakDb (o, 1.0, 4.0), (double) ra.peakLimited };
        };
        auto a = loudness (-10.0);
        CHECK (std::abs (a[1] - a[0]) < 1.5 && a[4] == 0.0, "AUTO KICK at -10 dB (all three on) keeps the loudness: in " + String (a[0], 1) + " dB RMS, out " + String (a[1], 1));
        auto b = loudness (-1.5);
        CHECK (b[3] <= -0.3 && b[4] == 1.0, "AUTO KICK at -1.5 dB: makeup is held back so the peaks stay under -1 dB (peak out " + String (b[3], 1) + " dB)");
    }
    {
        // a squashed beat (hardly any difference between hit and tail) gets more punch than the profile
        dyn::Engine e; e.prepare (sr, block);
        auto s = withProfile (dyn::kick);
        Signal squashed = [] (long long i)
        {
            const double t = std::fmod ((double) i / sr, 0.25);
            return (float) (0.4 * std::sin (2.0 * MathConstants<double>::pi * 60.0 * (double) i / sr) * (t < 0.2 ? 1.0 : 0.3));
        };
        listen (e, s, squashed);
        auto r = e.analyse (s, dyn::kick);
        CHECK (r.ok && r.tsAttack > dyn::profile (dyn::kick).tsAttack && r.satDrive < dyn::profile (dyn::kick).satDrive,
               "AUTO on a squashed beat (crest " + String (r.crestDb, 1) + " dB): ATTACK " + String (r.tsAttack) + ", DRIVE " + String (r.satDrive));
    }
    {
        // fast hits: the release is made short enough to open between them
        dyn::Engine e; e.prepare (sr, block);
        auto s = withProfile (dyn::snare);
        listen (e, s, kickAt (-6.0, 0.125));
        auto r = e.analyse (s, dyn::snare);
        CHECK (r.ok && dyn::releaseMs (r.release) <= 0.6 * 125.0 + 5.0, "hits every 125 ms: AUTO sets RELEASE to " + String (dyn::releaseMs (r.release), 0) + " ms");
        // pads keep their slow release
        dyn::Engine e2; e2.prepare (sr, block);
        auto sp = withProfile (dyn::pads);
        listen (e2, sp, kickAt (-6.0, 0.125));
        auto rp = e2.analyse (sp, dyn::pads);
        CHECK (rp.release == sp.release, "PADS keep their slow release");
    }
    {
        // each instrument squeezes its own amount
        dyn::Engine e; e.prepare (sr, block);
        auto s = withProfile (dyn::hihats);
        Signal hats = [] (long long i)
        {
            const double t = std::fmod ((double) i / sr, 0.25);
            const float n = (float) ((((i * 1103515245LL + 12345LL) >> 8) & 0xffff) / 32768.0 - 1.0);
            return n * 0.5f * (float) std::exp (-t * 30.0);
        };
        listen (e, s, hats);
        auto r = e.analyse (s, dyn::hihats);
        CHECK (r.ok && std::abs (r.gainReduction - 3.0) < 1.5, "AUTO HIHATS: squeeze " + String (r.gainReduction, 1) + " dB (gentle, about 3)");
    }
    {
        // silence: AUTO keeps listening; the stored picture is full after 30 s and says no sound
        dyn::Engine e; e.prepare (sr, block);
        dyn::Settings s;
        listen (e, s, [] (long long) { return 0.0f; }, 40.0);
        CHECK (e.getListenState() == dyn::Engine::heard && ! e.analyse (s, dyn::kick).ok, "silence: AUTO gives up and says there was no sound");
    }

    // ---- the plugin ------------------------------------------------------------------
    {
        DynProcessor p;
        p.setPlayConfigDetails (2, 2, sr, block);
        p.prepareToPlay (sr, block);
        MidiBuffer midi;

        p.selectInstrument (dyn::voice);
        auto s = p.readSettings();
        CHECK (p.getInstrument() == dyn::voice && s.detect == 1 && s.scFilter == 4 && s.ratio == 2 && s.satMode == 1 && s.tsOn && s.compOn && s.satOn,
               "VOICE sets RMS, SC 100 Hz, 3:1, TUBE, and switches all three on");
        p.selectInstrument (dyn::kick);
        CHECK (p.readSettings().detect == 0 && p.readSettings().scFilter == 0 && std::abs (dyn::attackMs (p.readSettings().attack) - 15.0) < 1.5,
               "KICK sets PEAK, no SC filter, attack 15 ms");

        // AUTO through the plugin
        p.toggleAuto();
        CHECK (p.isListening(), "AUTO listens");
        auto in = kickAt (-3.0, 0.5);
        AudioBuffer<float> buf (2, block);
        long long i = 0;
        for (int b = 0; b < 1000 && p.isListening(); ++b)
        {
            for (int k = 0; k < block; ++k) buf.setSample (0, k, in (i + k)), buf.setSample (1, k, in (i + k));
            i += block;
            p.processBlock (buf, midi);
            if (p.getEngine().getListenState() == dyn::Engine::heard) p.finishAutoNow();
        }
        s = p.readSettings();
        CHECK (! p.isListening() && p.getLastAutoResult().ok && s.threshold == p.getLastAutoResult().threshold && s.makeup == p.getLastAutoResult().makeup
                 && s.tsAttack == p.getLastAutoResult().tsAttack && s.satDrive == p.getLastAutoResult().satDrive && s.tsOn && s.satOn,
               "AUTO sets THRESHOLD " + String (s.threshold) + " and MAKEUP " + String (s.makeup) + ": " + p.getAutoMessage());
        p.toggleAuto();
        p.toggleAuto();
        CHECK (! p.isListening() && p.getAutoMessage() == "AUTO STOPPED", "a second click stops AUTO");

        CHECK (p.getNumPrograms() >= 15, String (p.getNumPrograms()) + " presets");
        int idx = -1;
        for (int k = 0; k < p.getNumPrograms(); ++k) if (p.getProgramName (k) == "BREAK SMASH") idx = k;
        p.setCurrentProgram (idx);
        s = p.readSettings();
        CHECK (s.mix == 60 && s.threshold == -30 && s.tsAttack == 60 && s.satMode == 2 && s.tsOn && s.satOn, "preset BREAK SMASH sets all three");
        int tape = -1;
        for (int k = 0; k < p.getNumPrograms(); ++k) if (p.getProgramName (k) == "TAPE WARMTH") tape = k;
        p.setCurrentProgram (tape);
        CHECK (! p.readSettings().tsOn && ! p.readSettings().compOn && p.readSettings().satOn, "preset TAPE WARMTH: only the saturation on");
        p.setCurrentProgram (0);
        CHECK (p.readSettings().mix == 100 && p.readSettings().threshold == -18 && p.readSettings().compOn && ! p.readSettings().tsOn && ! p.readSettings().satOn,
               "INIT puts it back (only the compressor on)");

        p.setCurrentProgram (idx);
        MemoryBlock state;
        p.getStateInformation (state);
        DynProcessor p2;
        p2.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (p2.readSettings().mix == 60 && p2.readSettings().tsAttack == 60 && p2.getInstrument() == dyn::percussion, "settings are saved with the song");

        // editor picture, with a kick going through
        for (int k = 0; k < p.getNumPrograms(); ++k) if (p.getProgramName (k) == "BREAK SMASH") p.setCurrentProgram (k);
        p.toggleAuto();
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        for (int b = 0; b < 160; ++b)
        {
            for (int k = 0; k < block; ++k) buf.setSample (0, k, in (i + k)), buf.setSample (1, k, in (i + k));
            i += block;
            p.processBlock (buf, midi);
            if (b % 16 == 0) if (auto* timer = dynamic_cast<Timer*> (ed.get())) timer->timerCallback();
        }
        CHECK (ed != nullptr && ed->getWidth() == 1100 && ed->getHeight() == 690, "editor opens at 1100 x 690");
        if (auto* timer = dynamic_cast<Timer*> (ed.get())) timer->timerCallback();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto f = outDir.getChildFile ("dynamics-processor.png");
        f.deleteFile();
        if (auto os = f.createOutputStream()) PNGImageFormat().writeImageToStream (img, *os);
        CHECK (f.existsAsFile(), "editor picture: " + f.getFullPathName());
        ed.reset();
    }

    std::cout << (failures == 0 ? "ALL PASSED\n" : String (failures) + " FAILED\n");
    return failures == 0 ? 0 : 1;
}

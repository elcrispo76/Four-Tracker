// SPDX-License-Identifier: AGPL-3.0-or-later
// FX Processor 2 - tests (run on Linux / CI)

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>
#include "FxEngine.h"
#include "PluginProcessor.h"

using namespace juce;

static int failures = 0;
static void CHECK (bool ok, const String& what)
{
    std::cout << (ok ? "ok:   " : "FAIL: ") << what << "\n";
    if (! ok) ++failures;
}

struct FakeHead : public AudioPlayHead
{
    bool playing = true;
    double bpm = 120.0, ppq = 0.0;
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        return p;
    }
};

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    const File outDir = argc > 1 ? File (argv[1]) : File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    constexpr double sr = 48000.0;
    constexpr int block = 480;
    const double spb = sr * 60.0 / 120.0;

    // runs the engine over `seconds` of input from `in` (sample index -> value), stereo, 120 BPM playing
    auto run = [&] (fx::Engine& e, const fx::Settings& s, double seconds, std::function<float (long long, int)> in, double bpm = 120.0)
    {
        const long long total = (long long) (seconds * sr);
        AudioBuffer<float> out (2, (int) total);
        std::vector<float> L ((size_t) block), R ((size_t) block);
        for (long long i = 0; i < total; i += block)
        {
            const int n = (int) std::min<long long> (block, total - i);
            for (int k = 0; k < n; ++k) { L[(size_t) k] = in (i + k, 0); R[(size_t) k] = in (i + k, 1); }
            fx::Transport t { true, bpm, (double) i / (sr * 60.0 / bpm) };
            e.process (L.data(), R.data(), n, t, s);
            out.copyFrom (0, (int) i, L.data(), n);
            out.copyFrom (1, (int) i, R.data(), n);
        }
        return out;
    };
    auto sine = [] (double hz, float amp) { return [hz, amp] (long long i, int) { return amp * (float) std::sin (2.0 * MathConstants<double>::pi * hz * (double) i / sr); }; };
    auto rms = [] (const AudioBuffer<float>& b, int ch, double from, double to)
    {
        double e = 0; int n = 0;
        for (int i = (int) (from * sr); i < jmin ((int) (to * sr), b.getNumSamples()); ++i, ++n) e += (double) b.getSample (ch, i) * b.getSample (ch, i);
        return n > 0 ? std::sqrt (e / n) : 0.0;
    };

    {
        fx::Engine e; e.prepare (sr, block);
        fx::Settings s;
        auto in = sine (440.0, 0.5f);
        auto o = run (e, s, 0.5, in);
        bool same = true;
        for (int i = 0; i < o.getNumSamples(); ++i) same = same && o.getSample (0, i) == in (i, 0);
        CHECK (same, "everything off: the sound passes through untouched");
    }

    // a spectrum check: how loud a few frequencies are in white noise after the effect
    auto noise = [] (long long i, int) { return (float) ((((i * 1103515245LL + 12345LL) >> 8) & 0xffff) / 32768.0 - 1.0) * 0.4f; };
    auto band = [&] (const AudioBuffer<float>& b, double hzWanted, double from, double to)
    {
        // Goertzel at one frequency
        const int i0 = (int) (from * sr), i1 = jmin ((int) (to * sr), b.getNumSamples());
        const double w = 2.0 * MathConstants<double>::pi * hzWanted / sr, c = 2.0 * std::cos (w);
        double s1 = 0, s2 = 0;
        for (int i = i0; i < i1; ++i) { const double s0 = b.getSample (0, i) + c * s1 - s2; s2 = s1; s1 = s0; }
        return std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / (i1 - i0);
    };

    {
        // PHASER: notches that move (the spectrum changes over the sweep), stereo apart
        fx::Engine e; e.prepare (sr, block);
        fx::Settings s; s.phaserOn = true; s.phSync = 5; s.phDepth = 100; s.phFeedback = 60; s.phStages = 2;
        auto o = run (e, s, 2.0, sine (700.0, 0.5f));
        double lo = 1e9, hi = 0;
        for (double t = 0.1; t < 1.9; t += 0.05) { const double v = rms (o, 0, t, t + 0.05); lo = jmin (lo, v); hi = jmax (hi, v); }
        CHECK (hi > lo * 3.0, "PHASER: a 700 Hz tone goes in and out of the moving notches (" + String (lo, 3) + " .. " + String (hi, 3) + ")");
        double diff = 0;
        for (int i = 24000; i < 72000; ++i) diff += std::abs (o.getSample (0, i) - o.getSample (1, i));
        CHECK (diff > 100.0, "PHASER: left and right sweep apart (stereo)");
    }

    {
        // FLANGER: comb filter; with feedback it rings; negative feedback flips the teeth
        fx::Engine e; e.prepare (sr, block);
        fx::Settings s; s.flangerOn = true; s.flSync = 0; s.flRate = 0; s.flDepth = 0; s.flDelay = 64; s.flFeedback = 0; s.flMix = 100;
        const double d = fx::flDelayMs (64) * 0.001;                    // ~1.8 ms: notches at odd multiples of 1/(2d)
        auto o = run (e, s, 1.0, noise);
        const double notch = band (o, 1.0 / (2.0 * d), 0.2, 1.0), peak = band (o, 1.0 / d, 0.2, 1.0);
        CHECK (peak > notch * 4.0, "FLANGER: comb filter, a notch at " + String (1.0 / (2.0 * d), 0) + " Hz and a peak at " + String (1.0 / d, 0) + " Hz ("
                 + String (notch, 5) + " vs " + String (peak, 5) + ")");
        s.flFeedback = -80;
        e.reset();
        auto o2 = run (e, s, 1.0, noise);
        CHECK (band (o2, 1.0 / (2.0 * d), 0.2, 1.0) > band (o2, 1.0 / d, 0.2, 1.0), "FLANGER: minus feedback turns the comb round");
    }

    {
        // RING MOD: a 1 kHz tone times a 200 Hz carrier = 800 Hz + 1200 Hz, and no 1 kHz left
        fx::Engine e; e.prepare (sr, block);
        fx::Settings s; s.ringOn = true; s.rmMix = 100; s.rmWave = 0;
        int fv = 0;
        while (fv < 127 && fx::rmFreqHz (fv + 1) <= 200.0) ++fv;
        s.rmFreq = fv;
        const double carrierHz = fx::rmFreqHz (fv);
        auto o = run (e, s, 1.0, sine (1000.0, 0.5f));
        const double orig = band (o, 1000.0, 0.1, 1.0), lower = band (o, 1000.0 - carrierHz, 0.1, 1.0), upper = band (o, 1000.0 + carrierHz, 0.1, 1.0);
        CHECK (lower > orig * 20.0 && upper > orig * 20.0, "RING MOD: 1 kHz x " + String (carrierHz, 0) + " Hz gives " + String (1000.0 - carrierHz, 0)
                 + " and " + String (1000.0 + carrierHz, 0) + " Hz, not 1 kHz (" + String (orig, 5) + " / " + String (lower, 4) + " / " + String (upper, 4) + ")");
        s.rmMix = 50;
        e.reset();
        auto o2 = run (e, s, 1.0, sine (1000.0, 0.5f));
        CHECK (band (o2, 1000.0, 0.1, 1.0) > 0.05, "RING MOD mix 50 %: the dry tone is there too");
    }

    {
        // the LFOs follow the song: 1 bar sine, after 1 beat it is at the top
        fx::Engine e; e.prepare (sr, block);
        fx::Settings s; s.phSync = 5; s.flSync = 5; s.rmSync = 5;
        run (e, s, 0.5, sine (100.0, 0.1f));
        CHECK (std::abs (e.getPhaserLfo() - 1.0f) < 0.01f && std::abs (e.getFlangerLfo() - 1.0f) < 0.01f && std::abs (e.getRingLfo() - 1.0f) < 0.01f,
               "the three LFOs are in time with the song (1 BAR: at the top after 1 beat)");
    }

    {
        // all on, extreme settings: no NaN, nothing above full scale
        fx::Engine e; e.prepare (sr, block);
        fx::Settings s; s.phaserOn = s.flangerOn = s.ringOn = true;
        s.phFeedback = 95; s.phStages = 3; s.phDepth = 127; s.flFeedback = 95; s.flDepth = 127; s.flDelay = 0; s.rmFreq = 127; s.rmLfoDepth = 127;
        s.phSync = 1; s.flSync = 1; s.rmSync = 1; s.outputDb = 6;
        auto o = run (e, s, 3.0, noise);
        bool ok = true; float pk = 0.0f;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < o.getNumSamples(); ++i) { const float v = o.getSample (c, i); ok = ok && std::isfinite (v); pk = jmax (pk, std::abs (v)); }
        CHECK (ok && pk <= 1.0f, "all on, extreme settings: no NaN, peak " + String (pk, 3));
    }

    // ---- the plugin ------------------------------------------------------------------
    {
        FxProcessor p;
        FakeHead head;
        p.setPlayHead (&head);
        p.setPlayConfigDetails (2, 2, sr, block);
        p.prepareToPlay (sr, block);
        AudioBuffer<float> buf (2, block);
        MidiBuffer midi;
        head.bpm = 140.0;
        p.processBlock (buf, midi);
        CHECK (p.isHostTempo() && std::abs (p.getShownBpm() - 140.0) < 1e-9, "follows the host's tempo (140)");
        p.setPlayHead (nullptr);
        p.processBlock (buf, midi);
        CHECK (! p.isHostTempo() && std::abs (p.getShownBpm() - 125.0) < 1e-9, "no host tempo: own TEMPO (125)");
        p.setPlayHead (&head);

        CHECK (p.getNumPrograms() >= 14, String (p.getNumPrograms()) + " presets");
        int idx = -1;
        for (int i = 0; i < p.getNumPrograms(); ++i) if (p.getProgramName (i) == "JET FLANGER") idx = i;
        p.setCurrentProgram (idx);
        auto s = p.readSettings();
        CHECK (s.flangerOn && s.flFeedback == 80 && ! s.phaserOn && ! s.ringOn, "preset JET FLANGER sets the flanger");
        p.setCurrentProgram (0);
        CHECK (! p.readSettings().flangerOn, "INIT switches everything off again");

        p.setCurrentProgram (idx);
        MemoryBlock state;
        p.getStateInformation (state);
        FxProcessor p2;
        p2.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (p2.readSettings().flFeedback == 80 && p2.readSettings().flangerOn, "settings are saved with the song");

        // editor picture, with a few modules on
        for (int i = 0; i < p.getNumPrograms(); ++i) if (p.getProgramName (i) == "PHASE + FLANGE") p.setCurrentProgram (i);
        for (int b = 0; b < 30; ++b) { head.ppq = b * block / spb; p.processBlock (buf, midi); }
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        CHECK (ed != nullptr && ed->getWidth() == 1100 && ed->getHeight() == 414, "editor opens at 1100 x 414");
        if (auto* timer = dynamic_cast<Timer*> (ed.get())) timer->timerCallback();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto f = outDir.getChildFile ("fx-processor-2.png");
        f.deleteFile();
        if (auto os = f.createOutputStream()) PNGImageFormat().writeImageToStream (img, *os);
        CHECK (f.existsAsFile(), "editor picture: " + f.getFullPathName());
        ed.reset();
        p.setPlayHead (nullptr);
    }

    std::cout << (failures == 0 ? "ALL PASSED\n" : String (failures) + " FAILED\n");
    return failures == 0 ? 0 : 1;
}

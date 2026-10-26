// SPDX-License-Identifier: AGPL-3.0-or-later
// FX Processor - tests (run on Linux / CI)

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

    {
        fx::Engine e; e.prepare (sr, block);
        fx::Settings s; s.filterOn = true; s.filterMode = 0; s.cutoff = 40; s.reso = 0;   // ~180 Hz
        const double lowIn = rms (run (e, s, 0.5, sine (60.0, 0.5f)), 0, 0.2, 0.5);
        e.reset();
        const double highIn = rms (run (e, s, 0.5, sine (5000.0, 0.5f)), 0, 0.2, 0.5);
        CHECK (lowIn > 0.3 && highIn < 0.01, "FILTER low-pass at " + String (fx::cutoffHz (40), 0) + " Hz: 60 Hz passes (" + String (lowIn, 3)
                 + "), 5 kHz is gone (" + String (highIn, 4) + ")");
        s.filterMode = 2; s.cutoff = 100;   // high-pass ~4.6 kHz
        e.reset();
        const double lowHp = rms (run (e, s, 0.5, sine (100.0, 0.5f)), 0, 0.2, 0.5);
        CHECK (lowHp < 0.01, "FILTER high-pass: 100 Hz is gone (" + String (lowHp, 4) + ")");
        s.filterMode = 0; s.cutoff = 70; s.reso = 120;
        e.reset();
        const double peak = rms (run (e, s, 0.5, sine (fx::cutoffHz (70), 0.1f)), 0, 0.2, 0.5);
        CHECK (peak > 0.2, "FILTER resonance boosts the cutoff frequency (" + String (peak, 3) + " from 0.071)");
    }

    {
        // PW gate 1/16 at 120 BPM = 125 ms steps; WIDTH 50 %: on for the first 62.5 ms of each
        fx::Engine e; e.prepare (sr, block);
        fx::Settings s; s.pwOn = true; s.pwRate = 2; s.pwWidth = 50; s.pwDepth = 100;
        auto o = run (e, s, 1.0, sine (440.0, 0.5f));
        const double on = rms (o, 0, 0.505, 0.555), off = rms (o, 0, 0.570, 0.620);
        CHECK (on > 0.3 && off < 0.01, "PW 1/16, width 50 %: on " + String (on, 3) + ", off " + String (off, 4));
        s.pwWidth = 25;
        e.reset();
        auto o2 = run (e, s, 1.0, sine (440.0, 0.5f));
        CHECK (rms (o2, 0, 0.545, 0.620) < 0.01 && rms (o2, 0, 0.505, 0.528) > 0.3, "PW width 25 %: on for a quarter of the step, then off");
        s.pwDepth = 50; s.pwWidth = 50;
        e.reset();
        auto o3 = run (e, s, 1.0, sine (440.0, 0.5f));
        const double half = rms (o3, 0, 0.570, 0.620);
        CHECK (std::abs (half - 0.177) < 0.02, "PW depth 50 %: half level between pulses (" + String (half, 3) + ")");
    }

    {
        // LFO, 1 bar sine, to PW: the width moves with the LFO (more sound when it is up)
        fx::Engine e; e.prepare (sr, block);
        fx::Settings s; s.pwOn = true; s.pwRate = 2; s.pwWidth = 50; s.lfoShape = 0; s.lfoSync = 5; s.lfoToPw = 100;
        auto o = run (e, s, 2.0, sine (440.0, 0.5f));
        const double up = rms (o, 0, 0.25, 0.75), down = rms (o, 0, 1.25, 1.75);   // beat 0.5-1.5 (LFO up) vs 2.5-3.5 (down)
        CHECK (up > down * 1.5, "LFO to PW: wider pulses when the LFO is up (" + String (up, 3) + " vs " + String (down, 3) + ")");
        fx::Settings s2; s2.lfoSync = 5; s2.lfoShape = 0;
        fx::Engine e2; e2.prepare (sr, block);
        run (e2, s2, 0.5, sine (100.0, 0.1f));         // 1 beat of a 4-beat sine: at its top
        CHECK (std::abs (e2.getLfo() - 1.0f) < 0.01f, "LFO 1 BAR is in time with the song (sine at the top after 1 beat: " + String (e2.getLfo(), 3) + ")");
    }

    {
        fx::Engine e; e.prepare (sr, block);
        fx::Settings s; s.chorusOn = true; s.chorusMix = 50;
        auto in = sine (440.0, 0.5f);
        auto o = run (e, s, 1.0, in);
        double diff = 0;
        for (int i = 24000; i < 48000; ++i) diff += std::abs (o.getSample (0, i) - o.getSample (1, i));
        CHECK (diff > 100.0, "CHORUS: left and right move apart (stereo)");
    }

    {
        // DELAY 1/8 dotted at 120 BPM = 0.375 s: an echo, then a second one (feedback)
        fx::Engine e; e.prepare (sr, block);
        fx::Settings s; s.delayOn = true; s.delayTime = 2; s.delayFeedback = 50; s.delayMix = 100; s.delayTone = 127;
        auto click = [] (long long i, int) { return i < 48 ? 0.5f : 0.0f; };
        auto o = run (e, s, 1.0, click);
        const double e1 = rms (o, 0, 0.37, 0.39), e2 = rms (o, 0, 0.745, 0.765), gapE = rms (o, 0, 0.1, 0.35);
        CHECK (e1 > 0.01 && e2 > 0.003 && e2 < e1 && gapE < 1e-4, "DELAY 1/8 D: echo at 0.375 s and a softer one at 0.75 s ("
                 + String (e1, 4) + ", " + String (e2, 4) + ")");
        s.delayPing = true;
        e.reset();
        auto p = run (e, s, 1.0, click);
        CHECK (rms (p, 0, 0.37, 0.39) > 0.005 && rms (p, 1, 0.37, 0.39) < 1e-4 && rms (p, 1, 0.745, 0.765) > 0.001,
               "PING-PONG: first echo left, the next one right");
    }

    {
        fx::Engine e; e.prepare (sr, block);
        fx::Settings s; s.reverbOn = true; s.reverbMix = 50; s.reverbSize = 100;
        auto click = [] (long long i, int) { return i < 48 ? 0.5f : 0.0f; };
        auto o = run (e, s, 1.5, click);
        CHECK (rms (o, 0, 0.5, 1.0) > 1e-3, "REVERB: a tail after a click");
    }

    {
        // everything on, extreme settings: no NaN, nothing above full scale
        fx::Engine e; e.prepare (sr, block);
        fx::Settings s; s.filterOn = s.pwOn = s.chorusOn = s.delayOn = s.reverbOn = true;
        s.reso = 127; s.cutoff = 127; s.delayFeedback = 95; s.delayMix = 100; s.reverbMix = 100; s.reverbSize = 127;
        s.lfoToFilter = 63; s.lfoSync = 1; s.lfoShape = 4; s.outputDb = 6;
        auto o = run (e, s, 3.0, [] (long long i, int) { return (float) (((i * 7919) % 2001) - 1000) / 1000.0f; });
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

        CHECK (p.getNumPrograms() >= 12, String (p.getNumPrograms()) + " presets");
        int idx = -1;
        for (int i = 0; i < p.getNumPrograms(); ++i) if (p.getProgramName (i) == "DUB DELAY") idx = i;
        p.setCurrentProgram (idx);
        auto s = p.readSettings();
        CHECK (s.delayOn && s.delayTime == 4 && s.delayFeedback == 70 && s.reverbOn && ! s.pwOn, "preset DUB DELAY sets delay + reverb");
        p.setCurrentProgram (0);
        CHECK (! p.readSettings().delayOn, "INIT switches everything off again");

        p.setCurrentProgram (idx);
        MemoryBlock state;
        p.getStateInformation (state);
        FxProcessor p2;
        p2.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (p2.readSettings().delayFeedback == 70 && p2.readSettings().reverbOn, "settings are saved with the song");

        // editor picture, with a few modules on
        for (int i = 0; i < p.getNumPrograms(); ++i) if (p.getProgramName (i) == "GATE + SWEEP") p.setCurrentProgram (i);
        if (auto* d = p.params.getParameter ("delayOn")) d->setValueNotifyingHost (1.0f);
        if (auto* r = p.params.getParameter ("reverbOn")) r->setValueNotifyingHost (1.0f);
        for (int b = 0; b < 30; ++b) { head.ppq = b * block / spb; p.processBlock (buf, midi); }
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        CHECK (ed != nullptr && ed->getWidth() == 1100 && ed->getHeight() == 648, "editor opens at 1100 x 648");
        if (auto* timer = dynamic_cast<Timer*> (ed.get())) timer->timerCallback();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto f = outDir.getChildFile ("fx-processor.png");
        f.deleteFile();
        if (auto os = f.createOutputStream()) PNGImageFormat().writeImageToStream (img, *os);
        CHECK (f.existsAsFile(), "editor picture: " + f.getFullPathName());
        ed.reset();
        p.setPlayHead (nullptr);
    }

    std::cout << (failures == 0 ? "ALL PASSED\n" : String (failures) + " FAILED\n");
    return failures == 0 ? 0 : 1;
}

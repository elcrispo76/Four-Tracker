// SPDX-License-Identifier: AGPL-3.0-or-later
// Breakbeat Processor - tests (run on Linux / CI)

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
#include "BreakEngine.h"
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
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;

    // FT_BB_DEMO="in.wav;out.wav;bpm": a demo of the patterns, two bars each
    if (auto* demo = std::getenv ("FT_BB_DEMO"))
    {
        auto parts = StringArray::fromTokens (demo, ";", "");
        WavAudioFormat wav;
        std::unique_ptr<AudioFormatReader> rd (wav.createReaderFor (File (parts[0]).createInputStream().release(), true));
        if (rd == nullptr) return 1;
        AudioBuffer<float> b ((int) rd->numChannels, (int) rd->lengthInSamples);
        rd->read (&b, 0, b.getNumSamples(), 0, true, true);
        const double dbpm = parts[2].getDoubleValue(), dsr = rd->sampleRate, dspb = dsr * 60.0 / dbpm;
        bb::Engine e; e.prepare (dsr);
        bb::Settings st;
        const char* order[] = { "CLEAR", "AMEN SHUFFLE", "JUNGLE", "ROLL", "BIT BREAK" };
        for (int i = 0; i < b.getNumSamples(); i += 256)
        {
            const int n = jmin (256, b.getNumSamples() - i);
            const int bar = (int) (i / dspb / 4.0);
            const String want = order[jmin (4, bar / 2)];
            for (auto& pt : BreakbeatProcessor::patterns()) if (want == pt.name) st.steps = pt.steps;
            e.process (b.getWritePointer (0, i), b.getWritePointer (1, i), n, bb::Transport { true, dbpm, i / dspb }, st);
        }
        File out (parts[1]); out.deleteFile();
        std::unique_ptr<AudioFormatWriter> wr (wav.createWriterFor (out.createOutputStream().release(), dsr, 2, 16, {}, 0));
        wr->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return 0;
    }
    const File outDir = argc > 1 ? File (argv[1]) : File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    constexpr double sr = 48000.0;
    constexpr double bpm = 120.0;
    const double spb = sr * 60.0 / bpm;          // 24000 samples per beat
    const int block = 480;

    // Runs the engine over a number of beats. `in` gives the input for an absolute sample index.
    auto run = [&] (bb::Engine& e, bb::Settings s, double fromBeat, double toBeat, std::function<float (long long)> in,
                    bool playing = true)
    {
        std::vector<float> out;
        long long i0 = (long long) std::llround (fromBeat * spb), i1 = (long long) std::llround (toBeat * spb);
        std::vector<float> L ((size_t) block), R ((size_t) block);
        for (long long i = i0; i < i1; i += block)
        {
            const int n = (int) std::min<long long> (block, i1 - i);
            for (int k = 0; k < n; ++k) L[(size_t) k] = R[(size_t) k] = in (i + k);
            bb::Transport t { playing, bpm, (double) i / spb };
            e.process (L.data(), R.data(), n, t, s);
            out.insert (out.end(), L.begin(), L.begin() + n);
        }
        return out;
    };
    auto at = [&] (const std::vector<float>& o, double fromBeat, double beat) { return o[(size_t) ((beat - fromBeat) * spb)]; };

    // a "break": each beat (a quarter of the 1-bar loop) has its own level 0.1 .. 0.4
    auto levels = [&] (long long i) { return 0.1f * (float) (1 + ((long long) (i / spb) % 4)); };

    {
        bb::Engine e; e.prepare (sr);
        bb::Settings s;
        auto o = run (e, s, 0, 2, levels, false);
        bool same = true;
        for (size_t k = 0; k < o.size(); ++k) same = same && o[k] == levels ((long long) k);
        CHECK (same, "host stopped: the input passes through untouched");
        CHECK (e.currentStep() == -1, "host stopped: no step playing");
    }

    {
        bb::Engine e; e.prepare (sr);
        bb::Settings s;                      // LENGTH 1 bar = 16 rows: a quarter = 1 beat, a step = 2 rows
        run (e, s, 0, 4, levels);            // record one bar
        s.steps = { bb::Q3, bb::Q1, bb::Stop, bb::Q2, bb::Thru, bb::Thru, bb::Q4, bb::Thru };
        auto o = run (e, s, 4, 8, levels);
        CHECK (std::abs (at (o, 4, 4.25) - 0.3f) < 1e-6f, "step 1 = 3/4 plays the third quarter, last time round (" + String (at (o, 4, 4.25)) + ")");
        CHECK (std::abs (at (o, 4, 4.75) - 0.1f) < 1e-6f, "step 2 = 1/4 repeats the first quarter (" + String (at (o, 4, 4.75)) + ")");
        CHECK (std::abs (at (o, 4, 5.25)) < 1e-6f, "step 3 = STOP is silent");
        CHECK (std::abs (at (o, 4, 5.75) - 0.2f) < 1e-6f, "step 4 = 2/4 plays the second quarter from its start");
        CHECK (std::abs (at (o, 4, 7.25) - 0.4f) < 1e-6f, "step 7 = 4/4 where the 4th quarter starts plays the input");
        CHECK (e.currentStep() == 7, "the last step is 8 at the end of the bar (16 rows)");
    }

    {
        // a 64-row break (SAMPLE 4 bars) with 16-row steps: 1/4 is always the first 16 rows of the break
        auto rampSample = [&] (long long i) { return (float) (std::fmod ((double) i / spb, 16.0) / 16.0); };   // 0 -> 1 over the break
        bb::Engine e; e.prepare (sr);
        bb::Settings s; s.lengthBeats = 16.0; s.stepsBeats = 4.0;
        run (e, s, 0, 16, rampSample);                       // one whole break
        s.steps[3] = bb::Q1; s.steps[5] = bb::Q4;            // step 4 = row 6 (+16 rows each time the steps repeat)
        auto o = run (e, s, 16, 32, rampSample);
        // second time through the steps inside the break: beats 4..8 (rows 16-31); step 4 starts at beat 5.5
        const float a1 = at (o, 16, 16 + 5.75), a4 = at (o, 16, 16 + 6.75);
        CHECK (std::abs (a1 - 0.25f / 16.0f) < 0.001f, "SAMPLE 64 / STEPS 16: 1/4 on step 4 plays from the very start of the break (" + String (a1, 4) + ")");
        CHECK (std::abs (a4 - 12.25f / 16.0f) < 0.001f, "SAMPLE 64 / STEPS 16: 4/4 plays from row 48 of the break (" + String (a4, 4) + ")");
        CHECK (e.currentStep() == 7, "STEPS 16 rows repeat 4 times over a 64-row sample");
    }

    {
        // READ AHEAD: what is heard is one LENGTH late, and every slice comes from that same time round
        auto perRound = [&] (long long i) { return 0.1f * (float) (1 + (long long) (i / (spb * 4.0))); };   // 0.1, 0.2, 0.3 ...
        bb::Engine e; e.prepare (sr);
        bb::Settings s; s.readAhead = true;
        CHECK (e.latencyFor (120.0, s) == (int) (4 * spb), "READ AHEAD: latency = LENGTH (" + String (e.latencyFor (120.0, s)) + " samples)");
        s.steps[0] = bb::Q3; s.steps[7] = bb::Q1;
        auto o = run (e, s, 0, 12, perRound);
        CHECK (std::abs (at (o, 0, 8.25) - 0.2f) < 1e-6f && std::abs (at (o, 0, 9.25) - 0.2f) < 1e-6f
                 && std::abs (at (o, 0, 11.75) - 0.2f) < 1e-6f,
               "READ AHEAD: steps and the input heard come from the same round ("
                 + String (at (o, 0, 8.25)) + ", " + String (at (o, 0, 9.25)) + ", " + String (at (o, 0, 11.75)) + ")");
        s.readAhead = false;
        CHECK (e.latencyFor (120.0, s) == 0, "READ AHEAD off: no latency");
    }

    {
        // RETRIGGER: 1/4 on step 4 (row 6) restarts the slice there and it plays on through the empty
        // steps after it, like putting the sample in again on row 06 (so the snare later in it is heard)
        bb::Engine e; e.prepare (sr);
        bb::Settings s;
        run (e, s, 0, 4, levels);
        s.steps[3] = bb::Q1;
        auto o = run (e, s, 4, 8, levels);
        CHECK (std::abs (at (o, 4, 5.75) - 0.1f) < 1e-6f && std::abs (at (o, 4, 6.25) - 0.1f) < 1e-6f
                 && std::abs (at (o, 4, 6.75) - 0.2f) < 1e-6f && std::abs (at (o, 4, 7.6) - 0.3f) < 1e-6f,
               "1/4 on row 06 restarts the sample and it plays on (" + String (at (o, 4, 5.75)) + ", " + String (at (o, 4, 6.25)) + ", "
                 + String (at (o, 4, 6.75)) + ", " + String (at (o, 4, 7.6)) + ")");
        s.steps[3] = bb::Thru;
        auto o2 = run (e, s, 8, 12, levels);
        CHECK (std::abs (at (o2, 8, 8.25) - 0.1f) < 1e-6f && std::abs (at (o2, 8, 10.25) - 0.3f) < 1e-6f,
               "the next slice starts with the song again");
    }

    {
        // reverse: input is a ramp inside every step (half a beat, 0 -> 1)
        auto ramp = [&] (long long i) { return (float) std::fmod ((double) i / spb * 2.0, 1.0); };
        bb::Engine e; e.prepare (sr);
        bb::Settings s;
        run (e, s, 0, 4, ramp);
        s.steps[5] = bb::Rev;                // beat 6.5..7
        auto o = run (e, s, 4, 8, ramp);
        CHECK (std::abs (at (o, 4, 6.625) - 0.75f) < 0.001f && std::abs (at (o, 4, 6.875) - 0.25f) < 0.001f,
               "REV plays the step backwards (" + String (at (o, 4, 6.625), 3) + ", " + String (at (o, 4, 6.875), 3) + ")");
    }

    {
        bb::Engine e; e.prepare (sr);
        bb::Settings s;
        s.bits = 2; s.rate = 1;
        s.steps[4] = bb::Bit;                // beat 2..2.5
        auto o = run (e, s, 0, 4, levels);
        CHECK (std::abs (at (o, 0, 2.25) - 0.5f) < 1e-6f, "BIT with 2 bits turns 0.3 into 0.5 (" + String (at (o, 0, 2.25)) + ")");
        s.bits = 16; s.rate = 8;
        auto sine = [&] (long long i) { return 0.5f * (float) std::sin (i * 0.01); };
        auto o2 = run (e, s, 8, 12, sine);
        const size_t k = (size_t) (2.1 * spb);
        int changes = 0;
        std::vector<int> gaps; size_t lastC = k;
        for (size_t j = k + 1; j < k + 801; ++j) if (o2[j] != o2[j - 1]) { ++changes; gaps.push_back ((int) (j - lastC)); lastC = j; }
        if (changes > 101) { for (int gp : gaps) std::cout << gp << " "; std::cout << "\n"; }
        CHECK (changes >= 95 && changes <= 101, "BIT RATE 1/8 holds every value for 8 samples (" + String (changes) + " changes in 800)");
    }

    {
        // no clicks: a sine through a busy pattern changes smoothly at every step
        auto sine = [&] (long long i) { return 0.8f * (float) std::sin (2.0 * MathConstants<double>::pi * 110.0 * (double) i / sr); };
        bb::Engine e; e.prepare (sr);
        bb::Settings s;
        s.steps = { bb::Q1, bb::Q3, bb::Rev, bb::Stop, bb::Q4, bb::Bit, bb::Q2, bb::Rev };
        s.bits = 16; s.rate = 1; s.smoothMs = 3.0;
        auto o = run (e, s, 0, 16, sine);
        float maxJump = 0.0f;
        size_t where = 0;
        for (size_t k = 1; k < o.size(); ++k) if (std::abs (o[k] - o[k - 1]) > maxJump) { maxJump = std::abs (o[k] - o[k - 1]); where = k; }
        if (maxJump >= 0.05f)
        {
            std::cout << "      largest jump at beat " << (double) where / spb << " sample " << where << ":";
            for (size_t k = where - 3; k < where + 3; ++k) std::cout << " " << o[k];
            std::cout << "\n";
        }
        CHECK (maxJump < 0.05f, "SMOOTH 3 ms: no clicks at the step changes (largest jump " + String (maxJump, 4) + ")");

        s.mix = 0.0f;
        auto dry = run (e, s, 16, 18, sine);
        bool same = true;
        for (size_t k = 300; k < dry.size(); ++k) same = same && std::abs (dry[k] - sine ((long long) (16 * spb) + (long long) k)) < 1e-6f;
        CHECK (same, "MIX 0% = the dry input");
    }

    {
        // 32 and 64 rows: a step is 4 and 8 rows (1 and 2 beats)
        bb::Engine e; e.prepare (sr);
        bb::Settings s; s.lengthBeats = 8.0; s.stepsBeats = 8.0;
        run (e, s, 0, 3.6, levels);
        CHECK (e.currentStep() == 3, "STEPS 32 ROWS: step 4 plays at row 14 (" + String (e.currentStep() + 1) + ")");
        bb::Engine e8; e8.prepare (sr);
        bb::Settings s8; s8.lengthBeats = 2.0; s8.stepsBeats = 2.0;
        run (e8, s8, 0, 1.3, levels);
        CHECK (e8.currentStep() == 5, "SLICE + STEPS 8 ROWS: a step is 1 row, step 6 at row 5 (" + String (e8.currentStep() + 1) + ")");
        bb::Engine e2; e2.prepare (sr);
        s.lengthBeats = 16.0; s.stepsBeats = 16.0;
        run (e2, s, 0, 15.0, levels);
        CHECK (e2.currentStep() == 7, "STEPS 64 ROWS: step 8 at row 60 (" + String (e2.currentStep() + 1) + ")");
    }

    // ---- the plugin --------------------------------------------------------------
    {
        BreakbeatProcessor p;
        FakeHead head;
        p.setPlayHead (&head);
        p.setPlayConfigDetails (2, 2, sr, block);
        p.prepareToPlay (sr, block);
        AudioBuffer<float> buf (2, block);
        MidiBuffer midi;
        for (int b = 0; b < 110; ++b)                      // 1.1 s = 2.2 beats
        {
            buf.clear();
            head.ppq = b * block / spb;
            p.processBlock (buf, midi);
        }
        CHECK (p.getCurrentStep() == 4 && std::abs (p.getHostBpm() - 120.0) < 1e-9, "plugin follows the host: step 5 after 2.2 beats at 120 BPM");

        p.setCurrentProgram (1);
        auto* step3 = p.params.getRawParameterValue ("step3");
        CHECK (p.getProgramName (1) == "AMEN SHUFFLE" && roundToInt (step3->load()) == bb::Q3, "patterns: AMEN SHUFFLE sets the steps");

        MemoryBlock state;
        p.getStateInformation (state);
        BreakbeatProcessor p2;
        p2.setStateInformation (state.getData(), (int) state.getSize());
        bool same = true;
        for (int i = 0; i < bb::kNumSteps; ++i)
            same = same && p2.params.getRawParameterValue (BreakbeatProcessor::stepId (i))->load()
                           == p.params.getRawParameterValue (BreakbeatProcessor::stepId (i))->load();
        CHECK (same, "the steps are saved with the song (state round trip)");

        head.playing = false;
        p.processBlock (buf, midi);
        CHECK (p.getCurrentStep() == -1, "host stops: no step");

        // the editor, saved as a picture
        p.setCurrentProgram (11);
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        CHECK (ed != nullptr && ed->getWidth() == 940 && ed->getHeight() == 560, "editor opens at 940 x 560");
        head.playing = true; head.ppq = 2.2;
        p.processBlock (buf, midi);
        if (auto* timer = dynamic_cast<Timer*> (ed.get())) timer->timerCallback();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto f = outDir.getChildFile ("breakbeat-processor.png");
        f.deleteFile();
        if (auto os = f.createOutputStream()) PNGImageFormat().writeImageToStream (img, *os);
        CHECK (f.existsAsFile(), "editor picture: " + f.getFullPathName());
        ed.reset();
        p.setPlayHead (nullptr);
    }

    {
        // a host that sends no tempo or position (no play head): the plugin runs on its own TEMPO,
        // starting with the first sound and stopping after half a second of silence
        BreakbeatProcessor p;
        p.setPlayConfigDetails (2, 2, sr, block);
        p.prepareToPlay (sr, block);
        AudioBuffer<float> buf (2, block);
        MidiBuffer midi;
        auto blocks = [&] (int count, float level)
        {
            for (int b = 0; b < count; ++b)
            {
                for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) buf.setSample (c, i, level * (float) std::sin (i * 0.05));
                p.processBlock (buf, midi);
            }
        };
        blocks (50, 0.0f);
        CHECK (p.isUsingOwnClock() && ! p.hostIsPlaying() && p.getCurrentStep() == -1, "no host tempo: waits for sound");
        // 125 BPM, LENGTH 16 rows: a step = half a beat = 11520 samples at 48 kHz; 2.6 steps = 29952 samples
        blocks (62, 0.5f);
        CHECK (p.hostIsPlaying() && p.getCurrentStep() == 2 && std::abs (p.getHostBpm() - 125.0) < 1e-9,
               "no host tempo: the sound starts the steps at 125 BPM (step " + String (p.getCurrentStep() + 1) + ")");
        blocks (60, 0.0f);                                  // 0.6 s silence
        CHECK (! p.hostIsPlaying() && p.getCurrentStep() == -1, "no host tempo: half a second of silence stops it");
        blocks (5, 0.5f);
        CHECK (p.getCurrentStep() == 0, "no host tempo: the next sound starts again from step 1");
    }

    {
        // a host whose song position only moves in coarse jumps (like some trackers): SYNC SONG stays smooth
        BreakbeatProcessor p;
        FakeHead head;
        p.setPlayHead (&head);
        p.setPlayConfigDetails (2, 2, sr, block);
        p.prepareToPlay (sr, block);
        if (auto* lp = p.params.getParameter ("length")) lp->setValueNotifyingHost (lp->convertTo0to1 (1.0f));   // SLICE 16 rows
        p.applyPattern ({ bb::Thru, bb::Thru, bb::Thru, bb::Q1, bb::Thru, bb::Thru, bb::Thru, bb::Thru });
        AudioBuffer<float> buf (2, block);
        MidiBuffer midi;
        std::vector<float> out;
        long long pos = 0;
        for (int b = 0; b < 400; ++b)                       // 4 s, the host position only moves every row (1/4 beat)
        {
            for (int i = 0; i < block; ++i)
            {
                const float v = 0.5f * (float) std::sin (2.0 * MathConstants<double>::pi * 110.0 * (double) (pos + i) / sr);
                buf.setSample (0, i, v); buf.setSample (1, i, v);
            }
            head.ppq = std::floor ((double) pos / spb * 4.0) / 4.0;
            p.processBlock (buf, midi);
            out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + block);
            pos += block;
        }
        float maxJump = 0.0f;
        for (size_t k = (size_t) (4 * spb); k < out.size(); ++k) maxJump = jmax (maxJump, std::abs (out[k] - out[k - 1]));
        CHECK (maxJump < 0.06f, "SYNC SONG with a coarse host position: no crunch (largest jump " + String (maxJump, 4) + ")");

        // SYNC SOUND: the first sound is row 0, at the host's tempo
        if (auto* sp = p.params.getParameter ("sync")) sp->setValueNotifyingHost (1.0f);
        head.playing = true; head.bpm = 140.0;
        for (int b = 0; b < 250; ++b) { buf.clear(); p.processBlock (buf, midi); }    // 2.5 s silence
        CHECK (p.getCurrentStep() == -1 && p.isUsingOwnClock(), "SYNC SOUND: waits for the first sound");
        for (int b = 0; b < 10; ++b) { for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) buf.setSample (c, i, 0.3f); p.processBlock (buf, midi); }
        CHECK (p.getCurrentStep() == 0 && std::abs (p.getHostBpm() - 140.0) < 1e-9, "SYNC SOUND: starts at step 1 with the host's 140 BPM");
        p.setPlayHead (nullptr);
    }

    {
        // a host with no tempo, but that says where it is in samples (like old trackers): playing from
        // the start again (position back to 0) restarts the steps at the next sound, even without a silence
        struct SampleHead : public AudioPlayHead
        {
            int64 samples = 0;
            Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setTimeInSamples (samples); return p; }
        } head;
        BreakbeatProcessor p;
        p.setPlayHead (&head);
        p.setPlayConfigDetails (2, 2, sr, block);
        p.prepareToPlay (sr, block);
        AudioBuffer<float> buf (2, block);
        MidiBuffer midi;
        auto play = [&] (int count)
        {
            for (int b = 0; b < count; ++b)
            {
                for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) buf.setSample (c, i, 0.4f);
                p.processBlock (buf, midi);
                head.samples += block;
            }
        };
        play (80);
        CHECK (p.getCurrentStep() > 0, "no host tempo: running (step " + String (p.getCurrentStep() + 1) + ")");
        head.samples = 0;                                    // the host plays from the start again
        play (2);
        CHECK (p.getCurrentStep() == 0, "no host tempo: playing from the start again restarts at step 1 (" + String (p.getCurrentStep() + 1) + ")");
        p.setPlayHead (nullptr);
    }

    std::cout << (failures == 0 ? "ALL PASSED\n" : String (failures) + " FAILED\n");
    return failures == 0 ? 0 : 1;
}

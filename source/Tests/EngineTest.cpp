// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

// Offline test: drives the processor with MIDI the way a live player would,
// checks pattern switching, writes the audio to a WAV file and (optionally)
// renders each editor page to PNG.
//
//   FourTrackerTests <outputDir> [--screens]

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::cout << "FAIL: " << msg << "\n"; } else std::cout << "ok:   " << msg << "\n"; } while (0)

struct Runner
{
    FourTrackerProcessor& p;
    AudioBuffer<float> all { 2, 0 };
    int written = 0;
    static constexpr int block = 256;

    void run (int blocks, std::function<void (int, MidiBuffer&)> midiFor = {})
    {
        for (int b = 0; b < blocks; ++b)
        {
            AudioBuffer<float> buf (2, block);
            MidiBuffer midi;
            if (midiFor) midiFor (b, midi);
            p.processBlock (buf, midi);
            all.setSize (2, written + block, true);
            for (int c = 0; c < 2; ++c) all.copyFrom (c, written, buf, c, 0, block);
            written += block;
        }
    }

    /** Runs until the engine reaches the given row (or gives up). */
    bool runUntilRow (int row, int maxBlocks = 4000)
    {
        for (int i = 0; i < maxBlocks; ++i)
        {
            if (p.getEngine().playRow.load() == row) return true;
            run (1);
        }
        return false;
    }
};

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    const File outDir = argc > 1 ? File::getCurrentWorkingDirectory().getChildFile (argv[1]) : File::getCurrentWorkingDirectory();
    outDir.createDirectory();
    const bool screens = argc > 2 && String (argv[2]) == "--screens";

    auto proc = std::make_unique<FourTrackerProcessor>();
    auto& e = proc->getEngine();
    proc->prepareToPlay (44100.0, 256);
    Runner r { *proc };

    CHECK (! e.playing.load(), "starts stopped");

    // C-2 (MIDI 36) starts
    r.run (1, [] (int, MidiBuffer& m) { m.addEvent (MidiMessage::noteOn (1, 36, (uint8) 100), 10); });
    CHECK (e.playing.load(), "C-2 starts playback");
    CHECK (e.playPattern.load() == 0, "plays pattern 00 by default");

    // let it play to row 10, then press D-2 -> pattern 01
    CHECK (r.runUntilRow (10), "reached row 10");
    r.run (1, [] (int, MidiBuffer& m) { m.addEvent (MidiMessage::noteOn (1, 38, (uint8) 100), 0); });
    CHECK (e.selectedPattern.load() == 1, "D-2 selects pattern 01");
    const int rowBefore = e.playRow.load();
    for (int i = 0; i < 200 && e.playPattern.load() != 1; ++i) r.run (1);
    CHECK (e.playPattern.load() == 1, "switched to pattern 01");
    CHECK (e.playRow.load() == rowBefore + 1, "legato: continued at row " + String (rowBefore + 1) + " (got " + String (e.playRow.load()) + ")");

    // play on a while, then C#2 -> pattern 00 again
    CHECK (r.runUntilRow (40), "reached row 40 in pattern 01");
    r.run (1, [] (int, MidiBuffer& m) { m.addEvent (MidiMessage::noteOn (1, 37, (uint8) 100), 0); });
    for (int i = 0; i < 200 && e.playPattern.load() != 0; ++i) r.run (1);
    CHECK (e.playPattern.load() == 0 && e.playRow.load() == 41, "C#2 back to pattern 00 at row 41");

    // let pattern 00 loop around
    CHECK (r.runUntilRow (63), "reached row 63");
    CHECK (r.runUntilRow (0), "looped to row 0");
    CHECK (e.playPattern.load() == 0, "still on pattern 00 after loop");

    // D#2 -> pattern 02 (breakdown), E-2 -> 03 (fill), with end-of-pattern mode
    e.switchMode = (int) ft::SwitchMode::EndOfPattern;
    r.run (1, [] (int, MidiBuffer& m) { m.addEvent (MidiMessage::noteOn (1, 39, (uint8) 100), 0); });
    CHECK (e.queuedPattern.load() == 2, "D#2 queues pattern 02");
    CHECK (r.runUntilRow (32) && e.playPattern.load() == 0, "end-of-pattern mode: still on 00 mid-pattern");
    for (int i = 0; i < 2000 && e.playPattern.load() != 2; ++i) r.run (1);
    CHECK (e.playPattern.load() == 2 && e.playRow.load() <= 1, "end-of-pattern mode: 02 starts at row 0");
    r.runUntilRow (32);
    e.switchMode = (int) ft::SwitchMode::LegatoNextRow;
    r.run (1, [] (int, MidiBuffer& m) { m.addEvent (MidiMessage::noteOn (1, 40, (uint8) 100), 0); });
    for (int i = 0; i < 200 && e.playPattern.load() != 3; ++i) r.run (1);
    CHECK (e.playPattern.load() == 3 && e.playRow.load() == 33, "E-2 jumps legato into pattern 03 at row 33");
    r.runUntilRow (63);
    r.runUntilRow (8);

    // C-2 stops
    r.run (1, [] (int, MidiBuffer& m) { m.addEvent (MidiMessage::noteOn (1, 36, (uint8) 100), 0); });
    CHECK (! e.playing.load(), "C-2 stops playback");
    r.run (200);

    // the on-screen transport does the same
    e.requestStartStop();
    r.run (1);
    CHECK (e.playing.load(), "requestStartStop starts");
    e.requestStartStop();
    r.run (1);
    CHECK (! e.playing.load(), "requestStartStop stops");

    // MIDI channel filter
    e.midiChannel = 2;
    r.run (1, [] (int, MidiBuffer& m) { m.addEvent (MidiMessage::noteOn (1, 36, (uint8) 100), 0); });
    CHECK (! e.playing.load(), "channel filter ignores channel 1");
    e.midiChannel = 0;

    // learn
    e.midiLearn = true;
    r.run (1, [] (int, MidiBuffer& m) { m.addEvent (MidiMessage::noteOn (1, 48, (uint8) 100), 0); });
    CHECK (e.midiBaseNote.load() == 48 && ! e.playing.load(), "learn sets start/stop key to C-3");
    e.midiBaseNote = 36;

    // audio sanity
    float peak = 0.0f; double sum = 0.0; bool finite = true;
    for (int c = 0; c < 2; ++c)
    {
        const float* d = r.all.getReadPointer (c);
        for (int i = 0; i < r.written; ++i)
        {
            finite = finite && std::isfinite (d[i]);
            peak = jmax (peak, std::abs (d[i]));
            sum += d[i] * d[i];
        }
    }
    const double rms = std::sqrt (sum / (2.0 * r.written));
    CHECK (finite, "no NaN/inf in output");
    CHECK (rms > 0.01, "output is not silent (rms " + String (rms, 4) + ")");
    CHECK (peak <= 1.0f, "peak is reasonable (" + String (peak, 3) + ")");

    {
        auto wavFile = outDir.getChildFile ("live_session.wav");
        wavFile.deleteFile();
        WavAudioFormat wav;
        std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (wavFile), 44100.0, 2, 16, {}, 0));
        w->writeFromAudioSampleBuffer (r.all, 0, r.written);
        std::cout << "wrote " << wavFile.getFullPathName() << " (" << String (r.written / 44100.0, 1) << " s)\n";
    }

    // state round trip
    {
        e.getPattern (5).at (3, 2) = { 50, 4, 0x0C, 0x20 };
        e.mix[1].pan = -20;
        proc->getInstrument (3).params.loop = true;
        proc->instrumentChanged (3);
        MemoryBlock state;
        proc->getStateInformation (state);

        auto p2 = std::make_unique<FourTrackerProcessor>();
        p2->setStateInformation (state.getData(), (int) state.getSize());
        const auto& c = p2->getEngine().getPattern (5).at (3, 2);
        CHECK (c.note == 50 && c.inst == 4 && c.fx == 0x0C && c.param == 0x20, "state: pattern cell survives");
        CHECK (p2->getEngine().mix[1].pan.load() == -20, "state: mixer survives");
        CHECK (p2->getInstrument (3).params.loop && p2->getInstrument (3).data->getLength() == proc->getInstrument (3).data->getLength(),
               "state: instrument sample + params survive (" + String (state.getSize() / 1024) + " KB)");

        // module = patterns + samples in one file
        auto f = outDir.getChildFile ("test.ftm");
        CHECK (proc->saveModule (f), "module saves");
        auto p3 = std::make_unique<FourTrackerProcessor>();
        p3->newPatternSet();
        CHECK (p3->getInstrument (1).isEmpty() && p3->getEngine().getPattern (5).isEmpty(), "fresh processor cleared before load");
        CHECK (p3->loadModule (f).isEmpty(), "module loads");
        const auto& c3 = p3->getEngine().getPattern (5).at (3, 2);
        CHECK (c3.note == 50 && c3.inst == 4 && c3.fx == 0x0C, "module: patterns come back");
        CHECK (! p3->getInstrument (1).isEmpty()
                 && p3->getInstrument (1).data->getLength() == proc->getInstrument (1).data->getLength()
                 && p3->getInstrument (3).params.loop, "module: samples and instrument settings come back");
        std::cout << "      module file size: " << (f.getSize() / 1024) << " KB\n";

        // old .ftps pattern sets still load (patterns only)
        {
            juce::ValueTree ps ("PATTERNSET");
            ps.setProperty ("speed", 4, nullptr);
            ps.setProperty ("bpm", 140, nullptr);
            juce::ValueTree pats ("PATTERNS");
            juce::ValueTree pt ("P");
            juce::MemoryBlock mb (64 * 4 * 4, true);
            static_cast<uint8_t*> (mb.getData())[0] = 49;   // C-4 in row 0, channel 1
            static_cast<uint8_t*> (mb.getData())[1] = 1;
            pt.setProperty ("index", 2, nullptr); pt.setProperty ("rows", 64, nullptr); pt.setProperty ("data", mb, nullptr);
            pats.appendChild (pt, nullptr); ps.appendChild (pats, nullptr);
            auto old = outDir.getChildFile ("old.ftps");
            old.deleteFile();
            { juce::FileOutputStream fos (old); fos.write ("FTPS", 4); ps.writeToStream (fos); }
            CHECK (p3->loadModule (old).isEmpty() && p3->getEngine().getPattern (2).at (0, 0).note == 49
                     && p3->getEngine().bpm.load() == 140 && ! p3->getInstrument (1).isEmpty(),
                   "old .ftps pattern set loads, samples kept");
        }
        CHECK (p3->loadModule (outDir.getChildFile ("live_session.wav")).isNotEmpty(), "a wav is rejected as module");
    }

    // standalone app always starts with the demo song, plugin restores its song
    {
        auto src = std::make_unique<FourTrackerProcessor>();
        src->newPatternSet();
        src->getEngine().midiBaseNote = 48;
        MemoryBlock st;
        src->getStateInformation (st);

        AudioProcessor::setTypeOfNextNewPlugin (AudioProcessor::wrapperType_Standalone);
        auto app = std::make_unique<FourTrackerProcessor>();
        AudioProcessor::setTypeOfNextNewPlugin (AudioProcessor::wrapperType_Undefined);
        app->setStateInformation (st.getData(), (int) st.getSize());
        CHECK (! app->getEngine().getPattern (0).isEmpty() && ! app->getInstrument (1).isEmpty(), "standalone: starts with the demo song");
        CHECK (app->getEngine().midiBaseNote.load() == 48, "standalone: MIDI settings still restored");

        AudioProcessor::setTypeOfNextNewPlugin (AudioProcessor::wrapperType_VST3);
        auto plug = std::make_unique<FourTrackerProcessor>();
        AudioProcessor::setTypeOfNextNewPlugin (AudioProcessor::wrapperType_Undefined);
        plug->setStateInformation (st.getData(), (int) st.getSize());
        CHECK (plug->getEngine().getPattern (0).isEmpty() && plug->getInstrument (1).isEmpty(), "plugin: restores the saved (empty) song");

        // demo song can always be loaded again
        plug->loadDemoSong();
        CHECK (! plug->getEngine().getPattern (0).isEmpty() && plug->getInstrument (1).name == "KICK.WAV", "loadDemoSong brings the demo back");

        // demo folder
        FourTrackerProcessor::ensureDemoFolder();
        auto demoDir = FourTrackerProcessor::getUserFolder().getChildFile ("Demo");
        CHECK (demoDir.getChildFile ("Demo Song.ftm").existsAsFile(), "demo folder has Demo Song.ftm");
        CHECK (demoDir.getChildFile ("Samples").getChildFile ("KICK.WAV").existsAsFile()
                 && demoDir.getChildFile ("Samples").getNumberOfChildFiles (File::findFiles) == 9, "demo folder has the 9 demo samples as WAV");
        auto p6 = std::make_unique<FourTrackerProcessor>();
        p6->newPatternSet();
        CHECK (p6->loadModule (demoDir.getChildFile ("Demo Song.ftm")).isEmpty() && ! p6->getEngine().getPattern (3).isEmpty(),
               "Demo Song.ftm loads");
        std::cout << "      demo folder: " << demoDir.getFullPathName() << "\n";
    }

    // slot 10 (hex) = instrument 16 is a real, playable slot
    {
        auto p7 = std::make_unique<FourTrackerProcessor>();
        p7->prepareToPlay (44100.0, 256);
        Runner r7 { *p7 };
        auto kick = FourTrackerProcessor::getUserFolder().getChildFile ("Demo").getChildFile ("Samples").getChildFile ("KICK.WAV");
        CHECK (p7->loadInstrument (ft::kNumSlots, kick).isEmpty() && p7->getInstrument (16).name == "KICK.WAV", "sample loads into slot 10 (16)");
        p7->newPatternSet();
        p7->loadInstrument (16, kick);
        p7->getEngine().getPattern (0).at (0, 0) = { (uint8_t) ft::kNativeNote, 16, 0, 0 };
        p7->getEngine().requestStart();
        r7.run (20);
        float pk = 0; for (int i = r7.written - 20 * Runner::block; i < r7.written; ++i) pk = jmax (pk, std::abs (r7.all.getSample (0, i)));
        CHECK (pk > 0.05f, "instrument 10 plays from the pattern");
        auto f7 = outDir.getChildFile ("slot16.ftm");
        p7->saveModule (f7);
        auto p8 = std::make_unique<FourTrackerProcessor>();
        CHECK (p8->loadModule (f7).isEmpty() && p8->getInstrument (16).name == "KICK.WAV" && p8->getEngine().getPattern (0).at (0, 0).inst == 16,
               "slot 10 survives save/load");
    }

    // holding a key: auto-repeat must not retrigger; release lets unlooped samples ring out
    {
        auto p9 = std::make_unique<FourTrackerProcessor>();
        p9->prepareToPlay (44100.0, 256);
        Runner r9 { *p9 };
        auto& e9 = p9->getEngine();
        std::unique_ptr<AudioProcessorEditor> ed9 (p9->createEditor());
        const KeyPress keyZ ('z', ModifierKeys(), 'z');      // C-4 = sample's own pitch

        p9->selectedInstrument = 1;                          // KICK.WAV, no loop, 22050 frames
        ed9->keyPressed (keyZ);
        r9.run (20);
        const int pos1 = e9.previewPosition.load();
        for (int rep = 0; rep < 5; ++rep) { ed9->keyPressed (keyZ); r9.run (2); }   // auto-repeat while held
        const int pos2 = e9.previewPosition.load();
        CHECK (pos1 > 0 && pos2 > pos1 + 2000, "held key: auto-repeat does not restart the sample (" + String (pos1) + " -> " + String (pos2) + ")");

        ed9->keyStateChanged (false);                        // key released
        r9.run (4);
        CHECK (e9.previewPosition.load() > pos2, "released: unlooped sample keeps playing");
        for (int b = 0; b < 200 && e9.previewPosition.load() >= 0; ++b) r9.run (1);
        CHECK (e9.previewPosition.load() < 0, "unlooped sample stops by itself at its end");

        p9->selectedInstrument = 8;                          // PAD.WAV, looped
        ed9->keyPressed (keyZ);
        r9.run (300);                                        // ~1.7 s, longer than the 1 s sample
        CHECK (e9.previewPosition.load() >= 0, "held key: looped sample keeps looping past its length");
        ed9->keyStateChanged (false);
        r9.run (4);
        CHECK (e9.previewPosition.load() < 0, "released: looped sample stops");

        ed9->keyPressed (keyZ);                              // a fresh press after release plays again
        r9.run (2);
        CHECK (e9.previewPosition.load() >= 0, "new press after release plays again");
        ed9.reset();
    }

    // Amiga samples: IFF 8SVX (plain, Fibonacci-compressed, stereo) and raw 8-bit
    {
        auto be32 = [] (MemoryOutputStream& m, uint32 v) { m.writeIntBigEndian ((int) v); };
        auto be16 = [] (MemoryOutputStream& m, uint16 v) { m.writeShortBigEndian ((short) v); };
        auto make8svx = [&] (const std::vector<int8_t>& body, uint32 oneShot, uint32 repeat, uint16 rate,
                             uint8 compression, uint32 volume, const char* nameChunk, int chan) -> MemoryBlock
        {
            MemoryOutputStream c;
            c.write ("VHDR", 4); be32 (c, 20);
            be32 (c, oneShot); be32 (c, repeat); be32 (c, 32); be16 (c, rate);
            c.writeByte (1); c.writeByte ((char) compression); be32 (c, volume);
            if (nameChunk != nullptr)
            {
                const auto len = (uint32) strlen (nameChunk);
                c.write ("NAME", 4); be32 (c, len); c.write (nameChunk, len); if (len & 1) c.writeByte (0);
            }
            if (chan != 0) { c.write ("CHAN", 4); be32 (c, 4); be32 (c, (uint32) chan); }
            c.write ("BODY", 4); be32 (c, (uint32) body.size()); c.write (body.data(), body.size()); if (body.size() & 1) c.writeByte (0);
            MemoryOutputStream f;
            f.write ("FORM", 4); be32 (f, (uint32) c.getDataSize() + 4); f.write ("8SVX", 4); f.write (c.getData(), c.getDataSize());
            return f.getMemoryBlock();
        };
        auto save = [&] (const String& n, const MemoryBlock& mb) { auto f = outDir.getChildFile (n); f.replaceWithData (mb.getData(), mb.getSize()); return f; };

        std::vector<int8_t> saw (1000);
        for (int i = 0; i < 1000; ++i) saw[(size_t) i] = (int8_t) ((i % 100) * 2 - 100);

        auto pA = std::make_unique<FourTrackerProcessor>();
        auto fPlain = save ("snare.iff", make8svx (saw, 600, 400, 16574, 0, 0x8000, "Amiga Snare", 0));
        auto err = pA->loadInstrument (2, fPlain);
        const auto& in2 = pA->getInstrument (2);
        CHECK (err.isEmpty() && in2.data->getLength() == 1000 && (int) in2.data->sampleRate == 16574 && in2.data->bitDepth == 8,
               "8SVX loads: 1000 frames, 16574 Hz, 8-bit (" + err + ")");
        CHECK (in2.name == "AMIGA SNARE" && in2.params.volume == 64, "8SVX: NAME chunk read, volume always full (64)");
        for (uint32 v : { 0u, 64u, 1u, 0x8000u, 0x10000u, 0xFFFFFFFFu })
        {
            pA->loadInstrument (8, save ("vol.iff", make8svx (saw, 1000, 0, 8363, 0, v, nullptr, 0)));
            if (pA->getInstrument (8).params.volume != 64) { CHECK (false, "8SVX volume field " + String::toHexString ((int) v) + " gives full volume"); }
        }
        CHECK (true, "8SVX: any VHDR volume value (0, 64, 1, 0x8000, 0x10000, 0xFFFFFFFF) gives a playable instrument");
        CHECK (in2.params.loop && in2.params.loopStart == 600 && in2.params.loopEnd == 999, "8SVX: loop 600-999 from oneShot/repeat");
        CHECK (std::abs (in2.data->buffer.getSample (0, 1) - (-98.0f / 128.0f)) < 1.0e-5f, "8SVX: signed 8-bit values decoded");

        // Fibonacci-delta: start value 0, codes 9 (+1) repeated -> 0,1,2,3...
        std::vector<int8_t> fib (2 + 50); fib[0] = 0; fib[1] = 0;
        for (size_t i = 2; i < fib.size(); ++i) fib[i] = (int8_t) 0x99;
        auto fFib = save ("fib.8svx", make8svx (fib, 0, 0, 8363, 1, 0x10000, nullptr, 0));
        err = pA->loadInstrument (3, fFib);
        const auto& in3 = pA->getInstrument (3);
        CHECK (err.isEmpty() && in3.data->getLength() == 100 && std::abs (in3.data->buffer.getSample (0, 99) - 100.0f / 128.0f) < 1.0e-5f
                 && ! in3.params.loop && in3.params.volume == 64,
               "8SVX Fibonacci-compressed decodes (" + err + ")");

        // stereo (CHAN 6): left half then right half of the body
        std::vector<int8_t> st (400);
        for (int i = 0; i < 200; ++i) { st[(size_t) i] = 50; st[(size_t) (200 + i)] = -50; }
        err = pA->loadInstrument (4, save ("stereo.iff", make8svx (st, 200, 0, 22050, 0, 0x10000, nullptr, 6)));
        const auto& in4 = pA->getInstrument (4);
        CHECK (err.isEmpty() && in4.data->getNumChannels() == 2 && in4.data->getLength() == 200
                 && in4.data->buffer.getSample (0, 10) > 0.3f && in4.data->buffer.getSample (1, 10) < -0.3f,
               "8SVX stereo splits into left/right (" + err + ")");

        // raw 8-bit, with and without extension
        MemoryBlock raw (saw.data(), saw.size());
        err = pA->loadInstrument (5, save ("kick.raw", raw));
        CHECK (err.isEmpty() && pA->getInstrument (5).data->getLength() == 1000 && (int) pA->getInstrument (5).data->sampleRate == 16574,
               "raw .raw sample loads as 8-bit at 16574 Hz");
        err = pA->loadInstrument (6, save ("st-01.hihat", raw));      // unknown extension: refused
        CHECK (err.isNotEmpty(), "unknown extension is not guessed as raw");
        auto noExt = outDir.getChildFile ("BASSDRUM");
        noExt.replaceWithData (raw.getData(), raw.getSize());
        err = pA->loadInstrument (6, noExt);
        CHECK (err.isEmpty() && pA->getInstrument (6).data->getLength() == 1000, "file without extension loads as raw Amiga sample");

        // an extensionless WAV is still read as WAV
        auto wavNoExt = outDir.getChildFile ("KICKWAV");
        FourTrackerProcessor::getUserFolder().getChildFile ("Demo").getChildFile ("Samples").getChildFile ("KICK.WAV").copyFileTo (wavNoExt);
        err = pA->loadInstrument (7, wavNoExt);
        CHECK (err.isEmpty() && (int) pA->getInstrument (7).data->sampleRate == 44100 && pA->getInstrument (7).data->getLength() == 22050,
               "WAV without extension is still read as WAV");

        // and it plays
        pA->prepareToPlay (44100.0, 256);
        Runner rA { *pA };
        pA->getEngine().requestPreview (2, ft::kNativeNote);
        rA.run (10);
        float pkA = 0; for (int i = 0; i < rA.written; ++i) pkA = jmax (pkA, std::abs (rA.all.getSample (0, i)));
        CHECK (pkA > 0.05f, "8SVX sample plays");
    }

    // NEW PATTERNSET clears everything
    {
        auto p3 = std::make_unique<FourTrackerProcessor>();
        p3->prepareToPlay (44100.0, 256);
        Runner r3 { *p3 };
        p3->getEngine().requestStart();
        r3.run (20);
        p3->getEngine().bpm = 140;
        p3->newPatternSet();
        r3.run (5);
        bool allPatternsEmpty = true, allInstrumentsEmpty = true;
        for (int i = 0; i < ft::kMaxPatterns; ++i) allPatternsEmpty = allPatternsEmpty && p3->getEngine().getPattern (i).isEmpty();
        for (int i = 1; i < ft::kNumInstruments; ++i) allInstrumentsEmpty = allInstrumentsEmpty && p3->getInstrument (i).isEmpty();
        CHECK (! p3->getEngine().playing.load(), "new patternset stops playback");
        CHECK (allPatternsEmpty, "new patternset clears all patterns");
        CHECK (allInstrumentsEmpty, "new patternset clears all samples");
        CHECK (p3->getEngine().bpm.load() == 125 && p3->getEngine().selectedPattern.load() == 0, "new patternset resets bpm and pattern");
        float peak3 = 0.0f;
        r3.run (40);
        for (int i = r3.written - 40 * Runner::block; i < r3.written; ++i) peak3 = jmax (peak3, std::abs (r3.all.getSample (0, i)));
        CHECK (peak3 == 0.0f, "silence after new patternset");
        p3->getEngine().requestStart();
        r3.run (40);
        CHECK (std::isfinite (r3.all.getSample (0, r3.written - 1)), "playing an empty set is safe");
    }

    // Ctrl+F3 / Ctrl+F4 / Ctrl+F5 = cut / copy / paste the whole pattern (all 4 channels)
    {
        auto p4 = std::make_unique<FourTrackerProcessor>();
        p4->prepareToPlay (44100.0, 256);
        Runner r4 { *p4 };
        auto& e4 = p4->getEngine();
        std::unique_ptr<AudioProcessorEditor> ed4 (p4->createEditor());
        const auto ctrl = ModifierKeys (ModifierKeys::ctrlModifier);
        auto same = [&] (int a, int b)
        {
            auto& pa = e4.getPattern (a); auto& pb = e4.getPattern (b);
            if (pa.getNumRows() != pb.getNumRows()) return false;
            for (int rr = 0; rr < pa.getNumRows(); ++rr)
                for (int c = 0; c < ft::kNumChannels; ++c)
                {
                    const auto& x = pa.at (rr, c); const auto& y = pb.at (rr, c);
                    if (x.note != y.note || x.inst != y.inst || x.fx != y.fx || x.param != y.param) return false;
                }
            return true;
        };

        e4.getPattern (0).at (63, 3) = { 30, 5, 0x0C, 0x10 };     // something in the last row of channel 4
        ed4->keyPressed (KeyPress (KeyPress::F4Key, ctrl, 0));
        e4.requestPattern (6);
        r4.run (1);
        CHECK (e4.getPattern (6).isEmpty(), "pattern 06 starts empty");
        ed4->keyPressed (KeyPress (KeyPress::F5Key, ctrl, 0));
        CHECK (same (0, 6), "Ctrl+F4 then Ctrl+F5 copies all 4 channels, all 64 rows");

        ed4->keyPressed (KeyPress (KeyPress::F5Key, ModifierKeys(), 0));
        e4.requestPattern (7);
        r4.run (1);
        ed4->keyPressed (KeyPress (KeyPress::F5Key, ModifierKeys(), 0));
        CHECK (e4.getPattern (7).isEmpty(), "plain F5 no longer pastes");

        e4.requestPattern (6);
        r4.run (1);
        ed4->keyPressed (KeyPress (KeyPress::F3Key, ctrl, 0));
        CHECK (e4.getPattern (6).isEmpty(), "Ctrl+F3 cuts the whole pattern");
        e4.requestPattern (8);
        r4.run (1);
        ed4->keyPressed (KeyPress (KeyPress::F5Key, ctrl, 0));
        CHECK (same (0, 8), "pasting after Ctrl+F3 gives the cut block back");

        ed4->keyPressed (KeyPress ('z', ctrl, 'z'));
        CHECK (e4.getPattern (8).isEmpty(), "Ctrl+Z undoes the block paste");
        // Shift+F3 cuts a track, Shift+F4 copies it, Shift+F5 pastes it
        {
            const auto shiftKey = ModifierKeys (ModifierKeys::shiftModifier);
            e4.requestPattern (0);
            r4.run (1);
            auto chEq = [&] (int pa, int ca, int pb, int cb)
            {
                for (int rr = 0; rr < 64; ++rr)
                {
                    const auto& x = e4.getPattern (pa).at (rr, ca); const auto& y = e4.getPattern (pb).at (rr, cb);
                    if (x.note != y.note || x.inst != y.inst || x.fx != y.fx || x.param != y.param) return false;
                }
                return true;
            };
            ed4->keyPressed (KeyPress (KeyPress::F4Key, shiftKey, 0));     // copy CH1 of pattern 00
            e4.requestPattern (9);
            r4.run (1);
            ed4->keyPressed (KeyPress (KeyPress::F5Key, shiftKey, 0));     // paste into CH1 of 09
            CHECK (chEq (0, 0, 9, 0) && ! e4.getPattern (9).isEmpty(), "Shift+F4 / Shift+F5 copy and paste a track");
            ed4->keyPressed (KeyPress (KeyPress::F3Key, shiftKey, 0));     // cut it again
            CHECK (e4.getPattern (9).isEmpty(), "Shift+F3 cuts a track");
            e4.requestPattern (8);
            r4.run (1);
        }

        ed4.reset();
    }

    // EDIT button / Space: off = keys only play, on = keys write notes
    {
        auto p5 = std::make_unique<FourTrackerProcessor>();
        p5->prepareToPlay (44100.0, 256);
        Runner r5 { *p5 };
        auto& e5 = p5->getEngine();
        p5->newPatternSet();
        r5.run (1);
        std::unique_ptr<AudioProcessorEditor> ed5 (p5->createEditor());

        std::function<PixelButton* (Component&)> findEdit = [&] (Component& c) -> PixelButton*
        {
            for (auto* ch : c.getChildren())
            {
                if (auto* b = dynamic_cast<PixelButton*> (ch)) if (b->getButtonText() == "EDIT") return b;
                if (auto* f = findEdit (*ch)) return f;
            }
            return nullptr;
        };
        auto* editBtn = findEdit (*ed5);
        CHECK (editBtn != nullptr && ! editBtn->getToggleState(), "EDIT button exists and starts off");

        ed5->keyPressed (KeyPress ('q', ModifierKeys(), 'q'));
        ed5->keyStateChanged (false);
        CHECK (e5.getPattern (0).isEmpty(), "edit off: note key does not write into the pattern");

        ed5->keyPressed (KeyPress (KeyPress::spaceKey, ModifierKeys(), ' '));
        CHECK (editBtn != nullptr && editBtn->getToggleState(), "Space turns EDIT on (button lit)");
        ed5->keyPressed (KeyPress ('q', ModifierKeys(), 'q'));
        ed5->keyStateChanged (false);
        CHECK (e5.getPattern (0).at (0, 0).note != 0, "edit on: note key writes a note");

        if (editBtn != nullptr) editBtn->triggerClick();
        MessageManager::getInstance()->runDispatchLoopUntil (20);
        CHECK (editBtn != nullptr && ! editBtn->getToggleState(), "clicking EDIT turns it off again");
        ed5->keyPressed (KeyPress ('w', ModifierKeys(), 'w'));
        CHECK (e5.getPattern (0).at (1, 0).note == 0, "edit off again: nothing written");
        if (editBtn != nullptr) editBtn->triggerClick();     // leave it lit for the screenshot test below
        MessageManager::getInstance()->runDispatchLoopUntil (20);
        ed5->keyStateChanged (false);
        // OCTAVE box: lower octaves can be written (octave 2 -> Z writes C-2, octave 0 -> C-0)
        {
            std::function<ValueBox* (Component&)> findOct = [&] (Component& c) -> ValueBox*
            {
                for (auto* ch : c.getChildren())
                {
                    if (auto* vb = dynamic_cast<ValueBox*> (ch)) if (vb->getValue() == 4 && vb->getBounds().getX() > 1190 && vb->getBounds().getX() < 1300) return vb;
                    if (auto* f = findOct (*ch)) return f;
                }
                return nullptr;
            };
            auto* oct = findOct (*ed5);
            CHECK (oct != nullptr, "OCTAVE box exists, default 4");
            if (oct != nullptr) oct->setValue (2, true);
            ed5->keyPressed (KeyPress (KeyPress::upKey, ModifierKeys(), 0));          // back to row 1
            ed5->keyPressed (KeyPress (KeyPress::homeKey, ModifierKeys(), 0));        // row 0
            ed5->keyPressed (KeyPress ('z', ModifierKeys(), 'z'));
            ed5->keyStateChanged (false);
            CHECK (ft::noteToString (e5.getPattern (0).at (0, 0).note) == "C-2", "octave 2: Z writes C-2 (got " + ft::noteToString (e5.getPattern (0).at (0, 0).note) + ")");
            ed5->keyPressed (KeyPress (KeyPress::F1Key, ModifierKeys(), 0));
            ed5->keyPressed (KeyPress (KeyPress::F1Key, ModifierKeys(), 0));
            ed5->keyPressed (KeyPress (KeyPress::F1Key, ModifierKeys(), 0));     // clamps at 0
            ed5->keyPressed (KeyPress ('z', ModifierKeys(), 'z'));
            ed5->keyStateChanged (false);
            CHECK (ft::noteToString (e5.getPattern (0).at (1, 0).note) == "C-0", "F1 lowers the octave down to 0: Z writes C-0");
            MessageManager::getInstance()->runDispatchLoopUntil (60);
            CHECK (oct != nullptr && oct->getValue() == 0, "OCTAVE box follows F1/F2");
            ed5->keyPressed (KeyPress (KeyPress::F2Key, ModifierKeys(), 0)); ed5->keyPressed (KeyPress (KeyPress::F2Key, ModifierKeys(), 0));
            ed5->keyPressed (KeyPress (KeyPress::F2Key, ModifierKeys(), 0)); ed5->keyPressed (KeyPress (KeyPress::F2Key, ModifierKeys(), 0));
        }
        ed5.reset();
    }

    // sample browser: selecting a file plays it without loading it; Enter loads
    {
        auto pb = std::make_unique<FourTrackerProcessor>();
        pb->prepareToPlay (44100.0, 256);
        Runner rb { *pb };
        auto& eb = pb->getEngine();
        pb->newPatternSet();
        pb->selectedInstrument = 3;
        rb.run (1);
        std::unique_ptr<AudioProcessorEditor> edb (pb->createEditor());
        edb->setSize (1536, 1024);

        std::function<Component* (Component&, std::function<bool (Component&)>)> findC =
            [&] (Component& c, std::function<bool (Component&)> pred) -> Component*
        {
            for (auto* ch : c.getChildren())
            {
                if (pred (*ch)) return ch;
                if (auto* f = findC (*ch, pred)) return f;
            }
            return nullptr;
        };
        auto* tab = dynamic_cast<Button*> (findC (*edb, [] (Component& c) { auto* b = dynamic_cast<PixelButton*> (&c); return b && b->getButtonText() == "INSTRUMENTS"; }));
        if (tab) tab->triggerClick();
        MessageManager::getInstance()->runDispatchLoopUntil (30);
        auto* loadBtn = dynamic_cast<Button*> (findC (*edb, [] (Component& c) { auto* b = dynamic_cast<PixelButton*> (&c); if (! (b && b->getButtonText() == "LOAD")) return false;
            for (Component* a = &c; a != nullptr && dynamic_cast<AudioProcessorEditor*> (a) == nullptr; a = a->getParentComponent()) if (! a->isVisible()) return false;
            return true; }));
        CHECK (loadBtn != nullptr, "LOAD button on the Instruments page");
        if (loadBtn) loadBtn->triggerClick();
        MessageManager::getInstance()->runDispatchLoopUntil (30);

        auto* sb = dynamic_cast<SampleBrowser*> (findC (*edb, [] (Component& c) { return dynamic_cast<SampleBrowser*> (&c) != nullptr; }));
        CHECK (sb != nullptr && sb->isVisible(), "LOAD opens the sample browser");
        auto* fb = dynamic_cast<FileBrowserComponent*> (findC (*edb, [] (Component& c) { return dynamic_cast<FileBrowserComponent*> (&c) != nullptr; }));
        auto samplesDir = FourTrackerProcessor::getUserFolder().getChildFile ("Demo").getChildFile ("Samples");
        if (fb) fb->setRoot (samplesDir);
        auto* list = dynamic_cast<ListBox*> (findC (*edb, [] (Component& c) { return dynamic_cast<ListBox*> (&c) != nullptr; }));
        for (int i = 0; i < 100 && list && list->getListBoxModel()->getNumRows() < 9; ++i)
            MessageManager::getInstance()->runDispatchLoopUntil (20);
        const int rows = list ? list->getListBoxModel()->getNumRows() : 0;
        CHECK (rows >= 9, "browser lists the demo samples (" + String (rows) + ")");

        if (list) list->selectRow (0);                           // first sample
        MessageManager::getInstance()->runDispatchLoopUntil (30);
        rb.run (10);
        CHECK (eb.previewPosition.load() > 0, "selecting a file plays it straight away");
        CHECK (pb->getInstrument (3).isEmpty(), "...without loading it into the slot");
        const auto firstName = pb->getAuditionSample().name;

        if (list) list->keyPressed (KeyPress (KeyPress::downKey));   // arrow down = next file, plays it
        MessageManager::getInstance()->runDispatchLoopUntil (30);
        rb.run (4);
        const auto secondName = pb->getAuditionSample().name;
        CHECK (secondName.isNotEmpty() && secondName != firstName && eb.previewPosition.load() >= 0,
               "arrow down plays the next file (" + firstName + " -> " + secondName + ")");

        if (screens)
        {
            MessageManager::getInstance()->runDispatchLoopUntil (100);
            auto img = edb->createComponentSnapshot (edb->getLocalBounds(), true, 1.0f);
            auto file = outDir.getChildFile ("08_sample_browser.png");
            file.deleteFile();
            FileOutputStream fos (file);
            PNGImageFormat().writeImageToStream (img, fos);
        }

        if (sb) sb->keyPressed (KeyPress (KeyPress::spaceKey));      // space stops
        rb.run (4);
        CHECK (eb.previewPosition.load() < 0, "Space stops the preview");

        if (list) list->keyPressed (KeyPress (KeyPress::returnKey));  // Enter loads
        MessageManager::getInstance()->runDispatchLoopUntil (30);
        CHECK (pb->getInstrument (3).name.equalsIgnoreCase (secondName), "Enter loads the file into slot 03 (" + pb->getInstrument (3).name + ")");
        CHECK (sb && ! sb->isVisible(), "browser closes after loading");
        CHECK (SampleBrowser::lastSampleFolder() == samplesDir, "browser remembers the folder");

        if (loadBtn) loadBtn->triggerClick();                        // reopen, Esc cancels
        MessageManager::getInstance()->runDispatchLoopUntil (30);
        CHECK (fb && fb->getRoot() == samplesDir, "browser reopens in the last folder");
        edb->keyPressed (KeyPress (KeyPress::escapeKey));
        CHECK (sb && ! sb->isVisible(), "Esc closes the browser");

        // a stereo sample: shown as two lanes (L / R) in Sample Edit
        {
            auto stereo = outDir.getChildFile ("STEREO TEST.WAV");
            stereo.deleteFile();
            AudioBuffer<float> buf (2, 44100);
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const float env = 1.0f - (float) i / (float) buf.getNumSamples();
                buf.setSample (0, i, 0.8f * env * std::sin ((float) i * 0.03f));
                buf.setSample (1, i, 0.5f * env * env * std::sin ((float) i * 0.11f) * std::sin ((float) i * 0.0007f));
            }
            {
                WavAudioFormat wav;
                std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (stereo), 44100.0, 2, 16, {}, 0));
                if (w) w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
            }
            CHECK (pb->loadInstrument (4, stereo).isEmpty() && pb->getInstrument (4).data->getNumChannels() == 2, "stereo WAV loads as stereo");
            pb->selectedInstrument = 4;
            auto* tabSE = dynamic_cast<Button*> (findC (*edb, [] (Component& c) { auto* b = dynamic_cast<PixelButton*> (&c); return b && b->getButtonText() == "SAMPLE EDIT"; }));
            if (tabSE) tabSE->triggerClick();
            MessageManager::getInstance()->runDispatchLoopUntil (60);
            if (screens)
            {
                auto img = edb->createComponentSnapshot (edb->getLocalBounds(), true, 1.0f);
                auto file = outDir.getChildFile ("09_stereo_sample_edit.png");
                file.deleteFile();
                FileOutputStream fos (file);
                PNGImageFormat().writeImageToStream (img, fos);
            }
        }
        rb.run (2);
        edb.reset();
    }

    // built-in synth: 5 waveforms, ADSR, resonant low-pass filter
    {
        auto ps = std::make_unique<FourTrackerProcessor>();
        ps->prepareToPlay (44100.0, 256);
        Runner rs { *ps };
        auto& es = ps->getEngine();
        ps->newPatternSet();
        rs.run (1);

        auto peakOf = [&] (int fromBlock, int toBlock)
        {
            float pk = 0.0f;
            for (int i = fromBlock * Runner::block; i < jmin (rs.written, toBlock * Runner::block); ++i)
                pk = jmax (pk, std::abs (rs.all.getSample (0, i)));
            return pk;
        };

        for (int w = 0; w < ft::numSynthWaves; ++w)
        {
            auto& in = ps->getInstrument (1 + w);
            in.makeSynth (w);
            ps->instrumentChanged (1 + w);
        }
        CHECK (ps->getInstrument (3).isSynth() && ps->getInstrument (3).name == "SYNTH SAW" && ps->getInstrument (3).params.loop,
               "makeSynth creates a looping synth instrument");
        CHECK (ps->getInstrument (5).data->getLength() == 32768, "noise uses a long buffer");

        // each waveform makes sound and keeps sounding while held (sustain 40)
        bool allSound = true;
        for (int w = 0; w < ft::numSynthWaves; ++w)
        {
            const int start = rs.written / Runner::block;
            es.requestPreview (1 + w, ft::kNativeNote);
            rs.run (150);                                    // ~0.9 s, past attack + decay
            const float pk = peakOf (start + 120, start + 150);
            if (pk < 0.05f) { allSound = false; std::cout << "      wave " << w << " peak " << pk << "\n"; }
            es.requestStopPreview();
            rs.run (60);
        }
        CHECK (allSound, "all 5 waveforms sound and sustain while held");
        CHECK (es.previewPosition.load() < 0, "release ends the synth note after note-off");

        // pitch: C-4 = middle C (261.6 Hz): count zero crossings of the sine
        {
            auto& in = ps->getInstrument (1);
            in.params.attack = 0; in.params.decay = 0; in.params.sustain = 64;
            ps->instrumentChanged (1);
            const int start = rs.written;
            es.requestPreview (1, ft::kNativeNote);
            rs.run (200);                                    // ~1.16 s
            int crossings = 0;
            for (int i = start + 4410; i < start + 4410 + 44100; ++i)
                if (rs.all.getSample (0, i - 1) < 0.0f && rs.all.getSample (0, i) >= 0.0f) ++crossings;
            CHECK (std::abs (crossings - 262) <= 2, "synth C-4 plays middle C (" + String (crossings) + " Hz)");
            es.requestStopPreview();
            rs.run (60);
        }

        // sustain 0: the note decays away by itself
        {
            auto& in = ps->getInstrument (4);
            in.params.attack = 1; in.params.decay = 200; in.params.sustain = 0;
            ps->instrumentChanged (4);
            es.requestPreview (4, ft::kNativeNote);
            rs.run (10);
            const bool on = es.previewPosition.load() >= 0;
            rs.run (60);                                     // 350 ms
            CHECK (on && es.previewPosition.load() < 0, "sustain 0: a plucked note decays to silence");
        }

        // filter: low cutoff takes the edge off a saw (less high-frequency energy)
        auto hfEnergy = [&] (int cutoffValue, int reso)
        {
            auto& in = ps->getInstrument (3);
            in.params.attack = 0; in.params.decay = 0; in.params.sustain = 64;
            in.params.cutoff = cutoffValue; in.params.resonance = reso;
            ps->instrumentChanged (3);
            const int start = rs.written;
            es.requestPreview (3, ft::kNativeNote + 12);
            rs.run (60);
            double e = 0;
            for (int i = start + 5000; i < start + 15000; ++i)
            {
                const float d = rs.all.getSample (0, i) - rs.all.getSample (0, i - 1);   // differentiator = high-pass
                e += d * d;
            }
            es.requestStopPreview();
            rs.run (60);
            return e;
        };
        const double open = hfEnergy (127, 0), closed = hfEnergy (40, 0), reso = hfEnergy (60, 120);
        CHECK (closed < open * 0.2, "low cutoff filters the saw (" + String (closed / open, 3) + " of open)");
        bool finite = true;
        for (int i = 0; i < rs.written; ++i) finite = finite && std::isfinite (rs.all.getSample (0, i));
        CHECK (finite && reso > 0.0, "high resonance stays stable");

        // pattern playback with a note-off
        {
            auto& pat = es.getPattern (0);
            pat.at (0, 0) = { (uint8_t) ft::kNativeNote, 2, 0, 0 };     // triangle
            pat.at (16, 0) = { ft::kNoteOff, 0, 0, 0 };
            auto& in = ps->getInstrument (2);
            in.params.release = 50; in.params.sustain = 64;
            ps->instrumentChanged (2);
            es.loopPattern = false;
            const int start = rs.written / Runner::block;
            es.requestStart();
            rs.run (150);
            const float during = peakOf (start + 20, start + 60);
            const int offBlock = start + (int) (16 * 6 * 44100.0 * 2.5 / 125.0 / Runner::block);
            const float after = peakOf (offBlock + 20, offBlock + 40);
            CHECK (during > 0.1f && after < 0.01f, "synth note in a pattern sustains, then releases at ===");
            es.requestStop();
            rs.run (4);
        }

        // module keeps the synth settings
        {
            auto& in = ps->getInstrument (3);
            in.params.cutoff = 55; in.params.resonance = 99; in.params.attack = 123; in.params.release = 456;
            ps->instrumentChanged (3);
            auto f = outDir.getChildFile ("synth.ftm");
            CHECK (ps->saveModule (f), "module with synths saves");
            auto p2 = std::make_unique<FourTrackerProcessor>();
            CHECK (p2->loadModule (f).isEmpty(), "module with synths loads");
            const auto& q = p2->getInstrument (3).params;
            CHECK (q.synth && q.synthWave == ft::waveSaw && q.cutoff == 55 && q.resonance == 99 && q.attack == 123 && q.release == 456,
                   "module keeps waveform, envelope and filter");
        }

        // the Instruments page: SOURCE SAMPLE / SYNTH
        if (screens)
        {
            ps->selectedInstrument = 3;
            std::unique_ptr<AudioProcessorEditor> eds (ps->createEditor());
            eds->setSize (1536, 1024);
            std::function<Button* (Component&, const String&)> findBtn = [&] (Component& c, const String& t) -> Button*
            {
                for (auto* ch : c.getChildren())
                {
                    if (auto* b = dynamic_cast<PixelButton*> (ch)) if (b->getButtonText() == t) return b;
                    if (auto* f = findBtn (*ch, t)) return f;
                }
                return nullptr;
            };
            if (auto* b = findBtn (*eds, "INSTRUMENTS")) b->triggerClick();
            MessageManager::getInstance()->runDispatchLoopUntil (60);
            auto img = eds->createComponentSnapshot (eds->getLocalBounds(), true, 1.0f);
            auto file = outDir.getChildFile ("10_synth.png");
            file.deleteFile();
            FileOutputStream fos (file);
            PNGImageFormat().writeImageToStream (img, fos);

            // clicking SYNTH on an empty slot makes a sine synth
            ps->selectedInstrument = 9;
            if (auto* b = findBtn (*eds, "INSTRUMENTS")) b->triggerClick();
            MessageManager::getInstance()->runDispatchLoopUntil (30);
            if (auto* b = findBtn (*eds, "SYNTH")) b->triggerClick();
            MessageManager::getInstance()->runDispatchLoopUntil (30);
            CHECK (ps->getInstrument (9).isSynth() && ps->getInstrument (9).params.synthWave == ft::waveSine, "SYNTH button turns an empty slot into a sine synth");
            if (auto* b = findBtn (*eds, "SQUARE")) b->triggerClick();
            MessageManager::getInstance()->runDispatchLoopUntil (30);
            CHECK (ps->getInstrument (9).params.synthWave == ft::waveSquare && ps->getInstrument (9).name == "SYNTH SQUARE", "SQUARE button changes the waveform");
            if (auto* b = findBtn (*eds, "SAMPLE")) b->triggerClick();
            MessageManager::getInstance()->runDispatchLoopUntil (30);
            CHECK (ps->getInstrument (9).isEmpty(), "SAMPLE clears the synth so a sample can be loaded");
            eds.reset();
        }
    }

    if (screens)
    {
        std::unique_ptr<AudioProcessorEditor> ed (proc->createEditor());
        ed->setSize (1536, 1024);
        ed->keyPressed (KeyPress (KeyPress::spaceKey, ModifierKeys(), ' '));   // EDIT on, so the red button shows
        e.requestPattern (0);
        r.run (2);
        const char* names[] = { "06_tracks", "02_instruments", "03_sample_edit", "04_song_pattern", "05_block", "07_midi_audio" };
        const char* tabs[]  = { "", "INSTRUMENTS", "SAMPLE EDIT", "SONG / PATTERN", "BLOCK", "MIDI / AUDIO" };

        std::function<Button* (Component&, const String&)> findTab = [&] (Component& c, const String& t) -> Button*
        {
            for (auto* ch : c.getChildren())
            {
                if (auto* b = dynamic_cast<PixelButton*> (ch))
                    if (b->getButtonText() == t && b->getHeight() == 66) return b;
                if (auto* f = findTab (*ch, t)) return f;
            }
            return nullptr;
        };

        for (int i = 0; i < 6; ++i)
        {
            if (auto* b = findTab (*ed, tabs[i])) b->triggerClick();
            MessageManager::getInstance()->runDispatchLoopUntil (50);
            if (i == 0)
            {
                // show something playing in the track view
                e.requestStart();
                for (int k = 0; k < 60; ++k) r.run (1);
                MessageManager::getInstance()->runDispatchLoopUntil (100);
            }
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto file = outDir.getChildFile (String (names[i]) + ".png");
            file.deleteFile();
            FileOutputStream fos (file);
            PNGImageFormat().writeImageToStream (img, fos);
            std::cout << "screenshot " << file.getFileName() << "\n";
            if (i == 0) { e.requestStop(); r.run (2); }
        }
        ed.reset();
    }

    proc.reset();
    std::cout << (failures == 0 ? "ALL PASSED" : String (failures) + " FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}

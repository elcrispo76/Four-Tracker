// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

// Offline test: drives the processor with MIDI the way a live player would,
// checks pattern switching, writes the audio to a WAV file and (optionally)
// renders each editor page to PNG.
//
//   FourTrackerTests <outputDir> [--screens]

#include <juce_gui_basics/juce_gui_basics.h>
#include <thread>
#include <cstdlib>
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
    std::cout << "      sizes: processor " << sizeof (FourTrackerProcessor) << " B, editor " << sizeof (FourTrackerEditor) << " B, engine " << sizeof (ft::TrackerEngine) << " B\n";
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
                    if (auto* vb = dynamic_cast<ValueBox*> (ch)) if (vb->getValue() == 4 && vb->getBounds().getX() > 1090 && vb->getBounds().getX() < 1199) return vb;
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
        // SPACING box: the cursor jumps that many rows after a note and with up/down
        {
            std::function<ValueBox* (Component&)> findSp = [&] (Component& c) -> ValueBox*
            {
                for (auto* ch : c.getChildren())
                {
                    if (auto* vb = dynamic_cast<ValueBox*> (ch)) if (vb->getBounds().getX() > 1199 && vb->getBounds().getX() < 1300) return vb;
                    if (auto* f = findSp (*ch)) return f;
                }
                return nullptr;
            };
            auto* sp = findSp (*ed5);
            CHECK (sp != nullptr && sp->getValue() == 1, "SPACING box exists, default 1");
            if (sp != nullptr) sp->setValue (4, true);
            ed5->keyPressed (KeyPress (KeyPress::homeKey, ModifierKeys(), 0));
            ed5->keyPressed (KeyPress ('z', ModifierKeys(), 'z')); ed5->keyStateChanged (false);   // row 0 -> cursor 4
            ed5->keyPressed (KeyPress ('z', ModifierKeys(), 'z')); ed5->keyStateChanged (false);   // row 4 -> cursor 8
            ed5->keyPressed (KeyPress (KeyPress::downKey, ModifierKeys(), 0));                     // 8 -> 12
            ed5->keyPressed (KeyPress ('z', ModifierKeys(), 'z')); ed5->keyStateChanged (false);   // row 12
            auto& pt = e5.getPattern (0);
            CHECK (pt.at (4, 0).note != 0 && pt.at (12, 0).note != 0 && pt.at (8, 0).note == 0, "spacing 4: notes land 4 rows apart, the down arrow skips 4 rows");
            CHECK (pt.at (5, 0).note == 0 && pt.at (6, 0).note == 0 && pt.at (7, 0).note == 0, "spacing 4: rows in between stay empty");
            ed5->keyPressed (KeyPress (KeyPress::upKey, ModifierKeys(), 0));                       // 16 -> 12
            ed5->keyPressed (KeyPress (KeyPress::upKey, ModifierKeys(), 0));                       // 12 -> 8
            ed5->keyPressed (KeyPress ('z', ModifierKeys(), 'z')); ed5->keyStateChanged (false);   // row 8
            CHECK (pt.at (8, 0).note != 0, "the up arrow also moves 4 rows");
            ed5->keyPressed (KeyPress (KeyPress::F6Key, ModifierKeys(), 0));
            MessageManager::getInstance()->runDispatchLoopUntil (40);
            CHECK (sp != nullptr && sp->getValue() == 3, "SPACING box follows F6/F7");
            for (int i = 0; i < 40; ++i) ed5->keyPressed (KeyPress (KeyPress::F7Key, ModifierKeys(), 0));
            MessageManager::getInstance()->runDispatchLoopUntil (40);
            CHECK (sp != nullptr && sp->getValue() == 32, "SPACING goes up to 32");
            if (screens && sp != nullptr)
            {
                auto img = ed5->createComponentSnapshot (ed5->getLocalBounds(), true, 1.0f);
                auto file = outDir.getChildFile ("16_spacing32.png"); file.deleteFile();
                FileOutputStream fos (file); PNGImageFormat().writeImageToStream (img, fos);
            }
            if (sp != nullptr) sp->setValue (1, true);
            for (int r = 2; r < 16; ++r) pt.at (r, 0) = {};
            // Insert pushes the rest of the track down, Backspace pulls it up, Delete clears and moves on
            for (int r = 0; r < 16; ++r) pt.at (r, 0) = {};
            pt.at (0, 0) = { 10, 1, 0, 0 }; pt.at (1, 0) = { 11, 1, 0, 0 }; pt.at (2, 0) = { 12, 1, 0, 0 };
            ed5->keyPressed (KeyPress (KeyPress::homeKey, ModifierKeys(), 0));
            ed5->keyPressed (KeyPress (KeyPress::downKey, ModifierKeys(), 0));                     // row 1
            ed5->keyPressed (KeyPress (KeyPress::insertKey, ModifierKeys(), 0));
            CHECK (pt.at (0, 0).note == 10 && pt.at (1, 0).note == 0 && pt.at (2, 0).note == 11 && pt.at (3, 0).note == 12,
                   "Insert pushes the rest of the track down one row");
            ed5->keyPressed (KeyPress (KeyPress::backspaceKey, ModifierKeys(), 0));
            CHECK (pt.at (1, 0).note == 11 && pt.at (2, 0).note == 12 && pt.at (3, 0).note == 0, "Backspace pulls the rest of the track up");
            ed5->keyPressed (KeyPress (KeyPress::deleteKey, ModifierKeys(), 0));                   // clears row 1, cursor -> 2
            ed5->keyPressed (KeyPress (KeyPress::deleteKey, ModifierKeys(), 0));                   // clears row 2, cursor -> 3
            CHECK (pt.at (1, 0).note == 0 && pt.at (2, 0).note == 0 && pt.at (0, 0).note == 10, "Delete clears the field and moves down to the next row");
            for (int r = 0; r < 16; ++r) pt.at (r, 0) = {};
        }
        // typing X / Z in the effect number gives the filter commands 0X / 0Z
        {
            ed5->keyPressed (KeyPress (KeyPress::homeKey, ModifierKeys(), 0));
            for (int i = 0; i < 4; ++i) ed5->keyPressed (KeyPress (KeyPress::rightKey, ModifierKeys(), 0));
            ed5->keyPressed (KeyPress ('x', ModifierKeys(), 'x'));
            ed5->keyStateChanged (false);
            ed5->keyPressed (KeyPress ('z', ModifierKeys(), 'z'));
            ed5->keyStateChanged (false);
            ed5->keyPressed (KeyPress ('y', ModifierKeys(), 'y'));
            ed5->keyStateChanged (false);
            ed5->keyPressed (KeyPress ('v', ModifierKeys(), 'v'));
            ed5->keyStateChanged (false);
            bool gotX = false, gotZ = false, gotY = false, gotV = false;
            for (int r = 0; r < 64; ++r)
            {
                const auto fx = e5.getPattern (0).at (r, 0).fx;
                gotX = gotX || fx == ft::fxFilterCutoff;
                gotZ = gotZ || fx == ft::fxFilterResonance;
                gotY = gotY || fx == ft::fxDistortion;
                gotV = gotV || fx == ft::fxAccent;
            }
            CHECK (gotX && gotZ && gotY && gotV, "typing X / Z / Y / V in the effect column enters 0X / 0Z / 0Y / 0V");
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

    // MIDI instrument: pattern notes go out as MIDI
    {
        auto pm = std::make_unique<FourTrackerProcessor>();
        pm->prepareToPlay (44100.0, 256);
        auto& em = pm->getEngine();
        pm->newPatternSet();
        auto& in = pm->getInstrument (6);
        in.makeMidi();
        in.params.midiChannel = 2; in.params.midiProgram = 5;
        pm->instrumentChanged (6);
        CHECK (pm->getInstrument (6).isMidi() && pm->getInstrument (6).name == "MIDI CH 1", "makeMidi makes a MIDI instrument");
        em.getPattern (0).at (0, 1) = { (uint8_t) ft::kNativeNote, 6, 0x0C, 0x20 };   // C-4, volume 20 -> velocity 63
        em.getPattern (0).at (2, 1) = { ft::kNoteOff, 0, 0, 0 };
        em.loopPattern = false;
        em.requestStart();
        Array<MidiMessage> out;
        for (int b = 0; b < 200; ++b)
        {
            AudioBuffer<float> buf (2, 256);
            MidiBuffer mb;
            pm->processBlock (buf, mb);
            for (const auto m : mb) out.add (m.getMessage());
        }
        bool prog = false, on = false, off = false; int vel = 0;
        for (auto& m : out)
        {
            if (m.isProgramChange() && m.getChannel() == 2 && m.getProgramChangeNumber() == 4) prog = true;
            if (m.isNoteOn() && m.getChannel() == 2 && m.getNoteNumber() == 60) { on = true; vel = m.getVelocity(); }
            if (m.isNoteOff() && m.getChannel() == 2 && m.getNoteNumber() == 60 && on) off = true;
        }
        CHECK (prog, "MIDI instrument: program change before the first note");
        CHECK (on && vel == 63, "MIDI instrument: C-4 sends note 60 on channel 2, velocity from 0C20 (" + String (vel) + ")");
        CHECK (off, "MIDI instrument: === sends note off");

        auto f = outDir.getChildFile ("midi.ftm");
        pm->saveModule (f);
        auto p2 = std::make_unique<FourTrackerProcessor>();
        p2->loadModule (f);
        CHECK (p2->getInstrument (6).isMidi() && p2->getInstrument (6).params.midiChannel == 2 && p2->getInstrument (6).params.midiProgram == 5,
               "MIDI instrument settings are saved in modules");
        if (screens)
        {
            pm->selectedInstrument = 6;
            std::unique_ptr<AudioProcessorEditor> edm (pm->createEditor());
            edm->setSize (1536, 1024);
            std::function<Button* (Component&, const String&)> fb = [&] (Component& c, const String& t) -> Button*
            {
                for (auto* ch : c.getChildren()) { if (auto* b = dynamic_cast<PixelButton*> (ch)) if (b->getButtonText() == t) return b; if (auto* x = fb (*ch, t)) return x; }
                return nullptr;
            };
            if (auto* b = fb (*edm, "INSTRUMENTS")) b->triggerClick();
            MessageManager::getInstance()->runDispatchLoopUntil (80);
            auto img = edm->createComponentSnapshot (edm->getLocalBounds(), true, 1.0f);
            auto file = outDir.getChildFile ("15_midi_instrument.png"); file.deleteFile();
            FileOutputStream fos (file); PNGImageFormat().writeImageToStream (img, fos);
            edm.reset();
        }
    }

    // 4 / 8 / 16 channels
    {
        auto pc = std::make_unique<FourTrackerProcessor>();
        pc->prepareToPlay (44100.0, 256);
        Runner rc { *pc };
        auto& ec = pc->getEngine();
        CHECK (pc->getNumChannels() == 4, "starts with 4 channels");
        pc->setNumChannels (8);
        CHECK (pc->getNumChannels() == 8, "8 channels");
        // a kick on channel 7 only (channels 1-4 muted)
        for (int c = 0; c < 4; ++c) ec.mix[c].mute = true;
        ec.getPattern (0).at (0, 6) = { (uint8_t) ft::kNativeNote, 1, 0, 0 };
        ec.requestStart();
        rc.run (40);
        float pk = 0; for (int i = rc.written - 40 * Runner::block; i < rc.written; ++i) pk = jmax (pk, std::abs (rc.all.getSample (0, i)));
        CHECK (pk > 0.05f, "channel 7 plays in an 8-channel song");
        ec.requestStop(); rc.run (2);

        auto f = outDir.getChildFile ("eight.ftm");
        pc->saveModule (f);
        auto p2 = std::make_unique<FourTrackerProcessor>();
        CHECK (p2->loadModule (f).isEmpty() && p2->getNumChannels() == 8 && p2->getEngine().getPattern (0).at (0, 6).note == ft::kNativeNote,
               "an 8-channel module loads with 8 channels and its notes");
        // an old 4-channel module (written before channel counts) still loads as 4 channels
        {
            auto p4 = std::make_unique<FourTrackerProcessor>();
            p4->setNumChannels (16);
            p4->loadModule (outDir.getChildFile ("test.ftm"));
            CHECK (p4->getNumChannels() == 4 || outDir.getChildFile ("test.ftm").existsAsFile() == false, "a module saved with 4 channels loads as 4");
        }
        // fewer channels clears the ones beyond
        pc->setNumChannels (4);
        CHECK (pc->getEngine().getPattern (0).at (0, 6).isEmpty(), "going back to 4 channels clears channels 5-8");

        // editor: + / - TRACK, and the view pages sideways
        pc->setNumChannels (4);
        std::unique_ptr<AudioProcessorEditor> edc (pc->createEditor());
        edc->setSize (1536, 1024);
        MessageManager::getInstance()->runDispatchLoopUntil (60);
        std::function<Component* (Component&, std::function<bool (Component&)>)> findC2 =
            [&] (Component& c, std::function<bool (Component&)> pred) -> Component*
        {
            for (auto* ch : c.getChildren()) { if (pred (*ch)) return ch; if (auto* fnd = findC2 (*ch, pred)) return fnd; }
            return nullptr;
        };
        auto* tc = findC2 (*edc, [] (Component& c) { return dynamic_cast<TrackCount*> (&c) != nullptr; });
        auto btn = [&] (const String& t) { return dynamic_cast<Button*> (tc ? findC2 (*tc, [t] (Component& c) { auto* b = dynamic_cast<PixelButton*> (&c); return b && b->getButtonText() == t; }) : nullptr); };
        CHECK (tc != nullptr && btn ("+") && btn ("-"), "title bar has - TRACKS +");
        for (int i = 0; i < 3; ++i) if (auto* b = btn ("+")) b->onClick();
        CHECK (pc->getNumChannels() == 7, "+ adds one track at a time (7)");
        pc->getEngine().getPattern (0).at (5, 6) = { 30, 2, 0, 0 };
        if (auto* b = btn ("-")) b->onClick();
        CHECK (pc->getNumChannels() == 7, "- on a track with notes asks first");
        if (auto* b = btn ("-")) b->onClick();
        CHECK (pc->getNumChannels() == 6, "- again removes it");
        if (auto* b = btn ("+")) b->onClick();
        if (auto* b = btn ("+")) b->onClick();
        CHECK (pc->getNumChannels() == 8, "back to 8 tracks");
        for (int i = 0; i < 20; ++i) if (auto* b = btn ("+")) b->onClick();
        CHECK (pc->getNumChannels() == 16, "16 tracks is the most");
        pc->setNumChannels (8);
        edc->keyPressed (KeyPress (KeyPress::homeKey));
        for (int i = 0; i < 4; ++i) edc->keyPressed (KeyPress (KeyPress::tabKey));   // to track 5
        pc->getEngine().getPattern (0).at (0, 4) = { 30, 2, 0, 0 };
        MessageManager::getInstance()->runDispatchLoopUntil (120);
        if (screens)
        {
            auto img = edc->createComponentSnapshot (edc->getLocalBounds(), true, 1.0f);
            auto file = outDir.getChildFile ("14_channels_5_8.png"); file.deleteFile();
            FileOutputStream fos (file); PNGImageFormat().writeImageToStream (img, fos);
        }
        auto* strip = findC2 (*edc, [] (Component& c) { return dynamic_cast<ChannelStrip*> (&c) != nullptr; });
        CHECK (strip != nullptr, "channel strips exist");

        // the < 1-4 5-8 ... > bar under the pattern
        auto* nav = dynamic_cast<TrackNav*> (findC2 (*edc, [] (Component& c) { return dynamic_cast<TrackNav*> (&c) != nullptr; }));
        auto navBtn = [&] (const String& t) { return dynamic_cast<PixelButton*> (nav ? findC2 (*nav, [t] (Component& c) { auto* b = dynamic_cast<PixelButton*> (&c); return b && b->isVisible() && b->getButtonText() == t; }) : nullptr); };
        auto navArrow = [&] (bool right) { PixelButton* found = nullptr; int x = right ? -1 : 99999;
            if (nav) for (auto* c : nav->getChildren()) if (auto* b = dynamic_cast<PixelButton*> (c)) if (b->getButtonText().isEmpty() && (right ? b->getX() > x : b->getX() < x)) { x = b->getX(); found = b; }
            return found; };
        CHECK (nav != nullptr && nav->isVisible(), "track bar shows under the pattern with 8 tracks");
        CHECK (navBtn ("TRACKS 1-4") != nullptr && navBtn ("TRACKS 1-4")->getToggleState(), "track bar marks 1-4 as shown");
        if (auto* b = navArrow (true)) b->onClick();
        MessageManager::getInstance()->runDispatchLoopUntil (60);
        CHECK (navBtn ("5-8") != nullptr && navBtn ("5-8")->getToggleState(), "> goes on to tracks 5-8");
        if (auto* b = navArrow (false)) b->onClick();
        MessageManager::getInstance()->runDispatchLoopUntil (60);
        CHECK (navBtn ("TRACKS 1-4") != nullptr && navBtn ("TRACKS 1-4")->getToggleState(), "< goes back to tracks 1-4");
        pc->setNumChannels (14);
        MessageManager::getInstance()->runDispatchLoopUntil (60);
        if (auto* b = navBtn ("9-12")) b->onClick();
        MessageManager::getInstance()->runDispatchLoopUntil (60);
        CHECK (navBtn ("9-12") != nullptr && navBtn ("9-12")->getToggleState(), "clicking 9-12 shows tracks 9-12");
        if (auto* b = navArrow (true)) b->onClick();
        MessageManager::getInstance()->runDispatchLoopUntil (60);
        CHECK (navBtn ("13-14") != nullptr && navBtn ("13-14")->getToggleState(), "> goes on to tracks 13-14");
        if (screens)
        {
            auto img = edc->createComponentSnapshot (edc->getLocalBounds(), true, 1.0f);
            auto file = outDir.getChildFile ("15_track_bar.png"); file.deleteFile();
            FileOutputStream fos (file); PNGImageFormat().writeImageToStream (img, fos);
        }
        pc->setNumChannels (4);
        MessageManager::getInstance()->runDispatchLoopUntil (60);
        CHECK (nav->isVisible() && navBtn ("TRACKS 1-4") != nullptr && navBtn ("5-8") == nullptr, "with 4 tracks the bar shows only TRACKS 1-4");
        edc.reset();
    }

    // Sample Edit shortcuts: click the waveform, then Ctrl+A / C / V / X / Delete
    {
        auto pk = std::make_unique<FourTrackerProcessor>();
        pk->prepareToPlay (44100.0, 256);
        pk->selectedInstrument = 1;                                   // KICK.WAV, 22050 frames
        std::unique_ptr<AudioProcessorEditor> edk (pk->createEditor());
        edk->setSize (1536, 1024);
        std::function<Component* (Component&, std::function<bool (Component&)>)> findK =
            [&] (Component& c, std::function<bool (Component&)> pred) -> Component*
        {
            for (auto* ch : c.getChildren()) { if (pred (*ch)) return ch; if (auto* f = findK (*ch, pred)) return f; }
            return nullptr;
        };
        if (auto* tab = dynamic_cast<Button*> (findK (*edk, [] (Component& c) { auto* b = dynamic_cast<PixelButton*> (&c); return b && b->getButtonText() == "SAMPLE EDIT"; })))
            tab->triggerClick();
        MessageManager::getInstance()->runDispatchLoopUntil (30);
        auto* page = findK (*edk, [] (Component& c) { return dynamic_cast<SampleEditPage*> (&c) != nullptr; });
        auto* wv = page ? dynamic_cast<WaveformView*> (findK (*page, [] (Component& c) { return dynamic_cast<WaveformView*> (&c) != nullptr; })) : nullptr;
        const auto ctrl = ModifierKeys (ModifierKeys::commandModifier);
        auto len = [&] { return pk->getInstrument (1).data->getLength(); };
        const int len0 = len();

        edk->keyPressed (KeyPress ('a', ctrl, 0));
        CHECK (wv != nullptr && ! wv->hasSelection(), "Ctrl+A does nothing before the waveform is clicked");
        if (wv && wv->onActivated) wv->onActivated();                 // = a click on the waveform
        edk->keyPressed (KeyPress ('a', ctrl, 0));
        CHECK (wv && wv->hasSelection() && wv->getSelectionStart() == 0 && wv->getSelectionEnd() == len0 - 1, "Ctrl+A selects the whole sample");
        edk->keyPressed (KeyPress ('c', ctrl, 0));
        if (wv) wv->clearSelection();
        edk->keyPressed (KeyPress ('v', ctrl, 0));
        CHECK (len() == len0 * 2, "Ctrl+C then Ctrl+V pastes a copy (" + String (len()) + ")");
        if (wv) wv->setSelection (0, len0 - 1);
        edk->keyPressed (KeyPress ('x', ctrl, 0));
        CHECK (len() == len0, "Ctrl+X cuts the selection");
        if (wv) wv->setSelection (0, 999);
        edk->keyPressed (KeyPress (KeyPress::deleteKey));
        CHECK (len() == len0 - 1000, "Delete removes the selection");
        edk.reset();
    }

    // MIX in Sample Edit: another slot's sample mixed in at a position
    {
        ft::Instrument a, b;
        ft::SampleData::Ptr da = new ft::SampleData(); da->sampleRate = 44100; da->buffer.setSize (1, 1000); da->buffer.clear();
        for (int i = 0; i < 1000; ++i) da->buffer.setSample (0, i, 0.25f);
        a.data = da; a.resetPoints();
        ft::SampleData::Ptr db = new ft::SampleData(); db->sampleRate = 22050; db->buffer.setSize (2, 600);
        for (int i = 0; i < 600; ++i) { db->buffer.setSample (0, i, 0.5f); db->buffer.setSample (1, i, -0.5f); }
        b.data = db; b.resetPoints();
        CHECK (a.mixIn (b, 500), "mixIn works");
        // 600 frames at 22050 Hz = 1199 frames at 44100 Hz, starting at 500 -> length 1699, stereo
        CHECK (a.data->getNumChannels() == 2 && std::abs (a.data->getLength() - 1699) <= 1, "MIX: longer and stereo (" + String (a.data->getLength()) + ")");
        CHECK (std::abs (a.data->buffer.getSample (0, 100) - 0.25f) < 1e-4f && std::abs (a.data->buffer.getSample (0, 800) - 0.75f) < 1e-3f
               && std::abs (a.data->buffer.getSample (1, 800) + 0.25f) < 1e-3f && std::abs (a.data->buffer.getSample (0, 1500) - 0.5f) < 1e-3f,
               "MIX: both samples are summed from the position");
        CHECK (a.params.end == a.data->getLength() - 1, "MIX: end point follows the longer sample");
        ft::Instrument c; c.data = da; c.resetPoints();
        ft::Instrument loud; ft::SampleData::Ptr dl = new ft::SampleData(); dl->buffer.setSize (1, 1000);
        for (int i = 0; i < 1000; ++i) dl->buffer.setSample (0, i, 0.9f);
        loud.data = dl; loud.resetPoints();
        c.mixIn (loud, 0);
        CHECK (c.data->buffer.getMagnitude (0, c.data->getLength()) <= 1.0f, "MIX: scaled down instead of clipping");
    }

   #if FOURTRACKER_FX_HOST
    // FX build: scan a folder, load a VST3 into a channel slot, bypass, settings survive a restart
    {
        auto vstDir = File (argc > 3 ? String (argv[3]) : String());
        AudioProcessor::setTypeOfNextNewPlugin (AudioProcessor::wrapperType_Standalone);
        auto pf = std::make_unique<FourTrackerProcessor>();
        AudioProcessor::setTypeOfNextNewPlugin (AudioProcessor::wrapperType_Undefined);
        auto* host = pf->getFxHost();
        CHECK (host != nullptr, "FX build: standalone has an FX host");
        pf->prepareToPlay (44100.0, 256);
        Runner rf { *pf };
        auto& ef = pf->getEngine();

        auto peakCh = [&] (int blocks)
        {
            const int start = rf.written;
            rf.run (blocks);
            float pk = 0;
            for (int i = start; i < rf.written; ++i) pk = jmax (pk, std::abs (rf.all.getSample (0, i)));
            return pk;
        };

        // pattern 00 of the demo: only channel 1 (kick) plays when the other channels are muted
        for (int c = 1; c < 4; ++c) ef.mix[c].mute = true;
        ef.requestStart();
        const float dry = peakCh (120);
        CHECK (dry > 0.05f, "FX build: channel 1 plays dry (" + String (dry, 3) + ")");

        // the scan report explains plugins it could not use (wrong bitness, no VST2 support)
        if (host != nullptr)
        {
            auto dir = outDir.getChildFile ("fake-plugins");
            dir.deleteRecursively();
            dir.createDirectory();
            auto makePE = [] (const File& f, int machine)
            {
                MemoryOutputStream m;
                m.writeShort (0x5a4d);
                for (int i = 2; i < 0x3c; ++i) m.writeByte (0);
                m.writeInt (0x40);
                m.writeInt (0x00004550);
                m.writeShort ((short) machine);
                for (int i = 0; i < 64; ++i) m.writeByte (0);
                f.replaceWithData (m.getData(), m.getDataSize());
            };
            makePE (dir.getChildFile ("Old32bitDelay.dll"), 0x14c);
            makePE (dir.getChildFile ("Modern64bitReverb.dll"), 0x8664);
            host->getSearchPath() = FileSearchPath (dir.getFullPathName());
            host->startScan();
            for (int i = 0; i < 200 && host->isScanning(); ++i) MessageManager::getInstance()->runDispatchLoopUntil (20);
            const auto rep = host->getScanReport();
            std::cout << "      scan report: " << rep << "\n";
            CHECK (rep.contains ("1 ARE 32-BIT"), "FX build: report finds the 32-bit plugin");
            CHECK (host->getFormatNames().contains ("VST2") || rep.contains (".DLL SKIPPED"),
                   "FX build: report says when VST2 support is missing");
        }

        if (host != nullptr && vstDir.isDirectory())
        {
            host->getSearchPath() = FileSearchPath (vstDir.getFullPathName());
            host->startScan();
            for (int i = 0; i < 400 && host->isScanning(); ++i) MessageManager::getInstance()->runDispatchLoopUntil (20);
            CHECK (! host->isScanning(), "FX build: scan finishes");
            // the test plugin is Four Tracker itself (an instrument), so it is not offered as an effect
            CHECK (host->getEffects().isEmpty(), "FX build: instruments are left out of the effect list");

            KnownPluginList kl;
            OwnedArray<PluginDescription> found;
            AudioPluginFormatManager fm; fm.addDefaultFormats();
            for (auto* f : fm.getFormats())
                for (auto& file : f->searchPathsForPlugins (FileSearchPath (vstDir.getFullPathName()), true))
                    kl.scanAndAddFile (file, true, found, *f);
            CHECK (found.size() > 0, "FX build: test VST3 found");
            if (found.size() > 0)
            {
                // an instrument in the insert replaces the channel with its own (silent) output
                const auto err = host->loadSlot (0, 0, *found[0]);
                CHECK (err.isEmpty() && host->hasPlugin (0, 0), "FX build: plugin loads into CH1 FX1 " + err);
                const float wet = peakCh (120);
                CHECK (wet < 0.001f, "FX build: CH1 audio goes through the plugin (" + String (wet, 4) + ")");
                host->setBypass (0, 0, true);
                const float byp = peakCh (120);
                CHECK (byp > 0.05f, "FX build: BYPASS gives the dry sound back");
                host->setBypass (0, 0, false);

                // settings file: a new FX host restores the slot after prepare
                host->saveSettings();
                ef.requestStop();
                rf.run (2);
                pf.reset();
                AudioProcessor::setTypeOfNextNewPlugin (AudioProcessor::wrapperType_Standalone);
                auto pf2 = std::make_unique<FourTrackerProcessor>();
                AudioProcessor::setTypeOfNextNewPlugin (AudioProcessor::wrapperType_Undefined);
                pf2->prepareToPlay (44100.0, 256);
                for (int i = 0; i < 20 && ! pf2->getFxHost()->hasPlugin (0, 0); ++i) MessageManager::getInstance()->runDispatchLoopUntil (50);
                CHECK (pf2->getFxHost()->hasPlugin (0, 0) && pf2->getFxHost()->getSlotName (0, 0) == found[0]->name,
                       "FX build: CH1 FX1 is restored after a restart");

                if (screens)
                {
                    std::unique_ptr<AudioProcessorEditor> edf (pf2->createEditor());
                    edf->setSize (1536, 1024);
                    MessageManager::getInstance()->runDispatchLoopUntil (80);
                    {
                        auto img0 = edf->createComponentSnapshot (edf->getLocalBounds(), true, 1.0f);
                        auto f0 = outDir.getChildFile ("12_fx_tracks.png");
                        f0.deleteFile();
                        FileOutputStream fos0 (f0);
                        PNGImageFormat().writeImageToStream (img0, fos0);
                    }
                    edf->keyPressed (KeyPress (KeyPress::F8Key));
                    MessageManager::getInstance()->runDispatchLoopUntil (80);
                    auto img = edf->createComponentSnapshot (edf->getLocalBounds(), true, 1.0f);
                    auto file = outDir.getChildFile ("11_fx.png");
                    file.deleteFile();
                    FileOutputStream fos (file);
                    PNGImageFormat().writeImageToStream (img, fos);
                    edf.reset();
                }
                // the FX bar on the tracks page: ON/OFF bypass button, and Enter still starts the pattern
                {
                    std::unique_ptr<AudioProcessorEditor> edb (pf2->createEditor());
                    edb->setSize (1536, 1024);
                    MessageManager::getInstance()->runDispatchLoopUntil (60);
                    std::function<Button* (Component&)> findOn = [&] (Component& c) -> Button*
                    {
                        for (auto* ch : c.getChildren())
                        {
                            if (auto* b = dynamic_cast<PixelButton*> (ch))
                                if (b->getButtonText() == "ON" && b->isEnabled() && dynamic_cast<FxBar*> (b->getParentComponent()) != nullptr) return b;
                            if (auto* f = findOn (*ch)) return f;
                        }
                        return nullptr;
                    };
                    auto* onBtn = findOn (*edb);
                    CHECK (onBtn != nullptr && ! onBtn->getWantsKeyboardFocus(), "FX bar has an ON/OFF button that doesn't take the keyboard");
                    if (onBtn && onBtn->onClick) onBtn->onClick();
                    CHECK (pf2->getFxHost()->isBypassed (0, 0), "FX bar ON/OFF bypasses the effect");
                    if (onBtn && onBtn->onClick) onBtn->onClick();
                    CHECK (! pf2->getFxHost()->isBypassed (0, 0), "...and switches it on again");
                    const bool before = pf2->getEngine().playing.load();
                    edb->keyPressed (KeyPress (KeyPress::returnKey));
                    AudioBuffer<float> tmp (2, 256); MidiBuffer mb; pf2->processBlock (tmp, mb);
                    CHECK (pf2->getEngine().playing.load() != before, "Enter starts the pattern on the tracks page with the FX bar");
                    pf2->getEngine().requestStop(); pf2->processBlock (tmp, mb);
                    edb.reset();
                }
                // chaining: CH2 > INTO CH1 FX, loops refused, the drop-downs under the pattern
                {
                    auto* h = pf2->getFxHost();
                    h->setRoute (1, 0);
                    CHECK (h->getRoute (1) == 0, "FX chaining: CH2 can go into CH1's effects");
                    h->setRoute (0, 1);
                    CHECK (h->getRoute (0) == -1, "FX chaining: a loop (CH1 into CH2 into CH1) is refused");
                    h->setRoute (2, 2);
                    CHECK (h->getRoute (2) == -1, "FX chaining: a channel can't go into itself");
                    std::unique_ptr<AudioProcessorEditor> edr (pf2->createEditor());
                    edr->setSize (1536, 1024);
                    MessageManager::getInstance()->runDispatchLoopUntil (60);
                    std::function<Component* (Component&, std::function<bool (Component&)>)> findR =
                        [&] (Component& c, std::function<bool (Component&)> pred) -> Component*
                    {
                        for (auto* ch : c.getChildren()) { if (pred (*ch)) return ch; if (auto* f = findR (*ch, pred)) return f; }
                        return nullptr;
                    };
                    auto* rb = dynamic_cast<RouteBar*> (findR (*edr, [] (Component& c) { return dynamic_cast<RouteBar*> (&c) != nullptr; }));
                    CHECK (rb != nullptr && rb->isVisible(), "FX chaining: drop-downs under the pattern on the tracks page");
                    auto boxWith = [&] (const String& prefix) { return dynamic_cast<ComboBox*> (rb ? findR (*rb, [prefix] (Component& c) { auto* b = dynamic_cast<ComboBox*> (&c); return b && b->isVisible() && b->getText().startsWith (prefix); }) : nullptr); };
                    auto* b2 = boxWith ("CH2 >");
                    CHECK (b2 != nullptr && b2->getText() == "CH2 > INTO CH1 FX" && ! b2->getWantsKeyboardFocus(), "FX chaining: CH2's list shows INTO CH1 FX");
                    if (auto* b3 = boxWith ("CH3 >")) b3->setSelectedId (1 + 2, sendNotificationSync);   // CH3 > INTO CH2 FX
                    CHECK (h->getRoute (2) == 1, "FX chaining: picking INTO CH2 FX in CH3's list chains it");
                    if (screens)
                    {
                        auto img = edr->createComponentSnapshot (edr->getLocalBounds(), true, 1.0f);
                        auto file = outDir.getChildFile ("17_fx_chain.png"); file.deleteFile();
                        FileOutputStream fos (file); PNGImageFormat().writeImageToStream (img, fos);
                    }
                    edr.reset();
                    pf2->setNumChannels (8);
                    h->setRoute (5, 0); h->setRoute (3, 6);
                    pf2->setNumChannels (4);
                    CHECK (h->getRoute (5) == -1 && h->getRoute (3) == -1, "FX chaining: removing tracks removes their chaining");
                }
                pf2->clearAllEffects();
                CHECK (! pf2->getFxHost()->hasPlugin (0, 0), "FX build: NEW MODULE / Empty removes the effects");
                CHECK (pf2->getFxHost()->getRoute (1) == -1 && pf2->getFxHost()->getRoute (2) == -1, "FX build: NEW MODULE / Empty removes the chaining too");
                pf2.reset();
            }
        }
        if (pf != nullptr) { pf->getEngine().requestStop(); rf.run (2); pf.reset(); }
    }
   #endif

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

        // live: moving CUTOFF while a note is held changes the sound straight away
        auto hf = [&] (int fromSample, int toSample)
        {
            double e = 0;
            for (int i = fromSample; i < toSample; ++i)
            {
                const float d = rs.all.getSample (0, i) - rs.all.getSample (0, i - 1);
                e += d * d;
            }
            return e;
        };
        {
            auto& in = ps->getInstrument (3);
            in.params.attack = 0; in.params.decay = 0; in.params.sustain = 64; in.params.cutoff = 127; in.params.resonance = 0;
            ps->instrumentChanged (3);
            es.requestPreview (3, ft::kNativeNote + 12);
            rs.run (40);
            const int a = rs.written; rs.run (20); const double before = hf (a, rs.written);
            in.params.cutoff = 30;                       // the slider moves while the key is held
            ps->instrumentChanged (3);
            rs.run (20);
            const int b = rs.written; rs.run (20); const double after = hf (b, rs.written);
            CHECK (after < before * 0.1, "live: CUTOFF slider is heard on the held note (" + String (after / before, 3) + ")");
            es.requestStopPreview();
            rs.run (60);
            in.params.cutoff = 127; ps->instrumentChanged (3);
        }

        // DISTORTION adds harmonics, ACCENT makes the note start louder
        {
            auto& in = ps->getInstrument (1);                   // sine
            in.params.attack = 0; in.params.decay = 0; in.params.sustain = 64; in.params.cutoff = 127; in.params.resonance = 0;
            in.params.distortion = 0; in.params.accent = 0;
            ps->instrumentChanged (1);
            es.requestPreview (1, ft::kNativeNote);
            rs.run (30);
            const int a = rs.written; rs.run (20); const double clean = hf (a, rs.written);
            in.params.distortion = 64; ps->instrumentChanged (1);
            rs.run (20);
            const int b = rs.written; rs.run (20); const double driven = hf (b, rs.written);
            CHECK (driven > clean * 4.0, "DISTORTION adds harmonics (" + String (driven / clean, 1) + "x)");
            es.requestStopPreview(); rs.run (60);
            in.params.distortion = 0;

            in.params.accent = 64; ps->instrumentChanged (1);
            const int c0 = rs.written;
            es.requestPreview (1, ft::kNativeNote);
            rs.run (100);
            float early = 0, late = 0;
            for (int i = c0 + 200; i < c0 + 2400; ++i)   early = jmax (early, std::abs (rs.all.getSample (0, i)));
            for (int i = c0 + 20000; i < c0 + 24000; ++i) late = jmax (late, std::abs (rs.all.getSample (0, i)));
            CHECK (early > late * 1.25f, "ACCENT: the note starts louder (" + String (early, 2) + " vs " + String (late, 2) + ")");
            es.requestStopPreview(); rs.run (60);
            in.params.accent = 0; ps->instrumentChanged (1);
        }

        // pattern commands: 0Xvv cutoff and 0Zvv resonance, vv = 00-64
        {
            auto& pat = es.getPattern (1);
            pat.at (0, 0)  = { (uint8_t) (ft::kNativeNote + 12), 3, 0, 0 };   // saw, open
            pat.at (16, 0) = { 0, 0, ft::fxFilterCutoff, 0x08 };              // 0X08: dark
            pat.at (17, 0) = { 0, 0, ft::fxFilterResonance, 0x30 };           // 0Z30
            pat.at (32, 0) = { 0, 0, ft::fxFilterCutoff, 0x64 };              // 0X64: open
            es.loopPattern = true;
            es.requestPattern (1);
            rs.run (2);
            const int rowSamples = (int) (6 * 44100.0 * 2.5 / 125.0);
            const int start = rs.written;
            es.requestStart();
            rs.run ((rowSamples * 40) / Runner::block + 4);
            const double open1  = hf (start + rowSamples * 8,  start + rowSamples * 14);
            const double closed = hf (start + rowSamples * 22, start + rowSamples * 28);
            const double open2  = hf (start + rowSamples * 34, start + rowSamples * 39);
            CHECK (closed < open1 * 0.1 && open2 > open1 * 0.5,
                   "0X08 closes and 0X64 reopens the filter during playback (" + String (closed / open1, 3) + ", " + String (open2 / open1, 2) + ")");
            CHECK (ft::fxToString (ft::fxFilterCutoff, 0x64) == "0X64" && ft::fxToString (ft::fxFilterResonance, 0x10) == "0Z10"
                   && ft::fxToString (ft::fxDistortion, 0x32) == "0Y32" && ft::fxToString (ft::fxAccent, 0x00) == "0V00",
                   "sound commands are shown as 0X / 0Z / 0Y / 0V");
            CHECK (ft::filterCommandValue (0x64) == 64 && ft::filterCommandValue (0x99) == 64 && ft::filterCommandValue (0x32) == 32,
                   "0X/0Z values are read as decimal 00-64");
            es.requestStop();
            es.requestPattern (0);
            rs.run (4);
            pat.clear();
        }

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
            in.params.distortion = 33; in.params.accent = 44;
            ps->instrumentChanged (3);
            auto f = outDir.getChildFile ("synth.ftm");
            CHECK (ps->saveModule (f), "module with synths saves");
            auto p2 = std::make_unique<FourTrackerProcessor>();
            CHECK (p2->loadModule (f).isEmpty(), "module with synths loads");
            const auto& q = p2->getInstrument (3).params;
            CHECK (q.synth && q.synthWave == ft::waveSaw && q.cutoff == 55 && q.resonance == 99 && q.attack == 123 && q.release == 456
                   && q.distortion == 33 && q.accent == 44,
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

    // synth MIDI LEARN: knobs on the MIDI keyboard move the synth sliders, per instrument
    {
        auto pm = std::make_unique<FourTrackerProcessor>();
        pm->prepareToPlay (44100.0, Runner::block);
        Runner rm { *pm };
        auto knob = [&] (int cc, int value, int ch = 1)
        {
            rm.run (1, [&] (int, MidiBuffer& mb) { mb.addEvent (MidiMessage::controllerEvent (ch, cc, value), 0); });
            pm->applyMidiKnobs();
        };
        auto& s9 = pm->getInstrument (9).params;
        auto& s10 = pm->getInstrument (10).params;
        pm->getInstrument (9).makeSynth (ft::waveSaw);   pm->instrumentChanged (9);
        pm->getInstrument (10).makeSynth (ft::waveSquare); pm->instrumentChanged (10);

        pm->startSynthLearn (9, ft::spCutoff);
        CHECK (pm->isSynthLearning (9, ft::spCutoff), "MIDI LEARN: waiting for a knob for slot 09 CUTOFF");
        knob (74, 10);
        CHECK (s9.cc[ft::spCutoff] == 74 && ! pm->isSynthLearning (9, ft::spCutoff), "MIDI LEARN: CC 74 learned for slot 09 CUTOFF");
        CHECK (s9.cutoff == 127, "MIDI LEARN: the learning move itself doesn't jump the value");
        knob (74, 0);
        CHECK (s9.cutoff == 0 && s10.cutoff == 127, "CC 74 moves slot 09's cutoff, not slot 10's");
        const int rev = pm->knobRevision.load();
        knob (74, 100);
        CHECK (s9.cutoff == 100 && pm->knobRevision.load() != rev, "the knob moves the cutoff again (and the UI is told)");

        pm->startSynthLearn (10, ft::spCutoff);
        knob (74, 5);
        CHECK (s10.cc[ft::spCutoff] == 74 && s9.cc[ft::spCutoff] == -1, "learning CC 74 for slot 10 takes it away from slot 09 (one knob, one slider)");
        knob (74, 64);
        CHECK (s10.cutoff == 64 && s9.cutoff == 100, "now CC 74 moves only slot 10");

        pm->startSynthLearn (9, ft::spResonance); knob (71, 0);
        pm->startSynthLearn (9, ft::spAttack);    knob (20, 0);
        pm->startSynthLearn (9, ft::spSustain);   knob (21, 0);
        knob (71, 127); knob (20, 127); knob (21, 127);
        CHECK (s9.resonance == 127 && s9.attack == 5000 && s9.sustain == 64, "knobs reach the top of resonance (127), attack (5 s) and sustain (64)");
        knob (20, 64);
        CHECK (s9.attack > 500 && s9.attack < 800, "attack follows the slider's curve (CC 64 -> " + String (s9.attack) + " ms)");

        pm->getEngine().midiChannel = 2;
        knob (71, 0, 1);
        CHECK (s9.resonance == 127, "a knob on another MIDI channel is ignored when MIDI CHANNEL is set");
        knob (71, 0, 2);
        CHECK (s9.resonance == 0, "...and works on the chosen channel");
        pm->getEngine().midiChannel = 0;

        pm->forgetSynthKnob (9, ft::spResonance);
        knob (71, 90);
        CHECK (s9.cc[ft::spResonance] == -1 && s9.resonance == 0, "FORGET KNOB: the knob no longer moves it");

        pm->startSynthLearn (9, ft::spDistortion);
        pm->cancelSynthLearn();
        knob (30, 50);
        CHECK (s9.cc[ft::spDistortion] == -1, "a cancelled MIDI LEARN learns nothing");

        auto f = outDir.getChildFile ("knobs.ftm");
        pm->saveModule (f);
        auto pm2 = std::make_unique<FourTrackerProcessor>();
        CHECK (pm2->loadModule (f).isEmpty() && pm2->getInstrument (9).params.cc[ft::spAttack] == 20
               && pm2->getInstrument (10).params.cc[ft::spCutoff] == 74 && pm2->getInstrument (9).params.cc[ft::spCutoff] == -1,
               "the learned knobs are saved in the module");

        // the Instruments page shows the knobs on the sliders
        pm->selectedInstrument = 9;
        std::unique_ptr<AudioProcessorEditor> edm (pm->createEditor());
        edm->setSize (1536, 1024);
        std::function<Component* (Component&, std::function<bool (Component&)>)> findM =
            [&] (Component& c, std::function<bool (Component&)> pred) -> Component*
        {
            for (auto* ch : c.getChildren()) { if (pred (*ch)) return ch; if (auto* fnd = findM (*ch, pred)) return fnd; }
            return nullptr;
        };
        if (auto* tab = dynamic_cast<Button*> (findM (*edm, [] (Component& c) { auto* b = dynamic_cast<PixelButton*> (&c); return b && b->getButtonText() == "INSTRUMENTS"; })))
            tab->triggerClick();
        pm->startSynthLearn (9, ft::spAccent);
        MessageManager::getInstance()->runDispatchLoopUntil (120);
        int sliders = 0, rightClick = 0;
        std::function<void (Component&)> count = [&] (Component& c)
        {
            for (auto* ch : c.getChildren())
            {
                if (auto* sl = dynamic_cast<PixelSlider*> (ch)) { if (sl->isVisible()) { ++sliders; if (sl->onRightClick) ++rightClick; } }
                count (*ch);
            }
        };
        count (*edm);
        CHECK (sliders == 8 && rightClick == 8, "all 8 synth sliders have MIDI LEARN on right-click");
        if (screens)
        {
            auto img = edm->createComponentSnapshot (edm->getLocalBounds(), true, 1.0f);
            auto file = outDir.getChildFile ("18_synth_knobs.png"); file.deleteFile();
            FileOutputStream fos (file); PNGImageFormat().writeImageToStream (img, fos);
        }
        pm->cancelSynthLearn();
        edm.reset();
    }

    // MUTE and SOLO on the channel strips
    {
        auto pso = std::make_unique<FourTrackerProcessor>();
        pso->prepareToPlay (44100.0, Runner::block);
        auto& es = pso->getEngine();
        std::array<double, ft::kNumChannels> energy {};
        es.channelInsert = [&] (int ch, float* l, float* r, int n)
        { for (int i = 0; i < n; ++i) energy[(size_t) ch] += (double) l[i] * l[i] + (double) r[i] * r[i]; };
        auto play = [&]
        {
            energy.fill (0.0);
            es.requestPattern (0);
            if (! es.playing.load()) es.requestStartStop();
            Runner rr { *pso };
            rr.run (300);
            es.requestStop(); rr.run (2);
        };
        play();
        CHECK (energy[0] > 0 && energy[1] > 0 && energy[2] > 0, "solo test: channels 1-3 play");
        es.mix[1].solo = true;
        play();
        CHECK (energy[1] > 0 && energy[0] == 0 && energy[2] == 0 && energy[3] == 0, "SOLO on CH2: only CH2 is heard");
        es.mix[2].solo = true;
        play();
        CHECK (energy[1] > 0 && energy[2] > 0 && energy[0] == 0, "SOLO on CH2 and CH3: both are heard, CH1 isn't");
        es.mix[2].mute = true;
        play();
        CHECK (energy[2] == 0 && energy[1] > 0, "MUTE wins over SOLO on the same channel");
        es.mix[1].solo = false; es.mix[2].solo = false; es.mix[2].mute = false;
        play();
        CHECK (energy[0] > 0 && energy[2] > 0, "SOLO off: everything is heard again");
        es.mix[1].solo = true;
        auto f = outDir.getChildFile ("solo.ftm");
        pso->saveModule (f);
        auto pso2 = std::make_unique<FourTrackerProcessor>();
        CHECK (pso2->loadModule (f).isEmpty() && pso2->getEngine().mix[1].solo.load(), "SOLO is saved in the module");
        pso->setNumChannels (8);
        es.mix[6].solo = true;
        pso->setNumChannels (4);
        CHECK (! es.mix[6].solo.load(), "removing a track takes its SOLO with it");
        es.mix[1].solo = false;
        es.channelInsert = nullptr;

        // the buttons on the strip
        std::unique_ptr<AudioProcessorEditor> eds (pso->createEditor());
        eds->setSize (1536, 1024);
        MessageManager::getInstance()->runDispatchLoopUntil (60);
        std::function<PixelButton* (Component&, const String&)> findB = [&] (Component& c, const String& t) -> PixelButton*
        {
            for (auto* ch : c.getChildren())
            {
                if (auto* b = dynamic_cast<PixelButton*> (ch)) if (b->getButtonText() == t && dynamic_cast<ChannelStrip*> (b->getParentComponent()) != nullptr) return b;
                if (auto* fnd = findB (*ch, t)) return fnd;
            }
            return nullptr;
        };
        auto* soloB = findB (*eds, "SOLO");
        auto* muteB = findB (*eds, "MUTE");
        CHECK (soloB != nullptr && muteB != nullptr && ! soloB->getWantsKeyboardFocus(), "channel strips have MUTE and SOLO buttons");
        if (soloB) soloB->onClick();
        if (muteB) muteB->onClick();
        CHECK (es.mix[0].solo.load() && es.mix[0].mute.load(), "the buttons switch SOLO and MUTE on CH1");
        es.mix[0].mute = false;
        pso->setNumChannels (16);
        es.mix[0].solo = true;
        es.mix[13].solo = true;
        eds->keyPressed (KeyPress (KeyPress::homeKey));
        MessageManager::getInstance()->runDispatchLoopUntil (120);
        if (screens)
        {
            auto img = eds->createComponentSnapshot (eds->getLocalBounds(), true, 1.0f);
            auto file = outDir.getChildFile ("19_solo.png"); file.deleteFile();
            FileOutputStream fos (file); PNGImageFormat().writeImageToStream (img, fos);
        }
        for (int i = 0; i < 12; ++i) eds->keyPressed (KeyPress (KeyPress::tabKey));   // to tracks 13-16
        MessageManager::getInstance()->runDispatchLoopUntil (120);
        if (screens)
        {
            auto img = eds->createComponentSnapshot (eds->getLocalBounds(), true, 1.0f);
            auto file = outDir.getChildFile ("20_solo_13_16.png"); file.deleteFile();
            FileOutputStream fos (file); PNGImageFormat().writeImageToStream (img, fos);
        }
        es.mix[0].solo = false; es.mix[13].solo = false;
        eds.reset();
    }

    // FX chaining: a channel's sound can go on into another channel's effects
    {
        auto pr = std::make_unique<FourTrackerProcessor>();
        pr->prepareToPlay (44100.0, Runner::block);
        auto& er = pr->getEngine();
        std::array<double, ft::kNumChannels> energy {};
        std::array<float, ft::kNumChannels> gain; gain.fill (1.0f);
        std::array<int, ft::kNumChannels> route; route.fill (-1);
        er.channelInsert = [&] (int ch, float* l, float* rr, int n)
        {
            for (int i = 0; i < n; ++i) { energy[(size_t) ch] += (double) l[i] * l[i] + (double) rr[i] * rr[i]; l[i] *= gain[(size_t) ch]; rr[i] *= gain[(size_t) ch]; }
        };
        er.channelRoute = [&] (int ch) { return route[(size_t) ch]; };
        auto play = [&] (double& outEnergy)
        {
            energy.fill (0.0);
            er.requestPattern (0);
            if (! er.playing.load()) er.requestStartStop();
            Runner rr { *pr };
            rr.run (400);
            er.requestStop(); rr.run (2);
            outEnergy = 0.0;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < rr.all.getNumSamples(); ++i) outEnergy += (double) rr.all.getSample (c, i) * rr.all.getSample (c, i);
        };
        double outPlain = 0, outChained = 0, outLoop = 0;
        play (outPlain);
        const double e0 = energy[0], e1 = energy[1];
        CHECK (e0 > 0.0 && e1 > 0.0, "chaining test: channels 1 and 2 both play");
        route[1] = 0;                                   // CH2 > INTO CH1 FX
        play (outChained);
        CHECK (energy[1] > 0.5 * e1 && energy[0] > e0 * 1.05, "CH2 chained into CH1: CH2's own effects still run, then CH1's effects get CH2's sound too");
        gain[0] = 0.0f;                                 // CH1's "effect" silences: CH2 must go silent with it
        double outMuted = 0; play (outMuted);
        gain[0] = 1.0f; route[1] = -1;
        double outMutedPlain = 0; gain[0] = 0.0f; play (outMutedPlain); gain[0] = 1.0f;
        CHECK (outMuted < outMutedPlain * 0.9, "CH2 chained into CH1 goes through CH1's effects (quieter when CH1's effect silences)");
        route[0] = 1; route[1] = 0;                     // a loop is ignored, nothing hangs
        play (outLoop);
        CHECK (outLoop > 0.0, "a chaining loop is ignored (the sound still comes out)");
        er.channelInsert = nullptr; er.channelRoute = nullptr;
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

    // soak: a standalone-like session with audio running on its own thread (FT_SOAK=1)
    if (std::getenv ("FT_SOAK") != nullptr)
    {
        AudioProcessor::setTypeOfNextNewPlugin (AudioProcessor::wrapperType_Standalone);
        auto ps2 = std::make_unique<FourTrackerProcessor>();
        AudioProcessor::setTypeOfNextNewPlugin (AudioProcessor::wrapperType_Undefined);
        ps2->prepareToPlay (48000.0, 480);
        std::atomic<bool> run { true };
        std::thread audio ([&]
        {
            AudioBuffer<float> buf (2, 480);
            while (run.load())
            {
                MidiBuffer mb;
                ps2->processBlock (buf, mb);
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
            }
        });
        std::unique_ptr<AudioProcessorEditor> eds2 (ps2->createEditor());
        eds2->setSize (1536, 1024);
        MessageManager::getInstance()->runDispatchLoopUntil (500);
        ps2->setNumChannels (16);
        MessageManager::getInstance()->runDispatchLoopUntil (500);
        eds2->keyPressed (KeyPress (KeyPress::returnKey));
        for (int i = 0; i < 20; ++i)
        {
            eds2->keyPressed (KeyPress (KeyPress::tabKey));
            MessageManager::getInstance()->runDispatchLoopUntil (100);
        }
        const char* tabs[] = { "INSTRUMENTS", "SAMPLE EDIT", "SONG / PATTERN", "BLOCK", "MIDI / AUDIO", "" };
        std::function<Button* (Component&, const String&)> ft2 = [&] (Component& c, const String& t) -> Button*
        {
            for (auto* ch : c.getChildren()) { if (auto* b = dynamic_cast<PixelButton*> (ch)) if (b->getButtonText() == t && b->getHeight() == 66) return b; if (auto* x = ft2 (*ch, t)) return x; }
            return nullptr;
        };
        for (auto* t : tabs) { if (auto* b = ft2 (*eds2, t)) b->triggerClick(); MessageManager::getInstance()->runDispatchLoopUntil (300); }
        eds2->keyPressed (KeyPress (KeyPress::F8Key));
        MessageManager::getInstance()->runDispatchLoopUntil (1500);
        run = false;
        audio.join();
        eds2.reset();
        ps2.reset();
        CHECK (true, "soak: standalone session with audio thread survived");
    }

    proc.reset();
    std::cout << (failures == 0 ? "ALL PASSED" : String (failures) + " FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}

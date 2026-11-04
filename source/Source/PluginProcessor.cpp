// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DemoContent.h"
#include "FxHost.h"

using namespace ft;

namespace ids
{
    static const juce::Identifier root ("FOURTRACKER"), patterns ("PATTERNS"), pat ("P"), insts ("INSTRUMENTS"), inst ("I"),
        mixer ("MIXER"), chan ("CH"), patternSet ("PATTERNSET"), module ("MODULE"), patternFile ("PATTERNFILE");
}

FourTrackerProcessor::FourTrackerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formats.registerBasicFormats();

    createDemoInstruments (instruments);
    createDemoPatterns (engine);
    for (int i = 1; i < kNumInstruments; ++i)
        engine.publishInstrument (i, instruments[(size_t) i].makeSnapshot());


   #if FOURTRACKER_FX_HOST
    // VST effects only in the standalone app: inside a DAW, use the DAW's own effects
    if (wrapperType == wrapperType_Standalone)
    {
        fx = std::make_unique<FxHost>();
        engine.channelInsert = [this] (int ch, float* l, float* r, int n)
        {
            const auto& t = engine.insertTransport;
            fx->setTransport ({ t.playing, t.bpm, t.ppq, t.samples });
            fx->process (ch, l, r, n);
        };
        engine.channelRoute = [this] (int ch) { return fx->getRoute (ch); };
        engine.masterInsert = [this] (float* l, float* r, int n)
        {
            const auto& t = engine.insertTransport;
            fx->setTransport ({ t.playing, t.bpm, t.ppq, t.samples });
            fx->process (FxHost::kMaster, l, r, n);
        };
    }
   #endif

    if (wrapperType == wrapperType_Standalone) JamRecorder::rescueLeftover();
    else recorder.setUniqueTakeName();
    markSaved();      // the demo song as it starts counts as unchanged

    startTimer (1000);
    knobPump.tick = [this] { applyMidiKnobs(); };
    knobPump.startTimerHz (60);
}

FourTrackerProcessor::~FourTrackerProcessor()
{
    stopTimer();
    knobPump.stopTimer();
   #if FOURTRACKER_FX_HOST
    engine.channelInsert = nullptr;
    engine.channelRoute = nullptr;
    engine.masterInsert = nullptr;
    if (fx != nullptr) { fx->saveSettings(); fx.reset(); }
   #endif
}

void FourTrackerProcessor::clearAllEffects()
{
   #if FOURTRACKER_FX_HOST
    if (fx != nullptr) fx->clearAll();
   #endif
}

FxHost* FourTrackerProcessor::getFxHost() const noexcept
{
   #if FOURTRACKER_FX_HOST
    return fx.get();
   #else
    return nullptr;
   #endif
}

void FourTrackerProcessor::timerCallback()
{
    engine.collectGarbage();
}

void FourTrackerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
   #if FOURTRACKER_FX_HOST
    if (fx != nullptr) fx->prepare (sampleRate, samplesPerBlock);
   #endif
}

bool FourTrackerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void FourTrackerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    double hostBpm = -1.0;
    if (wrapperType != wrapperType_Standalone)
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm())
                    hostBpm = *b;

    engine.process (buffer, midi, hostBpm);
    if (wrapperType == wrapperType_Standalone) recorder.process (buffer);   // REC (the program only): what you hear, after every effect
    midi.clear();
    midi.swapWith (engine.midiOut);       // notes from MIDI instruments go out (device or DAW)
    engine.midiOut.clear();
}

juce::AudioProcessorEditor* FourTrackerProcessor::createEditor()
{
    return new FourTrackerEditor (*this);
}

// ===========================================================================
// instruments
// ===========================================================================

void FourTrackerProcessor::instrumentChanged (int slot)
{
    if (slot <= 0 || slot >= kNumInstruments) return;
    auto& in = instruments[(size_t) slot];
    in.sanitise();
    engine.publishInstrument (slot, in.makeSnapshot());
    sendChangeMessage();
}

// ---------------------------------------------------------------------------
// unsaved changes
// ---------------------------------------------------------------------------

juce::uint64 FourTrackerProcessor::songFingerprint() const
{
    // FNV-1a over everything that a module file holds
    juce::uint64 h = 1469598103934665603ull;
    auto add = [&h] (const void* data, size_t n)
    {
        const auto* p = static_cast<const uint8_t*> (data);
        for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    };
    auto addInt = [&add] (juce::int64 v) { add (&v, sizeof (v)); };
    auto& eng = const_cast<TrackerEngine&> (engine);
    {
        const juce::ScopedLock sl (eng.getDataLock());
        for (int p = 0; p < kMaxPatterns; ++p)
        {
            const auto& pat = eng.getPattern (p);
            addInt (pat.getNumRows());
            add (&pat.cells[0][0], sizeof (Cell) * (size_t) pat.getNumRows() * kNumChannels);
        }
    }
    addInt (engine.speed.load()); addInt (engine.bpm.load()); addInt (engine.loopPattern.load()); addInt (engine.numChannels.load());
    for (int c = 0; c < kNumChannels; ++c)
    {
        addInt (engine.mix[c].volume.load()); addInt (engine.mix[c].pan.load()); addInt (engine.mix[c].pitch.load());
        addInt (engine.mix[c].mute.load()); addInt (engine.mix[c].solo.load());
    }
    for (int i = 1; i < kNumInstruments; ++i)
    {
        const auto& in = instruments[(size_t) i];
        add (in.name.toRawUTF8(), in.name.getNumBytesAsUTF8());
        addInt ((juce::int64) (juce::pointer_sized_int) in.data.get());     // sample edits make a new SampleData
        const auto& p = in.params;
        for (juce::int64 v : { (juce::int64) p.volume, (juce::int64) p.transpose, (juce::int64) p.fineTune, (juce::int64) p.loop,
                               (juce::int64) p.pingPong, (juce::int64) p.loopBackward, (juce::int64) p.reverse, (juce::int64) p.start,
                               (juce::int64) p.end, (juce::int64) p.loopStart, (juce::int64) p.loopEnd, (juce::int64) p.synth,
                               (juce::int64) p.synthWave, (juce::int64) p.attack, (juce::int64) p.decay, (juce::int64) p.sustain,
                               (juce::int64) p.release, (juce::int64) p.cutoff, (juce::int64) p.resonance, (juce::int64) p.distortion,
                               (juce::int64) p.accent, (juce::int64) p.synth2, (juce::int64) p.midi, (juce::int64) p.midiChannel,
                               (juce::int64) p.midiProgram })
            addInt (v);
        if (p.synth2) { const auto t = ft::synth2::toString (p.s2); add (t.toRawUTF8(), t.getNumBytesAsUTF8()); }
    }
    return h;
}

void FourTrackerProcessor::markSaved()        { savedFingerprint = songFingerprint(); }
bool FourTrackerProcessor::hasUnsavedChanges() const { return songFingerprint() != savedFingerprint; }

// ---------------------------------------------------------------------------
// RE-SAMPLE
// ---------------------------------------------------------------------------

juce::String FourTrackerProcessor::resampleInto (int slot, const ResampleRequest& rq, bool& ok)
{
    ok = false;
    if (slot <= 0 || slot >= kNumInstruments) return "CHOOSE AN INSTRUMENT SLOT FIRST";
    const int used = juce::jlimit (1, kNumChannels, engine.numChannels.load());
    juce::uint32 tracks = rq.tracks & ((used >= 32 ? 0xffffffffu : (1u << used) - 1u));
    if (tracks == 0) return "NO TRACKS CHOSEN";

    const double rate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    auto r = std::make_unique<TrackerEngine>();
    r->prepare (rate, 64);

    // the song: every pattern (jumps may go elsewhere), the instruments, tempo and the mixer
    const int pat = juce::jlimit (0, kMaxPatterns - 1, rq.pattern);
    int rows = 0;
    {
        const juce::ScopedLock sl (engine.getDataLock());
        for (int p = 0; p < kMaxPatterns; ++p)
        {
            auto& src = engine.getPattern (p);
            auto& dst = r->getPattern (p);
            dst.numRows = src.getNumRows();
            std::copy (&src.cells[0][0], &src.cells[0][0] + kMaxRows * kNumChannels, &dst.cells[0][0]);
        }
        rows = engine.getPattern (pat).getNumRows();
    }
    const int from = juce::jlimit (0, rows - 1, rq.fromRow);
    const int to = juce::jlimit (from, rows - 1, rq.toRow);
    const int speed0 = juce::jlimit (1, 31, engine.speed.load()), bpm0 = juce::jlimit (32, 999, engine.bpm.load());
    const double rowSeconds = 2.5 / bpm0 * speed0;

    const bool looped = rq.ending == ResampleRequest::loopEnd;
    const int len = to - from + 1;
    // LOOP: the rows are played twice in a row and the second time is kept: it is exactly what you hear
    // when the pattern loops (new notes cut the old ones, only what really rings on goes over the start).
    // The rows are laid out twice (plus one row that marks the end); a very long part loops instead.
    const bool twice = looped && 2 * len + 1 <= kMaxRows;
    if (looped)
    {
        auto& p = r->getPattern (pat);
        Cell sel[kMaxRows][kNumChannels];
        for (int row = 0; row < len; ++row)
            for (int c = 0; c < kNumChannels; ++c)
            {
                Cell cell = p.cells[from + row][c];
                const bool jump = cell.fx == 0x0B || cell.fx == 0x0D || (cell.fx == 0x0E && (cell.param >> 4) == 0x6);
                if (jump) { cell.fx = 0; cell.param = 0; }       // pattern jumps / breaks / loops would leave the part
                sel[row][c] = cell;
            }
        for (int row = 0; row < kMaxRows; ++row)
            for (int c = 0; c < kNumChannels; ++c)
                p.cells[row][c] = row < (twice ? 2 * len + 1 : len) ? sel[row % len][c] : Cell();
        p.numRows = twice ? 2 * len + 1 : len;
    }
    else
    {
        // the rows after the last one are emptied; RING OUT gets a few more empty rows to ring out in
        auto& p = r->getPattern (pat);
        for (int row = to + 1; row < kMaxRows; ++row)
            for (int c = 0; c < kNumChannels; ++c) p.cells[row][c].clear();
        // (at least one empty row: the end is where that row starts)
        const int tailRows = rq.ending == ResampleRequest::ringOut ? (int) std::ceil (4.0 / rowSeconds) + 1 : 1;
        p.numRows = juce::jmin (kMaxRows, to + 1 + tailRows);
    }
    for (int i = 1; i < kNumInstruments; ++i) r->publishInstrument (i, instruments[(size_t) i].makeSnapshot());
    r->speed = speed0;
    r->bpm = bpm0;
    r->numChannels = used;
    r->loopPattern = looped && ! twice;   // otherwise it stops at the end of the prepared pattern
    r->followHostTempo = false;
    r->rawOutput = true;
    for (int c = 0; c < kNumChannels; ++c)
    {
        r->mix[c].volume = engine.mix[c].volume.load();
        r->mix[c].pan = engine.mix[c].pan.load();
        r->mix[c].pitch = engine.mix[c].pitch.load();
        r->mix[c].solo = false;
        r->mix[c].mute = (tracks & (1u << c)) == 0;
    }
    r->requestPattern (pat);
    r->requestStart();

    // play it one sample at a time, so the first and last rows are exact
    const int maxFrames = (int) (rate * juce::jmin (900.0, (to + 1 + len) * rowSeconds * 4.0 + 6.0));
    juce::AudioBuffer<float> out (2, juce::jmax (1, maxFrames));
    out.clear();
    juce::MidiBuffer noMidi;
    float frame[2] = { 0.0f, 0.0f };
    float* chans[2] = { &frame[0], &frame[1] };
    juce::AudioBuffer<float> one (chans, 2, 1);
    int startAt = -1, endAt = -1, n = 0, quiet = 0;
    const int quietNeeded = (int) (rate * 0.05);
    bool started = false;
    int prevRow = 0, wraps = 0;
    while (n < maxFrames)
    {
        frame[0] = frame[1] = 0.0f;
        r->process (one, noMidi, -1.0);
        r->midiOut.clear();
        const bool playing = r->playing.load();
        if (playing) started = true;
        const int rowNow = r->playRow.load();
        if (looped)
        {
            if (started && playing && rowNow < prevRow) ++wraps;
            prevRow = rowNow;
            // the second time round: rows len .. 2 len - 1 (or the second loop of a long part)
            if (startAt < 0 && started && playing && (twice ? rowNow >= len : wraps >= 1)) startAt = n;
            if (startAt >= 0 && endAt < 0 && (twice ? (rowNow >= 2 * len || ! playing) : wraps >= 2)) endAt = n;
            out.setSample (0, n, frame[0]);
            out.setSample (1, n, frame[1]);
            ++n;
            if (endAt >= 0) break;
            if (started && ! playing && startAt < 0) break;
            continue;
        }
        if (startAt < 0 && started && playing && rowNow >= from) startAt = n;     // this sample is the first of row `from`
        out.setSample (0, n, frame[0]);
        out.setSample (1, n, frame[1]);
        ++n;
        if (started && startAt >= 0 && endAt < 0 && (! playing || rowNow > to)) endAt = n - 1;   // the last row is over
        if (endAt >= 0)
        {
            if (rq.ending == ResampleRequest::cutEnd) break;
            quiet = (std::abs (frame[0]) < 1.0e-4f && std::abs (frame[1]) < 1.0e-4f) ? quiet + 1 : 0;
            if (quiet >= quietNeeded || n - endAt > (int) (rate * 4.0)) break;
        }
        if (started && ! playing && startAt < 0) break;
    }
    if (startAt < 0) return "NOTHING WAS PLAYED";
    if (endAt < 0) endAt = n;
    const int last = rq.ending == ResampleRequest::ringOut ? juce::jmax (endAt, n - quiet) : endAt;
    if (last - startAt <= 0) return "NOTHING WAS PLAYED";

    const int length = looped ? endAt - startAt : last - startAt;
    if (length <= 0) return "NOTHING WAS PLAYED";

    float peak = 0.0f;
    for (int c = 0; c < 2; ++c) peak = juce::jmax (peak, out.getMagnitude (c, startAt, length));
    if (peak < 1.0e-4f) return "THOSE TRACKS ARE SILENT THERE";

    auto data = juce::ReferenceCountedObjectPtr<SampleData> (new SampleData());
    data->buffer.setSize (2, length);
    for (int c = 0; c < 2; ++c) data->buffer.copyFrom (c, 0, out, c, startAt, length);
    data->sampleRate = rate;
    data->bitDepth = 16;
    juce::String extra;
    if (rq.normalise || peak > 0.99f)
    {
        data->buffer.applyGain (0.98f / peak);
        extra = rq.normalise ? ", NORMALIZED" : ", TURNED DOWN SO IT DOESN'T CLIP";
    }

    auto& in = instruments[(size_t) slot];
    in.clear();
    in.data = data;
    juce::String trackText;   // short, so it fits in the instrument list: "RS P02 ALL", "RS P02 1+3"
    if (tracks == (((used >= 32 ? 0xffffffffu : (1u << used) - 1u))))
        trackText = "ALL";
    else
        for (int c = 0; c < used; ++c)
            if (tracks & (1u << c)) trackText << (trackText.isEmpty() ? "" : "+") << (c + 1);
    in.name = "RS P" + formatNumber (pat, 2, hexMode) + " " + trackText;
    in.resetPoints();
    if (looped)
    {
        in.params.loop = true;
        in.params.loopStart = 0;
        in.params.loopEnd = length - 1;
        extra << ", LOOP SET";
    }
    instrumentChanged (slot);
    ok = true;
    return "RE-SAMPLED PATTERN " + formatNumber (pat, 2, hexMode) + " ROWS " + formatNumber (from, 2, hexMode) + "-" + formatNumber (to, 2, hexMode)
           + " INTO " + formatNumber (slot, 2, hexMode) + " (" + juce::String (length / rate, 2) + " S" + extra + ")";
}

// ---------------------------------------------------------------------------
// synth MIDI LEARN
// ---------------------------------------------------------------------------

namespace
{
    // MIDI LEARN targets: 0..7 = SYNTH 1's sliders, kSynth2Knob + n = SYNTH 2's knob n
    bool validKnobTarget (int param)
    {
        return juce::isPositiveAndBelow (param, (int) ft::numSynthParams)
            || juce::isPositiveAndBelow (param - FourTrackerProcessor::kSynth2Knob, (int) ft::s2NumParams);
    }
    int* knobSlot (ft::InstrumentParams& p, int param)
    {
        if (param >= FourTrackerProcessor::kSynth2Knob) return &p.cc2[(size_t) (param - FourTrackerProcessor::kSynth2Knob)];
        return &p.cc[(size_t) param];
    }
    juce::String knobTargetName (int param)
    {
        return param >= FourTrackerProcessor::kSynth2Knob ? juce::String (ft::synth2::name (param - FourTrackerProcessor::kSynth2Knob))
                                                          : juce::String (ft::synthParamName (param));
    }
}

void FourTrackerProcessor::startSynthLearn (int slot, int param)
{
    if (slot <= 0 || slot >= kNumInstruments || ! validKnobTarget (param)) return;
    int cc = 0, v = 0;
    while (engine.popController (cc, v)) {}      // only a knob moved from now on counts
    learnSlot = slot;
    learnParam = param;
    ++knobRevision;
}

void FourTrackerProcessor::cancelSynthLearn()
{
    if (learnSlot < 0) return;
    learnSlot = learnParam = -1;
    ++knobRevision;
}

void FourTrackerProcessor::forgetSynthKnob (int slot, int param)
{
    if (slot <= 0 || slot >= kNumInstruments || ! validKnobTarget (param)) return;
    *knobSlot (instruments[(size_t) slot].params, param) = -1;
    instrumentChanged (slot);
    ++knobRevision;
}

void FourTrackerProcessor::applyMidiKnobs()
{
    int cc = 0, value = 0;
    std::array<int, 128> latest;              // only the last value of each knob counts
    latest.fill (-1);
    bool any = false;
    while (engine.popController (cc, value))
    {
        if (learnSlot > 0)
        {
            // one knob moves one slider: take it away from wherever it was
            for (auto& in : instruments)
            {
                for (auto& c : in.params.cc)  if (c == cc) c = -1;
                for (auto& c : in.params.cc2) if (c == cc) c = -1;
            }
            auto& in = instruments[(size_t) learnSlot];
            *knobSlot (in.params, learnParam) = cc;
            knobMessage = "CC " + juce::String (cc) + " NOW MOVES " + knobTargetName (learnParam)
                        + " OF SLOT " + juce::String::toHexString (learnSlot).paddedLeft ('0', 2).toUpperCase();
            const int slot = learnSlot;
            learnSlot = learnParam = -1;
            instrumentChanged (slot);
            ++knobRevision;
            continue;   // the knob that was just learned doesn't jump the value yet
        }
        latest[(size_t) cc] = value;
        any = true;
    }
    if (! any) return;

    bool moved = false;
    for (int slot = 1; slot < kNumInstruments; ++slot)
    {
        auto& in = instruments[(size_t) slot];
        bool changed = false;
        if (in.isSynth())
            for (int p = 0; p < ft::numSynthParams; ++p)
            {
                const int c = in.params.cc[(size_t) p];
                if (c < 0 || latest[(size_t) c] < 0) continue;
                int& ref = ft::synthParamRef (in.params, p);
                const int v = ft::synthParamFromMidi (p, latest[(size_t) c]);
                if (v != ref) { ref = v; changed = true; }
            }
        else if (in.isSynth2())
            for (int p = 0; p < ft::s2NumParams; ++p)
            {
                const int c = in.params.cc2[(size_t) p];
                if (c < 0 || latest[(size_t) c] < 0) continue;
                int& ref = in.params.s2[(size_t) p];
                const int v = juce::roundToInt (latest[(size_t) c] * ft::synth2::maxValue (p) / 127.0);
                if (v != ref) { ref = v; changed = true; }
            }
        else continue;
        if (changed)
        {
            in.sanitise();
            engine.publishInstrument (slot, in.makeSnapshot());   // heard at once; no full UI refresh
            moved = true;
        }
    }
    if (moved) ++knobRevision;
}

juce::String FourTrackerProcessor::loadInstrument (int slot, const juce::File& file)
{
    if (slot <= 0 || slot >= kNumInstruments) return "Pick an instrument slot from 01 upwards";
    Instrument tmp;
    auto err = tmp.loadFromFile (file, formats);
    if (err.isNotEmpty()) return err;
    instruments[(size_t) slot] = tmp;
    instrumentChanged (slot);
    return {};
}

void FourTrackerProcessor::discardInstrument (int slot)
{
    if (slot <= 0 || slot >= kNumInstruments) return;
    instruments[(size_t) slot].clear();
    instrumentChanged (slot);
}

bool FourTrackerProcessor::auditionFile (const juce::File& file, bool play)
{
    if (file != auditionSource || audition.isEmpty())
    {
        ft::Instrument tmp;
        if (! file.existsAsFile() || tmp.loadFromFile (file, formats).isNotEmpty())
        {
            stopAudition();
            audition.clear();
            auditionSource = juce::File();
            engine.publishAudition (nullptr);
            return false;
        }
        tmp.params.loop = false;     // a browser preview always plays once, to the end
        tmp.params.volume = 64;
        tmp.sanitise();
        audition = tmp;
        auditionSource = file;
        engine.publishAudition (audition.makeSnapshot());
    }
    if (play) engine.requestAudition();
    return true;
}

void FourTrackerProcessor::stopAudition()
{
    engine.requestStopPreview();
}

juce::String FourTrackerProcessor::getSupportedAudioWildcard() const
{
    // + Amiga samples: IFF 8SVX, raw 8-bit, and files without an extension ("*.")
    return formats.getWildcardForAllFormats() + ";*.iff;*.8svx;*.8sv;*.raw;*.smp;*.sam;*.snd;*.sample;*.";
}

// ===========================================================================
// serialisation
// ===========================================================================

juce::ValueTree FourTrackerProcessor::patternsToTree() const
{
    juce::ValueTree t (ids::patterns);
    auto& eng = const_cast<TrackerEngine&> (engine);
    for (int p = 0; p < kMaxPatterns; ++p)
    {
        const auto& pat = eng.getPattern (p);
        const int rows = pat.getNumRows();
        if (pat.isEmpty() && rows == kDefaultRows) continue;

        const int chans = juce::jlimit (1, kNumChannels, engine.numChannels.load());
        juce::MemoryBlock mb ((size_t) (rows * chans * 4));
        auto* d = static_cast<uint8_t*> (mb.getData());
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < chans; ++c)
            {
                const auto& cell = pat.at (r, c);
                *d++ = cell.note; *d++ = cell.inst; *d++ = cell.fx; *d++ = cell.param;
            }

        juce::ValueTree pt (ids::pat);
        pt.setProperty ("index", p, nullptr);
        pt.setProperty ("rows", rows, nullptr);
        pt.setProperty ("chans", chans, nullptr);
        pt.setProperty ("data", mb, nullptr);
        t.appendChild (pt, nullptr);
    }
    return t;
}

void FourTrackerProcessor::patternsFromTree (const juce::ValueTree& t)
{
    const juce::ScopedLock sl (engine.getDataLock());
    for (int p = 0; p < kMaxPatterns; ++p)
    {
        engine.getPattern (p).clear();
        engine.getPattern (p).numRows = kDefaultRows;
    }

    for (auto pt : t)
    {
        if (! pt.hasType (ids::pat)) continue;
        const int p = pt.getProperty ("index", -1);
        if (p < 0 || p >= kMaxPatterns) continue;

        auto& pat = engine.getPattern (p);
        const int rows = juce::jlimit (1, kMaxRows, (int) pt.getProperty ("rows", kDefaultRows));
        pat.numRows = rows;

        if (auto* mb = pt.getProperty ("data").getBinaryData())
        {
            const auto* d = static_cast<const uint8_t*> (mb->getData());
            const int chans = juce::jlimit (1, kNumChannels, (int) pt.getProperty ("chans", 4));   // old files: 4
            const int cells = juce::jmin (rows * chans, (int) mb->getSize() / 4);
            for (int i = 0; i < cells; ++i)
            {
                auto& cell = pat.at (i / chans, i % chans);
                cell.note = d[0] <= kNoteOff ? d[0] : 0;
                cell.inst = d[1] < kNumInstruments ? d[1] : 0;
                cell.fx = d[2];
                cell.param = d[3];
                d += 4;
            }
        }
    }
}

/** One instrument (settings and sample) as a tree; used by modules and pattern files. */
static juce::ValueTree instrumentToTree (const Instrument& in, int slot)
{
    juce::ValueTree it (ids::inst);
    const auto& p = in.params;
    it.setProperty ("slot", slot, nullptr);
    it.setProperty ("name", in.name, nullptr);
    it.setProperty ("path", in.sourcePath, nullptr);
    it.setProperty ("volume", p.volume, nullptr);
    it.setProperty ("transpose", p.transpose, nullptr);
    it.setProperty ("finetune", p.fineTune, nullptr);
    it.setProperty ("loop", p.loop, nullptr);
    it.setProperty ("pingpong", p.pingPong, nullptr);
    it.setProperty ("loopBackward", p.loopBackward, nullptr);
    it.setProperty ("reverse", p.reverse, nullptr);
    it.setProperty ("start", p.start, nullptr);
    it.setProperty ("end", p.end, nullptr);
    it.setProperty ("loopStart", p.loopStart, nullptr);
    it.setProperty ("loopEnd", p.loopEnd, nullptr);
    if (p.midi)
    {
        it.setProperty ("midi", true, nullptr);
        it.setProperty ("midiCh", p.midiChannel, nullptr);
        it.setProperty ("midiProg", p.midiProgram, nullptr);
    }
    if (p.synth)
    {
        it.setProperty ("synth", true, nullptr);
        it.setProperty ("wave", p.synthWave, nullptr);
        it.setProperty ("attack", p.attack, nullptr);
        it.setProperty ("decay", p.decay, nullptr);
        it.setProperty ("sustain", p.sustain, nullptr);
        it.setProperty ("release", p.release, nullptr);
        it.setProperty ("cutoff", p.cutoff, nullptr);
        it.setProperty ("resonance", p.resonance, nullptr);
        it.setProperty ("distortion", p.distortion, nullptr);
        it.setProperty ("accent", p.accent, nullptr);
        juce::StringArray ccs;
        for (auto c : p.cc) ccs.add (juce::String (c));
        it.setProperty ("knobs", ccs.joinIntoString (","), nullptr);
    }
    if (p.synth2)
    {
        it.setProperty ("synth2", true, nullptr);
        it.setProperty ("s2", ft::synth2::toString (p.s2), nullptr);
        juce::StringArray ccs;
        for (auto c : p.cc2) ccs.add (juce::String (c));
        it.setProperty ("knobs2", ccs.joinIntoString (","), nullptr);
    }
    it.setProperty ("rate", in.data->sampleRate, nullptr);
    it.setProperty ("bits", in.data->bitDepth, nullptr);
    it.setProperty ("channels", in.data->getNumChannels(), nullptr);
    it.setProperty ("length", in.data->getLength(), nullptr);
    
    const int len = in.data->getLength(), nch = in.data->getNumChannels();
    juce::MemoryBlock mb ((size_t) len * (size_t) nch * sizeof (float));
    for (int c = 0; c < nch; ++c)
        mb.copyFrom (in.data->buffer.getReadPointer (c), (size_t) c * (size_t) len * sizeof (float), (size_t) len * sizeof (float));
    it.setProperty ("pcm", mb, nullptr);
    return it;
}

juce::ValueTree FourTrackerProcessor::instrumentsToTree() const
{
    juce::ValueTree t (ids::insts);
    for (int i = 1; i < kNumInstruments; ++i)
    {
        const auto& in = instruments[(size_t) i];
        if (in.isEmpty()) continue;
        t.appendChild (instrumentToTree (in, i), nullptr);
    }
    return t;
}

/** Reads one instrument written by instrumentToTree. False if it has no usable sample. */
static bool instrumentFromTree (const juce::ValueTree& it, Instrument& in)
{
    const int len = it.getProperty ("length", 0), nch = juce::jlimit (1, 2, (int) it.getProperty ("channels", 1));
    auto* mb = it.getProperty ("pcm").getBinaryData();
    if (mb == nullptr || len <= 0 || mb->getSize() < (size_t) len * (size_t) nch * sizeof (float)) return false;
    
    SampleData::Ptr d = new SampleData();
    d->sampleRate = it.getProperty ("rate", 44100.0);
    d->bitDepth = it.getProperty ("bits", 16);
    d->buffer.setSize (nch, len);
    for (int c = 0; c < nch; ++c)
        memcpy (d->buffer.getWritePointer (c), static_cast<const char*> (mb->getData()) + (size_t) c * (size_t) len * sizeof (float), (size_t) len * sizeof (float));
    
    in.data = d;
    in.name = it.getProperty ("name").toString();
    in.sourcePath = it.getProperty ("path").toString();
    auto& p = in.params;
    p.volume = it.getProperty ("volume", 64);
    p.transpose = it.getProperty ("transpose", 0);
    p.fineTune = it.getProperty ("finetune", 0);
    p.loop = it.getProperty ("loop", false);
    p.pingPong = it.getProperty ("pingpong", false);
    p.loopBackward = it.getProperty ("loopBackward", false);
    p.reverse = it.getProperty ("reverse", false);
    p.start = it.getProperty ("start", 0);
    p.end = it.getProperty ("end", len - 1);
    p.loopStart = it.getProperty ("loopStart", 0);
    p.loopEnd = it.getProperty ("loopEnd", len - 1);
    p.synth = it.getProperty ("synth", false);
    p.synthWave = it.getProperty ("wave", 0);
    p.attack = it.getProperty ("attack", 2);
    p.decay = it.getProperty ("decay", 250);
    p.sustain = it.getProperty ("sustain", 40);
    p.release = it.getProperty ("release", 200);
    p.cutoff = it.getProperty ("cutoff", 127);
    p.resonance = it.getProperty ("resonance", 0);
    p.distortion = it.getProperty ("distortion", 0);
    p.accent = it.getProperty ("accent", 0);
    {
        juce::StringArray ccs;
        ccs.addTokens (it.getProperty ("knobs", "").toString(), ",", "");
        for (int k = 0; k < (int) p.cc.size(); ++k) p.cc[(size_t) k] = k < ccs.size() ? ccs[k].getIntValue() : -1;
    }
    p.synth2 = it.getProperty ("synth2", false);
    if (p.synth2)
    {
        p.s2 = ft::synth2::fromString (it.getProperty ("s2", "").toString());
        juce::StringArray ccs;
        ccs.addTokens (it.getProperty ("knobs2", "").toString(), ",", "");
        for (int k = 0; k < (int) p.cc2.size(); ++k) p.cc2[(size_t) k] = k < ccs.size() ? ccs[k].getIntValue() : -1;
    }
    p.midi = it.getProperty ("midi", false);
    p.midiChannel = it.getProperty ("midiCh", 1);
    p.midiProgram = it.getProperty ("midiProg", 0);
    in.sanitise();
    return true;
}

void FourTrackerProcessor::instrumentsFromTree (const juce::ValueTree& t)
{
    for (auto& in : instruments) in.clear();

    for (auto it : t)
    {
        if (! it.hasType (ids::inst)) continue;
        const int slot = it.getProperty ("slot", 0);
        if (slot <= 0 || slot >= kNumInstruments) continue;
        Instrument in;
        if (instrumentFromTree (it, in)) instruments[(size_t) slot] = in;
    }

    for (int i = 1; i < kNumInstruments; ++i)
        engine.publishInstrument (i, instruments[(size_t) i].makeSnapshot());
}

// ---------------------------------------------------------------------------
// The "song": everything a module file holds (patterns, samples, instrument
// settings, speed/BPM, loop, mixer). App settings such as MIDI mapping are
// not part of it.
// ---------------------------------------------------------------------------

void FourTrackerProcessor::writeSong (juce::ValueTree& t) const
{
    t.setProperty ("speed", engine.speed.load(), nullptr);
    t.setProperty ("bpm", engine.bpm.load(), nullptr);
    t.setProperty ("loop", engine.loopPattern.load(), nullptr);
    t.setProperty ("selectedPattern", engine.selectedPattern.load(), nullptr);
    t.setProperty ("selectedInstrument", selectedInstrument, nullptr);
    t.setProperty ("channels", engine.numChannels.load(), nullptr);

    juce::ValueTree mixer (ids::mixer);
    for (int c = 0; c < kNumChannels; ++c)
    {
        juce::ValueTree ch (ids::chan);
        ch.setProperty ("vol", engine.mix[c].volume.load(), nullptr);
        ch.setProperty ("pan", engine.mix[c].pan.load(), nullptr);
        ch.setProperty ("pitch", engine.mix[c].pitch.load(), nullptr);
        ch.setProperty ("mute", engine.mix[c].mute.load(), nullptr);
        ch.setProperty ("solo", engine.mix[c].solo.load(), nullptr);
        mixer.appendChild (ch, nullptr);
    }
    t.appendChild (mixer, nullptr);
    t.appendChild (patternsToTree(), nullptr);
    t.appendChild (instrumentsToTree(), nullptr);
}

void FourTrackerProcessor::readSong (const juce::ValueTree& t)
{
    engine.requestStop();
    // modules from before 8 / 16 channels have no "channels": they are 4-channel songs
    engine.numChannels = normaliseChannels ((int) t.getProperty ("channels", 4));
    engine.speed = juce::jlimit (1, 31, (int) t.getProperty ("speed", 6));
    engine.bpm = juce::jlimit (32, 255, (int) t.getProperty ("bpm", 125));
    engine.loopPattern = (bool) t.getProperty ("loop", true);
    selectedInstrument = juce::jlimit (1, kNumInstruments - 1, (int) t.getProperty ("selectedInstrument", 1));

    auto mixer = t.getChildWithName (ids::mixer);
    for (int c = 0; c < kNumChannels; ++c)
    {
        auto ch = mixer.getChild (c);   // invalid tree -> defaults
        engine.mix[c].volume = juce::jlimit (0, 64, (int) ch.getProperty ("vol", 64));
        engine.mix[c].pan = juce::jlimit (-64, 64, (int) ch.getProperty ("pan", 0));
        engine.mix[c].pitch = juce::jlimit (-24, 24, (int) ch.getProperty ("pitch", 0));
        engine.mix[c].mute = (bool) ch.getProperty ("mute", false);
        engine.mix[c].solo = (bool) ch.getProperty ("solo", false);
        engine.lastInstrumentOnChannel[c] = 0;
    }

    patternsFromTree (t.getChildWithName (ids::patterns));
    instrumentsFromTree (t.getChildWithName (ids::insts));
    engine.requestPattern (juce::jlimit (0, kMaxPatterns - 1, (int) t.getProperty ("selectedPattern", 0)));
}

void FourTrackerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root (ids::root);
    root.setProperty ("version", 1, nullptr);
    writeSong (root);
    root.setProperty ("midiBase", engine.midiBaseNote.load(), nullptr);
    root.setProperty ("fadeNote", engine.midiFadeNote.load(), nullptr);
    root.setProperty ("fadeBars", engine.fadeBars.load(), nullptr);
    root.setProperty ("midiChannel", engine.midiChannel.load(), nullptr);
    root.setProperty ("switchMode", engine.switchMode.load(), nullptr);
    root.setProperty ("followHost", engine.followHostTempo.load(), nullptr);
    root.setProperty ("midiStartStop", engine.respondToMidiStartStop.load(), nullptr);
    root.setProperty ("master", engine.masterVolume.load(), nullptr);
    root.setProperty ("hex", hexMode, nullptr);

    juce::MemoryOutputStream mos (destData, false);
    root.writeToStream (mos);
}

void FourTrackerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto root = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! root.hasType (ids::root)) return;

    // The standalone app always starts with the demo song: only its settings
    // (MIDI mapping, master volume ...) are restored. A plugin restores the
    // whole song, because it belongs to the DAW project.
    const bool restoreSong = wrapperType != wrapperType_Standalone;

    engine.midiBaseNote = juce::jlimit (0, 127 - kMaxPatterns, (int) root.getProperty ("midiBase", 36));
    engine.midiFadeNote = juce::jlimit (0, 127, (int) root.getProperty ("fadeNote", 84));
    engine.fadeBars = juce::jlimit (1, 16, (int) root.getProperty ("fadeBars", 4));
    engine.midiChannel = juce::jlimit (0, 16, (int) root.getProperty ("midiChannel", 0));
    engine.switchMode = juce::jlimit (0, 3, (int) root.getProperty ("switchMode", 0));
    engine.followHostTempo = (bool) root.getProperty ("followHost", false);
    engine.respondToMidiStartStop = (bool) root.getProperty ("midiStartStop", true);
    engine.masterVolume = juce::jlimit (0, 100, (int) root.getProperty ("master", 80));
    hexMode = root.getProperty ("hex", true);

    if (restoreSong)
        readSong (root);
    markSaved();
    sendChangeMessage();
}

// ===========================================================================
// new / demo / modules
// ===========================================================================

int FourTrackerProcessor::normaliseChannels (int n)
{
    return juce::jlimit (1, kNumChannels, n);
}

bool FourTrackerProcessor::trackHasNotes (int channel)
{
    for (int p = 0; p < kMaxPatterns; ++p)
        for (int r = 0; r < kMaxRows; ++r)
            if (! engine.getPattern (p).at (r, channel).isEmpty()) return true;
    return false;
}

void FourTrackerProcessor::setNumChannels (int n)
{
    n = normaliseChannels (n);
    engine.requestStop();
    {
        const juce::ScopedLock sl (engine.getDataLock());
        for (int p = 0; p < kMaxPatterns; ++p)
            for (int r = 0; r < kMaxRows; ++r)
                for (int c = n; c < kNumChannels; ++c)
                    engine.getPattern (p).at (r, c).clear();
        for (int c = n; c < kNumChannels; ++c) { engine.mix[c].solo = false; engine.mix[c].mute = false; }
        engine.numChannels = n;
    }
   #if FOURTRACKER_FX_HOST
    if (fx != nullptr)       // a removed track takes its effects with it
        for (int c = n; c < kNumChannels; ++c)
            for (int i = 0; i < FxHost::kSlotsPerChannel; ++i)
                if (fx->hasPlugin (c, i)) fx->clearSlot (c, i);
    if (fx != nullptr)       // ...and its chaining, both from it and into it
        for (int c = 0; c < kNumChannels; ++c)
            if ((c >= n && fx->getRoute (c) != -1) || fx->getRoute (c) >= n) fx->setRoute (c, -1);
   #endif
    sendChangeMessage();
}

void FourTrackerProcessor::newPatternSet()
{
    engine.requestStop();
    {
        const juce::ScopedLock sl (engine.getDataLock());
        for (int p = 0; p < kMaxPatterns; ++p)
        {
            engine.getPattern (p).clear();
            engine.getPattern (p).numRows = kDefaultRows;
        }
    }

    for (int i = 1; i < kNumInstruments; ++i)
    {
        instruments[(size_t) i].clear();
        engine.publishInstrument (i, nullptr);
    }

    engine.speed = 6;
    engine.bpm = 125;
    engine.loopPattern = true;
    for (int c = 0; c < kNumChannels; ++c)
    {
        engine.mix[c].volume = 64;
        engine.mix[c].pan = 0;
        engine.mix[c].pitch = 0;
        engine.mix[c].mute = false;
        engine.mix[c].solo = false;
        engine.lastInstrumentOnChannel[c] = 0;
    }
    engine.requestPattern (0);
    selectedInstrument = 1;
    sendChangeMessage();
    currentModuleFile = juce::File();
    markSaved();
}

void FourTrackerProcessor::loadDemoSong()
{
    newPatternSet();
    clearAllEffects();      // the demo song has no VST effects
    {
        const juce::ScopedLock sl (engine.getDataLock());
        createDemoPatterns (engine);
    }
    createDemoInstruments (instruments);
    for (int i = 1; i < kNumInstruments; ++i)
        engine.publishInstrument (i, instruments[(size_t) i].makeSnapshot());
    currentModuleFile = juce::File();
    markSaved();
    sendChangeMessage();
}

juce::File FourTrackerProcessor::getUserFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Four Tracker");
}

void FourTrackerProcessor::ensureDemoFolder()
{
    const auto demoDir = getUserFolder().getChildFile ("Demo");
    const auto song = demoDir.getChildFile ("Demo Song.ftm");
    const auto samplesDir = demoDir.getChildFile ("Samples");
    if (song.existsAsFile() && samplesDir.isDirectory()) return;

    if (! samplesDir.createDirectory()) return;

    // a second processor that starts with the demo song (on the heap: it is too big for the stack on Windows)
    AudioProcessor::setTypeOfNextNewPlugin (AudioProcessor::wrapperType_Undefined);
    auto demoPtr = std::make_unique<FourTrackerProcessor>();
    auto& demo = *demoPtr;
    if (! song.existsAsFile())
        demo.saveModule (song);

    juce::WavAudioFormat wav;
    for (int i = 1; i < kNumInstruments; ++i)
    {
        const auto& in = demo.getInstrument (i);
        if (in.isEmpty()) continue;
        auto f = samplesDir.getChildFile (in.name.isNotEmpty() ? in.name : ("SAMPLE" + hex2 (i) + ".WAV"));
        if (! f.hasFileExtension ("wav")) f = f.withFileExtension ("wav");
        if (f.existsAsFile()) continue;

        std::unique_ptr<juce::OutputStream> out (new juce::FileOutputStream (f));
        if (! static_cast<juce::FileOutputStream*> (out.get())->openedOk()) continue;
        auto opts = juce::AudioFormatWriterOptions{}
                        .withSampleRate (in.data->sampleRate)
                        .withNumChannels (in.data->getNumChannels())
                        .withBitsPerSample (16);
        if (auto writer = wav.createWriterFor (out, opts))
            writer->writeFromAudioSampleBuffer (in.data->buffer, 0, in.data->getLength());
    }
}

static const char kModuleMagic[4]     = { 'F', 'T', 'M', 'D' };
static const char kPatternFileMagic[4] = { 'F', 'T', 'P', 'T' };
static const char kPatternSetMagic[4] = { 'F', 'T', 'P', 'S' };

bool FourTrackerProcessor::saveModule (const juce::File& file)
{
    juce::ValueTree t (ids::module);
    t.setProperty ("version", 1, nullptr);
    t.setProperty ("name", file.getFileNameWithoutExtension(), nullptr);
    writeSong (t);
   #if FOURTRACKER_FX_HOST
    // the song's VST effects (each slot's plugin and settings, and the chaining) go in the module too
    if (fx != nullptr)
        if (auto x = fx->createSongXml())
            t.setProperty ("effects", x->toString (juce::XmlElement::TextFormat().singleLine()), nullptr);
   #endif

    juce::MemoryOutputStream raw;
    t.writeToStream (raw);

    const auto tmp = file.getSiblingFile (file.getFileName() + ".tmp");
    tmp.deleteFile();
    {
        juce::FileOutputStream fos (tmp);
        if (! fos.openedOk()) return false;
        fos.write (kModuleMagic, 4);
        {
            juce::GZIPCompressorOutputStream gz (fos, 6);
            gz.write (raw.getData(), raw.getDataSize());
            gz.flush();
        }
        fos.flush();
        if (fos.getStatus().failed()) return false;
    }
    if (! tmp.moveFileTo (file)) return false;   // replaces the old file only once the new one is complete
    currentModuleFile = file;
    markSaved();
    return true;
}

juce::String FourTrackerProcessor::loadModule (const juce::File& file)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb) || mb.getSize() < 4)
        return "Can't read " + file.getFileName();

    const auto* bytes = static_cast<const char*> (mb.getData());

    if (memcmp (bytes, kModuleMagic, 4) == 0)
    {
        juce::MemoryInputStream mis (bytes + 4, mb.getSize() - 4, false);
        juce::GZIPDecompressorInputStream gz (mis);
        juce::MemoryBlock unpacked;
        gz.readIntoMemoryBlock (unpacked);
        auto t = juce::ValueTree::readFromData (unpacked.getData(), unpacked.getSize());
        if (! t.hasType (ids::module))
            return file.getFileName() + " is damaged";
        readSong (t);
        juce::String note;
       #if FOURTRACKER_FX_HOST
        // the module's own effects replace the ones in use (a module without effects clears them)
        if (fx != nullptr)
        {
            std::unique_ptr<juce::XmlElement> x;
            if (t.hasProperty ("effects")) x = juce::parseXML (t.getProperty ("effects").toString());
            if (fx->loadSongXml (x.get()) > 0)
                note = "LOADED " + file.getFileName().toUpperCase() + " - NOT INSTALLED HERE: "
                       + fx->getMissingEffects().joinIntoString (", ").toUpperCase();
        }
       #endif
        currentModuleFile = file;
        markSaved();
        sendChangeMessage();
        lastLoadNote = note;
        return {};
    }

    if (memcmp (bytes, kPatternSetMagic, 4) == 0)
    {
        // old pattern set: patterns + speed/BPM only, samples are left as they are
        auto t = juce::ValueTree::readFromData (bytes + 4, mb.getSize() - 4);
        if (! t.hasType (ids::patternSet))
            return file.getFileName() + " is damaged";
        engine.speed = juce::jlimit (1, 31, (int) t.getProperty ("speed", 6));
        engine.bpm = juce::jlimit (32, 255, (int) t.getProperty ("bpm", 125));
        patternsFromTree (t.getChildWithName (ids::patterns));
        sendChangeMessage();
        return {};
    }

    return file.getFileName() + " is not a Four Tracker module";
}

// ---------------------------------------------------------------------------
// single patterns (.ftpat): the rows plus the instruments they use, to share and to reuse
// ---------------------------------------------------------------------------

int FourTrackerProcessor::patternTracksUsed (int pattern)
{
    const auto& pat = engine.getPattern (pattern);
    int last = -1;
    for (int r = 0; r < pat.getNumRows(); ++r)
        for (int c = 0; c < kNumChannels; ++c)
            if (! pat.at (r, c).isEmpty()) last = juce::jmax (last, c);
    return last + 1;
}

juce::String FourTrackerProcessor::savePattern (int pattern, const juce::File& file)
{
    pattern = juce::jlimit (0, kMaxPatterns - 1, pattern);
    juce::ValueTree t (ids::patternFile);
    std::array<bool, kNumInstruments> used {};
    {
        const juce::ScopedLock sl (engine.getDataLock());
        const auto& pat = engine.getPattern (pattern);
        if (pat.isEmpty()) return "PATTERN " + formatNumber (pattern, 2, hexMode) + " IS EMPTY";
        const int rows = pat.getNumRows();
        const int chans = juce::jmax (1, patternTracksUsed (pattern));
        juce::MemoryBlock mb ((size_t) (rows * chans * 4));
        auto* d = static_cast<uint8_t*> (mb.getData());
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < chans; ++c)
            {
                const auto& cell = pat.at (r, c);
                *d++ = cell.note; *d++ = cell.inst; *d++ = cell.fx; *d++ = cell.param;
                if (cell.inst > 0 && cell.inst < kNumInstruments) used[cell.inst] = true;
            }
        t.setProperty ("version", 1, nullptr);
        t.setProperty ("name", file.getFileNameWithoutExtension(), nullptr);
        t.setProperty ("rows", rows, nullptr);
        t.setProperty ("chans", chans, nullptr);
        t.setProperty ("data", mb, nullptr);
        t.setProperty ("speed", engine.speed.load(), nullptr);   // for information only: loading keeps the song's tempo
        t.setProperty ("bpm", engine.bpm.load(), nullptr);
    }
    juce::ValueTree insts (ids::insts);
    int numInsts = 0;
    for (int i = 1; i < kNumInstruments; ++i)
        if (used[(size_t) i] && ! instruments[(size_t) i].isEmpty()) { insts.appendChild (instrumentToTree (instruments[(size_t) i], i), nullptr); ++numInsts; }
    t.appendChild (insts, nullptr);

    juce::MemoryOutputStream raw;
    t.writeToStream (raw);
    const auto tmp = file.getSiblingFile (file.getFileName() + ".tmp");
    tmp.deleteFile();
    {
        juce::FileOutputStream fos (tmp);
        if (! fos.openedOk()) return "COULD NOT SAVE " + file.getFileName().toUpperCase();
        fos.write (kPatternFileMagic, 4);
        {
            juce::GZIPCompressorOutputStream gz (fos, 6);
            gz.write (raw.getData(), raw.getDataSize());
            gz.flush();
        }
        fos.flush();
        if (fos.getStatus().failed()) return "COULD NOT SAVE " + file.getFileName().toUpperCase();
    }
    if (! tmp.moveFileTo (file)) return "COULD NOT SAVE " + file.getFileName().toUpperCase();
    return {};
}

int FourTrackerProcessor::findEmptyPattern (int from)
{
    for (int k = 0; k < kMaxPatterns; ++k)
    {
        const int p = (from + k) % kMaxPatterns;
        if (engine.getPattern (p).isEmpty()) return p;
    }
    return -1;
}

juce::String FourTrackerProcessor::loadPattern (const juce::File& file, int target, bool& ok)
{
    ok = false;
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb) || mb.getSize() < 4) return "CAN'T READ " + file.getFileName().toUpperCase();
    const auto* bytes = static_cast<const char*> (mb.getData());
    if (memcmp (bytes, kPatternFileMagic, 4) != 0) return file.getFileName().toUpperCase() + " IS NOT A FOUR TRACKER PATTERN";
    juce::MemoryInputStream mis (bytes + 4, mb.getSize() - 4, false);
    juce::GZIPDecompressorInputStream gz (mis);
    juce::MemoryBlock unpacked;
    gz.readIntoMemoryBlock (unpacked);
    const auto t = juce::ValueTree::readFromData (unpacked.getData(), unpacked.getSize());
    if (! t.hasType (ids::patternFile)) return file.getFileName().toUpperCase() + " IS DAMAGED";

    if (target < 0 || target >= kMaxPatterns) return "NO EMPTY PATTERN LEFT";
    if (! engine.getPattern (target).isEmpty()) return "PATTERN " + formatNumber (target, 2, hexMode) + " IS NOT EMPTY";

    // its instruments: the same sound already loaded is used again, the rest go into empty slots
    std::array<int, kNumInstruments> map {};
    std::array<bool, kNumInstruments> taken {};
    std::vector<std::pair<int, Instrument>> toAdd;
    int reused = 0;
    for (auto it : t.getChildWithName (ids::insts))
    {
        const int orig = it.getProperty ("slot", 0);
        if (orig <= 0 || orig >= kNumInstruments) continue;
        Instrument in;
        if (! instrumentFromTree (it, in)) continue;
        int found = -1;
        for (int i = 1; i < kNumInstruments && found < 0; ++i)
        {
            const auto& have = instruments[(size_t) i];
            if (have.isEmpty() || have.name != in.name || have.data->getLength() != in.data->getLength()
                || have.data->getNumChannels() != in.data->getNumChannels() || have.params.synth2 != in.params.synth2) continue;
            bool same = true;
            for (int c = 0; c < in.data->getNumChannels() && same; ++c)
                same = memcmp (have.data->buffer.getReadPointer (c), in.data->buffer.getReadPointer (c), sizeof (float) * (size_t) in.data->getLength()) == 0;
            if (same) found = i;
        }
        if (found > 0) { map[(size_t) orig] = found; ++reused; continue; }
        int free = -1;
        for (int i = 1; i < kNumInstruments && free < 0; ++i)
            if (instruments[(size_t) i].isEmpty() && ! taken[(size_t) i]) free = i;
        if (free < 0)
            return "NOT ENOUGH EMPTY INSTRUMENT SLOTS: THE PATTERN NEEDS " + juce::String (t.getChildWithName (ids::insts).getNumChildren() - reused) + " MORE";
        taken[(size_t) free] = true;
        map[(size_t) orig] = free;
        toAdd.push_back ({ free, in });
    }

    const int rows = juce::jlimit (1, kMaxRows, (int) t.getProperty ("rows", kDefaultRows));
    const int chans = juce::jlimit (1, kNumChannels, (int) t.getProperty ("chans", 4));
    auto* data = t.getProperty ("data").getBinaryData();
    if (data == nullptr) return file.getFileName().toUpperCase() + " IS DAMAGED";

    for (auto& [slot, in] : toAdd) { instruments[(size_t) slot] = in; instruments[(size_t) slot].sanitise(); }
    for (auto& [slot, in] : toAdd) engine.publishInstrument (slot, instruments[(size_t) slot].makeSnapshot());

    int tempoRemoved = 0;
    {
        const juce::ScopedLock sl (engine.getDataLock());
        auto& pat = engine.getPattern (target);
        pat.clear();
        pat.numRows = rows;
        const auto* d = static_cast<const uint8_t*> (data->getData());
        const int cells = juce::jmin (rows * chans, (int) data->getSize() / 4);
        for (int i = 0; i < cells; ++i, d += 4)
        {
            auto& cell = pat.at (i / chans, i % chans);
            cell.note = d[0] <= kNoteOff ? d[0] : 0;
            cell.inst = d[1] < kNumInstruments ? (uint8_t) map[d[1]] : 0;
            cell.fx = d[2];
            cell.param = d[3];
            if (cell.fx == 0x0F) { cell.fx = 0; cell.param = 0; ++tempoRemoved; }   // Fxx: the song keeps its own SPEED and BPM
        }
        if (chans > engine.numChannels.load()) engine.numChannels = normaliseChannels (chans);
    }
    sendChangeMessage();
    ok = true;
    juce::String msg = "LOADED " + file.getFileNameWithoutExtension().toUpperCase() + " INTO PATTERN " + formatNumber (target, 2, hexMode);
    if (! toAdd.empty()) msg << ", " << (int) toAdd.size() << " INSTRUMENT" << (toAdd.size() == 1 ? "" : "S") << " ADDED";
    if (reused > 0) msg << ", " << reused << " ALREADY HERE";
    if (tempoRemoved > 0) msg << ", TEMPO COMMANDS REMOVED";
    return msg;
}

// ===========================================================================

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FourTrackerProcessor();
}

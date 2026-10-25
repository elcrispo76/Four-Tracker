// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#include "Instrument.h"
#include "AmigaSamples.h"

namespace ft
{

static constexpr int kMaxSampleSeconds = 120;

void Instrument::sanitise()
{
    auto& p = params;
    p.volume    = juce::jlimit (0, 64, p.volume);
    p.transpose = juce::jlimit (-48, 48, p.transpose);
    p.fineTune  = juce::jlimit (-128, 127, p.fineTune);
    p.synthWave = juce::jlimit (0, (int) numSynthWaves - 1, p.synthWave);
    p.attack    = juce::jlimit (0, 5000, p.attack);
    p.decay     = juce::jlimit (0, 5000, p.decay);
    p.sustain   = juce::jlimit (0, 64, p.sustain);
    p.release   = juce::jlimit (0, 5000, p.release);
    p.cutoff    = juce::jlimit (0, 127, p.cutoff);
    p.resonance = juce::jlimit (0, 127, p.resonance);
    p.distortion = juce::jlimit (0, 64, p.distortion);
    p.accent    = juce::jlimit (0, 64, p.accent);
    for (auto& c : p.cc) if (c < 0 || c > 119) c = -1;
    for (auto& c : p.cc2) if (c < 0 || c > 119) c = -1;
    for (int k = 0; k < s2NumParams; ++k) p.s2[(size_t) k] = synth2::clamp (k, p.s2[(size_t) k]);
    p.midiChannel = juce::jlimit (1, 16, p.midiChannel);
    p.midiProgram = juce::jlimit (0, 128, p.midiProgram);

    const int len = data != nullptr ? data->getLength() : 0;
    if (len <= 0) { p.start = p.end = p.loopStart = p.loopEnd = 0; return; }

    p.start = juce::jlimit (0, len - 1, p.start);
    p.end   = juce::jlimit (p.start, len - 1, p.end);
    p.loopStart = juce::jlimit (p.start, p.end, p.loopStart);
    p.loopEnd   = juce::jlimit (p.loopStart, p.end, p.loopEnd);
}

void Instrument::resetPoints()
{
    const int len = data != nullptr ? data->getLength() : 0;
    params.start = 0;
    params.end = juce::jmax (0, len - 1);
    params.loopStart = 0;
    params.loopEnd = params.end;
}

const char* synthWaveName (int wave)
{
    static const char* names[] = { "SINE", "TRIANGLE", "SAW", "SQUARE", "NOISE" };
    return names[juce::jlimit (0, (int) numSynthWaves - 1, wave)];
}

const char* synthParamName (int param)
{
    static const char* names[] = { "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "CUTOFF", "RESONANCE", "DISTORTION", "ACCENT" };
    return juce::isPositiveAndBelow (param, (int) numSynthParams) ? names[param] : "";
}

int& synthParamRef (InstrumentParams& p, int param)
{
    switch (param)
    {
        case spAttack:     return p.attack;
        case spDecay:      return p.decay;
        case spSustain:    return p.sustain;
        case spRelease:    return p.release;
        case spCutoff:     return p.cutoff;
        case spResonance:  return p.resonance;
        case spDistortion: return p.distortion;
        default:           return p.accent;
    }
}

int synthParamFromMidi (int param, int midiValue)
{
    const double x = juce::jlimit (0, 127, midiValue) / 127.0;
    switch (param)
    {
        case spAttack: case spDecay: case spRelease:
            return juce::roundToInt (5000.0 * x * x * x);          // same curve as the sliders (skew 3)
        case spCutoff: case spResonance:
            return juce::jlimit (0, 127, midiValue);
        default:
            return juce::roundToInt (64.0 * x);                     // sustain, distortion, accent
    }
}

void Instrument::makeSynth (int wave)
{
    wave = juce::jlimit (0, (int) numSynthWaves - 1, wave);
    const bool wasSynth = isSynth();
    const auto oldAutoName = wasSynth ? juce::String ("SYNTH ") + synthWaveName (params.synthWave) : juce::String();

    // One cycle of 128 frames, played at a rate where C-4 is middle C
    // (261.63 Hz), so the synth is in tune with the note names.
    // Noise is a long looped buffer instead, so it doesn't sound pitched.
    constexpr int cycle = 128;
    const double rate = cycle * 261.6255653;
    const int len = wave == waveNoise ? 32768 : cycle;

    SampleData::Ptr d = new SampleData();
    d->sampleRate = rate;
    d->bitDepth = 16;
    d->buffer.setSize (1, len);
    auto* w = d->buffer.getWritePointer (0);
    juce::Random rnd (0x4654524bLL);   // fixed seed: the same noise every time
    for (int i = 0; i < len; ++i)
    {
        const double ph = (double) i / cycle;   // 0..1 over one cycle
        float v = 0.0f;
        switch (wave)
        {
            case waveSine:     v = (float) std::sin (ph * juce::MathConstants<double>::twoPi) * 0.8f; break;
            case waveTriangle: v = (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph) * 0.8f; break;
            case waveSaw:      v = (float) (2.0 * ph - 1.0) * 0.6f; break;
            case waveSquare:   v = ph < 0.5 ? 0.5f : -0.5f; break;
            default:           v = (rnd.nextFloat() * 2.0f - 1.0f) * 0.6f; break;
        }
        w[i] = v;
    }

    if (! wasSynth)
    {
        const int vol = isEmpty() ? 64 : params.volume;
        params = InstrumentParams();
        params.volume = vol;
    }
    data = d;
    params.synth = true;
    params.synthWave = wave;
    params.loop = true;
    params.pingPong = params.loopBackward = params.reverse = false;
    resetPoints();
    if (name.isEmpty() || name == oldAutoName || ! wasSynth) name = juce::String ("SYNTH ") + synthWaveName (wave);
    sourcePath = {};
    sanitise();
}

void Instrument::makeSynth2()
{
    if (isSynth2()) return;
    const int vol = isEmpty() ? 64 : params.volume;
    // One saw cycle at the same rate as SYNTH 1, so the tracker's pitch
    // handling (portamento, vibrato, arpeggio) drives it; the engine makes
    // the actual oscillators from the voice's speed through it.
    constexpr int cycle = 128;
    SampleData::Ptr d = new SampleData();
    d->sampleRate = cycle * 261.6255653;
    d->bitDepth = 16;
    d->buffer.setSize (1, cycle);
    auto* w = d->buffer.getWritePointer (0);
    for (int i = 0; i < cycle; ++i) w[i] = (float) (2.0 * i / cycle - 1.0) * 0.6f;
    data = d;
    params = InstrumentParams();
    params.volume = vol;
    params.synth2 = true;
    params.loop = true;
    params.s2 = synth2::defaults();
    resetPoints();
    name = "SYNTH 2 INIT";
    sourcePath = {};
    sanitise();
}

void Instrument::applySynth2Patch (const synth2::Patch& patch)
{
    if (! isSynth2()) makeSynth2();
    params.s2 = patch.values;
    name = patch.name.toUpperCase();
    sanitise();
}

void Instrument::makeMidi()
{
    const bool was = isMidi();
    const auto oldName = was ? "MIDI CH " + juce::String (params.midiChannel) : juce::String();
    const int ch = was ? params.midiChannel : 1, prog = was ? params.midiProgram : 0;
    const int vol = isEmpty() ? 64 : params.volume;

    // one silent frame, so the slot counts as an instrument everywhere; the sound is outside
    SampleData::Ptr d = new SampleData();
    d->sampleRate = 44100.0;
    d->buffer.setSize (1, 1);
    d->buffer.clear();
    data = d;
    params = InstrumentParams();
    params.volume = vol;
    params.midi = true;
    params.midiChannel = ch;
    params.midiProgram = prog;
    resetPoints();
    if (! was || name.isEmpty() || name == oldName) name = "MIDI CH " + juce::String (ch);
    sourcePath = {};
    sanitise();
}

InstrumentSnapshot::Ptr Instrument::makeSnapshot() const
{
    if (isEmpty()) return nullptr;

    InstrumentSnapshot::Ptr s = new InstrumentSnapshot();
    s->data = data;
    s->p = params;
    s->length = params.end - params.start + 1;

    // Loop points in virtual (playback-order) frames, end exclusive.
    if (params.reverse)
    {
        s->vLoopStart = params.end - params.loopEnd;
        s->vLoopEnd   = params.end - params.loopStart + 1;
    }
    else
    {
        s->vLoopStart = params.loopStart - params.start;
        s->vLoopEnd   = params.loopEnd - params.start + 1;
    }

    s->looping = params.loop && (s->vLoopEnd - s->vLoopStart) >= 2;
    return s;
}

// ---------------------------------------------------------------------------

namespace
{
    SampleData::Ptr cloneWithLength (const SampleData& src, int newLength)
    {
        SampleData::Ptr d = new SampleData();
        d->sampleRate = src.sampleRate;
        d->bitDepth   = src.bitDepth;
        d->buffer.setSize (src.getNumChannels(), juce::jmax (0, newLength));
        d->buffer.clear();
        return d;
    }

    SampleData::Ptr clone (const SampleData& src)
    {
        auto d = cloneWithLength (src, src.getLength());
        for (int c = 0; c < src.getNumChannels(); ++c)
            d->buffer.copyFrom (c, 0, src.buffer, c, 0, src.getLength());
        return d;
    }

    struct PointKeeper
    {
        explicit PointKeeper (Instrument& i) : inst (i)
        {
            oldLen = i.data != nullptr ? i.data->getLength() : 0;
            endAtTail = i.params.end >= oldLen - 1;
            loopEndAtTail = i.params.loopEnd >= oldLen - 1;
        }
        ~PointKeeper()
        {
            const int len = inst.data != nullptr ? inst.data->getLength() : 0;
            if (endAtTail)     inst.params.end = len - 1;
            if (loopEndAtTail) inst.params.loopEnd = len - 1;
            inst.sanitise();
        }
        Instrument& inst;
        int oldLen = 0;
        bool endAtTail = false, loopEndAtTail = false;
    };

    bool validRange (const Instrument& i, int& from, int& to)
    {
        if (i.isEmpty()) return false;
        const int len = i.data->getLength();
        if (from > to) std::swap (from, to);
        from = juce::jlimit (0, len - 1, from);
        to   = juce::jlimit (0, len - 1, to);
        return to >= from;
    }
}

void Instrument::copy (int from, int to, juce::AudioBuffer<float>& clip) const
{
    if (! validRange (*this, from, to)) { clip.setSize (0, 0); return; }
    const int n = to - from + 1;
    clip.setSize (data->getNumChannels(), n);
    for (int c = 0; c < data->getNumChannels(); ++c)
        clip.copyFrom (c, 0, data->buffer, c, from, n);
}

bool Instrument::cut (int from, int to, juce::AudioBuffer<float>& clip)
{
    if (! validRange (*this, from, to)) return false;
    copy (from, to, clip);
    PointKeeper keep (*this);

    const int len = data->getLength();
    const int n = to - from + 1;
    auto d = cloneWithLength (*data, len - n);
    for (int c = 0; c < d->getNumChannels(); ++c)
    {
        if (from > 0)       d->buffer.copyFrom (c, 0, data->buffer, c, 0, from);
        if (to + 1 < len)   d->buffer.copyFrom (c, from, data->buffer, c, to + 1, len - to - 1);
    }
    data = d;
    if (data->getLength() == 0) { clear(); return true; }
    return true;
}

bool Instrument::paste (int at, const juce::AudioBuffer<float>& clip)
{
    if (clip.getNumSamples() == 0) return false;

    if (isEmpty())
    {
        SampleData::Ptr d = new SampleData();
        d->buffer.makeCopyOf (clip);
        data = d;
        if (name.isEmpty()) name = "PASTED";
        resetPoints();
        return true;
    }

    PointKeeper keep (*this);
    const int len = data->getLength();
    at = juce::jlimit (0, len, at);
    const int n = clip.getNumSamples();
    auto d = cloneWithLength (*data, len + n);

    for (int c = 0; c < d->getNumChannels(); ++c)
    {
        const int srcCh = juce::jmin (c, clip.getNumChannels() - 1);
        if (at > 0)    d->buffer.copyFrom (c, 0, data->buffer, c, 0, at);
        d->buffer.copyFrom (c, at, clip, srcCh, 0, n);
        if (at < len)  d->buffer.copyFrom (c, at + n, data->buffer, c, at, len - at);
    }
    data = d;
    return true;
}

bool Instrument::mixIn (const Instrument& other, int at)
{
    if (isEmpty() || other.isEmpty()) return false;

    // the other sample's played range (start..end), in playback order
    const auto& op = other.params;
    const int oStart = juce::jlimit (0, other.data->getLength() - 1, op.start);
    const int oEnd   = juce::jlimit (oStart, other.data->getLength() - 1, op.end);
    const int oLen = oEnd - oStart + 1;

    // resample to this sample's rate (cubic), so it keeps its own pitch
    const double ratio = other.data->sampleRate / data->sampleRate;      // source frames per output frame
    const int mixLen = juce::jmax (1, (int) std::floor ((oLen - 1) / ratio) + 1);

    PointKeeper keep (*this);
    const int len = data->getLength();
    at = juce::jlimit (0, len, at);
    const int newLen = juce::jmax (len, at + mixLen);
    const int nch = juce::jmax (data->getNumChannels(), other.data->getNumChannels());

    SampleData::Ptr d = new SampleData();
    d->sampleRate = data->sampleRate;
    d->bitDepth = juce::jmax (data->bitDepth, other.data->bitDepth);
    d->buffer.setSize (nch, newLen);
    d->buffer.clear();
    for (int c = 0; c < nch; ++c)
        d->buffer.copyFrom (c, 0, data->buffer, juce::jmin (c, data->getNumChannels() - 1), 0, len);

    for (int c = 0; c < nch; ++c)
    {
        const float* src = other.data->buffer.getReadPointer (juce::jmin (c, other.data->getNumChannels() - 1));
        auto s = [&] (int i)
        {
            i = juce::jlimit (0, oLen - 1, i);
            return src[op.reverse ? oEnd - i : oStart + i];
        };
        float* dst = d->buffer.getWritePointer (c, at);
        for (int i = 0; i < mixLen; ++i)
        {
            const double pos = i * ratio;
            const int k = (int) pos;
            const float f = (float) (pos - k);
            const float xm1 = s (k - 1), x0 = s (k), x1 = s (k + 1), x2 = s (k + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            dst[i] += ((c3 * f + c2) * f + c1) * f + x0;
        }
    }

    const float peak = d->buffer.getMagnitude (0, newLen);
    if (peak > 0.999f) d->buffer.applyGain (0.999f / peak);
    data = d;
    return true;
}

bool Instrument::fadeIn (int from, int to)
{
    if (! validRange (*this, from, to)) return false;
    auto d = clone (*data);
    const int n = to - from + 1;
    for (int c = 0; c < d->getNumChannels(); ++c)
        d->buffer.applyGainRamp (c, from, n, 0.0f, 1.0f);
    data = d;
    return true;
}

bool Instrument::fadeOut (int from, int to)
{
    if (! validRange (*this, from, to)) return false;
    auto d = clone (*data);
    const int n = to - from + 1;
    for (int c = 0; c < d->getNumChannels(); ++c)
        d->buffer.applyGainRamp (c, from, n, 1.0f, 0.0f);
    data = d;
    return true;
}

bool Instrument::normalise (int from, int to)
{
    if (! validRange (*this, from, to)) return false;
    const int n = to - from + 1;
    float peak = 0.0f;
    for (int c = 0; c < data->getNumChannels(); ++c)
        peak = juce::jmax (peak, data->buffer.getMagnitude (c, from, n));
    if (peak <= 1.0e-6f) return false;

    auto d = clone (*data);
    for (int c = 0; c < d->getNumChannels(); ++c)
        d->buffer.applyGain (c, from, n, 1.0f / peak);
    data = d;
    return true;
}

bool Instrument::reverseRange (int from, int to)
{
    if (! validRange (*this, from, to)) return false;
    auto d = clone (*data);
    d->buffer.reverse (from, to - from + 1);
    data = d;
    return true;
}

juce::String Instrument::loadFromFile (const juce::File& file, juce::AudioFormatManager& formats)
{
    // ---- Amiga formats (ProTracker / OctaMED): IFF 8SVX and raw 8-bit ----
    auto useAmiga = [&] (const amiga::Decoded& dec) -> juce::String
    {
        data = dec.data;
        name = (dec.name.isNotEmpty() ? dec.name : file.getFileName()).toUpperCase();
        sourcePath = file.getFullPathName();
        params = InstrumentParams();
        resetPoints();
        params.volume = dec.volume;
        if (dec.loop)
        {
            params.loop = true;
            params.loopStart = dec.loopStart;
            params.loopEnd = dec.loopEnd;
        }
        sanitise();
        return {};
    };

    if (amiga::is8SVX (file))
    {
        juce::MemoryBlock mb;
        if (! file.loadFileAsData (mb)) return "Can't read " + file.getFileName();
        amiga::Decoded dec;
        auto err = amiga::read8SVX (mb, dec);
        if (err.isNotEmpty()) return file.getFileName() + ": " + err;
        return useAmiga (dec);
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr && amiga::isRawExtension (file))
    {
        // A file without a known extension may still be a WAV/AIFF: check its contents first.
        reader.reset (formats.createReaderFor (std::make_unique<juce::FileInputStream> (file)));
        if (reader == nullptr)
        {
            juce::MemoryBlock mb;
            if (! file.loadFileAsData (mb)) return "Can't read " + file.getFileName();
            amiga::Decoded dec;
            auto err = amiga::readRaw8 (mb, dec);
            if (err.isNotEmpty()) return file.getFileName() + ": " + err;
            return useAmiga (dec);
        }
    }

    if (reader == nullptr)
        return "Can't read " + file.getFileName() + " (unsupported format?)";

    const auto maxFrames = (juce::int64) (reader->sampleRate * kMaxSampleSeconds);
    if (reader->lengthInSamples <= 0)
        return file.getFileName() + " is empty";
    if (reader->lengthInSamples > maxFrames)
        return file.getFileName() + " is longer than " + juce::String (kMaxSampleSeconds) + " seconds";

    const int frames   = (int) reader->lengthInSamples;
    const int srcChans = (int) reader->numChannels;
    const int chans    = juce::jmin (2, srcChans);

    SampleData::Ptr d = new SampleData();
    d->sampleRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    d->bitDepth   = (int) reader->bitsPerSample;

    juce::AudioBuffer<float> tmp (srcChans, frames);
    reader->read (&tmp, 0, frames, 0, true, true);

    d->buffer.setSize (chans, frames);
    if (srcChans <= 2)
    {
        for (int c = 0; c < chans; ++c)
            d->buffer.copyFrom (c, 0, tmp, c, 0, frames);
    }
    else
    {
        // Fold surround files down to stereo: even channels left, odd right.
        d->buffer.clear();
        for (int c = 0; c < srcChans; ++c)
            d->buffer.addFrom (c % 2, 0, tmp, c, 0, frames, 2.0f / (float) srcChans);
    }

    data = d;
    name = file.getFileName().toUpperCase();
    sourcePath = file.getFullPathName();
    params = InstrumentParams();
    resetPoints();

    // loop points from the WAV file's 'smpl' chunk (made by most sample editors and our sample pack)
    const auto& md = reader->metadataValues;
    if (md.getValue ("NumSampleLoops", "0").getIntValue() > 0)
    {
        const int ls = md.getValue ("Loop0Start", "-1").getIntValue();
        const int le = md.getValue ("Loop0End", "-1").getIntValue();      // the last frame of the loop
        if (ls >= 0 && le > ls && le < frames)
        {
            params.loop = true;
            params.loopStart = ls;
            params.loopEnd = le;
            const int type = md.getValue ("Loop0Type", "0").getIntValue();
            params.pingPong = type == 1;
            params.loopBackward = type == 2;
        }
    }
    sanitise();
    return {};
}

} // namespace ft

// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker Pattern Maker - Copyright (C) 2026 elcrispo76

#include "Slicer.h"
#include "MakerComponent.h"
#include "PluginProcessor.h"
#include "UI/PixelFont.h"
#include <cmath>

using namespace juce;

namespace pm
{

int firstSliceLane (int numUsed)
{
    // the empty lanes 9-16 first; more than 8 go on from 16 down, so the kick and the first sounds stay
    return numUsed <= kNumLanes - kNumGenLanes ? kNumGenLanes : jmax (0, kNumLanes - numUsed);
}

// ===========================================================================
// Cutting
// ===========================================================================

std::vector<int> detectSlices (const AudioBuffer<float>& audio, double sampleRate, float sensitivity, int maxSlices)
{
    std::vector<int> out { 0 };
    const int n = audio.getNumSamples(), chans = audio.getNumChannels();
    if (n < 64 || chans == 0) return out;
    // the energy in three bands (low < 150 Hz, mid, high > 3 kHz) per 5.8 ms: a hat stands out in the
    // high band even while the kick before it still rings in the low one
    const int hop = jmax (64, roundToInt (sampleRate * 0.0058));
    const int frames = n / hop;
    constexpr int kBands = 3;
    std::vector<float> db[kBands];
    for (auto& d : db) d.assign ((size_t) frames, -120.0f);
    {
        auto coef = [&] (double fc) { return std::exp (-MathConstants<double>::twoPi * fc / sampleRate); };
        const double aL = coef (150.0), aH = coef (3000.0);
        double l1 = 0, l2 = 0, h1 = 0;
        std::vector<double> e ((size_t) kBands * (size_t) frames, 0.0);
        for (int i = 0; i < frames * hop; ++i)
        {
            double x = 0.0;
            for (int c = 0; c < chans; ++c) x += audio.getSample (c, i);
            x /= chans;
            l1 = (1.0 - aL) * x + aL * l1;              // two one-pole low-passes: the low band
            l2 = (1.0 - aL) * l1 + aL * l2;
            h1 = (1.0 - aH) * x + aH * h1;              // under 3 kHz
            const double low = l2, mid = h1 - l1, high = x - h1;
            const size_t f = (size_t) (i / hop);
            e[f] += low * low;
            e[(size_t) frames + f] += mid * mid;
            e[2 * (size_t) frames + f] += high * high;
        }
        for (int b = 0; b < kBands; ++b)
            for (int f = 0; f < frames; ++f)
                db[b][(size_t) f] = (float) (10.0 * std::log10 (e[(size_t) b * (size_t) frames + (size_t) f] / hop + 1e-12));
    }
    // a hit: in one of the bands the energy rises well over the last 35 ms, 70 ms after the last hit at least
    const float s = jlimit (0.0f, 1.0f, sensitivity);
    const float rise = 8.0f - 5.0f * s;                  // dB over the frames before
    const int lookBack = jmax (2, roundToInt (0.035 * sampleRate / hop));
    const int minGap = roundToInt (0.07 * sampleRate / hop);
    std::vector<float> onset ((size_t) frames, 0.0f);
    for (int b = 0; b < kBands; ++b)
    {
        const float floorDb = *std::max_element (db[b].begin(), db[b].end()) - (30.0f + 20.0f * s);   // quieter: the room, a tail
        for (int f = 1; f < frames; ++f)
        {
            float before = -120.0f;
            for (int k = jmax (0, f - lookBack); k < f; ++k) before = jmax (before, db[b][(size_t) k]);
            if (db[b][(size_t) f] >= floorDb) onset[(size_t) f] = jmax (onset[(size_t) f], db[b][(size_t) f] - before);
        }
    }
    struct Hit { int frame; float strength; };
    std::vector<Hit> hits;
    int last = -minGap;
    for (int f = 1; f < frames - 1; ++f)
    {
        const float o = onset[(size_t) f];
        if (o < rise) continue;
        if (f - last < minGap) continue;
        if (f * hop < hop * 2) continue;                    // the very start is slice 1 anyway
        hits.push_back ({ f, o });
        last = f;
    }
    if ((int) hits.size() > maxSlices - 1)
    {
        std::sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.strength > b.strength; });
        hits.resize ((size_t) (maxSlices - 1));
    }
    for (auto& h : hits)
    {
        // start just before the hit: the quietest point in the 6 ms before it
        const int centre = h.frame * hop;
        int best = centre, from = jmax (1, centre - roundToInt (0.006 * sampleRate));
        float bestV = 1e9f;
        for (int i = from; i <= centre; ++i)
        {
            float v = 0.0f;
            for (int c = 0; c < chans; ++c) v += std::abs (audio.getSample (c, i));
            if (v < bestV) { bestV = v; best = i; }
        }
        out.push_back (best);
    }
    std::sort (out.begin(), out.end());
    out.erase (std::unique (out.begin(), out.end()), out.end());
    return out;
}

int guessBpm (int numSamples, double sampleRate, const std::vector<int>& hits, double& bars)
{
    bars = 0.0;
    if (numSamples <= 0 || sampleRate <= 0.0) return 0;
    const double secs = numSamples / sampleRate;
    ignoreUnused (hits);
    struct Cand { double bars, bpm; };
    std::vector<Cand> fits;
    for (double b : { 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 })
    {
        const double bpm = b * 4.0 * 60.0 / secs;
        if (bpm < 70.0 || bpm > 190.0) continue;
        if (std::abs (bpm - std::round (bpm)) > 0.2) continue;        // a loop is cut to a whole BPM
        fits.push_back ({ b, bpm });
    }
    if (fits.empty()) return 0;
    // two fit (87 / 174, 80 / 160): drum & bass, jungle and breaks are the fast one (/2 halves it);
    // else the one nearest 125
    Cand pick = fits.front();
    bool fast = false;
    for (auto& c : fits) if (c.bpm >= 150.0) { pick = c; fast = true; break; }
    if (! fast)
        for (auto& c : fits) if (std::abs (c.bpm - 125.0) < std::abs (pick.bpm - 125.0)) pick = c;
    bars = pick.bars;
    return (int) std::round (pick.bpm);
}

std::vector<int> gridSlices (int numSamples, double sampleRate, double bpm, int sixteenths, int maxSlices)
{
    std::vector<int> out;
    const double len = sampleRate * 60.0 / jmax (1.0, bpm) * jmax (1, sixteenths) / 4.0;
    for (int i = 0; i < maxSlices; ++i)
    {
        const int p = roundToInt (i * len);
        if (p >= numSamples - 32) break;
        out.push_back (p);
    }
    if (out.empty()) out.push_back (0);
    return out;
}

int snapToZero (const AudioBuffer<float>& audio, double sampleRate, int pos)
{
    const int n = audio.getNumSamples();
    if (n < 2) return 0;
    pos = jlimit (0, n - 1, pos);
    const int range = roundToInt (0.002 * sampleRate);
    auto mono = [&] (int i) { float v = 0.0f; for (int c = 0; c < audio.getNumChannels(); ++c) v += audio.getSample (c, i); return v; };
    for (int d = 0; d <= range; ++d)
        for (int sgn : { -1, 1 })
        {
            const int i = pos + sgn * d;
            if (i < 1 || i >= n) continue;
            if ((mono (i - 1) <= 0.0f) != (mono (i) <= 0.0f)) return i;
        }
    return pos;
}

String writeSlice (const AudioBuffer<float>& audio, double sampleRate, int start, int end, const File& file)
{
    start = jlimit (0, audio.getNumSamples(), start);
    end = jlimit (start, audio.getNumSamples(), end);
    const int len = end - start;
    if (len < 16) return "THE SLICE IS TOO SHORT";
    AudioBuffer<float> b (audio.getNumChannels(), len);
    for (int c = 0; c < b.getNumChannels(); ++c) b.copyFrom (c, 0, audio, c, start, len);
    const int fin = jmin (len / 4, roundToInt (0.001 * sampleRate)), fout = jmin (len / 4, roundToInt (0.005 * sampleRate));
    if (fin > 0) b.applyGainRamp (0, fin, 0.0f, 1.0f);
    if (fout > 0) b.applyGainRamp (len - fout, fout, 1.0f, 0.0f);
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    auto os = std::unique_ptr<OutputStream> (file.createOutputStream());
    if (os == nullptr) return "COULD NOT WRITE " + file.getFullPathName();
    WavAudioFormat wav;
    auto w = std::unique_ptr<AudioFormatWriter> (wav.createWriterFor (os.get(), sampleRate, (unsigned int) b.getNumChannels(), 24, {}, 0));
    if (w == nullptr) return "COULD NOT WRITE " + file.getFullPathName();
    os.release();
    w->writeFromAudioSampleBuffer (b, 0, len);
    return {};
}

// ===========================================================================
// SlicePlayer
// ===========================================================================

void SlicePlayer::setAudio (std::shared_ptr<const AudioBuffer<float>> a, double sampleRate)
{
    const ScopedLock sl (lock);
    buffer = std::move (a);
    fileRate = sampleRate;
    playing = false;
}

void SlicePlayer::play (int start, int end)
{
    const ScopedLock sl (lock);
    if (buffer == nullptr) return;
    startAt = jlimit (0, buffer->getNumSamples(), start);
    endAt = jlimit (startAt, buffer->getNumSamples(), end);
    pos = startAt;
    playing = endAt > startAt;
}

void SlicePlayer::stop() { playing = false; }

void SlicePlayer::render (float* const* out, int numChannels, int numSamples, double devRate)
{
    for (int c = 0; c < numChannels; ++c) if (out[c] != nullptr) FloatVectorOperations::clear (out[c], numSamples);
    const ScopedTryLock sl (lock);
    if (! sl.isLocked() || ! playing.load() || buffer == nullptr) return;
    const auto& b = *buffer;
    const double step = fileRate / jmax (1.0, devRate);
    const int fade = jmax (1, roundToInt (0.003 * fileRate));
    for (int i = 0; i < numSamples; ++i)
    {
        const int p = (int) pos;
        if (p + 1 >= endAt) { playing = false; break; }
        const float fr = (float) (pos - p);
        float g = 0.85f;
        if (p - startAt < fade) g *= (float) (p - startAt) / fade;
        if (endAt - p < fade)   g *= (float) (endAt - p) / fade;
        for (int c = 0; c < numChannels; ++c)
        {
            if (out[c] == nullptr) continue;
            const int src = jmin (c, b.getNumChannels() - 1);
            const float* x = b.getReadPointer (src);
            out[c][i] = g * (x[p] + fr * (x[p + 1] - x[p]));
        }
        pos += step;
    }
}

void SlicePlayer::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int numSamples,
                                                    const AudioIODeviceCallbackContext&)
{
    render (out, numOut, numSamples, deviceRate);
}

// ===========================================================================
// SlicerComponent
// ===========================================================================

namespace
{
    const Colour kWave { 0xffe4e6eb }, kCut { 0xffffa53a }, kSel { 0x40ffffff }, kRed { 0xffd9342b };
    void fitText (Graphics& g, const String& t, juce::Rectangle<float> r, float h, Colour c, Justification j = Justification::centredLeft, bool bold = false)
    {
        Theme::text (g, t, r, jmin (h, h * (r.getWidth() - 2.0f) / jmax (1.0f, PixelFont::getWidth (t, h))), c, j, bold);
    }
    const char* gridName (int sixteenths)
    {
        switch (sixteenths)
        {
            case 16: return "1 BAR";
            case 8:  return "1/2 BAR";
            case 4:  return "1 BEAT";
            case 2:  return "1/8";
            default: return "1/16";
        }
    }
}

SlicerComponent::SlicerComponent (MakerComponent& m) : maker (m)
{
    for (auto* c : std::initializer_list<Component*> { &loadBtn, &autoBtn, &sensBox, &gridBtn, &gridSizeBtn, &bpmBox, &clearBtn,
                                                        &useAllBtn, &useNoneBtn, &generateBtn, &closeBtn })
        addAndMakeVisible (c);
    for (auto* v : { &sensBox, &bpmBox }) { v->setShowSpinner (true); v->setJustification (Justification::centred); }
    bpmBox.setValue (maker.getPattern().settings.bpm);
    loadBtn.setComponentID ("slicerLoad");
    generateBtn.setComponentID ("slicerGenerate");
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("A sample to slice", file.existsAsFile() ? file : FourTrackerProcessor::getUserFolder(),
                                                 maker.getProcessor().getSupportedAudioWildcard());
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        {
            if (fc.getResult() == File()) return;
            const auto err = loadFile (fc.getResult());
            if (err.isNotEmpty()) setStatus (err);
        });
    };
    autoBtn.onClick = [this] { autoSlice(); };
    sensBox.onChange = [this] (int) { if (audio != nullptr) autoSlice(); };
    gridBtn.onClick = [this] { gridSlice(); };
    gridSizeBtn.onClick = [this]
    {
        PopupMenu menu;
        for (int g : { 16, 8, 4, 2, 1 }) menu.addItem (g, gridName (g), true, g == gridSize);
        Component::SafePointer<SlicerComponent> safe (this);
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&gridSizeBtn), [safe] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            safe->gridSize = r;
            safe->refreshControls();
            if (safe->audio != nullptr) safe->gridSlice();
        });
    };
    bpmBox.onChange = [this] (int) { refreshControls(); };
    for (auto* b : { &halfBtn, &doubleBtn }) addAndMakeVisible (b);
    toMakerBtn.onClick = [this]
    {
        maker.setBpm (bpmBox.getValue());
        setStatus ("THE PATTERN MAKER IS AT " + String (bpmBox.getValue()) + " BPM NOW");
    };
    addAndMakeVisible (toMakerBtn);
    halfBtn.onClick   = [this] { bpmBox.setValue (jmax (40, roundToInt (bpmBox.getValue() / 2.0))); bpmSource = "HALVED"; setStatus ("BPM " + String (bpmBox.getValue())); };
    doubleBtn.onClick = [this] { bpmBox.setValue (jmin (255, bpmBox.getValue() * 2)); bpmSource = "DOUBLED"; setStatus ("BPM " + String (bpmBox.getValue())); };
    clearBtn.onClick = [this] { clearSlices(); };
    for (int i = 0; i < 4; ++i)
    {
        auto b = std::make_unique<PixelButton> ("PADS " + String (i * 16 + 1) + "-" + String (i * 16 + 16), PixelButton::Style::Segment, 14.0f);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (8201);
        b->onClick = [this, i] { page = i; refreshControls(); repaint(); };
        addAndMakeVisible (*b);
        pageBtns[(size_t) i] = std::move (b);
    }
    useAllBtn.onClick = [this]
    {
        for (int i = page * 16; i < jmin (numSlices(), page * 16 + 16); ++i) setUsed (i, true);
        repaint();
    };
    useNoneBtn.onClick = [this] { std::fill (used.begin(), used.end(), false); refreshControls(); repaint(); };
    generateBtn.onClick = [this] { setStatus (generateSampleList()); };
    closeBtn.onClick = [this] { player.stop(); if (onClose) onClose(); };

    status = "LOAD SAMPLE: A LONG SAMPLE (VOICES, A BEAT ...). AUTO CUTS AT EVERY HIT, GRID ON THE BPM";
    setWantsKeyboardFocus (true);
    setSize (kWidth, kHeight);
    refreshControls();
    startTimerHz (30);
}

SlicerComponent::~SlicerComponent()
{
    stopTimer();
    player.stop();
}

String SlicerComponent::loadFile (const File& f)
{
    auto& formats = maker.getProcessor().getFormatManager();
    std::unique_ptr<AudioFormatReader> r (formats.createReaderFor (f));
    if (r == nullptr) return "CAN'T READ " + f.getFileName().toUpperCase();
    const auto maxLen = (int64) (r->sampleRate * 600.0);
    if (r->lengthInSamples > maxLen) return "TOO LONG: 10 MINUTES IS THE MOST";
    auto b = std::make_shared<AudioBuffer<float>> ((int) jmin (2u, jmax (1u, r->numChannels)), (int) r->lengthInSamples);
    r->read (b.get(), 0, (int) r->lengthInSamples, 0, true, b->getNumChannels() > 1);
    audio = b;
    rate = r->sampleRate;
    file = f;
    player.setAudio (audio, rate);
    buildPeaks();
    // "... 136 BPM ..." in the name: that is the BPM for GRID; else from the loop's length
    bpmSource = {};
    {
        auto words = StringArray::fromTokens (f.getFileNameWithoutExtension().toUpperCase().replace ("BPM", " BPM "), " _-", "");
        words.removeEmptyStrings();
        for (int w = 1; w < words.size(); ++w)
            if (words[w] == "BPM" && words[w - 1].isNotEmpty() && words[w - 1].containsOnly ("0123456789"))
            {
                const int b = words[w - 1].getIntValue();
                if (b >= 40 && b <= 255) { bpmBox.setValue (b); bpmSource = "FROM THE FILE NAME"; }
            }
    }
    page = 0;
    autoSlice();
    if (bpmSource.isEmpty())
    {
        double bars = 0.0;
        const int b = guessBpm (audio->getNumSamples(), rate, starts, bars);
        if (b > 0)
        {
            bpmBox.setValue (b);
            bpmSource = bars == 0.5 ? String ("1/2 BAR LONG") : String (bars, 0) + (bars == 1.0 ? " BAR LONG" : " BARS LONG");
        }
    }
    setStatus (String (numSlices()) + " SLICES, " + (bpmSource.isNotEmpty() ? String (bpmBox.getValue()) + " BPM (" + bpmSource + ")"
                                                                        : String ("BPM: NOT A LOOP - SET IT FOR GRID"))
               + ". PLAY THE PADS, TICK THE ONES YOU WANT");
    return {};
}

void SlicerComponent::autoSlice()
{
    if (audio == nullptr) { setStatus ("LOAD A SAMPLE FIRST"); return; }
    starts = detectSlices (*audio, rate, sensBox.getValue() / 100.0f);
    used.assign (starts.size(), false);
    selected = -1;
    refreshControls();
    setStatus ("AUTO: " + String (numSlices()) + " SLICES (SENS " + String (sensBox.getValue()) + ": HIGHER = MORE SLICES)");
}

void SlicerComponent::gridSlice()
{
    if (audio == nullptr) { setStatus ("LOAD A SAMPLE FIRST"); return; }
    starts = gridSlices (audio->getNumSamples(), rate, bpmBox.getValue(), gridSize);
    used.assign (starts.size(), false);
    selected = -1;
    refreshControls();
    const double total = audio->getNumSamples() / (rate * 60.0 / bpmBox.getValue() * gridSize / 4.0);
    setStatus ("GRID: " + String (numSlices()) + " SLICES OF " + gridName (gridSize) + " AT " + String (bpmBox.getValue()) + " BPM"
               + (total > kMaxSlices ? " (THE FIRST " + String (kMaxSlices) + ")" : String()));
}

void SlicerComponent::clearSlices()
{
    if (audio == nullptr) return;
    starts = { 0 };
    used.assign (1, false);
    selected = -1;
    refreshControls();
    setStatus ("ONE SLICE - DOUBLE-CLICK THE WAVE TO CUT IT");
}

int SlicerComponent::sliceEnd (int i) const
{
    if (! isPositiveAndBelow (i, numSlices())) return 0;
    return i + 1 < numSlices() ? starts[(size_t) i + 1] : (audio != nullptr ? audio->getNumSamples() : 0);
}

void SlicerComponent::playSlice (int i)
{
    if (audio == nullptr || ! isPositiveAndBelow (i, numSlices())) return;
    player.play (sliceStart (i), sliceEnd (i));
    selected = lastPlayed = i;
    playStartMs = Time::getMillisecondCounterHiRes();
    repaint();
}

void SlicerComponent::setUsed (int slice, bool use)
{
    if (! isPositiveAndBelow (slice, numSlices())) return;
    if (use && ! used[(size_t) slice] && usedSlices().size() >= kMaxUsedSlices)
    {
        setStatus ("16 SLICES IS THE MOST - THERE ARE 16 LANES");
        return;
    }
    used[(size_t) slice] = use;
    refreshControls();
    repaint();
}

bool SlicerComponent::isUsed (int slice) const { return isPositiveAndBelow (slice, (int) used.size()) && used[(size_t) slice]; }

Array<int> SlicerComponent::usedSlices() const
{
    Array<int> out;
    for (int i = 0; i < (int) used.size(); ++i) if (used[(size_t) i]) out.add (i);
    return out;
}

String SlicerComponent::generateSampleList()
{
    if (audio == nullptr) return "LOAD A SAMPLE FIRST";
    const auto list = usedSlices();
    if (list.isEmpty()) return "TICK USE ON THE SLICES YOU WANT (OR USE ALL 16)";
    const auto name = File::createLegalFileName (file.getFileNameWithoutExtension()).toUpperCase();
    const auto folder = FourTrackerProcessor::getUserFolder().getChildFile ("Slices").getChildFile (name);
    const int first = firstSliceLane (list.size());
    for (int k = 0; k < list.size(); ++k)
    {
        const int i = list[k];
        const auto f = folder.getChildFile (name + " SLICE " + String (i + 1).paddedLeft ('0', 2) + ".wav");
        const auto err = writeSlice (*audio, rate, sliceStart (i), sliceEnd (i), f);
        if (err.isNotEmpty()) return err;
        maker.setLaneSound (first + k, f, true);
    }
    maker.setLaneView (first + list.size() - 1 >= 8 ? 1 : 0);
    const bool bpmToo = bpmSource.isNotEmpty() && maker.getPattern().settings.bpm != bpmBox.getValue();
    if (bpmToo) maker.setBpm (bpmBox.getValue());      // the pattern at the sample's tempo
    const auto lanes = "LANES " + String (first + 1) + "-" + String (first + list.size());
    maker.showStatus (String (list.size()) + " SLICES ON " + lanes + (first > 0 ? " - THE KICK AND THE OTHER SOUNDS STAY" : String())
                      + ". CLICK THE STEPS IN, OR PLAY THEM WITH REC");
    return String (list.size()) + " SLICES ARE ON " + lanes + (bpmToo ? ", THE MAKER AT " + String (bpmBox.getValue()) + " BPM" : String())
           + " (SAVED IN FOUR TRACKER/SLICES/" + name + ")";
}

void SlicerComponent::addCut (int samplePos)
{
    if (audio == nullptr) return;
    if (numSlices() >= kMaxSlices) { setStatus ("64 SLICES IS THE MOST"); return; }
    const int p = snapToZero (*audio, rate, samplePos);
    if (p <= 0 || p >= audio->getNumSamples() - 32) return;
    auto it = std::lower_bound (starts.begin(), starts.end(), p);
    if (it != starts.end() && *it == p) return;
    const auto idx = (size_t) (it - starts.begin());
    starts.insert (it, p);
    used.insert (used.begin() + (long) idx, false);
    refreshControls();
    repaint();
}

void SlicerComponent::removeCut (int index)
{
    if (index <= 0 || index >= numSlices()) return;          // the first one is the start
    starts.erase (starts.begin() + index);
    used.erase (used.begin() + index);
    selected = -1;
    refreshControls();
    repaint();
}

void SlicerComponent::buildPeaks()
{
    peaks.clear();
    if (audio == nullptr || wave.getWidth() <= 0) return;
    const int w = wave.getWidth(), n = audio->getNumSamples();
    peaks.resize ((size_t) w);
    for (int x = 0; x < w; ++x)
    {
        const int a = (int) ((int64) x * n / w), b = jmax (a + 1, (int) ((int64) (x + 1) * n / w));
        float lo = 0.0f, hi = 0.0f;
        for (int c = 0; c < audio->getNumChannels(); ++c)
        {
            auto r = audio->findMinMax (c, a, jmin (b, n) - a);
            lo = jmin (lo, r.getStart());
            hi = jmax (hi, r.getEnd());
        }
        peaks[(size_t) x] = { lo, hi };
    }
}

int SlicerComponent::xForSample (int s) const
{
    if (audio == nullptr || audio->getNumSamples() == 0) return wave.getX();
    return wave.getX() + (int) ((int64) s * wave.getWidth() / audio->getNumSamples());
}

int SlicerComponent::sampleForX (int x) const
{
    if (audio == nullptr) return 0;
    return jlimit (0, audio->getNumSamples() - 1, (int) ((int64) (x - wave.getX()) * audio->getNumSamples() / jmax (1, wave.getWidth())));
}

int SlicerComponent::cutNear (int x) const
{
    for (int i = 1; i < numSlices(); ++i)
        if (std::abs (xForSample (starts[(size_t) i]) - x) <= 6) return i;
    return -1;
}

int SlicerComponent::sliceAt (int samplePos) const
{
    auto it = std::upper_bound (starts.begin(), starts.end(), samplePos);
    return jmax (0, (int) (it - starts.begin()) - 1);
}

Rectangle<int> SlicerComponent::padBounds (int pad) const
{
    const int pw = (padsArea.getWidth() - 7 * 10) / 8, ph = (padsArea.getHeight() - 10) / 2;
    return { padsArea.getX() + (pad % 8) * (pw + 10), padsArea.getY() + (pad / 8) * (ph + 10), pw, ph };
}

void SlicerComponent::refreshControls()
{
    gridSizeBtn.setButtonText (gridName (gridSize));
    const int pages = jmax (1, (numSlices() + 15) / 16);
    page = jmin (page, pages - 1);
    for (int i = 0; i < 4; ++i)
    {
        pageBtns[(size_t) i]->setVisible (i < pages && pages > 1);
        pageBtns[(size_t) i]->setToggleState (i == page, dontSendNotification);
    }
    generateBtn.setButtonText ("GENERATE SAMPLE LIST (" + String (usedSlices().size()) + ")");
    repaint();
}

void SlicerComponent::timerCallback()
{
    if (player.isPlaying() || lastPlayed >= 0) repaint();
    if (! player.isPlaying()) lastPlayed = -1;
}

void SlicerComponent::paint (Graphics& g)
{
    g.fillAll (Theme::background);
    Theme::drawPanel (g, getLocalBounds().reduced (12).toFloat());
    Theme::text (g, "SLICER", { 28.0f, 20.0f, 200.0f, 40.0f }, 30.0f, Theme::ink, Justification::centredLeft, true);
    fitText (g, file.existsAsFile() ? file.getFileName().toUpperCase() : String ("NO SAMPLE"), { 200.0f, 28.0f, 820.0f, 24.0f }, 15.0f,
             Theme::ink.withAlpha (0.7f));
    auto label = [&] (const String& t, Component& c)
    {
        Theme::text (g, t, { (float) c.getX(), (float) c.getY() - 20.0f, (float) c.getWidth() + 40.0f, 18.0f }, 13.0f, Theme::ink, Justification::bottomLeft);
    };
    label ("SENS", sensBox);
    label ("GRID SIZE", gridSizeBtn);
    label ("BPM", bpmBox);

    // the wave and the cuts
    Theme::drawScreen (g, wave.toFloat().expanded (4.0f));
    if (audio != nullptr)
    {
        if (isPositiveAndBelow (selected, numSlices()))
        {
            g.setColour (kSel);
            const int a = xForSample (sliceStart (selected)), b = xForSample (sliceEnd (selected));
            g.fillRect (a, wave.getY(), jmax (1, b - a), wave.getHeight());
        }
        g.setColour (kWave);
        const float mid = (float) wave.getCentreY(), half = wave.getHeight() * 0.48f;
        for (int x = 0; x < (int) peaks.size(); ++x)
        {
            auto [lo, hi] = peaks[(size_t) x];
            g.drawVerticalLine (wave.getX() + x, mid - hi * half, mid - lo * half + 1.0f);
        }
        for (int i = 0; i < numSlices(); ++i)
        {
            const int x = xForSample (starts[(size_t) i]);
            const bool onPage = i / 16 == page;
            g.setColour (isUsed (i) ? kRed : kCut.withAlpha (onPage ? 1.0f : 0.45f));
            g.drawVerticalLine (x, (float) wave.getY(), (float) wave.getBottom());
            g.fillRect (x, wave.getY(), 3, 14);
            const int nextX = xForSample (sliceEnd (i));
            if (nextX - x > 18)
                Theme::text (g, String (i + 1), { (float) x + 4.0f, (float) wave.getY() + 1.0f, (float) jmin (40, nextX - x - 4), 14.0f }, 12.0f,
                             isUsed (i) ? kRed : kCut, Justification::centredLeft);
        }
        // where it plays
        if (player.isPlaying() && isPositiveAndBelow (lastPlayed, numSlices()))
        {
            const double el = (Time::getMillisecondCounterHiRes() - playStartMs) * 0.001 * rate;
            const int x = xForSample (sliceStart (lastPlayed) + (int) el);
            g.setColour (Colours::white);
            g.drawVerticalLine (jmin (x, xForSample (sliceEnd (lastPlayed))), (float) wave.getY(), (float) wave.getBottom());
        }
    }
    else
        Theme::text (g, "LOAD SAMPLE", wave.toFloat(), 22.0f, Theme::screenDim, Justification::centred);

    Theme::text (g, "CLICK: HEAR A SLICE    DOUBLE-CLICK: CUT    DRAG A CUT: MOVE IT    RIGHT-CLICK A CUT: REMOVE IT",
                 { (float) wave.getX(), (float) wave.getBottom() + 8.0f, (float) wave.getWidth(), 18.0f }, 13.0f, Theme::ink.withAlpha (0.65f), Justification::centredLeft);

    // the 16 pads of the page
    const auto usedList = usedSlices();
    for (int pad = 0; pad < 16; ++pad)
    {
        const int i = page * 16 + pad;
        const auto r = padBounds (pad);
        const bool there = i < numSlices();
        const bool lit = there && i == lastPlayed && player.isPlaying();
        Theme::drawRaised (g, r.toFloat(), lit ? kRed : (there ? Theme::buttonLight : Theme::buttonDark), lit);
        if (! there) continue;
        Theme::text (g, String (i + 1).paddedLeft ('0', 2), r.toFloat().removeFromTop (r.getHeight() * 0.55f), 26.0f, Theme::ink, Justification::centred, true);
        const double ms = (sliceEnd (i) - sliceStart (i)) * 1000.0 / rate;
        Theme::text (g, ms >= 1000.0 ? String (ms / 1000.0, 2) + " S" : String (roundToInt (ms)) + " MS",
                     { (float) r.getX(), r.getY() + r.getHeight() * 0.52f, (float) r.getWidth(), 18.0f }, 13.0f, Theme::ink.withAlpha (0.7f), Justification::centred);
        // USE: the box in the corner, and the lane it goes to
        auto box = juce::Rectangle<int> (r.getRight() - 30, r.getY() + 8, 22, 22);
        g.setColour (Theme::ink);
        g.drawRect (box, 2);
        const int k = usedList.indexOf (i);
        if (k >= 0)
        {
            g.setColour (lit ? Colours::white : kRed);
            g.fillRect (box.reduced (4));
            fitText (g, "> " + laneLabel (firstSliceLane (usedList.size()) + k), { (float) r.getX() + 6.0f, (float) r.getBottom() - 24.0f, (float) r.getWidth() - 12.0f, 18.0f }, 12.0f,
                     lit ? Colours::white : kRed.darker (0.3f), Justification::centred, true);
        }
    }
    fitText (g, "CLICK A PAD: HEAR IT    THE BOX: USE IT (ONTO LANES 9-16 FIRST, THE KICK STAYS)    MIDI PADS / KEYS FROM C-2    COMPUTER: 1-8, Q-I",
             { (float) padsArea.getX(), (float) padsArea.getBottom() + 6.0f, (float) padsArea.getWidth(), 18.0f }, 13.0f, Theme::ink.withAlpha (0.65f));

    Theme::drawScreen (g, statusArea.toFloat());
    fitText (g, status, statusArea.toFloat().reduced (12.0f, 0.0f), 15.0f, Theme::screenText);
}

void SlicerComponent::resized()
{
    const int y = 84, h = 46;
    loadBtn.setBounds (28, y, 200, h);
    autoBtn.setBounds (246, y, 100, h);
    sensBox.setBounds (354, y, 110, h);
    gridBtn.setBounds (484, y, 100, h);
    gridSizeBtn.setBounds (592, y, 130, h);
    bpmBox.setBounds (730, y, 120, h);
    halfBtn.setBounds (856, y, 50, h);
    doubleBtn.setBounds (910, y, 50, h);
    toMakerBtn.setBounds (966, y, 130, h);
    clearBtn.setBounds (1102, y, 110, h);
    closeBtn.setBounds (getWidth() - 28 - 120, 24, 120, 44);
    wave = { 32, 152, getWidth() - 64, 210 };
    buildPeaks();
    const int py = wave.getBottom() + 38;
    for (int i = 0; i < 4; ++i) pageBtns[(size_t) i]->setBounds (28 + i * 150, py, 142, 34);
    useNoneBtn.setBounds (getWidth() - 28 - 140, py, 140, 34);
    useAllBtn.setBounds (useNoneBtn.getX() - 150, py, 140, 34);
    padsArea = { 28, py + 46, getWidth() - 56, 210 };
    generateBtn.setBounds (28, padsArea.getBottom() + 30, 420, 52);
    statusArea = { 460, padsArea.getBottom() + 30, getWidth() - 488, 52 };
}

bool SlicerComponent::keyPressed (const KeyPress& k)
{
    const String row1 = "12345678", row2 = "QWERTYUI";
    const auto ch = (juce_wchar) CharacterFunctions::toUpperCase (k.getTextCharacter());
    if (! k.getModifiers().isAnyModifierKeyDown())
    {
        if (row1.containsChar (ch)) { padHit (row1.indexOfChar (ch)); return true; }
        if (row2.containsChar (ch)) { padHit (8 + row2.indexOfChar (ch)); return true; }
    }
    if (k.getKeyCode() == KeyPress::spaceKey) { player.stop(); return true; }
    if (k.getKeyCode() == KeyPress::escapeKey) { player.stop(); if (onClose) onClose(); return true; }
    return false;
}

void SlicerComponent::mouseDown (const MouseEvent& e)
{
    grabKeyboardFocus();
    dragging = -1;
    if (wave.expanded (0, 4).contains (e.getPosition()) && audio != nullptr)
    {
        const int c = cutNear (e.x);
        if (e.mods.isPopupMenu()) { if (c > 0) removeCut (c); return; }
        if (c > 0) { dragging = c; return; }
        playSlice (sliceAt (sampleForX (e.x)));
        return;
    }
    for (int pad = 0; pad < 16; ++pad)
    {
        const auto r = padBounds (pad);
        if (! r.contains (e.getPosition())) continue;
        const int i = page * 16 + pad;
        if (i >= numSlices()) return;
        const auto box = juce::Rectangle<int> (r.getRight() - 40, r.getY(), 40, 40);
        if (box.contains (e.getPosition()) || e.mods.isPopupMenu()) setUsed (i, ! isUsed (i));
        else playSlice (i);
        return;
    }
}

void SlicerComponent::mouseDrag (const MouseEvent& e)
{
    if (dragging <= 0 || audio == nullptr) return;
    const int lo = starts[(size_t) dragging - 1] + 32;
    const int hi = (dragging + 1 < numSlices() ? starts[(size_t) dragging + 1] : audio->getNumSamples()) - 32;
    if (hi <= lo) return;
    starts[(size_t) dragging] = jlimit (lo, hi, sampleForX (e.x));
    repaint();
}

void SlicerComponent::mouseUp (const MouseEvent&)
{
    if (dragging > 0 && audio != nullptr)
    {
        starts[(size_t) dragging] = snapToZero (*audio, rate, starts[(size_t) dragging]);
        playSlice (dragging);
    }
    dragging = -1;
}

void SlicerComponent::mouseDoubleClick (const MouseEvent& e)
{
    if (audio != nullptr && wave.contains (e.getPosition()) && cutNear (e.x) < 0)
    {
        addCut (sampleForX (e.x));
        setStatus (String (numSlices()) + " SLICES");
    }
}

} // namespace pm

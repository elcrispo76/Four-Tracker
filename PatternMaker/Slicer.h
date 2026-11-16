// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker Pattern Maker - Copyright (C) 2026 elcrispo76
//
// SLICER: a long sample (voices, a beat ...) cut into slices - at each hit (AUTO), on a grid of the
// BPM (GRID) or by hand - to hear on the pads and put on the lanes (GENERATE SAMPLE LIST).

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>
#include "UI/Widgets.h"
#include "PatternGen.h"

class MakerComponent;

namespace pm
{

constexpr int kMaxSlices = 64;      // 4 pages of 16 pads
constexpr int kMaxUsedSlices = 16;  // as many as there are lanes

/** The lane the first of n used slices goes on (0-based): 1-8 slices on lanes 9-16 (9, 10 ...),
    more end on lane 16 (10 slices: lanes 7-16), so the kick and the first sounds stay. */
int firstSliceLane (int numUsed);

/** Where each hit starts (in samples, sorted, the first one 0). sensitivity 0..1: more slices higher up.
    At most maxSlices (the strongest hits are kept). */
std::vector<int> detectSlices (const juce::AudioBuffer<float>& audio, double sampleRate, float sensitivity, int maxSlices = kMaxSlices);

/** The BPM of a loop from its length: the number of bars (1/2, 1, 2, 4, 8, 16) that gives a whole BPM
    between 70 and 190. When two fit (87 or 174), the hits decide: the one where they are not faster than
    16ths. 0: can't tell (not a loop cut to the bar). bars gets the bars it found (0.5 for half a bar). */
int guessBpm (int numSamples, double sampleRate, const std::vector<int>& hits, double& bars);

/** Equal slices: sixteenths 16ths of a 4/4 bar each, at the BPM (16 = 1 bar, 4 = a beat), from the start. */
std::vector<int> gridSlices (int numSamples, double sampleRate, double bpm, int sixteenths, int maxSlices = kMaxSlices);

/** The nearest place within 2 ms where the sound crosses zero (a cut there doesn't click). */
int snapToZero (const juce::AudioBuffer<float>& audio, double sampleRate, int pos);

/** Writes samples [start, end) as a WAV, with a short fade in and out so it doesn't click. */
juce::String writeSlice (const juce::AudioBuffer<float>& audio, double sampleRate, int start, int end, const juce::File& file);

/** Plays one slice at a time on the audio device, next to the Pattern Maker's own sound. */
class SlicePlayer : public juce::AudioIODeviceCallback
{
public:
    void setAudio (std::shared_ptr<const juce::AudioBuffer<float>> audio, double sampleRate);
    void play (int start, int end);
    void stop();
    bool isPlaying() const { return playing.load(); }

    /** The sound for n samples at the device's rate (also for the test). */
    void render (float* const* out, int numChannels, int numSamples, double deviceRate);

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override { deviceRate = d->getCurrentSampleRate(); }
    void audioDeviceStopped() override {}

private:
    juce::CriticalSection lock;
    std::shared_ptr<const juce::AudioBuffer<float>> buffer;
    double fileRate = 44100.0, deviceRate = 44100.0, pos = 0.0;
    int startAt = 0, endAt = 0;
    std::atomic<bool> playing { false };
};

/** The SLICER: waveform with the cuts, 16 pads, GENERATE SAMPLE LIST. */
class SlicerComponent : public juce::Component, private juce::Timer
{
public:
    static constexpr int kWidth = 1240, kHeight = 780;

    explicit SlicerComponent (MakerComponent& maker);
    ~SlicerComponent() override;

    juce::String loadFile (const juce::File& f);           // "" = OK
    void autoSlice();
    void gridSlice();
    void clearSlices();
    int  numSlices() const { return (int) starts.size(); }
    int  sliceStart (int i) const { return starts[(size_t) i]; }
    int  sliceEnd (int i) const;
    void playSlice (int i);
    /** A pad (0..15) on the page in view; for MIDI and the keys. */
    void padHit (int pad) { playSlice (page * 16 + pad); }
    void setUsed (int slice, bool use);
    bool isUsed (int slice) const;
    juce::Array<int> usedSlices() const;                   // in order, onto the lanes from firstSliceLane()
    /** GENERATE SAMPLE LIST: the used slices as WAVs, onto lanes 9-16 first (see firstSliceLane). Returns what happened. */
    juce::String generateSampleList();
    void addCut (int samplePos);
    void removeCut (int index);
    void setSensitivity (int s) { sensBox.setValue (s); }
    void setGridSize (int sixteenths) { gridSize = sixteenths; refreshControls(); }
    int  getBpm() const { return bpmBox.getValue(); }
    void setBpm (int b) { bpmBox.setValue (b); bpmSource = "SET"; }
    juce::String bpmSource;                                 // how it got the BPM: "FILE NAME", "1 BAR" ...

    SlicePlayer player;
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void refreshControls();
    void buildPeaks();
    void setStatus (const juce::String& s) { status = s; repaint(); }
    int  xForSample (int s) const;
    int  sampleForX (int x) const;
    int  cutNear (int x) const;               // index of a cut (not the first) within 6 px, or -1
    int  sliceAt (int samplePos) const;
    juce::Rectangle<int> padBounds (int pad) const;

    MakerComponent& maker;
    std::shared_ptr<juce::AudioBuffer<float>> audio;
    double rate = 44100.0;
    juce::File file;
    std::vector<int> starts;
    std::vector<bool> used;
    std::vector<std::pair<float, float>> peaks;   // min / max per pixel of the waveform
    int page = 0, selected = -1, dragging = -1, gridSize = 4, lastPlayed = -1;
    double playStartMs = 0.0;
    juce::String status;
    juce::Rectangle<int> wave, padsArea, statusArea;
    std::unique_ptr<juce::FileChooser> chooser;

    PixelButton loadBtn { "LOAD SAMPLE", PixelButton::Style::Dark, 18.0f };
    PixelButton autoBtn { "AUTO", PixelButton::Style::Raised, 16.0f };
    ValueBox sensBox { 0, 100, 50, 22.0f };
    PixelButton gridBtn { "GRID", PixelButton::Style::Raised, 16.0f };
    PixelButton gridSizeBtn { "1/4 BAR", PixelButton::Style::Raised, 14.0f };
    ValueBox bpmBox { 40, 255, 124, 22.0f };
    PixelButton toMakerBtn { "BPM TO MAKER", PixelButton::Style::Raised, 13.0f };
    PixelButton halfBtn { "/2", PixelButton::Style::Raised, 14.0f };
    PixelButton doubleBtn { "X2", PixelButton::Style::Raised, 14.0f };
    PixelButton clearBtn { "CLEAR", PixelButton::Style::Raised, 16.0f };
    std::array<std::unique_ptr<PixelButton>, 4> pageBtns;
    PixelButton useAllBtn { "USE ALL 16", PixelButton::Style::Raised, 14.0f };
    PixelButton useNoneBtn { "USE NONE", PixelButton::Style::Raised, 14.0f };
    PixelButton generateBtn { "GENERATE SAMPLE LIST", PixelButton::Style::Record, 20.0f };
    PixelButton closeBtn { "CLOSE", PixelButton::Style::Raised, 16.0f };
};

} // namespace pm

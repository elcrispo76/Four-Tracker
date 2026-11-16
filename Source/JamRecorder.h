// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

// REC: records everything you hear (the tracks, their VST effects, the master effects, MASTER volume and
// FADE) while you jam, straight to a WAV file on the disk in the background, so a long jam neither fills
// the memory nor makes the sound stutter. Afterwards it can be saved as WAV (24-bit), FLAC (24-bit) or
// MP3 (320 kbps, LAME), listened to, or thrown away to try again.
//
// The take is written to "Documents/Four Tracker/Recordings/REC in progress.wav". If the program closes
// before it is saved, it is kept as "Unsaved jam <date>.wav" the next time.

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>

class JamRecorder
{
public:
    JamRecorder();
    ~JamRecorder();

    enum class Format { wav, flac, mp3 };
    static juce::String extensionFor (Format f) { return f == Format::flac ? ".flac" : f == Format::mp3 ? ".mp3" : ".wav"; }

    /** Message thread. Starts a new take (the last unsaved take is thrown away). */
    juce::String start (double sampleRate, int numChannels);
    /** Message thread. Stops; the take waits to be saved or discarded. */
    void stop();
    bool isRecording() const noexcept { return recording.load(); }
    /** A finished take that hasn't been saved or discarded yet. */
    bool hasTake() const;
    void discard();
    /** Seconds recorded (while recording: so far). */
    double getSeconds() const noexcept;
    /** The take's own file (WAV), e.g. to listen to it. */
    juce::File getTakeFile() const { return takeFile(); }
    /** Plugin instances each get their own take file (several can record in one DAW). */
    void setUniqueTakeName() { takeName = "REC in progress " + juce::Uuid().toString().substring (0, 8) + ".wav"; }

    /** Message thread (or a background thread): writes the take as WAV / FLAC / MP3. Empty = it worked.
        progress (0..1) is updated while it runs. The take is kept, so it can be saved more than once. */
    juce::String save (const juce::File& dest, Format format, std::atomic<float>* progress = nullptr) const;

    /** Audio thread: the finished sound of one block. */
    void process (const juce::AudioBuffer<float>& buffer) noexcept;

    static juce::File folder();
    /** A name like "Jam 2026-11-01 22-31". */
    static juce::String suggestedName();
    /** At program start (the standalone app): a take left over from a crash becomes "Unsaved jam <date>.wav". */
    static void rescueLeftover();

private:
    juce::File takeFile() const;
    juce::String takeName { "REC in progress.wav" };
    juce::TimeSliceThread writerThread { "Four Tracker REC" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    juce::SpinLock writerLock;
    juce::AudioFormatWriter::ThreadedWriter* active = nullptr;   // the audio thread writes into this
    std::atomic<bool> recording { false };
    std::atomic<juce::int64> samples { 0 };
    double rate = 44100.0;
    int channels = 2;
    bool takeReady = false;
};

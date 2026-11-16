// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#include "JamRecorder.h"
#include <lame.h>

using namespace juce;

JamRecorder::JamRecorder()
{
    writerThread.startThread();
}

JamRecorder::~JamRecorder()
{
    stop();
    writerThread.stopThread (2000);
}

File JamRecorder::folder()
{
    return File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Four Tracker").getChildFile ("Recordings");
}

File JamRecorder::takeFile() const
{
    return folder().getChildFile (takeName);
}

String JamRecorder::suggestedName()
{
    return "Jam " + Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M");
}

void JamRecorder::rescueLeftover()
{
    for (const auto& f : folder().findChildFiles (File::findFiles, false, "*.wav"))
    {
        if (! f.getFileName().startsWith ("REC in progress")) continue;
        if (f.getSize() > 1024)
            f.moveFileTo (folder().getNonexistentChildFile ("Unsaved jam " + f.getLastModificationTime().formatted ("%Y-%m-%d %H-%M"), ".wav", false));
        else
            f.deleteFile();
    }
}

String JamRecorder::start (double sampleRate, int numChannels)
{
    stop();
    discard();
    rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    channels = jlimit (1, 2, numChannels);
    folder().createDirectory();
    const auto f = takeFile();
    f.deleteFile();
    auto out = f.createOutputStream();
    if (out == nullptr) return "CAN'T WRITE IN " + folder().getFullPathName().toUpperCase();

    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os (out.release());
    auto opts = AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (channels).withBitsPerSample (24);
    auto w = wav.createWriterFor (os, opts);
    if (w == nullptr) return "CAN'T WRITE THE RECORDING";
    // about 4 seconds of room between the audio thread and the disk
    writer = std::make_unique<AudioFormatWriter::ThreadedWriter> (w.release(), writerThread, (int) (rate * 4.0));
    samples = 0;
    {
        const SpinLock::ScopedLockType sl (writerLock);
        active = writer.get();
    }
    recording = true;
    return {};
}

void JamRecorder::stop()
{
    if (! recording.load() && writer == nullptr) return;
    {
        const SpinLock::ScopedLockType sl (writerLock);
        active = nullptr;
    }
    recording = false;
    const bool had = writer != nullptr;
    writer.reset();                 // writes what is left and closes the file
    takeReady = had && samples.load() > 0;
    if (had && ! takeReady) takeFile().deleteFile();
}

bool JamRecorder::hasTake() const
{
    return takeReady && ! recording.load() && takeFile().existsAsFile();
}

void JamRecorder::discard()
{
    if (recording.load()) stop();
    takeFile().deleteFile();
    takeReady = false;
    samples = 0;
}

double JamRecorder::getSeconds() const noexcept
{
    return (double) samples.load() / rate;
}

void JamRecorder::process (const AudioBuffer<float>& buffer) noexcept
{
    if (! recording.load()) return;
    const SpinLock::ScopedTryLockType sl (writerLock);
    if (! sl.isLocked() || active == nullptr) return;
    const float* chans[2] = { buffer.getReadPointer (0), buffer.getReadPointer (buffer.getNumChannels() > 1 && channels > 1 ? 1 : 0) };
    if (active->write (chans, buffer.getNumSamples()))
        samples += buffer.getNumSamples();
}

String JamRecorder::save (const File& dest, Format format, std::atomic<float>* progress) const
{
    const auto src = takeFile();
    WavAudioFormat wav;
    std::unique_ptr<AudioFormatReader> reader (wav.createReaderFor (src.createInputStream().release(), true));
    if (reader == nullptr) return "THE RECORDING CAN'T BE READ";
    const int nch = jlimit (1, 2, (int) reader->numChannels);
    const auto total = reader->lengthInSamples;
    if (total <= 0) return "THE RECORDING IS EMPTY";

    const auto tmp = dest.getSiblingFile (dest.getFileName() + ".tmp");
    tmp.deleteFile();
    constexpr int chunk = 32768;
    AudioBuffer<float> buf (nch, chunk);
    String err;
    {
        auto out = tmp.createOutputStream();
        if (out == nullptr) return "CAN'T WRITE " + dest.getFileName().toUpperCase();

        if (format == Format::mp3)
        {
            lame_global_flags* gf = lame_init();
            lame_set_in_samplerate (gf, (int) reader->sampleRate);
            lame_set_num_channels (gf, nch);
            lame_set_mode (gf, nch == 2 ? JOINT_STEREO : MONO);
            lame_set_brate (gf, 320);
            lame_set_quality (gf, 2);
            if (lame_init_params (gf) < 0) { lame_close (gf); return "MP3 ENCODER COULDN'T START"; }
            HeapBlock<unsigned char> mp3 ((size_t) (chunk * 5 / 4 + 7200));
            for (juce::int64 pos = 0; pos < total; pos += chunk)
            {
                const int n = (int) jmin<juce::int64> (chunk, total - pos);
                reader->read (&buf, 0, n, pos, true, nch > 1);
                const int bytes = lame_encode_buffer_ieee_float (gf, buf.getReadPointer (0), buf.getReadPointer (nch > 1 ? 1 : 0), n,
                                                                 mp3.getData(), chunk * 5 / 4 + 7200);
                if (bytes < 0) { err = "MP3 ENCODING FAILED"; break; }
                out->write (mp3.getData(), (size_t) bytes);
                if (progress != nullptr) *progress = (float) ((double) (pos + n) / (double) total);
            }
            if (err.isEmpty())
            {
                const int bytes = lame_encode_flush (gf, mp3.getData(), chunk * 5 / 4 + 7200);
                if (bytes > 0) out->write (mp3.getData(), (size_t) bytes);
            }
            lame_close (gf);
            out->flush();
            if (out->getStatus().failed()) err = "CAN'T WRITE " + dest.getFileName().toUpperCase();
        }
        else
        {
            std::unique_ptr<OutputStream> os (out.release());
            std::unique_ptr<AudioFormatWriter> w;
            auto opts = AudioFormatWriterOptions().withSampleRate (reader->sampleRate).withNumChannels (nch).withBitsPerSample (24);
            if (format == Format::flac) { FlacAudioFormat flac; w = flac.createWriterFor (os, opts); }
            else                        { w = wav.createWriterFor (os, opts); }
            if (w == nullptr) return "CAN'T WRITE " + dest.getFileName().toUpperCase();
            for (juce::int64 pos = 0; pos < total; pos += chunk)
            {
                const int n = (int) jmin<juce::int64> (chunk, total - pos);
                reader->read (&buf, 0, n, pos, true, nch > 1);
                if (! w->writeFromAudioSampleBuffer (buf, 0, n)) { err = "CAN'T WRITE " + dest.getFileName().toUpperCase(); break; }
                if (progress != nullptr) *progress = (float) ((double) (pos + n) / (double) total);
            }
        }
    }
    if (err.isNotEmpty()) { tmp.deleteFile(); return err; }
    dest.deleteFile();
    if (! tmp.moveFileTo (dest)) return "CAN'T WRITE " + dest.getFileName().toUpperCase();
    if (progress != nullptr) *progress = 1.0f;
    return {};
}

// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#include "AmigaSamples.h"

namespace ft::amiga
{

namespace
{
    uint32_t be32 (const uint8_t* p) noexcept { return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3]; }
    uint16_t be16 (const uint8_t* p) noexcept { return (uint16_t) (p[0] << 8 | p[1]); }

    /** Fibonacci-delta decoding (8SVX sCompression = 1). The first byte is
        padding, the second the start value, then two 4-bit codes per byte. */
    std::vector<int8_t> fibonacciDecode (const uint8_t* src, size_t n)
    {
        static const int8_t table[16] = { -34, -21, -13, -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8, 13, 21 };
        std::vector<int8_t> out;
        if (n < 2) return out;
        out.reserve ((n - 2) * 2);
        int8_t x = (int8_t) src[1];
        for (size_t i = 2; i < n; ++i)
        {
            x = (int8_t) (x + table[src[i] >> 4]);   out.push_back (x);
            x = (int8_t) (x + table[src[i] & 15]);   out.push_back (x);
        }
        return out;
    }
}

bool isRawExtension (const juce::File& f)
{
    return f.getFileExtension().isEmpty() || f.hasFileExtension ("raw;smp;sam;snd;8sv;sample");
}

bool is8SVX (const juce::File& f)
{
    juce::FileInputStream in (f);
    if (! in.openedOk()) return false;
    char head[12] = {};
    if (in.read (head, 12) != 12) return false;
    return memcmp (head, "FORM", 4) == 0 && memcmp (head + 8, "8SVX", 4) == 0;
}

juce::String read8SVX (const juce::MemoryBlock& mb, Decoded& out)
{
    const auto* bytes = static_cast<const uint8_t*> (mb.getData());
    const size_t size = mb.getSize();
    if (size < 12 || memcmp (bytes, "FORM", 4) != 0 || memcmp (bytes + 8, "8SVX", 4) != 0)
        return "not an IFF 8SVX file";

    uint32_t oneShot = 0, repeat = 0, volumeFixed = 0x10000;
    uint16_t rate = 0;
    uint8_t compression = 0;
    uint32_t chan = 2;                    // 2 = left/mono, 4 = right, 6 = stereo
    bool haveVhdr = false;
    const uint8_t* body = nullptr;
    size_t bodyLen = 0;

    const size_t formEnd = juce::jmin (size, (size_t) be32 (bytes + 4) + 8);
    size_t pos = 12;
    while (pos + 8 <= formEnd)
    {
        const uint8_t* id = bytes + pos;
        const size_t len = be32 (bytes + pos + 4);
        const size_t dataPos = pos + 8;
        const size_t avail = dataPos <= formEnd ? juce::jmin (len, formEnd - dataPos) : 0;
        const uint8_t* d = bytes + dataPos;

        if (memcmp (id, "VHDR", 4) == 0 && avail >= 20)
        {
            oneShot = be32 (d);
            repeat = be32 (d + 4);
            rate = be16 (d + 12);
            compression = d[15];
            volumeFixed = be32 (d + 16);
            haveVhdr = true;
        }
        else if (memcmp (id, "BODY", 4) == 0)
        {
            body = d;
            bodyLen = avail;
        }
        else if (memcmp (id, "NAME", 4) == 0 && avail > 0)
        {
            out.name = juce::String::fromUTF8 ((const char*) d, (int) strnlen ((const char*) d, avail)).trim();
        }
        else if (memcmp (id, "CHAN", 4) == 0 && avail >= 4)
        {
            chan = be32 (d);
        }

        pos = dataPos + len + (len & 1);     // chunks are padded to an even length
    }

    if (! haveVhdr) return "8SVX file has no VHDR chunk";
    if (body == nullptr || bodyLen == 0) return "8SVX file has no sample data";
    if (compression > 1) return "8SVX compression type " + juce::String (compression) + " is not supported";

    const int numChannels = chan == 6 ? 2 : 1;

    // Split the body into channels (stereo bodies hold all left data, then all right),
    // decompressing each one separately when needed.
    std::vector<std::vector<int8_t>> chans ((size_t) numChannels);
    const size_t part = bodyLen / (size_t) numChannels;
    for (int c = 0; c < numChannels; ++c)
    {
        const uint8_t* src = body + (size_t) c * part;
        if (compression == 1) chans[(size_t) c] = fibonacciDecode (src, part);
        else                  chans[(size_t) c].assign ((const int8_t*) src, (const int8_t*) src + part);
    }

    size_t frames = chans[0].size();
    // Multi-octave instruments store several octaves one after another; keep the first.
    if (oneShot + repeat > 0 && oneShot + repeat <= frames)
        frames = oneShot + repeat;
    if (frames == 0) return "8SVX file has no sample data";

    SampleData::Ptr data = new SampleData();
    data->sampleRate = rate > 0 ? (double) rate : 8363.0;
    data->bitDepth = 8;
    data->buffer.setSize (numChannels, (int) frames);
    for (int c = 0; c < numChannels; ++c)
    {
        auto* w = data->buffer.getWritePointer (c);
        const auto& src = chans[(size_t) c];
        for (size_t i = 0; i < frames; ++i)
            w[i] = i < src.size() ? (float) src[i] / 128.0f : 0.0f;
    }

    out.data = data;
    // The VHDR volume is written inconsistently by Amiga tools (a 16.16 fixed value,
    // a plain 0..64, or garbage), and reading it wrongly gives a silent instrument.
    // ProTracker ignores it as well, so the sample always starts at full volume.
    juce::ignoreUnused (volumeFixed);
    out.volume = 64;
    out.loop = repeat > 2 && oneShot < frames;           // a 2-byte repeat is the classic "no loop"
    if (out.loop)
    {
        out.loopStart = (int) oneShot;
        out.loopEnd = (int) juce::jmin (frames - 1, (size_t) (oneShot + repeat - 1));
    }
    return {};
}

juce::String readRaw8 (const juce::MemoryBlock& mb, Decoded& out)
{
    const size_t n = mb.getSize();
    if (n == 0) return "the file is empty";
    if (n > (size_t) (kRawSampleRate * 120)) return "the file is too large to be a raw 8-bit sample";

    SampleData::Ptr data = new SampleData();
    data->sampleRate = kRawSampleRate;
    data->bitDepth = 8;
    data->buffer.setSize (1, (int) n);
    const auto* src = static_cast<const int8_t*> (mb.getData());
    auto* w = data->buffer.getWritePointer (0);
    for (size_t i = 0; i < n; ++i) w[i] = (float) src[i] / 128.0f;

    out.data = data;
    return {};
}

} // namespace ft::amiga

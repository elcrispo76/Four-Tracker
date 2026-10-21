// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include <juce_core/juce_core.h>
#include "Instrument.h"

namespace ft
{
    /** Amiga sample formats used by ProTracker, OctaMED, MED and friends. */
    namespace amiga
    {
        /** Sample rate used for raw samples: ProTracker's C-3 on a PAL Amiga (period 214). */
        constexpr double kRawSampleRate = 16574.0;

        /** File extensions treated as raw 8-bit signed samples (plus files with no extension). */
        bool isRawExtension (const juce::File& f);

        /** True if the file starts with an IFF FORM ... 8SVX header. */
        bool is8SVX (const juce::File& f);

        /** Result of decoding an Amiga sample. */
        struct Decoded
        {
            SampleData::Ptr data;
            juce::String name;          // from the NAME chunk, if any
            int  volume = 64;           // 0..64, from the VHDR volume
            bool loop = false;
            int  loopStart = 0, loopEnd = 0;   // inclusive frames
        };

        /** Decodes an IFF 8SVX file (uncompressed or Fibonacci-delta, mono or stereo).
            Returns an empty string on success, otherwise an error message. */
        juce::String read8SVX (const juce::MemoryBlock& file, Decoded& out);

        /** Treats the bytes as raw signed 8-bit mono PCM at kRawSampleRate. */
        juce::String readRaw8 (const juce::MemoryBlock& file, Decoded& out);
    }
}

// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include "Instrument.h"
#include "TrackerEngine.h"

namespace ft
{
    /** Fills the instrument slots with small synthesized sounds named like
        the mockups (KICK.WAV, SNARE.WAV ...) so the tracker makes sound on
        first launch, before any samples are loaded. */
    void createDemoInstruments (std::array<Instrument, kNumInstruments>& instruments);

    /** Writes four demo patterns (00..03) that show off live switching. */
    void createDemoPatterns (TrackerEngine& engine);
}

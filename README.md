# Four Tracker

**A free 4-channel sample tracker for Windows that you play live: switch patterns from a MIDI keyboard while it plays.**

**Website and download:** https://elcrispo76.github.io/Four-Tracker/
**Releases (installer):** https://github.com/elcrispo76/Four-Tracker/releases

> **Status: beta.** Four Tracker is written with the help of an AI coding assistant (Claude) and checked by an
> automated test program (`source/Tests`), but only a few people have used it so far. Expect bugs, save your
> modules often, and use it at your own risk. Road to 1.0: alpha → **beta (now)** → release candidate → 1.0 gold.
> Please report bugs under [Issues](https://github.com/elcrispo76/Four-Tracker/issues).

## Features

- **Live pattern switching.** C-2 on your MIDI keyboard starts and stops; C#2 and up pick patterns 00, 01, 02…
  Switching is legato by default: the new pattern carries on from the same row.
- **FastTracker 2 / ProTracker effect codes:** arpeggio, portamento, vibrato, tremolo, volume slides, retrigger,
  pattern break and jump, Exx commands and more.
- **Samples:** WAV, AIFF, FLAC, Ogg, MP3 and Amiga IFF 8SVX / raw 8-bit. A sample browser plays each file as
  you select it.
- **Built-in synth:** sine, triangle, saw, square and noise, with ADSR and a resonant low-pass filter.
- **Sample editor:** loops (forward, ping-pong, backward), fades, normalize, cut / copy / paste, stereo view.
- **Modules (.ftm)** hold patterns and samples in one file.
- **Standalone app** (Windows Audio, DirectSound, ASIO when built with the ASIO SDK) and **VST3 plugin**,
  64-bit and 32-bit. Two instances in a DAW give you 8 channels.

## Repository layout

| Path | Contents |
|---|---|
| `index.html`, `images/`, `Four-Tracker-User-Manual.pdf` | The website (GitHub Pages) |
| `source/` | The program's source code, build scripts and installer script |
| `source/BUILDING-AND-USAGE.md` | Keys, effect list, file formats, building and making the installer |
| `CREDITS.md` | Third-party software and licences |
| `CLONE-CHECK.md` | Check of the source against six open-source trackers for copied code |
| `LICENSE` | GNU AGPLv3 |

## Building

Windows, Visual Studio 2022 or newer with "Desktop development with C++" and the Windows 11 SDK.
Run `source\build-windows.bat`: it fetches JUCE 8 and builds 64-bit and 32-bit versions into `source\dist`.
Optional SDKs go in `source\SDKs` (they are never committed, see `.gitignore`). Details are in
`source/BUILDING-AND-USAGE.md`.

## License

Four Tracker is free software: you can redistribute it and/or modify it under the terms of the
GNU Affero General Public License as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version. It is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY. See `LICENSE`.

Third-party components keep their own licences, see `CREDITS.md`.

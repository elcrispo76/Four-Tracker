# Four Tracker

**A free sample tracker for Windows and Mac (1 to 16 tracks) that you play live: switch patterns from a MIDI keyboard while it plays.**

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
- **1 to 16 tracks:** add or remove one track at a time with - TRACKS +. The pattern shows 4 at a time; the
  track bar under the pattern (◀ 1-4 5-8 9-12 13-16 ▶) or Tab moves between them.
- **Built-in synth:** sine, triangle, saw, square and noise, with ADSR, a resonant low-pass filter, distortion
  and accent. Pattern commands 0Xvv / 0Zvv / 0Yvv / 0Vvv change cutoff, resonance, distortion and accent live.
- **MIDI instruments:** an instrument slot can send MIDI notes (channel 1-16, optional program change) to a
  hardware or software synth.
- **Sample editor:** loops (forward, ping-pong, backward), fades, normalize, cut / copy / paste, OctaMED-style
  MIX of two samples, Ctrl+A / C / X / V, stereo view.
- **SPACING (0-32):** how many rows the cursor moves after a note and with the up / down arrows.
- **VST3 effects** in the standalone app: two per track, with bypass, and chaining (send a track on into another
  track's effects, so one reverb or delay can be shared).
- **Modules (.ftm)** hold patterns and samples in one file.
- **Windows:** standalone app (Windows Audio, DirectSound, ASIO) and **VST3 plugin**, 64-bit and 32-bit.
- **Mac:** standalone app and **Audio Unit** instrument, Apple Silicon + Intel, with Audio Unit effects.

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
`source\build-windows-fx-share.bat` builds the version with VST3 effect hosting into `source\dist-fx-share`
(this is the one in the releases). It never uses the VST2 SDK.

Mac: Xcode command line tools and CMake, then `bash source/build-mac.sh`. Without a Mac, run the **Mac build**
workflow under Actions: it makes the .dmg on GitHub's Macs (and adds it to a release when one is published).
The ASIO SDK (GPLv3) is in `source\SDKs\asiosdk`. The VST2 SDK, if you have it, also goes in `source\SDKs` but is
never committed (see `.gitignore`). Details are in
`source/BUILDING-AND-USAGE.md`.

## License

Four Tracker is free software: you can redistribute it and/or modify it under the terms of the
GNU Affero General Public License as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version. It is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY. See `LICENSE`.

Third-party components keep their own licences, see `CREDITS.md`.

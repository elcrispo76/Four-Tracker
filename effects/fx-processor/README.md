# FX Processor

**A free multi-effect: Filter, PW gate, Chorus, LFO, Delay and Reverb in one plugin. VST3 for Windows (64-bit and
32-bit), VST3 and Audio Unit for Mac.** From the maker of [Four Tracker](https://elcrispo76.github.io/Four-Tracker/),
in the same look as Four Tracker and the Breakbeat Processor.

The sound goes **FILTER → PW → CHORUS → DELAY → REVERB → OUTPUT**. Each effect has its own **ON / OFF** button
(when everything is off, the sound passes through untouched). The **LFO** moves the filter and the PW gate.

| Effect | Knobs |
|---|---|
| **FILTER** | **MODE** (low-pass, band-pass, high-pass), **CUTOFF** (20 Hz - 20 kHz), **RESO** |
| **PW** | A pulse-width gate in time with the song (trance gate): **RATE** (1/4, 1/8, 1/16, 1/32, 1/8T, 1/16T), **WIDTH** (how much of each step the sound is on, 5-95 %), **DEPTH** (how far it goes down in between) |
| **LFO** | Top row, the LFO itself: **SHAPE** (sine, triangle, saw, square, sample & hold), **SPEED** (one wave lasts 1/16 to 4 bars, or FREE), **FREE HZ** (0.05-20 Hz, only when SPEED is FREE). Under **THE LFO MOVES:** how much it moves the **CUTOFF** of the FILTER (up to ±4 octaves) and the **PW WIDTH** of the PW gate |
| **CHORUS** | **RATE**, **DEPTH**, **MIX** (left and right a quarter turn apart: wide stereo) |
| **DELAY** | **TIME** in time with the song (1/16, 1/8, 1/8 D, 1/4, 1/4 D, 1/2, 1 bar), **FEEDBACK**, **TONE** (low-pass on the repeats), **MIX**, **PING-PONG** |
| **REVERB** | **SIZE**, **DAMP**, **WIDTH**, **MIX** |
| **MASTER** | **PRESETS** (13 ready-made settings, also the plugin's programs), **OUTPUT** (-24 to +6 dB, with a soft limiter), **TEMPO** |

- The PW gate, the LFO and the delay follow the tempo and song position of your DAW or of Four Tracker 0.5.0 and newer.
  When the host sends no tempo, the display says **OWN TEMPO** and the plugin runs at **TEMPO** (40-300 BPM).
- Turn a knob by dragging up or down (Shift = fine), the mouse wheel, or double-click for the default. Two-way knobs
  (PING-PONG) switch with a click. Every knob can be automated, and everything is saved with the song.
- The lights next to PW and LFO show the gate and the LFO moving.

## Install

- **Windows:** run `FxProcessor-Setup-1.0.0.exe`. It asks where to put the VST3 (standard: `C:\Program Files\Common Files\VST3`).
  Rescan the plugins in your DAW (in Four Tracker: FX, SCAN). Windows may say "Windows protected your PC" because the
  installer isn't signed: click **More info**, then **Run anyway**.
- **Mac:** open the .dmg and copy `FX Processor.vst3` to `/Library/Audio/Plug-Ins/VST3` and/or
  `FX Processor.component` to `/Library/Audio/Plug-Ins/Components`.

## Building

- **Windows:** Visual Studio 2022 or 2026 with "Desktop development with C++". Put JUCE 8.0.10 as a folder `JUCE`
  here or one folder up, then run `build-windows.bat`: it builds 64-bit and 32-bit into `dist`. `build-installer.bat`
  (Inno Setup 6.3+) makes `dist\FxProcessor-Setup-1.0.0.exe`.
- **VST2, for your own use only:** put the VST2 SDK anywhere under `SDKs` and run `build-windows-vst2.bat`
  (`dist-vst2\32-bit\FX Processor.dll` for 32-bit hosts like MadTracker). Never share a VST2 build: the VST2 SDK
  licence does not allow new VST2 plugins to be distributed. The installer is VST3 only.
- **Mac:** `bash build-mac.sh` → `dist-mac/FxProcessor-1.0.0-mac.dmg`.
- **Tests (Linux):** `cmake -S . -B build -DFXPROC_TESTS=ON && cmake --build build`, then run `FxTests`.

| Path | Contents |
|---|---|
| `Source/FxEngine.h` | The sound: filter, PW gate, LFO, chorus, delay, reverb |
| `Source/PluginProcessor.*` | Parameters, host tempo, presets, saving |
| `Source/PluginEditor.*`, `Source/PixelUI.h` | The window and its knobs and buttons |
| `Source/PixelFont.h` | The Amiga-style pixel font from Four Tracker |
| `Tests/FxTests.cpp` | Automated tests |

## Licence

GNU AGPLv3 or later, like Four Tracker. Built with [JUCE](https://juce.com) 8 (AGPLv3) and the Steinberg VST3 SDK (MIT).
The reverb is JUCE's `juce::Reverb` (based on Freeverb). VST is a registered trademark of Steinberg Media Technologies GmbH.

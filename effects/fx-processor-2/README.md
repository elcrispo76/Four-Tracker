# FX Processor 2

**The follow-up to FX Processor: a Phaser, a Flanger and a Ring Modulator in one free plugin. VST3 for Windows
(64-bit and 32-bit), VST3 and Audio Unit for Mac.** From the maker of [Four Tracker](https://elcrispo76.github.io/Four-Tracker/),
in the same look as Four Tracker, FX Processor and the Breakbeat Processor.

The sound goes **PHASER → FLANGER → RING MOD → OUTPUT**. Each effect has its own **ON / OFF** button (when everything
is off, the sound passes through untouched) and its **own LFO**, with a light that shows it moving.

| Effect | Knobs |
|---|---|
| **PHASER** | **SPEED** (one sweep lasts 1/16 to 4 bars, or FREE), **FREE HZ** (0.05-20 Hz, only when SPEED is FREE), **DEPTH** (how far the notches sweep, up to 5 octaves), **CENTER** (where the sweep sits, 200 Hz - 3 kHz), **FEEDBACK** (stronger, ringing notches), **STAGES** (4, 6, 8 or 12), **MIX** |
| **FLANGER** | **SPEED**, **FREE HZ**, **DEPTH**, **DELAY** (0.3 - 10 ms: short = jet, long = chorus), **FEEDBACK** (-95 to +95 %: plus = jet whoosh, minus = hollow metal), **MIX** |
| **RING MOD** | **FREQ** (carrier 10 Hz - 4 kHz: low = tremolo / robot, high = bells and metal), **WAVE** (sine, triangle, square), **MIX**; its LFO sweeps FREQ: **LFO SPEED**, **FREE HZ**, **LFO DEPTH** (up to 2 octaves each way, 0 = off) |
| **MASTER** | **PRESETS** (14 ready-made settings, also the plugin's programs), **OUTPUT** (-24 to +6 dB, with a soft limiter), **TEMPO** |

- The LFOs follow the tempo and song position of your DAW or of Four Tracker 0.5.0 and newer. When the host sends no
  tempo, the display says **OWN TEMPO** and the plugin runs at **TEMPO** (40-300 BPM).
- The phaser's and flanger's left and right LFOs are a quarter turn apart, for wide stereo.
- Turn a knob by dragging up or down (Shift = fine), the mouse wheel, or double-click for the default. Every knob can
  be automated, and everything is saved with the song.

## Install

- **Windows:** run `FxProcessor2-Setup-1.0.0.exe`. It asks where to put the VST3 (standard: `C:\Program Files\Common Files\VST3`).
  Rescan the plugins in your DAW (in Four Tracker: FX, SCAN). Windows may say "Windows protected your PC" because the
  installer isn't signed: click **More info**, then **Run anyway**.
- **Mac:** open the .dmg and copy `FX Processor 2.vst3` to `/Library/Audio/Plug-Ins/VST3` and/or
  `FX Processor 2.component` to `/Library/Audio/Plug-Ins/Components`.

## Building

- **Windows:** Visual Studio 2022 or 2026 with "Desktop development with C++". Put JUCE 8.0.10 as a folder `JUCE`
  here or one folder up, then run `build-windows.bat`: it builds 64-bit and 32-bit into `dist`. `build-installer.bat`
  (Inno Setup 6.3+) makes `dist\FxProcessor2-Setup-1.0.0.exe`.
- **VST2, for your own use only:** put the VST2 SDK anywhere under `SDKs` and run `build-windows-vst2.bat`
  (`dist-vst2\32-bit\FX Processor 2.dll` for 32-bit hosts like MadTracker). Never share a VST2 build: the VST2 SDK
  licence does not allow new VST2 plugins to be distributed. The installer is VST3 only.
- **Mac:** `bash build-mac.sh` → `dist-mac/FxProcessor2-1.0.0-mac.dmg`.
- **Tests (Linux):** `cmake -S . -B build -DFXPROC2_TESTS=ON && cmake --build build`, then run `Fx2Tests`.

| Path | Contents |
|---|---|
| `Source/FxEngine.h` | The sound: phaser, flanger, ring modulator and their LFOs |
| `Source/PluginProcessor.*` | Parameters, host tempo, presets, saving |
| `Source/PluginEditor.*`, `Source/PixelUI.h` | The window and its knobs and buttons |
| `Source/PixelFont.h` | The Amiga-style pixel font from Four Tracker |
| `Tests/Fx2Tests.cpp` | Automated tests |

## Licence

GNU AGPLv3 or later, like Four Tracker. Built with [JUCE](https://juce.com) 8 (AGPLv3) and the Steinberg VST3 SDK (MIT). VST is a registered trademark of Steinberg Media Technologies GmbH.

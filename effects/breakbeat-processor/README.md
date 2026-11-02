# Breakbeat Processor

**A free 8 step breakbeat slicer effect. VST3 for Windows (64-bit and 32-bit), VST3 and Audio Unit for Mac.**
From the maker of [Four Tracker](https://elcrispo76.github.io/Four-Tracker/).

Put it on a track with a break, a beat or a bassline. It records the track as it plays and, on each of its 8 steps,
retriggers a part of it, plays it backwards, cuts it or crushes it, always in time with the song: it follows the
tempo and song position of your DAW, or of Four Tracker.

## How it works

The plugin doesn't know your samples: it works on the sound that comes through the channel, whatever plays there
(one long break, short samples triggered again and again, a bassline ...). **SLICE** is how much of that sound is cut
into four quarters, **1/4 - 4/4**: **8, 16, 32 or 64 rows** in a tracker (**1/2, 1, 2 or 4 bars** in a DAW).
**STEPS** is how much of the track the 8 steps cover (8, 16, 32 or 64 rows, at most SLICE); they repeat over the
slice. With SLICE 64 and STEPS 16, a step is 2 rows, the steps run 4 times per slice, and 1/4 is always the first
16 rows of those 64. The number under each step shows the row it starts on.

It works like a tracker playing a sample: a play head runs through the slice, normally along with the song, so
you hear the channel as it is.

| Step | Does |
|---|---|
| (empty) | Nothing new: whatever is playing goes on |
| **1/4 2/4 3/4 4/4** | **Retrigger:** the play head jumps to the start of that quarter and plays on from there, through the empty steps after it, until the next retrigger or the next slice. Like putting the sample in again on that row (9xx offset). |
| **← (REVERSE)** | This step plays backwards what would have played |
| **■ (STOP)** | Silence for this step |
| **BIT** | Bit crusher on what is playing, for this step (**BITS** 1-16, **RATE** 1/1 - 1/32 sample-rate reduction) |

At the start of every slice the play head is back with the song. A part of the slice that hasn't been heard yet this
time round comes from the last time round (or, with READ AHEAD, from the same round).

- Pick a tool at the bottom (it turns black), then click or drag over the steps. Clicking a step that already has
  that tool empties it. **CLEAR** is the eraser; click it twice to empty all steps. **Right-click** a step for a menu.
- **RANDOM** makes a new break. **PATTERNS** has ready-made ones (AMEN SHUFFLE, CHOP, JUNGLE, ROLL, GATE ...); they
  are also the plugin's programs (presets).
- **SMOOTH** cross-fades 0-20 ms between steps so there are no clicks. **MIX** blends dry and processed.
- The red light over a step shows where it is. The steps run only while the song plays; stopped, the sound passes
  straight through. Every step and knob can be automated, and everything is saved with the song.

### SYNC: SONG or SOUND (and TEMPO)

- **SONG** (default, for DAWs): the steps follow the host's song position. If the host only updates its position in
  coarse steps (some trackers do), the plugin keeps its own smooth clock and lets it run a little faster or slower to
  stay with the host, so there is no crunch.
- **SOUND** (for trackers such as MadTracker): the steps start with the first sound on the channel after a silence,
  so row 0 is the first note, at the host's tempo, and stop again after half a second of silence or when the host stops. Click the knob to switch.
- When the host sends no tempo at all (some simple hosts, Four Tracker before 0.5.0), the display says **OWN** and
  the plugin works as SOUND at **TEMPO** (40-300 BPM, default 125). Set TEMPO to the song's tempo.

### READ AHEAD

- **Off** (default): no delay. A step that plays a part of the track that hasn't been heard yet this time round takes
  it from the last time round, which for a looped break, beat or bassline is the same. The first time round, those
  parts are silent. Use this in Four Tracker and in hosts without plugin delay compensation.
- **On**: the plugin reads the whole SLICE of the track before it is heard. It tells the host it has a latency of
  one SLICE (1/2 to 4 bars), and a DAW with plugin delay compensation (Ableton Live, Reaper, Cubase, FL Studio,
  Bitwig, Logic ...) lines the rest of the song up with it. Every step then plays from the same time round of the
  track, from the very first bar. Not for live playing (what you play is heard one SLICE late).

### In Four Tracker

Load it on a track's FX1 or FX2 in the FX version of Four Tracker (FX / F8, SCAN). It follows Four Tracker's tempo
(4 rows to a beat: SPEED 6 at 125 = 125 BPM, SPEED 3 = 250 BPM) and the pattern rows: step 1 is row 00, and each time
a pattern starts from the top the steps start again. Four Tracker 0.5.0 or newer is needed for this.

## Install

- **Windows:** run `BreakbeatProcessor-Setup-1.0.0.exe`. It asks where to put the VST3 (standard: `C:\Program Files\Common Files\VST3`).
  Rescan the plugins in your DAW. Windows may say "Windows protected your PC" because the installer isn't signed:
  click **More info**, then **Run anyway**.
- **Mac:** open the .dmg and copy `Breakbeat Processor.vst3` to `/Library/Audio/Plug-Ins/VST3` and/or
  `Breakbeat Processor.component` to `/Library/Audio/Plug-Ins/Components`.

## Building

- **Windows:** Visual Studio 2022 or 2026 with "Desktop development with C++". Double-click `build-windows.bat`: it
  needs the JUCE folder (copy it from your Four Tracker folder, or unpack JUCE 8.0.10 as `JUCE`), builds 64-bit and 32-bit into `dist`, and makes
  `dist\BreakbeatProcessor-Setup-1.0.0.exe` when Inno Setup 6.3+ is installed (https://jrsoftware.org/isdl.php).
- **VST2, for your own use only:** put the VST2 SDK anywhere under `SDKs` and run `build-windows-vst2.bat`. It makes
  `dist-vst2\32-bit\Breakbeat Processor.dll` (for 32-bit hosts like MadTracker) and a 64-bit one. Never share a VST2
  build: the VST2 SDK licence does not allow new VST2 plugins to be distributed. The installer is VST3 only.
- **Mac:** Xcode command line tools and CMake, then `bash build-mac.sh` → `dist-mac/BreakbeatProcessor-1.0.0-mac.dmg`.
- **Tests (Linux):** `cmake -S . -B build -DBREAKBEAT_TESTS=ON && cmake --build build`, then run `BreakbeatTests`.

| Path | Contents |
|---|---|
| `Source/BreakEngine.h` | The sound engine (recording, steps, read ahead, cross-fades, bit crusher) |
| `Source/PluginProcessor.*` | The plugin: parameters, host tempo and position, patterns, saving |
| `Source/PluginEditor.*` | The window |
| `Source/PixelFont.h` | The Amiga-style pixel font from Four Tracker |
| `Tests/BreakbeatTests.cpp` | Automated tests |

## Licence

GNU AGPLv3 or later, like Four Tracker. Built with [JUCE](https://juce.com) 8 (AGPLv3) and the Steinberg VST3 SDK (MIT).
VST is a registered trademark of Steinberg Media Technologies GmbH.

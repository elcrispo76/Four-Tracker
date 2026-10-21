# Four Tracker

A 4-channel sample tracker built for live use. You switch patterns from a MIDI keyboard while it plays.
It runs as a standalone Windows app (with ASIO) and as a VST3 / VST2 plugin, in 64-bit and 32-bit.
The code is C++ with JUCE 8.

> **Status: beta (0.x).** Written with the help of an AI coding assistant (Claude) and checked by the automated
> test program in `Tests/`, but only a few people have used it so far. Expect bugs, save often, use at your own risk.
> Road to 1.0: alpha → **beta (now)** → release candidate (fixes only) → 1.0 gold. Report bugs on GitHub Issues.

## Live MIDI control

| Key (default) | Does |
|---|---|
| **C-2** (MIDI 36) | Start / stop |
| **C#2** | Pattern 00 |
| **D-2** | Pattern 01 |
| **D#2** | Pattern 02 |
| ... | ... one key per pattern, up to pattern 3F (63) |

- When stopped, a pattern key selects the pattern that C-2 will start. Pattern 00 is selected by default.
- When playing, a pattern key switches pattern **without restarting**. Playback carries on from the next row at
  the same position, and notes that are already ringing keep ringing.
- The on-screen START / STOP (Song / Pattern page), Enter, and the pattern PREV / NEXT buttons do the same as the keys.
- In the **MIDI / AUDIO** page you can:
  - move the start/stop key (LEARN, then press a key),
  - pick a MIDI channel (or OMNI),
  - choose how a switch takes over:
    - **Next row (legato)**: the default
    - **Next beat (legato)**: on the next 4-row boundary
    - **End of pattern**
    - **Restart at row 0**
- MIDI Start / Stop messages (from a clock or sequencer) also start and stop playback. You can turn this off.

## Audio settings

- **Standalone app:** the MIDI / AUDIO page shows the driver type (ASIO, WASAPI, DirectSound), the device, the
  sample rate, the buffer size and which MIDI inputs are enabled. These settings are saved between sessions.
- **Plugin:** the host owns the audio driver. Route your MIDI keyboard to the plugin's track. You can turn on
  FOLLOW HOST TEMPO to use the DAW's BPM instead of the tracker's.

## Keyboard (computer)

| Key | Action |
|---|---|
| Enter | Start / stop pattern |
| Esc | Stop |
| Space, or the red **EDIT** button | Edit mode on/off. The button is lit red while editing. When off (the default), note keys only play the selected instrument and nothing is written into the pattern. |
| Arrows, Tab / Shift+Tab, PgUp/PgDn, Home/End | Move the cursor |
| Z S X D C V G B H N J M , L . | Notes (lower octave) |
| Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P | Notes (upper octave) |
| (holding a note key) | Plays the sample once: holding does not retrigger it. A sample without a loop plays to its end even if you let go; a looped sample keeps looping while held and stops when you let go. |
| 1 or ` | Note off (`===`) in the note column |
| 0-9, A-F | Instrument / effect digits |
| Delete / Insert / Backspace | Clear field / insert row / delete row |
| F1 / F2, or the OCTAVE box | Octave down / up (0–7, default 4; octave 2 makes Z write C-2) |
| F6 / F7 | Edit step down / up |
| Numpad + / - | Next / previous instrument |
| Shift + arrows, or mouse drag | Mark a selection |
| Ctrl+C / Ctrl+X / Ctrl+V | Copy / cut / paste the marked selection |
| Ctrl+F3 / F4 / F5 | Cut / copy / paste the block: the whole pattern, all 4 channels and all rows (64 rows = 4 bars) |
| Shift+F3 / F4 / F5 | Cut / copy / paste track (one whole channel) |
| Ctrl+Z / Ctrl+Y | Undo / redo pattern edits |
| F9 / F10 / F11 / F12 | Tracks / Instruments / Sample Edit / MIDI-Audio page |

Mouse tricks:
- Drag value boxes up/down. Hold Shift for fine steps. Use the mouse wheel, or double-click to type a value.
- Click a CH label to mute that channel.
- Drop audio files on the window to load them into the selected slot (several files fill consecutive slots).
- Drop a `.ftm` module (or an old `.ftps` pattern set) to load it.

## Pattern format

Each cell reads `NOTE INS CCPP`, for example `C-4 01 0C40`. `CC` is the effect number and `PP` is its parameter.
C-4 plays a sample at its own sample rate.

The effect numbers follow **FastTracker 2**. 00-0F are the classic ProTracker/MOD commands that OctaMED also uses.

| CC | Effect | CC | Effect |
|---|---|---|---|
| 00 xy | Arpeggio | 0E 1x / 2x | Fine porta up / down |
| 01 xx | Portamento up | 0E 4x / 7x | Vibrato / tremolo waveform |
| 02 xx | Portamento down | 0E 5x | Set finetune |
| 03 xx | Tone portamento | 0E 6x | Pattern loop |
| 04 xy | Vibrato | 0E 8x | Set panning (coarse) |
| 05 xy | Tone porta + volume slide | 0E 9x | Retrigger every x ticks |
| 06 xy | Vibrato + volume slide | 0E Ax / Bx | Fine volume up / down |
| 07 xy | Tremolo | 0E Cx | Note cut at tick x |
| 08 xx | Set panning (00 left, 80 centre, FF right) | 0E Dx | Note delay x ticks |
| 09 xx | Sample offset (xx * 256) | 0E Ex | Pattern delay x rows |
| 0A xy | Volume slide | 0F xx | Speed (01-1F) or BPM (20-FF) |
| 0B xx | Jump to pattern xx | 10 xx | Global volume (Gxx) |
| 0C xx | Set volume (00-40) | 11 xy | Global volume slide (Hxy) |
| 0D xy | Pattern break to row xy | 14 xx | Key off at tick xx (Kxx) |
| | | 19 xy | Panning slide (Pxy) |
| | | 1B xy | Multi retrigger (Rxy) |
| | | 1D xy | Tremor (Txy) |
| | | 21 1x / 2x | Extra fine porta (X1x / X2x) |

## Samples

- **Formats:** WAV, AIFF, FLAC, Ogg and MP3, plus whatever Windows Media can decode. Samples can be mono or stereo,
  up to 2 minutes long.
- **Amiga samples** (ProTracker / OctaMED disks): IFF 8SVX (`.iff`, `.8svx`; plain, Fibonacci-compressed and
  stereo, with loop and name, always at full volume like ProTracker) and raw signed 8-bit samples (`.raw`, `.smp`, `.sam`, `.snd` or no
  extension), played at 16574 Hz like ProTracker's C-3.
- **Sample browser:** LOAD (Instruments and Sample Edit pages) opens a browser inside the program. Click a file, or move
  through the list with the arrow keys, and it plays at once, like in MadTracker, so you can hear it before you load it.
  The right side shows the waveform, length, rate and bit depth. AUTO PLAY turns this off, PLAY / STOP (or Space) plays
  it again. Enter, double-click or LOAD puts the file into the selected slot (select several to fill the following
  slots); Esc or CANCEL closes it; Backspace goes up a folder. It remembers the folder while the program runs.
- **Stereo samples** are drawn as two waveforms, L on top and R below, in Sample Edit and in the browser.
- **Instruments page:** volume, transpose, fine tune, loop on/off, ping-pong and reverse playback.
  Reverse playback doesn't change the sample.
- **Sample Edit page:**
  - drag the S / E markers and the loop handles,
  - drag a selection, then SET LOOP,
  - PLAY LOOP to audition the loop,
  - cut / copy / paste, fade in / out, normalize and reverse (on the selection, or the whole sample if nothing is selected),
  - zoom and scroll. Ctrl+wheel also zooms.
- Instrument slots 01-0F come pre-filled with small synthesized sounds (KICK.WAV, SNARE.WAV ...), and patterns 00-03
  hold a demo. That way it makes sound straight away and you can try the live switching (C#2, D-2, D#2, E-2).
  Replace them with your own samples.

## Built-in synth

Any instrument slot can be a small synth instead of a sample. On the Instruments page set **SOURCE** to **SYNTH**:

- **Waveform:** SINE, TRIANGLE, SAW, SQUARE or NOISE. The wave is one looping cycle (like the chip sounds on
  Amiga modules), tuned so C-4 plays middle C. All effects work on it as on a sample.
- **Envelope:** ATTACK, DECAY and RELEASE in milliseconds, SUSTAIN as a level 0-64. A note holds at the sustain
  level until a note-off (`===`), a new note in the channel, or `ECx`; then it fades out over the release time.
  Sustain 0 gives plucked / percussive notes that die away by themselves.
- **Filter:** a resonant low-pass. CUTOFF 0-127 (127 = open), RESONANCE 0-127.
- Sliders: click or drag, mouse wheel for fine steps, double-click resets.
- Setting SOURCE back to SAMPLE clears the slot so you can load a sample.
- Synth settings are saved in modules and in DAW projects.

## Saving

- **Modules (.ftm):** SAVE MODULE writes one compressed file with everything in the song: all patterns,
  all samples, instrument settings, speed, BPM, loop and mixer. LOAD MODULE reads `.ftm` files and also
  old `.ftps` pattern sets (those hold patterns only, so the loaded samples are kept). You can also drop
  a `.ftm` file on the window.
- **NEW MODULE** asks: **Empty** (clears every pattern and sample), **Demo song** (loads the built-in demo
  again) or **Cancel**.
- **Standalone app:** always starts with the demo song. MIDI mapping, master volume and audio settings
  are remembered, but the song is not, so save your work as a module before closing.
- **Plugin:** the whole song is stored in your DAW project and comes back when you open it.
- **Demo folder:** the first time the editor opens, it creates `Documents\Four Tracker\Demo` with
  `Demo Song.ftm` and the demo sounds as WAV files in `Samples`. LOAD / SAVE start in `Documents\Four Tracker`.

## Building on Windows

1. Install Visual Studio 2022 (or newer) with **Desktop development with C++**, and tick the
   **Windows 11 SDK**. JUCE 8 needs it, also for 32-bit builds. Install Git as well.
2. Optional:
   - **ASIO:** download the ASIO SDK from Steinberg's developer site and unzip it so this file exists:
     `SDKs\asiosdk\common\iasiodrv.h`. Without it the app still runs on WASAPI / DirectSound.
   - **VST2:** Steinberg no longer distributes the VST2 SDK. If you have a copy, put it so this file exists:
     `SDKs\vst2sdk\pluginterfaces\vst2.x\aeffect.h`. Without it you get Standalone + VST3.
3. Double-click `build-windows.bat`, or run it from a command prompt. It fetches JUCE, builds 64-bit and 32-bit
   Release versions, and copies the results to:
   - `dist\64-bit\` and `dist\32-bit\`
   - `Four Tracker.exe`, `Four Tracker.vst3` and `Four Tracker.dll` (VST2)
4. Install the plugins:
   - **VST3:** copy `Four Tracker.vst3` to `C:\Program Files\Common Files\VST3` (64-bit) or
     `C:\Program Files (x86)\Common Files\VST3` (32-bit).
   - **VST2:** copy the `.dll` to your DAW's VST plugin folder (use the 32-bit one for 32-bit hosts).

Manual CMake build:

```
cmake -B build -A x64 -DASIO_SDK_DIR=C:/path/asiosdk -DVST2_SDK_DIR=C:/path/vst2sdk
cmake --build build --config Release
```

Use `-A Win32` for 32-bit. Add `-DFOURTRACKER_BUILD_TESTS=ON` to also build `FourTrackerTests`. That offline test
plays the patterns through MIDI, checks the switching and saves a WAV.

## Making an installer

1. Build the program with `build-windows.bat` (the `dist` folder must hold the program).
2. Install **Inno Setup 6.3 or newer** (free): https://jrsoftware.org/isdl.php
3. Double-click `build-installer.bat`. It writes `dist\FourTracker-Setup-0.2.0-share.exe (or -personal)`.

The installer (64-bit Windows) installs the program in Program Files with Start menu shortcuts and the
user manual (PDF), and offers the VST3 and VST2 plugins in 64-bit and 32-bit as optional parts. VST3 goes
to `Common Files\VST3`; for VST2 it asks which folder your music program scans. Only plugins that were
actually built are offered. Uninstall from Windows Settings → Apps. Change the version number in
`installer\FourTracker.iss` (`AppVersion`) for new releases.

## Source layout

| Path | Contents |
|---|---|
| `Source/TrackerEngine.*` | Playback, effects, timing, MIDI live control (audio thread) |
| `Source/Instrument.*` | Sample data, loop points, destructive sample edits |
| `Source/PluginProcessor.*` | Plugin wrapper, state save/load, pattern sets |
| `Source/DemoContent.*` | Built-in demo sounds and patterns |
| `Source/UI/*` | Pixel font, widgets, pattern view, pages (laid out on the 1536x1024 mockup grid and scaled) |

# Credits and third-party software

Four Tracker is written from scratch for this project by elcrispo76, with the help of an AI coding
assistant (Claude, by Anthropic). It does not contain code from ProTracker, FastTracker 2, OctaMED,
OpenMPT, MilkyTracker or other trackers. It follows the same publicly documented effect commands
(FastTracker 2 / ProTracker) and sample formats (WAV, AIFF, IFF 8SVX), so it behaves in a familiar way.

Some parts use well-known, published techniques that are written much the same way everywhere:
cubic (Hermite / Catmull-Rom) interpolation for resampling, the "TPT" state-variable low-pass filter
(Vadim Zavalishin, *The Art of VA Filter Design*), and the Fibonacci-delta table from the IFF 8SVX
specification (Electronic Arts, 1985).

The pixel font, icons and graphics are original to Four Tracker.

The Four Tracker Sample Pack (`source/installer/Sample Pack`) is synthesised from scratch for this
project (drums in code, bass and synth sounds with Four Tracker's own SYNTH 2). It contains no
third-party recordings and is released under CC0 1.0 (public domain).

## Libraries and SDKs

| Component | Used for | License |
|---|---|---|
| [JUCE](https://juce.com) 8 | Windows, audio and MIDI devices, plugin formats, file formats | JUCE licence (Starter) or AGPLv3 |
| Steinberg VST3 SDK (bundled with JUCE) | VST3 plugin | MIT |
| Steinberg ASIO SDK (in `source/SDKs/asiosdk`) | ASIO driver support in the released builds | GPLv3 (Steinberg's open-source ASIO licence, October 2025) |
| Steinberg VST2 SDK (optional, not in this repository) | VST2 plugin, personal builds only | Not redistributable |

VST is a registered trademark of Steinberg Media Technologies GmbH. ASIO is a trademark and software of
Steinberg Media Technologies GmbH.

Thanks to the tracker group for testing and suggestions.

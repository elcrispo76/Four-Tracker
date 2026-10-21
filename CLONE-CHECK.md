# Clone check: Four Tracker vs open-source trackers

**Date:** 21 October 2026, Four Tracker 0.2.0
**Question:** does Four Tracker's source contain code copied from other trackers?
**Short answer:** no copied code was found. There were two short matches, and both are standard data tables that every implementation shares (details below).

## What was compared

All 31 C++ source files of Four Tracker (`source/Source`, `source/Tests`, about 75,000 tokens) were compared
against every C/C++ file in these projects (latest version on GitHub on the date above):

| Project | Commit | Files scanned |
|---|---|---|
| [MilkyTracker](https://github.com/milkytracker/MilkyTracker) | e3ceb05 | 578 |
| [ft2-clone](https://github.com/8bitbubsy/ft2-clone) (FastTracker 2 clone) | f19f9e3 | 288 |
| [pt2-clone](https://github.com/8bitbubsy/pt2-clone) (ProTracker 2 clone) | 18954f2 | 345 |
| [Schism Tracker](https://github.com/schismtracker/schismtracker) | a982a37 | 302 |
| [libxmp](https://github.com/libxmp/libxmp) | 5d4f6aa | 1,350 |
| [OpenMPT](https://github.com/OpenMPT/openmpt) | dc49d56 | 3,869 |

## How

`source/tools/clonecheck.py` does token-based clone detection, the same method used by tools such as jscpd and
Moss. Comments, whitespace and formatting are ignored, and the code is compared as a stream of tokens. It runs
two passes:

- **Exact:** sequences of 20 or more identical tokens, with names and numbers kept.
- **Renamed:** sequences of 40 or more tokens where every name, number and string is replaced by a
  placeholder. This finds code that was copied and then had its variables renamed.

Anyone can repeat it:
`python3 clonecheck.py <four-tracker-source> <tracker1> <tracker2> ...`

## Results

**Exact matches: 2, both standard tables.**

1. `AmigaSamples.cpp`: the 16-value Fibonacci-delta table `{-34,-21,-13,-8,-5,-3,-2,-1,0,1,2,3,5,8,13,21}`.
   It also appears in MilkyTracker. The values come from the IFF 8SVX specification (Electronic Arts, 1985),
   and every 8SVX decoder must contain them.
2. `TrackerTypes.cpp`: the note-name table `{"C-","C#","D-",…,"B-"}`. It also appears in Schism Tracker.
   It is the 12 note names in tracker notation.

**Renamed matches: none of substance.** Every hit was a run of repetitive code with only 3 to 10 different tokens,
which matches any repetitive code. Examples:

- a list like `&a, &b, &c, …`
- a row of `x.y = z.getValue();` lines
- a list of `void copyX(); void cutX(); …` declarations
- a table of float numbers

The largest of these, the multi-retrigger volume switch for effect `Rxy`, has the same shape as the one in
ft2-clone. The reason is that FastTracker 2's documentation defines that table: 1 = −1, 2 = −2, 3 = −4,
… E = ×1.5, F = ×2. Four Tracker's version differs in its arithmetic (`v * 2 / 3` and `v * 3 / 2`, where
ft2-clone uses bit shifts) and in its layout.

## Limits

- This compares against the six largest open-source trackers. It cannot compare against closed-source
  programs (the original FastTracker 2, ProTracker or OctaMED binaries) or against every program ever written.
- It finds copied or near-copied code of roughly 20 to 40 tokens or more, which is about two to four lines. It
  cannot show where ideas came from. Four Tracker intentionally behaves like FT2 and ProTracker, because it
  follows their public effect documentation.
- Four Tracker was written with the help of an AI assistant (Claude). A check like this is how you verify that
  kind of code from the outside, because the AI cannot check its own output against what it was trained on.

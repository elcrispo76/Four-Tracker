#!/bin/bash
# ===========================================================================
#  Four Tracker - Mac build (Apple Silicon + Intel in one, macOS 11 or newer)
#
#  Makes: Four Tracker.app        (the program, with Audio Unit effects: 2 per track)
#         Four Tracker.component  (the Audio Unit instrument for Logic, Live, GarageBand ...)
#  and a disk image dist-mac/FourTracker-<version>-mac.dmg to share.
#
#  Needs: Xcode or just the command line tools  (run: xcode-select --install)
#         CMake                                 (https://cmake.org/download or: brew install cmake)
#
#  Run in Terminal from this folder:   bash build-mac.sh
#  Also install it on this Mac:        bash build-mac.sh install
# ===========================================================================
set -e
cd "$(dirname "$0")"

echo ""
echo " FOUR TRACKER - MAC BUILD"
echo " ========================"
echo ""

if [ ! -f CMakeLists.txt ]; then
    echo " [ERROR] CMakeLists.txt was not found next to this script. Unpack the zip first."
    exit 1
fi
if ! xcode-select -p >/dev/null 2>&1; then
    echo " [ERROR] The Xcode command line tools are missing. Run:  xcode-select --install"
    exit 1
fi
CMAKE="$(command -v cmake || true)"
[ -z "$CMAKE" ] && [ -x "/Applications/CMake.app/Contents/bin/cmake" ] && CMAKE="/Applications/CMake.app/Contents/bin/cmake"
if [ -z "$CMAKE" ]; then
    echo " [ERROR] CMake was not found. Get it from https://cmake.org/download (or: brew install cmake)"
    exit 1
fi

# ---- JUCE 8.0.10 (downloaded the first time) --------------------------------
if [ ! -f JUCE/CMakeLists.txt ]; then
    echo " Downloading JUCE 8.0.10 (about 100 MB, only the first time) ..."
    curl -L --fail --retry 3 -o juce.zip "https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.10.zip"
    rm -rf JUCE _juce_tmp
    mkdir _juce_tmp
    unzip -q juce.zip -d _juce_tmp
    mv _juce_tmp/JUCE-8.0.10 JUCE
    rm -rf _juce_tmp
fi

VERSION="$(sed -n 's/^project(FourTracker VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
GEN=""
command -v ninja >/dev/null 2>&1 && GEN="-G Ninja"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo " Building Four Tracker $VERSION (this takes a few minutes the first time) ..."
"$CMAKE" -S . -B build-mac $GEN -DCMAKE_BUILD_TYPE=Release -DFOURTRACKER_FX_HOST=ON
"$CMAKE" --build build-mac --config Release --parallel "$JOBS" --target FourTracker_Standalone FourTracker_AU

# ---- collect the results -------------------------------------------------------
ART="build-mac/FourTracker_artefacts/Release"
[ -d "$ART" ] || ART="build-mac/FourTracker_artefacts"
APP="$ART/Standalone/Four Tracker.app"
AU="$ART/AU/Four Tracker.component"
if [ ! -d "$APP" ] || [ ! -d "$AU" ]; then
    echo " [ERROR] The build finished but Four Tracker.app / .component was not found under $ART"
    exit 1
fi

rm -rf dist-mac
mkdir -p "dist-mac/Four Tracker"
cp -R "$APP" "dist-mac/Four Tracker/"
cp -R "$AU" "dist-mac/Four Tracker/"

# "ad-hoc" signature: needed to run on Apple Silicon. Not an Apple Developer ID,
# so macOS asks once the first time (see READ ME).
codesign --force --deep -s - "dist-mac/Four Tracker/Four Tracker.app"
codesign --force --deep -s - "dist-mac/Four Tracker/Four Tracker.component"

cat > "dist-mac/Four Tracker/READ ME.txt" <<EOF
FOUR TRACKER $VERSION FOR MAC (beta)
=====================================

Works on Apple Silicon and Intel Macs, macOS 11 (Big Sur) or newer.

THE PROGRAM
  Drag "Four Tracker.app" into your Applications folder.

THE AUDIO UNIT (for Logic, Ableton Live, GarageBand, Reaper ...)
  Copy "Four Tracker.component" to the folder
      /Library/Audio/Plug-Ins/Components
  (in Finder: Go > Go to Folder..., paste the path). Restart your music program.

THE FIRST TIME
  Four Tracker is free and not signed with a paid Apple Developer ID, so macOS
  says it "cannot be opened" or is "from an unidentified developer":
  - Right-click (Ctrl-click) Four Tracker.app and choose Open, then Open again.
  - Or open System Settings > Privacy & Security and click "Open Anyway".
  If the Audio Unit doesn't show up in your music program, open Terminal and run:
      xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/Four Tracker.component"

EFFECTS
  The program can use your Audio Unit effects: 2 per track, plus chaining.
  Press FX (or F8), then SCAN. Your keyboard's F-keys may need the fn key.

Beta software: expect bugs, save your modules often, and use it at your own risk.
https://elcrispo76.github.io/Four-Tracker/
EOF

ln -s /Applications "dist-mac/Four Tracker/Applications"
hdiutil create -volname "Four Tracker" -srcfolder "dist-mac/Four Tracker" -ov -format UDZO \
    "dist-mac/FourTracker-$VERSION-mac.dmg" >/dev/null

if [ "$1" = "install" ]; then
    echo " Installing on this Mac ..."
    rm -rf "/Applications/Four Tracker.app"
    cp -R "dist-mac/Four Tracker/Four Tracker.app" /Applications/
    mkdir -p "$HOME/Library/Audio/Plug-Ins/Components"
    rm -rf "$HOME/Library/Audio/Plug-Ins/Components/Four Tracker.component"
    cp -R "dist-mac/Four Tracker/Four Tracker.component" "$HOME/Library/Audio/Plug-Ins/Components/"
    killall -9 AudioComponentRegistrar 2>/dev/null || true   # so music programs see the new Audio Unit
fi

echo ""
echo " ==================================================================="
echo "  DONE!  dist-mac/FourTracker-$VERSION-mac.dmg"
echo "         (and the program + Audio Unit in dist-mac/Four Tracker)"
echo " ==================================================================="
open dist-mac || true

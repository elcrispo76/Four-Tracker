#!/bin/bash
# ===========================================================================
#  Graphic EQ - Mac build (VST3 + Audio Unit, Apple Silicon + Intel)
#  Needs the Xcode command line tools (xcode-select --install) and CMake.
#  Run in Terminal from this folder:  bash build-mac.sh   (or: bash build-mac.sh install)
#  Result: dist-mac/GraphicEq-<version>-mac.dmg
# ===========================================================================
set -e
cd "$(dirname "$0")"

CMAKE="$(command -v cmake || true)"
[ -z "$CMAKE" ] && [ -x "/Applications/CMake.app/Contents/bin/cmake" ] && CMAKE="/Applications/CMake.app/Contents/bin/cmake"
[ -z "$CMAKE" ] && { echo " [ERROR] CMake was not found (https://cmake.org/download or: brew install cmake)"; exit 1; }

if [ ! -f JUCE/CMakeLists.txt ]; then
    echo " Downloading JUCE 8.0.10 (about 100 MB, only the first time) ..."
    curl -L --fail --retry 3 -o juce.zip "https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.10.zip"
    rm -rf JUCE _juce_tmp && mkdir _juce_tmp
    unzip -q juce.zip -d _juce_tmp
    mv _juce_tmp/JUCE-8.0.10 JUCE
    rm -rf _juce_tmp juce.zip
fi

VERSION="$(sed -n 's/^project(GraphicEq VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
GEN=""; command -v ninja >/dev/null 2>&1 && GEN="-G Ninja"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

"$CMAKE" -S . -B build-mac $GEN -DCMAKE_BUILD_TYPE=Release
"$CMAKE" --build build-mac --config Release --parallel "$JOBS"

ART="build-mac/GraphicEq_artefacts/Release"
[ -d "$ART" ] || ART="build-mac/GraphicEq_artefacts"
D="dist-mac/Graphic EQ"
rm -rf dist-mac && mkdir -p "$D"
cp -R "$ART/VST3/Graphic EQ.vst3" "$D/"
cp -R "$ART/AU/Graphic EQ.component" "$D/"
codesign --force --deep -s - "$D/Graphic EQ.vst3"
codesign --force --deep -s - "$D/Graphic EQ.component"
{
    echo "GRAPHIC EQ $VERSION FOR MAC"
    echo ""
    echo "VST3:        copy \"Graphic EQ.vst3\" to /Library/Audio/Plug-Ins/VST3"
    echo "Audio Unit:  copy \"Graphic EQ.component\" to /Library/Audio/Plug-Ins/Components"
    echo "(Finder: Go > Go to Folder..., paste the path). Then restart your music program."
    echo ""
    echo "Not signed with a paid Apple Developer ID. If it doesn't show up, run in Terminal:"
    echo "  xattr -dr com.apple.quarantine \"/Library/Audio/Plug-Ins/VST3/Graphic EQ.vst3\""
    echo "  xattr -dr com.apple.quarantine \"/Library/Audio/Plug-Ins/Components/Graphic EQ.component\""
    echo ""
    echo "https://elcrispo76.github.io/Four-Tracker/"
} > "$D/READ ME.txt"
hdiutil create -volname "Graphic EQ" -srcfolder "$D" -ov -format UDZO "dist-mac/GraphicEq-$VERSION-mac.dmg" >/dev/null

if [ "$1" = "install" ]; then
    mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Library/Audio/Plug-Ins/Components"
    rm -rf "$HOME/Library/Audio/Plug-Ins/VST3/Graphic EQ.vst3" "$HOME/Library/Audio/Plug-Ins/Components/Graphic EQ.component"
    cp -R "$D/Graphic EQ.vst3" "$HOME/Library/Audio/Plug-Ins/VST3/"
    cp -R "$D/Graphic EQ.component" "$HOME/Library/Audio/Plug-Ins/Components/"
    killall -9 AudioComponentRegistrar 2>/dev/null || true
fi
echo " DONE: dist-mac/GraphicEq-$VERSION-mac.dmg"

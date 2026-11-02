#!/bin/bash
# ===========================================================================
#  FX Processor 2 - Mac build (VST3 + Audio Unit, Apple Silicon + Intel)
#  Needs the Xcode command line tools (xcode-select --install) and CMake.
#  Run in Terminal from this folder:  bash build-mac.sh   (or: bash build-mac.sh install)
#  Result: dist-mac/FxProcessor2-<version>-mac.dmg
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

VERSION="$(sed -n 's/^project(FxProcessor2 VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
GEN=""; command -v ninja >/dev/null 2>&1 && GEN="-G Ninja"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

"$CMAKE" -S . -B build-mac $GEN -DCMAKE_BUILD_TYPE=Release
"$CMAKE" --build build-mac --config Release --parallel "$JOBS"

ART="build-mac/FxProcessor2_artefacts/Release"
[ -d "$ART" ] || ART="build-mac/FxProcessor2_artefacts"
D="dist-mac/FX Processor 2"
rm -rf dist-mac && mkdir -p "$D"
cp -R "$ART/VST3/FX Processor 2.vst3" "$D/"
cp -R "$ART/AU/FX Processor 2.component" "$D/"
codesign --force --deep -s - "$D/FX Processor 2.vst3"
codesign --force --deep -s - "$D/FX Processor 2.component"
{
    echo "FX PROCESSOR 2 $VERSION FOR MAC"
    echo ""
    echo "VST3:        copy \"FX Processor 2.vst3\" to /Library/Audio/Plug-Ins/VST3"
    echo "Audio Unit:  copy \"FX Processor 2.component\" to /Library/Audio/Plug-Ins/Components"
    echo "(Finder: Go > Go to Folder..., paste the path). Then restart your music program."
    echo ""
    echo "Not signed with a paid Apple Developer ID. If it doesn't show up, run in Terminal:"
    echo "  xattr -dr com.apple.quarantine \"/Library/Audio/Plug-Ins/VST3/FX Processor 2.vst3\""
    echo "  xattr -dr com.apple.quarantine \"/Library/Audio/Plug-Ins/Components/FX Processor 2.component\""
    echo ""
    echo "https://elcrispo76.github.io/Four-Tracker/"
} > "$D/READ ME.txt"
hdiutil create -volname "FX Processor 2" -srcfolder "$D" -ov -format UDZO "dist-mac/FxProcessor2-$VERSION-mac.dmg" >/dev/null

if [ "$1" = "install" ]; then
    mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Library/Audio/Plug-Ins/Components"
    rm -rf "$HOME/Library/Audio/Plug-Ins/VST3/FX Processor 2.vst3" "$HOME/Library/Audio/Plug-Ins/Components/FX Processor 2.component"
    cp -R "$D/FX Processor 2.vst3" "$HOME/Library/Audio/Plug-Ins/VST3/"
    cp -R "$D/FX Processor 2.component" "$HOME/Library/Audio/Plug-Ins/Components/"
    killall -9 AudioComponentRegistrar 2>/dev/null || true
fi
echo " DONE: dist-mac/FxProcessor2-$VERSION-mac.dmg"

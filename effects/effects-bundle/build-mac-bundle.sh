#!/bin/bash
# ===========================================================================
#  Four Tracker Effects Bundle - Mac build (Apple Silicon + Intel, macOS 11+)
#
#  Builds all five effects as VST3 and Audio Unit and puts them in ONE installer:
#      effects-bundle/dist-mac/FourTrackerEffects-Bundle-<version>-mac.pkg
#  VST3       -> /Library/Audio/Plug-Ins/VST3
#  Audio Unit -> /Library/Audio/Plug-Ins/Components
#  Under "Customize" in the installer each effect can be left out.
#
#  Needs: Xcode command line tools (xcode-select --install) and CMake.
#  The plugin folders (breakbeat-processor, fx-processor ...) must lie next to
#  the effects-bundle folder. JUCE is downloaded next to them the first time.
#  Run in Terminal:   bash effects-bundle/build-mac-bundle.sh
#  No Mac? GitHub builds it: Actions > "Mac build: effects bundle" > Run workflow.
#  Never VST2: the Mac builds are VST3 + AU only.
# ===========================================================================
set -e
cd "$(dirname "$0")"
BUNDLE_DIR="$(pwd)"
TOP="$(cd .. && pwd)"
VERSION="1.0.0"

echo ""
echo " FOUR TRACKER EFFECTS BUNDLE - MAC BUILD"
echo " ======================================="

if ! xcode-select -p >/dev/null 2>&1; then
    echo " [ERROR] The Xcode command line tools are missing. Run:  xcode-select --install"
    exit 1
fi
CMAKE="$(command -v cmake || true)"
[ -z "$CMAKE" ] && [ -x "/Applications/CMake.app/Contents/bin/cmake" ] && CMAKE="/Applications/CMake.app/Contents/bin/cmake"
[ -z "$CMAKE" ] && { echo " [ERROR] CMake was not found (https://cmake.org/download or: brew install cmake)"; exit 1; }

if [ ! -f "$TOP/JUCE/CMakeLists.txt" ]; then
    echo " Downloading JUCE 8.0.10 (about 100 MB, only the first time) ..."
    curl -L --fail --retry 3 -o "$TOP/juce.zip" "https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.10.zip"
    rm -rf "$TOP/JUCE" "$TOP/_juce_tmp" && mkdir "$TOP/_juce_tmp"
    unzip -q "$TOP/juce.zip" -d "$TOP/_juce_tmp"
    mv "$TOP/_juce_tmp/JUCE-8.0.10" "$TOP/JUCE"
    rm -rf "$TOP/_juce_tmp" "$TOP/juce.zip"
fi

GEN=""; command -v ninja >/dev/null 2>&1 && GEN="-G Ninja"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# folder | CMake target | product name | id | description
PLUGINS=(
  "breakbeat-processor|BreakbeatProcessor|Breakbeat Processor|bb|8 step breakbeat slicer in time with the song"
  "fx-processor|FxProcessor|FX Processor|fx1|Filter, PW gate, chorus, LFO, delay and reverb"
  "fx-processor-2|FxProcessor2|FX Processor 2|fx2|Phaser, flanger and ring modulator"
  "dynamics-processor|DynamicsProcessor|Dynamics Processor|dyn|Transient shaper, compressor and saturation, with AUTO"
  "graphic-eq|GraphicEq|Graphic EQ|eq|10 band graphic EQ, +12 / -12 dB"
)

OUT="$BUNDLE_DIR/dist-mac"
rm -rf "$OUT"
mkdir -p "$OUT/pkgs" "$OUT/resources" "$OUT/scripts/au"
cat > "$OUT/scripts/au/postinstall" <<'EOF'
#!/bin/bash
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
EOF
chmod +x "$OUT/scripts/au/postinstall"

nonrelocatable() {   # $1 = root folder, $2 = plist to write
    pkgbuild --analyze --root "$1" "$2" >/dev/null
    local i=0
    while /usr/libexec/PlistBuddy -c "Print :$i" "$2" >/dev/null 2>&1; do
        /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$2" 2>/dev/null || true
        i=$((i + 1))
    done
}

LINES=""; CHOICES=""; REFS=""; LIST=""; FOUND=0
for entry in "${PLUGINS[@]}"; do
    IFS='|' read -r DIR TARGET NAME ID DESC <<< "$entry"
    if [ ! -f "$TOP/$DIR/CMakeLists.txt" ]; then
        echo " ($DIR was not found: left out)"
        continue
    fi
    echo ""
    echo " ---- $NAME ----"
    "$CMAKE" -S "$TOP/$DIR" -B "$TOP/$DIR/build-mac" $GEN -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR="$TOP/JUCE"
    "$CMAKE" --build "$TOP/$DIR/build-mac" --config Release --parallel "$JOBS" --target "${TARGET}_VST3" "${TARGET}_AU"

    ART="$TOP/$DIR/build-mac/${TARGET}_artefacts/Release"
    [ -d "$ART" ] || ART="$TOP/$DIR/build-mac/${TARGET}_artefacts"
    for f in "$ART/VST3/$NAME.vst3" "$ART/AU/$NAME.component"; do
        [ -d "$f" ] || { echo " [ERROR] $f was not built"; exit 1; }
    done
    lipo -info "$ART/VST3/$NAME.vst3/Contents/MacOS/$NAME"

    mkdir -p "$OUT/root/$ID-vst3" "$OUT/root/$ID-au"
    cp -R "$ART/VST3/$NAME.vst3"     "$OUT/root/$ID-vst3/"
    cp -R "$ART/AU/$NAME.component"  "$OUT/root/$ID-au/"
    # ad-hoc signature (needed on Apple Silicon); not a paid Apple Developer ID
    codesign --force --deep -s - "$OUT/root/$ID-vst3/$NAME.vst3"
    codesign --force --deep -s - "$OUT/root/$ID-au/$NAME.component"

    nonrelocatable "$OUT/root/$ID-vst3" "$OUT/$ID-vst3.plist"
    nonrelocatable "$OUT/root/$ID-au"   "$OUT/$ID-au.plist"
    pkgbuild --root "$OUT/root/$ID-vst3" --component-plist "$OUT/$ID-vst3.plist" \
             --install-location /Library/Audio/Plug-Ins/VST3 \
             --identifier "com.fourtracker.effects.$ID.vst3" --version "$VERSION" "$OUT/pkgs/$ID-vst3.pkg"
    pkgbuild --root "$OUT/root/$ID-au" --component-plist "$OUT/$ID-au.plist" \
             --install-location /Library/Audio/Plug-Ins/Components --scripts "$OUT/scripts/au" \
             --identifier "com.fourtracker.effects.$ID.au" --version "$VERSION" "$OUT/pkgs/$ID-au.pkg"

    LINES="$LINES<line choice=\"$ID\"/>"
    CHOICES="$CHOICES<choice id=\"$ID\" title=\"$NAME\" description=\"$DESC. VST3 and Audio Unit.\"><pkg-ref id=\"com.fourtracker.effects.$ID.vst3\"/><pkg-ref id=\"com.fourtracker.effects.$ID.au\"/></choice>"
    REFS="$REFS<pkg-ref id=\"com.fourtracker.effects.$ID.vst3\" version=\"$VERSION\" onConclusion=\"none\">$ID-vst3.pkg</pkg-ref>"
    REFS="$REFS<pkg-ref id=\"com.fourtracker.effects.$ID.au\" version=\"$VERSION\" onConclusion=\"none\">$ID-au.pkg</pkg-ref>"
    LIST="$LIST  - $NAME: $DESC"$'\n'
    FOUND=$((FOUND + 1))
done

[ "$FOUND" -gt 0 ] || { echo " [ERROR] No effects found next to the effects-bundle folder"; exit 1; }

cat > "$OUT/resources/welcome.txt" <<EOF
Four Tracker Effects Bundle $VERSION for Mac

Free effects made for Four Tracker, in the same look. They follow the song's tempo.

$LIST
Each one is installed as VST3 (/Library/Audio/Plug-Ins/VST3) and as an Audio Unit
(/Library/Audio/Plug-Ins/Components), so they work in Four Tracker, Logic Pro, GarageBand,
Ableton Live, Bitwig, Reaper and others. Click "Customize" to leave one out.

Works on Apple Silicon and Intel Macs, macOS 11 or newer.
Not signed with a paid Apple Developer ID: if macOS says the installer can't be opened, open
System Settings > Privacy & Security and click "Open Anyway".
https://elcrispo76.github.io/Four-Tracker/
EOF

cat > "$OUT/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Four Tracker Effects Bundle $VERSION</title>
    <welcome file="welcome.txt" mime-type="text/plain"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <volume-check><allowed-os-versions><os-version min="11.0"/></allowed-os-versions></volume-check>
    <choices-outline>$LINES</choices-outline>
    $CHOICES
    $REFS
</installer-gui-script>
EOF

PKG="$OUT/FourTrackerEffects-Bundle-$VERSION-mac.pkg"
productbuild --distribution "$OUT/distribution.xml" --package-path "$OUT/pkgs" --resources "$OUT/resources" "$PKG"

echo ""
echo " ==================================================================="
echo "  DONE!  $PKG"
echo "  $FOUND effects, VST3 + Audio Unit ($(du -h "$PKG" | cut -f1))"
echo " ==================================================================="
[ -n "$CI" ] || open "$OUT" || true

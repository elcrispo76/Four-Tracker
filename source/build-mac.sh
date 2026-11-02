#!/bin/bash
# ===========================================================================
#  Four Tracker - Mac build (Apple Silicon + Intel in one, macOS 11 or newer)
#
#  Makes an installer:  dist-mac/FourTracker-<version>-mac.pkg
#  that installs (each part can be ticked off under "Customize"):
#    Four Tracker.app        -> /Applications            (the program, with AU + VST3 effects)
#    Four Tracker.vst3       -> /Library/Audio/Plug-Ins/VST3
#    Four Tracker.component  -> /Library/Audio/Plug-Ins/Components   (Logic, GarageBand ...)
#    the user manual and the sample pack -> /Users/Shared/Four Tracker
#                                           (and copied to Documents/Four Tracker)
#
#  Needs: Xcode or just the command line tools  (run: xcode-select --install)
#         CMake                                 (https://cmake.org/download or: brew install cmake)
#
#  Run in Terminal from this folder:   bash build-mac.sh
#  No Mac? GitHub builds it: Actions > "Mac build" > Run workflow.
# ===========================================================================
set -e
cd "$(dirname "$0")"
HERE="$(pwd)"

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
    rm -rf _juce_tmp juce.zip
fi

VERSION="$(sed -n 's/^project(FourTracker VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
GEN=""
command -v ninja >/dev/null 2>&1 && GEN="-G Ninja"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo " Building Four Tracker $VERSION (this takes a while the first time) ..."
"$CMAKE" -S . -B build-mac $GEN -DCMAKE_BUILD_TYPE=Release -DFOURTRACKER_FX_HOST=ON -DFOURTRACKER_NO_VST2=ON
"$CMAKE" --build build-mac --config Release --parallel "$JOBS" \
    --target FourTracker_Standalone FourTracker_AU FourTracker_VST3

# ---- collect the results -------------------------------------------------------
ART="build-mac/FourTracker_artefacts/Release"
[ -d "$ART" ] || ART="build-mac/FourTracker_artefacts"
APP="$ART/Standalone/Four Tracker.app"
AU="$ART/AU/Four Tracker.component"
VST3="$ART/VST3/Four Tracker.vst3"
for f in "$APP" "$AU" "$VST3"; do
    if [ ! -d "$f" ]; then
        echo " [ERROR] The build finished but $f was not found"
        exit 1
    fi
done
for f in "$APP" "$AU" "$VST3"; do lipo -info "$(find "$f/Contents/MacOS" -type f | head -1)"; done

rm -rf dist-mac
mkdir -p dist-mac/root/app dist-mac/root/au dist-mac/root/vst3 dist-mac/root/extras \
         dist-mac/scripts/au dist-mac/scripts/extras dist-mac/pkgs dist-mac/resources
cp -R "$APP"  dist-mac/root/app/
cp -R "$AU"   dist-mac/root/au/
cp -R "$VST3" dist-mac/root/vst3/

# "ad-hoc" signature: needed to run on Apple Silicon. Not an Apple Developer ID,
# so macOS asks once the first time (see the READ ME).
codesign --force --deep -s - "dist-mac/root/app/Four Tracker.app"
codesign --force --deep -s - "dist-mac/root/au/Four Tracker.component"
codesign --force --deep -s - "dist-mac/root/vst3/Four Tracker.vst3"

# ---- the manual and the sample pack ---------------------------------------------
EX="dist-mac/root/extras"
HAS_EXTRAS=0
if [ -f "installer/Four Tracker User Manual.pdf" ]; then
    cp "installer/Four Tracker User Manual.pdf" "$EX/"
    HAS_EXTRAS=1
fi
if [ -d "installer/Sample Pack" ]; then
    mkdir -p "$EX/Four Tracker Sample Pack"
    cp -R "installer/Sample Pack/." "$EX/Four Tracker Sample Pack/"
    HAS_EXTRAS=1
else
    echo " (installer/Sample Pack was not found: the installer is made without the sample pack)"
fi

cat > "$EX/READ ME.txt" <<EOF
FOUR TRACKER $VERSION FOR MAC (beta)

The program is in your Applications folder. The plugins are in
  /Library/Audio/Plug-Ins/VST3/Four Tracker.vst3
  /Library/Audio/Plug-Ins/Components/Four Tracker.component
This folder has the user manual and the sample pack. They are also copied to
Documents/Four Tracker (the sample pack under Samples).

THE FIRST TIME
  Four Tracker is free and not signed with a paid Apple Developer ID. If macOS
  says it can't be opened: open System Settings > Privacy & Security, scroll down
  and click "Open Anyway" next to Four Tracker.

EFFECTS
  The program can use your Audio Unit and VST3 effects: 3 per track and 3 on the
  master. Press FX (or F8), then SCAN. Mac keyboards may need the fn key for the
  F-keys (F3 = REC, F4 = STOP).

Beta software: save your modules often, and use it at your own risk.
https://elcrispo76.github.io/Four-Tracker/
EOF

cat > dist-mac/scripts/extras/postinstall <<'EOF'
#!/bin/bash
# copy the sample pack and the manual into the user's Documents/Four Tracker (never overwrites anything)
SRC="/Users/Shared/Four Tracker"
U="$(stat -f%Su /dev/console 2>/dev/null)"
[ -z "$U" ] || [ "$U" = "root" ] && exit 0
H="$(dscl . -read "/Users/$U" NFSHomeDirectory 2>/dev/null | awk '{print $2}')"
[ -d "$H/Documents" ] || exit 0
D="$H/Documents/Four Tracker"
sudo -u "$U" mkdir -p "$D/Samples" 2>/dev/null || exit 0
if [ -d "$SRC/Four Tracker Sample Pack" ] && [ ! -e "$D/Samples/Four Tracker Sample Pack" ]; then
    sudo -u "$U" cp -R "$SRC/Four Tracker Sample Pack" "$D/Samples/" 2>/dev/null || true
fi
if [ -f "$SRC/Four Tracker User Manual.pdf" ]; then
    sudo -u "$U" cp -f "$SRC/Four Tracker User Manual.pdf" "$D/" 2>/dev/null || true
fi
exit 0
EOF
cat > dist-mac/scripts/au/postinstall <<'EOF'
#!/bin/bash
# so music programs see the new Audio Unit straight away
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
EOF
chmod +x dist-mac/scripts/extras/postinstall dist-mac/scripts/au/postinstall

# ---- the packages ----------------------------------------------------------------
# A bundle must never be "relocated" to another copy the Mac already has somewhere else
nonrelocatable() {   # $1 = root folder, $2 = plist to write
    pkgbuild --analyze --root "$1" "$2" >/dev/null
    local i=0
    while /usr/libexec/PlistBuddy -c "Print :$i" "$2" >/dev/null 2>&1; do
        /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$2" 2>/dev/null || true
        i=$((i + 1))
    done
}
nonrelocatable dist-mac/root/app  dist-mac/app.plist
nonrelocatable dist-mac/root/au   dist-mac/au.plist
nonrelocatable dist-mac/root/vst3 dist-mac/vst3.plist

pkgbuild --root dist-mac/root/app --component-plist dist-mac/app.plist --install-location /Applications \
         --identifier com.fourtracker.pkg.app --version "$VERSION" dist-mac/pkgs/app.pkg
pkgbuild --root dist-mac/root/vst3 --component-plist dist-mac/vst3.plist --install-location /Library/Audio/Plug-Ins/VST3 \
         --identifier com.fourtracker.pkg.vst3 --version "$VERSION" dist-mac/pkgs/vst3.pkg
pkgbuild --root dist-mac/root/au --component-plist dist-mac/au.plist --install-location /Library/Audio/Plug-Ins/Components \
         --scripts dist-mac/scripts/au --identifier com.fourtracker.pkg.au --version "$VERSION" dist-mac/pkgs/au.pkg
pkgbuild --root dist-mac/root/extras --install-location "/Users/Shared/Four Tracker" \
         --scripts dist-mac/scripts/extras --identifier com.fourtracker.pkg.extras --version "$VERSION" dist-mac/pkgs/extras.pkg

cat > dist-mac/resources/welcome.txt <<EOF
Four Tracker $VERSION for Mac (beta)

A sample tracker with 1 to 16 tracks in the style of ProTracker and FastTracker 2, built for playing live.

This installs:
  - Four Tracker (the program) in Applications
  - the VST3 plugin and the Audio Unit for your music programs
  - the user manual and the Four Tracker Sample Pack (88 sounds, CC0) in /Users/Shared/Four Tracker and Documents/Four Tracker

Click "Customize" on the next pages to leave a part out.
Works on Apple Silicon and Intel Macs, macOS 11 or newer. Free, beta, use it at your own risk.
EOF

EXTRAS_LINE=""; EXTRAS_CHOICE=""; EXTRAS_REF=""
if [ "$HAS_EXTRAS" = 1 ]; then
    EXTRAS_LINE='<line choice="extras"/>'
    EXTRAS_CHOICE='<choice id="extras" title="User manual and sample pack" description="The user manual (PDF) and the Four Tracker Sample Pack: 88 drum, bass, synth and FX sounds (CC0), in /Users/Shared/Four Tracker and Documents/Four Tracker."><pkg-ref id="com.fourtracker.pkg.extras"/></choice>'
    EXTRAS_REF="<pkg-ref id=\"com.fourtracker.pkg.extras\" version=\"$VERSION\" onConclusion=\"none\">extras.pkg</pkg-ref>"
fi
cat > dist-mac/distribution.xml <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Four Tracker $VERSION</title>
    <welcome file="welcome.txt" mime-type="text/plain"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <volume-check><allowed-os-versions><os-version min="11.0"/></allowed-os-versions></volume-check>
    <choices-outline>
        <line choice="app"/>
        <line choice="vst3"/>
        <line choice="au"/>
        $EXTRAS_LINE
    </choices-outline>
    <choice id="app" title="Four Tracker (the program)" description="The standalone program, in Applications. It can use your Audio Unit and VST3 effects and record your jams."><pkg-ref id="com.fourtracker.pkg.app"/></choice>
    <choice id="vst3" title="VST3 plugin" description="Four Tracker as a VST3 instrument for Ableton Live, Bitwig, Reaper, Cubase, Studio One ..."><pkg-ref id="com.fourtracker.pkg.vst3"/></choice>
    <choice id="au" title="Audio Unit plugin" description="Four Tracker as an Audio Unit instrument for Logic Pro, GarageBand, Ableton Live ..."><pkg-ref id="com.fourtracker.pkg.au"/></choice>
    $EXTRAS_CHOICE
    <pkg-ref id="com.fourtracker.pkg.app" version="$VERSION" onConclusion="none">app.pkg</pkg-ref>
    <pkg-ref id="com.fourtracker.pkg.vst3" version="$VERSION" onConclusion="none">vst3.pkg</pkg-ref>
    <pkg-ref id="com.fourtracker.pkg.au" version="$VERSION" onConclusion="none">au.pkg</pkg-ref>
    $EXTRAS_REF
</installer-gui-script>
EOF

OUT="dist-mac/FourTracker-$VERSION-mac.pkg"
productbuild --distribution dist-mac/distribution.xml --package-path dist-mac/pkgs \
             --resources dist-mac/resources "$OUT"

echo ""
echo " ==================================================================="
echo "  DONE!  $OUT"
echo "  ($(du -h "$OUT" | cut -f1)) - double-click it to install"
echo " ==================================================================="
[ -n "$CI" ] || open dist-mac || true

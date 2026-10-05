#!/bin/bash
# Builds a macOS installer package (Celadon Installer.pkg) from the compiled
# plugin, ready to send to friends.
#
# Compiles first, so the .pkg can never ship a stale binary. This script used to
# assume you'd already run ./build.sh, and only checked that an artefact EXISTED
# rather than that it matched the current source -- so it would happily package a
# months-old build and report success.
#
#   ./make-installer.sh              build, then package  (what you want)
#   ./make-installer.sh --no-build   package build/ as-is (CI, or a known-good tree)
set -e
cd "$(dirname "$0")"

if [ "$1" = "--no-build" ]; then
    echo "[--no-build] packaging whatever is already in build/ -- may be stale"
else
    ./build.sh
    echo ""
fi

# Read the version from CMakeLists so every installer carries the real build
# version. This used to be hardcoded to 1.0.0, so each new .pkg claimed to be
# the same version as the last one.
VERSION=$(sed -n 's/^project(Celadon VERSION \([0-9][0-9.]*\)).*/\1/p' CMakeLists.txt)
if [ -z "$VERSION" ]; then
    echo "Could not read the version from CMakeLists.txt (expected: project(Celadon VERSION x.y.z))"
    exit 1
fi

VST3="build/Celadon_artefacts/Release/VST3/Celadon.vst3"
AU="build/Celadon_artefacts/Release/AU/Celadon.component"

echo "=== Celadon installer builder ==="
echo "[ok] version $VERSION"

if [ ! -d "$VST3" ]; then
    # Some CMake generators skip the Release subfolder
    VST3="build/Celadon_artefacts/VST3/Celadon.vst3"
    AU="build/Celadon_artefacts/AU/Celadon.component"
fi

if [ ! -d "$VST3" ]; then
    echo ""
    echo "No built plugin found at build/Celadon_artefacts/. If you passed"
    echo "--no-build, drop it and let this script compile for you."
    exit 1
fi

# Belt and braces: even with the auto-build above, refuse to package a binary
# older than the newest source file. Catches a silently skipped build.
NEWEST_SRC=$(find Source CMakeLists.txt -type f -newer "$VST3/Contents/MacOS/Celadon" 2>/dev/null | head -1)
if [ -n "$NEWEST_SRC" ]; then
    echo ""
    echo "[!!] The built plugin is OLDER than your source ($NEWEST_SRC)."
    echo "     Refusing to package a stale build. Run ./build.sh and check for errors."
    exit 1
fi

STAGE=$(mktemp -d)
mkdir -p "$STAGE/vst3" "$STAGE/au" dist
cp -R "$VST3" "$STAGE/vst3/"
echo "[ok] VST3 found"

PACKAGES=()

# Force a component-plist flag whether or not pkgbuild emitted it. Add fails if
# the key exists; Set fails if it doesn't -- so try Add, fall back to Set.
plist_force () {
    local plist="$1" key="$2" val="$3"
    /usr/libexec/PlistBuddy -c "Add :0:$key bool $val" "$plist" >/dev/null 2>&1 \
        || /usr/libexec/PlistBuddy -c "Set :0:$key $val" "$plist" >/dev/null
}

# Two Installer behaviours will otherwise make a new .pkg fail to replace an old
# install. Both are decided by the component plist, so pin them explicitly rather
# than inheriting whatever the host macOS defaults to:
#
#   BundleIsVersionChecked - if true (pkgbuild's default), the Installer compares
#     against the copy already on disk and SKIPS the payload unless the new one is
#     newer. Same version number => old plugin silently survives. This is the one
#     that bites when sharing rebuilds that carry the same version.
#
#   BundleIsRelocatable - if true, the Installer redirects the payload on top of
#     any existing copy of the bundle it can find, ignoring --install-location.
#     Recent macOS omits the key (defaults false); older macOS emits it as true,
#     so set it for the benefit of whoever you send the .pkg to.
pin_component () {
    local root="$1" plist="$2"
    pkgbuild --analyze --root "$root" "$plist" >/dev/null

    plist_force "$plist" BundleIsVersionChecked false
    plist_force "$plist" BundleIsRelocatable    false

    local ver rel
    ver=$(/usr/libexec/PlistBuddy -c "Print :0:BundleIsVersionChecked" "$plist" 2>/dev/null || echo "?")
    rel=$(/usr/libexec/PlistBuddy -c "Print :0:BundleIsRelocatable"    "$plist" 2>/dev/null || echo "?")
    if [ "$ver" != "false" ] || [ "$rel" != "false" ]; then
        echo "[!!] Could not pin component flags for $root"
        echo "     BundleIsVersionChecked=$ver BundleIsRelocatable=$rel (both must be false)"
        exit 1
    fi
}

pin_component "$STAGE/vst3" "$STAGE/vst3-component.plist"
pkgbuild --root "$STAGE/vst3" \
         --component-plist "$STAGE/vst3-component.plist" \
         --identifier com.adrianeliasson.celadon.vst3 \
         --version "$VERSION" \
         --install-location "/Library/Audio/Plug-Ins/VST3" \
         "$STAGE/CeladonVST3.pkg" >/dev/null
PACKAGES+=(--package "$STAGE/CeladonVST3.pkg")
echo "[ok] VST3 pinned to /Library/Audio/Plug-Ins/VST3"

if [ -d "$AU" ]; then
    cp -R "$AU" "$STAGE/au/"
    pin_component "$STAGE/au" "$STAGE/au-component.plist"
    pkgbuild --root "$STAGE/au" \
             --component-plist "$STAGE/au-component.plist" \
             --identifier com.adrianeliasson.celadon.au \
             --version "$VERSION" \
             --install-location "/Library/Audio/Plug-Ins/Components" \
             "$STAGE/CeladonAU.pkg" >/dev/null
    PACKAGES+=(--package "$STAGE/CeladonAU.pkg")
    echo "[ok] AU found, pinned to /Library/Audio/Plug-Ins/Components"
fi

productbuild "${PACKAGES[@]}" "dist/Celadon Installer.pkg" >/dev/null
rm -rf "$STAGE"

echo ""
echo "=== Done! ==="
echo "Installer: dist/Celadon Installer.pkg"
echo ""
echo "Send that file to your friend. Notes for them:"
echo "  - Needs an Apple Silicon Mac (M1/M2/M3/M4)"
echo "  - The installer is unsigned, so double-clicking may be blocked:"
echo "    RIGHT-CLICK the .pkg -> Open -> Open. (Or approve it under"
echo "    System Settings > Privacy & Security after the first attempt.)"
echo "  - Installs to /Library/Audio/Plug-Ins (VST3 + AU), then rescan"
echo "    plugins in the DAW. In Ableton: Settings > Plug-Ins > Rescan."
echo "  - Upgrading from an older build? Hunt down any stray copy first:"
echo "        mdfind -name Celadon.vst3"
echo "    Installers before this one could relocate themselves on top of an"
echo "    existing copy. Delete anything outside /Library/Audio/Plug-Ins/VST3"
echo "    (and ~/Library/Audio/Plug-Ins/VST3), then run this installer."

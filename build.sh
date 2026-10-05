#!/bin/bash
# Celadon build script — checks your setup, fetches the pixel fonts, builds.
set -e
cd "$(dirname "$0")"

echo "=== Celadon build ==="

# 1. Check for Xcode Command Line Tools
if ! xcode-select -p >/dev/null 2>&1; then
    echo ""
    echo "Xcode Command Line Tools are missing."
    echo "Run this, accept the popup, then re-run ./build.sh:"
    echo ""
    echo "    xcode-select --install"
    echo ""
    exit 1
fi
echo "[ok] Xcode Command Line Tools found"

# 2. Check for CMake
if ! command -v cmake >/dev/null 2>&1; then
    echo ""
    echo "CMake is missing."
    if command -v brew >/dev/null 2>&1; then
        echo "Installing via Homebrew..."
        brew install cmake
    else
        echo "Install Homebrew first (https://brew.sh), then run:"
        echo ""
        echo "    brew install cmake"
        echo ""
        exit 1
    fi
fi
echo "[ok] CMake found: $(cmake --version | head -1)"

# 3. Fetch the pixel fonts (SIL OFL 1.1 — free to embed). Not fatal if the
#    download fails; the plugin then falls back to system fonts.
mkdir -p Assets
fetch_font () {
    local out="Assets/$1"; shift
    [ -f "$out" ] && return 0
    for url in "$@"; do
        if curl -fsSL "$url" -o "$out" 2>/dev/null; then
            echo "[ok] fetched $(basename "$out")"
            return 0
        fi
    done
    echo "[warn] could not download $(basename "$out") — using system-font fallback"
    rm -f "$out"
    return 0
}
fetch_font "PressStart2P-Regular.ttf" \
    "https://raw.githubusercontent.com/google/fonts/main/ofl/pressstart2p/PressStart2P-Regular.ttf"
fetch_font "Silkscreen-Regular.ttf" \
    "https://raw.githubusercontent.com/google/fonts/main/ofl/silkscreen/Silkscreen-Regular.ttf" \
    "https://raw.githubusercontent.com/google/fonts/main/ofl/silkscreen/slkscr.ttf"
fetch_font "VT323-Regular.ttf" \
    "https://raw.githubusercontent.com/google/fonts/main/ofl/vt323/VT323-Regular.ttf"

# 4. Configure (first run downloads JUCE — needs internet, takes a few minutes)
# If the project folder was moved/renamed, the old CMake cache is invalid — clear it.
if [ -f build/CMakeCache.txt ] && ! grep -q "CMAKE_CACHEFILE_DIR:INTERNAL=$(pwd)/build" build/CMakeCache.txt; then
    echo "Project folder moved or renamed — clearing stale build cache..."
    rm -rf build
fi

echo ""
echo "Configuring... (first run downloads JUCE, ~2-5 min)"
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 5. Build
echo ""
echo "Building... (~2-5 min first time)"
cmake --build build --config Release -j"$(sysctl -n hw.ncpu)"

echo ""
echo "=== Done! ==="
echo "The plugin was auto-copied to your plugin folders:"
echo "  VST3: ~/Library/Audio/Plug-Ins/VST3/Celadon.vst3   (Ableton)"
echo "  AU:   ~/Library/Audio/Plug-Ins/Components/Celadon.component"
echo ""
echo "In Ableton: Settings > Plug-Ins > Rescan, then find 'Celadon'"
echo "under AdrianEliasson in the VST3 browser section."
echo "(After a rebuild: quit and reopen Ableton — it caches plugin UIs.)"
echo ""
echo "First thing inside the plugin: click the CART at the top and load"
echo "your .sf2 — e.g. Pokemon_RSE_HD_Soundfont_Release.sf2 in this folder's"
echo "parent directory."
echo ""

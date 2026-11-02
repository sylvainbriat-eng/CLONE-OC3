#!/bin/bash
# Compile CLONE OC3 sur Mac et l'installe pour Ableton Live.
# Usage : bash scripts/build_mac.sh
set -euo pipefail

cd "$(dirname "$0")/.."

# CMake installé dans Applications (sans Homebrew)
CMAKE="cmake"
if ! command -v cmake >/dev/null 2>&1; then
    CMAKE="/Applications/CMake.app/Contents/bin/cmake"
fi
if [ ! -x "$CMAKE" ] && ! command -v "$CMAKE" >/dev/null 2>&1; then
    echo "CMake introuvable : installez CMake.app dans /Applications." >&2
    exit 1
fi

echo "== Configuration (la 1re fois, JUCE est téléchargé : quelques minutes) =="
"$CMAKE" -S . -B build -DCMAKE_BUILD_TYPE=Release

echo "== Test du moteur audio =="
"$CMAKE" --build build --target dsp_test --config Release -j 8
./build/dsp_test

echo "== Compilation du plugin =="
"$CMAKE" --build build --target CloneOC3_VST3 CloneOC3_AU --config Release -j 8

VST3="$HOME/Library/Audio/Plug-Ins/VST3/CLONE OC3.vst3"
AU="$HOME/Library/Audio/Plug-Ins/Components/CLONE OC3.component"
for P in "$VST3" "$AU"; do
    if [ -d "$P" ]; then
        codesign --force --deep --sign - "$P" >/dev/null 2>&1 || true
        xattr -dr com.apple.quarantine "$P" 2>/dev/null || true
        echo "Installé : $P"
    fi
done

echo ""
echo "Terminé. Dans Ableton Live : Réglages > Plug-ins > « Utiliser les plug-ins VST3 »,"
echo "puis « Rescanner ». CLONE OC3 apparaît dans Plug-ins > Sylvain."

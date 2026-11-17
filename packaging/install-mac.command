#!/bin/bash
# Installs Octaline into your user plug-in folders and clears the
# "downloaded from the internet" flag so your DAW can load it.
set -e
cd "$(dirname "$0")"

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_DIR" "$AU_DIR"

rm -rf "$VST3_DIR/Octaline.vst3" "$AU_DIR/Octaline.component"
cp -R Octaline.vst3 "$VST3_DIR/"
cp -R Octaline.component "$AU_DIR/"
xattr -dr com.apple.quarantine "$VST3_DIR/Octaline.vst3" "$AU_DIR/Octaline.component" 2>/dev/null || true

# Make Logic/GarageBand notice the new Audio Unit.
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo "Octaline installed:"
echo "  $VST3_DIR/Octaline.vst3"
echo "  $AU_DIR/Octaline.component"
echo "Restart your DAW (in Cubase, rescan plug-ins if it does not appear)."

#!/bin/bash
# Restores the original game binary and removes the minimap plugin.
#   ./uninstall.sh [path/to/Grand Theft Auto Vice City.app]
set -euo pipefail

APP="${1:-/Applications/Grand Theft Auto Vice City.app}"
DATA="$HOME/Library/Application Support/macVC/vcminimap"
BIN="$APP/Contents/MacOS/macVC"
BACKUP="$DATA/macVC.vanilla"

if pgrep -f "$BIN" >/dev/null; then
	echo "Quit the game first."
	exit 1
fi
[ -f "$BACKUP" ] || { echo "No backup at $BACKUP; nothing to restore."; exit 1; }

cp "$BACKUP" "$BIN"
rm -f "$APP/Contents/Frameworks/libVCMinimap.dylib"
echo "Restored the original game. Map data and settings remain in $DATA (delete it to remove them too)."

#!/bin/bash
# Installs VC Modern HUD into Grand Theft Auto Vice City.app (macVC 1.0.5217).
#   ./install.sh [path/to/Grand Theft Auto Vice City.app]
# Re-running is safe: it always patches from the saved vanilla binary.
set -euo pipefail

APP="${1:-/Applications/Grand Theft Auto Vice City.app}"
HERE="$(cd "$(dirname "$0")" && pwd)"
SUPPORT="$HOME/Library/Application Support/macVC"
DATA="$SUPPORT/vcmodernhud"
BIN="$APP/Contents/MacOS/macVC"
BACKUP="$DATA/macVC.vanilla"
VANILLA_SHA="a715c2ce363265385b73ac6d2decd386f7d8bbccb61519766907a563a663cc95"

[ -f "$BIN" ] || { echo "No game binary at $BIN"; exit 1; }
if pgrep -f "$BIN" >/dev/null; then
	echo "Quit the game first."
	exit 1
fi

echo "==> Building"
make -C "$HERE" all >/dev/null

# Installs from before the project was renamed (VC Minimap) kept everything under other names.
if [ -d "$SUPPORT/vcminimap" ] && [ ! -d "$DATA" ]; then
	mv "$SUPPORT/vcminimap" "$DATA"
	echo "==> Moved $SUPPORT/vcminimap to $DATA"
fi
if [ -f "$SUPPORT/vcminimap.ini" ] && [ ! -f "$SUPPORT/vcmodernhud.ini" ]; then
	mv "$SUPPORT/vcminimap.ini" "$SUPPORT/vcmodernhud.ini"
fi
rm -f "$SUPPORT/vcminimap.log" "$APP/Contents/Frameworks/libVCMinimap.dylib"

mkdir -p "$DATA"
if [ ! -f "$BACKUP" ]; then
	if [ "$(shasum -a 256 "$BIN" | cut -d' ' -f1)" != "$VANILLA_SHA" ]; then
		echo "$BIN is not the vanilla macVC 1.0.5217 binary and no backup exists; refusing to patch."
		exit 1
	fi
	cp "$BIN" "$BACKUP"
	echo "==> Saved the original game binary to $BACKUP"
fi

echo "==> Baking map data from your game files"
"$HERE/build/vcmm-bake" "$APP/Contents/Resources/GameData" "$DATA/map.bin"

echo "==> Patching and re-signing the game binary"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
python3 -I "$HERE/tools/patch_macvc.py" "$BACKUP" "$TMP/macVC"
chmod +x "$TMP/macVC"
codesign -f -s - -i com.github.gtamac.macVC "$TMP/macVC" 2>/dev/null

if ! cp "$TMP/macVC" "$BIN" || ! cp "$HERE/build/libVCModernHUD.dylib" "$APP/Contents/Frameworks/"; then
	echo "Could not write into $APP."
	echo "If macOS blocked it, allow your terminal under System Settings > Privacy & Security > App Management."
	exit 1
fi
echo "==> Installed. Settings: ~/Library/Application Support/macVC/vcmodernhud.ini"
echo "    Log:      ~/Library/Application Support/macVC/vcmodernhud.log"

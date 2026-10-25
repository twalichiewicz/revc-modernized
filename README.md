# Modernized Grand Theft Auto: Vice City

GTA Vice City on the Mac, playing like a modern Rockstar game.

This mod takes the native Apple Silicon port of Vice City, built from reVC, and brings in what GTA V and
Red Dead Redemption 2 made standard: a tilted GPS minimap, modern controls and aiming, swimming and
climbing, phone calls on the go, and the small open-world conveniences of the last decade. Vice City's
story, missions and world stay as they are.

![In game](docs/hud.jpg)

## What's new

**Map and HUD**
- A tilted 3D minimap with the real buildings and roads, and GPS to your waypoint or the next objective.
- Health, armour and stamina bars under the map; money when it changes or where you can spend it;
  wanted stars over the weapon.
- A GTA IV-style pause menu that opens on the map, with hospitals, police stations and businesses marked.

**Controls**
- A GTA V layout: LT aims and RT fires, the triggers drive, weapon and radio wheels on LB and the D-pad.
- Over-the-shoulder aiming, free-aim drive-bys, a smooth right stick and controller rumble.
- PlayStation controllers work properly.

**On foot**
- Swimming and diving, with a breath meter, instead of drowning. A sinking car gives you time to get out.
- Climbing: vault walls, pull up onto ledges and climb out of the water.
- RDR2-style responses: greet or insult people, pick up or wave off hookers.

**The world**
- Story phone calls you can take anywhere, on a 1980s brick phone.
- Car radios you can hear from the street.
- Ken texts what the story needs once it waits on your businesses, and you can buy them on credit.

## Install

You need an Apple Silicon Mac, the macOS port of Vice City (version 1.0.5217) set up with your own copy
of the game, and the Xcode Command Line Tools (`xcode-select --install`).

```sh
git clone https://github.com/twalichiewicz/revc-modernized.git
cd revc-modernized
./install.sh    # quit the game first; add the app's path if it isn't in /Applications
```

The installer builds the mod, keeps a copy of the original game, and patches it. It only touches the
exact 1.0.5217 build. Run it again whenever the port updates itself. If macOS blocks it, allow your
terminal under System Settings → Privacy & Security → App Management.

`./uninstall.sh` puts the original game back. Your saves are never touched.

## Settings

Everything is on by default, and every feature can be switched off in
`~/Library/Application Support/macVC/vcmodernhud.ini`, which explains each setting. If something looks
wrong, the log next to it (`vcmodernhud.log`) usually says why.

## How it works

The port is a native arm64 build of reVC with its symbols intact. The installer patches a jump into each
game function the mod changes, and a plugin (`src/`) does the rest: the new HUD and menus, the
controller, and posing Tommy bone by bone where the game has no animation (swimming, climbing). The map
is built from your own game files at install time, so nothing from the game is in this repository.

## Development

`make` builds, `make test` runs the unit tests, and `make preview` builds an offline renderer for the
minimap. Offsets and call sites are for the 1.0.5217 binary only.

## Disclaimer

A fan-made mod, not affiliated with or endorsed by Rockstar Games or Take-Two Interactive. It contains no
game code or assets: you need your own copy of the game and the macOS port. Use at your own risk;
`./uninstall.sh` restores the original game.

## License

[MIT](LICENSE)

# Installed wing geometry test

The existing Metal game remains the default mode. The experimental mode is a separate pinned source host inside the same app bundle. It renders the user's flattened Dodo model with a selected outboard wing, an untouched control aircraft and a fixed wall/contact marker. This is an interactive geometry publication and query test. It does not enable force-derived damage, flight simulation or a dynamic aircraft/building impact.

## Run

Quit any existing game instance. Hold Option while opening `/Applications/Grand Theft Auto Vice City.app` and select **Wing / building test**, or run:

```sh
open "/Applications/Grand Theft Auto Vice City.app" --args --physics-test
```

- **D** removes the selected wing tip (48 to 28 collision triangles).
- **R** repairs it. **Space** repairs and resets the view.
- **Arrow keys** orbit; **+ / -** zoom; **Esc** exits.
- Contact, LOS and vertical indicators change from HIT to CLEAR when the tip disappears. The rear control aircraft stays intact.

Launch normally for the existing game. The experimental executable does not load `libVCModernHUD.dylib`. Logs append to `~/Library/Logs/VCPhysicsTest.log`.

## Reproduce and package

Prerequisites: macOS ARM64, Xcode command line tools, CMake and the versions in `source-lock.json`. No global dependencies are installed by these helpers. The asset exporter reads the user's GameData without modifying it. Generated aircraft data is local and is not committed.

First reproduce the source-host proof in a new directory (details and earlier evidence are in `docs/research/2026-11-05-source-host-query-proof.md`):

```sh
python3 tools/host/build_host.py --work-dir build/host-source/reviewed --control
python3 tools/host/build_lab.py --work-dir build/installed-wing
python3 -m unittest discover -s tests -p test_physics_app_install.py
python3 tools/host/app_package.py
```

`build_lab.py` reuses the pinned static GLFW from the preceding proof, checks the pinned librw tree and recorded audio dependencies, and records engine changes, exporter include dependencies, toolchain, binary/data hashes and static dependency hashes in `build/installed-wing/build-manifest.json`. The source-host proof is reproducible byte-for-byte in its two builds; that claim does not establish byte reproducibility for the new interactive host.

`app_package.py` prepares a complete signed staging bundle, private audio libraries and native launcher. It verifies normal and experimental argument routing, 120 scripted frames of damage/repair/reset, and framebuffer damage/repair. It preserves the normal game/mod executable bytes, gives copied dependency libraries local ad-hoc signatures and changes `CFBundleExecutable` to the selector. The code signature and dependency signatures therefore differ from the original app.

Install only after staging/review and with the game closed:

```sh
python3 tools/host/app_package.py --install
```

The original complete app is renamed beside the installed app to `Grand Theft Auto Vice City.before-physics-<timestamp>.app`. A failed installed validation restores it. `package-manifest.json` records the exact backup path, original/installed fingerprints and staged payload hashes. The current implementation uses local directory renames on the same filesystem; installation across filesystems is not supported.

To restore the original after quitting either mode:

```sh
python3 tools/host/app_package.py --rollback
```

Rollback preserves the experimental app under another timestamped path and restores the original bundle. It refuses changed app/backup fingerprints rather than overwriting them. The record is for one installation; use a new work directory/package for another installation.

## Evidence and limits

Retained local artifacts live under `build/installed-wing/`: `lab-debug.log` (initial contact-bounds failure), `lab-smoke.log`, `host-query.log`, frame captures, build/package manifests, project/asset test logs and installed smoke captures. The native host query proof retains its positive 65-check run and failing controls in `build/host-source/reviewed/`.

The smoke run is a hidden, unpaced, isolated OpenGL scene. Frame timing includes CPU submission/presentation; query timing covers the four native probes. Neither is a measurement of the normal game's frame rate, GPU completion or a custom solver step. The wing COL uses native compressed vertices (1/128 coordinate precision), a stationary sphere contact probe and a predetermined topology change. The wall is a visual marker, not an authoritative building collider. The actors do not advance under game or custom flight physics. Motion ownership, dynamic building contacts, solver/airframe/aerodynamic coupling, save/stream/mission integration and wider-world lifecycle remain open in the controlled-slice plan. Existing physics tests and numerical limitations are unchanged.

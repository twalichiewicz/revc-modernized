# Installed interactive wing test implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans inline. One fresh final reviewer; retain existing physics tests and earlier slice plans.

**Goal:** Give the user an interactive, visibly inspectable wing/building geometry test launched from `/Applications/Grand Theft Auto Vice City.app`.

**Architecture:** Keep the current macVC executable/plugin as the normal game. Bundle the pinned source host as an explicit experimental mode, with a small launcher selecting it through `--physics-test` or an Option-launch chooser. Render and native collision queries consume the same wing revision; keyboard controls damage/repair/reset the wing and orbit the scene. Installation preserves a complete rollback app and validates the installed mode before completion.

**Tech Stack:** Existing pinned ARM64 source host/librw/GLFW, C++17, Cocoa launcher, Python packaging. No solver expansion.

**Spec:** Existing world-physics design and controlled aircraft/building slice; latest user explicitly authorizes development and installation into the named app. This supersedes the previous milestone's no-install constraint.

## Global constraints

- Keep normal gameplay on the existing Metal binary/mod; never load the fixed-address plugin into the different source host.
- Experimental mode is an interactive geometry/contact-query slice, not accepted flight dynamics, force-derived fracture or whole-world migration.
- Preserve all existing tests and numerical limitations. No new physics solver.
- Use user-owned assets read-only; generated mesh data remains in local build/app resources.
- Prepare and review a complete staging bundle before replacing the installed path. Preserve the original app intact for rollback; do not install while either mode is running.

## Review focus

- Rendered damage and contact/LOS/vertical surfaces disagreeing or leaking across instances.
- Stale render/COL pointers after repeated damage/repair/reset and shutdown.
- Launcher losing normal-game arguments or applying the binary plugin to the source host.
- Packaging using repository/Homebrew paths instead of the bundled resources/libraries.
- Partial install or failure removing the user's only working app; misleading full-physics/performance claims.

### Task 1: Interactive native geometry scene

**Files:** `tools/host/WingLab.cpp`, asset export/build helpers and retained source-host entry/CMake patch.

- [x] Implement the scene using the existing native query seam and RenderWare geometry. Show the actual aircraft mesh, highlighted wing, a wall and probe results; provide damage, repair, reset, orbit and exit controls.
- [x] Script the same controls in a smoke mode; assert render/COL face counts, query agreement, untouched control, revisions and teardown. Retain existing65-check host test and its failing controls.
- [x] Capture/view real framebuffer output and record per-frame/query costs for this isolated mode, explicitly distinct from full game/solver performance.

### Task 2: Reversible app package

**Files:** `tools/host/launcher.mm`, packaging/install helper, meaningful temporary-bundle tests, reproduction/run note.

- [x] Test normal-game routing, test-mode routing, missing experimental resources and a failed staged install/rollback before implementing installation.
- [x] Build/sign a launcher, bundle source host plus private audio libraries/assets, patch its library paths, and prepare a complete staging app. Keep normal game binary/plugin bytes identical.
- [ ] Fresh review and one tested material fix pass. Run existing project/native/physics checks appropriate to this isolated addition.

### Task 3: Authorized install and installed verification

- [ ] Verify the game is closed and target identity is still the packaged baseline. Rename the original app to a timestamped rollback path, install the staged app atomically, and restore the backup on installation failure.
- [ ] Verify signatures, installed routing and installed experimental smoke test, including screenshot/metrics/logs from the installed path. Confirm normal game binary/plugin hashes and provide exact launch/rollback instructions.
- [ ] Keep the broader controlled physics island open until motion ownership, solver/geometry/aero coupling and dynamic wing/building impact are accepted.

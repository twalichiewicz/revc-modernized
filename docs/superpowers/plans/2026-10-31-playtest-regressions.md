# Installed playtest regressions implementation plan

> Continue the approved feedback spec and explicit playtest corrections. Use the systematic debugging,
> TDD and parallel-agent workflows. Prior Proceed authorization remains in force; do not repeat design
> approval. Root owns shared integration and all game control/installation.

**Goal:** Correct the concrete failures reported after the 73-test installed build, retain the user's
confirmed improvements, and verify the actual native call paths and source assets.

**Architecture:** Diagnose the failing boundary using native/asset fixtures before changing rules.
Map geometry comes from the actual model/placement data; physical and visual geometry must not be
confused. Native AI, animation, rendering and material state retain ownership outside the corrected
scope. Log inspection establishes activity, while observed gameplay establishes acceptance.

**Tech stack:** Existing C++17/arm64 macVC 1.0.5217 plugin, native binary, RenderWare assets, Makefile.

**Spec:** `../specs/2026-10-31-feedback-and-npc-reactivity-design.md` and the latest playtest message.

## Constraints and review focus

- Preserve the dirty checkout, installed binaries/profiles and saves. Snapshot marker:
  `build/feedback-session/playtest-regressions-session.txt`. No commits, pushes or GitHub writes.
- No worker drives/restarts/installs the game or edits user preferences/saves. Root coordinates any
  live checks with the user's play session. Read-only log, source, binary and asset analysis is allowed.
- Root alone edits shared Makefile/config/game/minimap/hook files and this ledger. Workers report
  interfaces, new sources/tests, exact hook signatures and required shared changes.
- Preserve crosshair settings, cabin brake-light fix, improved steering grips, radio-wheel look lock,
  vertical look, running shotgun clearance and window-mode persistence, as confirmed by the user.
- Test native ownership/caller order, alternate models/outfits, level/area changes, world streaming,
  mission/cutscene actors, weapon aim states and restored render state.

## 1. Camera, weapon posing and first-person feedback

**Owner/files:** camera specialist: `src/aimcam.*`, `src/camera.*`, `src/fpfeedback.*`, `src/fpgun.*`,
`src/fpwalk.*`, `src/playerreact*` only if necessary; existing camera/aim/feedback/viewmodel tests.
The camera owner also owns damage/HUD integration in `src/player.cpp` and `src/hud.*` for this pass;
root retains `src/minimap.cpp` and other shared integration files.

- [x] Reproduce ADS+walking dropping the offhand at the native animation/pose seam, then fix it.
- [x] Reproduce absent damage overlay through accepted native damage, update and actual draw chain;
  correct the broken boundary rather than testing only the overlay math.
- [ ] Reproduce flipped-car whiteout; distinguish camera placement, interior material state, native
  post-effects and exposure. Fix the identified source while retaining ordinary lighting.
- [x] Inspect the combat-shotgun asset mismatch and report whether selection or asset geometry is
  wrong. Coordinate any asset ownership before editing the gun generator/model.
- [x] Verify regressions, targeted sanitizers and normal aim/reload/death/seat transitions.

## 2. Helicopter light and building impacts

**Owner/files:** aircraft specialist: `src/heli.*`, `src/helizones.*`, `src/aircraft.*`, related tests.

- [x] Reproduce every actual emitted spotlight/corona/beam source against the added lamp geometry;
  trace missing native callers or stale transforms and fix the resulting origin mismatch.
- [x] Reproduce full-speed tall-building contact through native navigation/physics order. Provide
  avoidance with body/rotor clearance and real collision/destruction for an unavoidable hard impact.
- [x] Inspect existing patrol/station logs read-only and report activity versus missing evidence;
  coordinate findings with the NPC owner without editing population/police files.
- [x] Run native-hook/geometry/transition regressions and sanitizers; report live acceptance needed.

## 3. NPC threat responses and population

**Owner/files:** NPC specialist: `src/drivers.*`, `src/intimidation.*`, `src/population.*`,
`src/populationmodel.*`, `src/populationrules.h`, `src/traffic.*`, `src/police.*` as needed;
existing driver/population tests and new focused integration fixtures. Root owns shared declarations.

- [x] Reproduce aimed-at pedestrians not reacting. Add visible hands-up/flee/fight according to
  native temperament, proximity and credible line of sight; preserve script/mission ownership.
- [x] Reproduce civilian drivers always exiting and police drivers doing nothing. Use distinct
  native civilian/police responses, including armed fight and vehicle ramming where appropriate.
- [ ] Replay a driving route across the virtual population/materialization seam. Diagnose sparse
  streets, stale reservations, streaming/pool/model gates and police/civilian budget interaction.
- [x] Inspect patrol/station/replenishment logs and add bounded useful lifecycle counters where
  activity cannot currently be distinguished from declared budgets.
- [x] Run regression fixtures, generation/mission/area/cooldown checks and sanitizers.

## 4. Minimap geometry and estate lighting

**Owner/files:** root: `tools/bake/*`, `tools/preview/*`, `src/mapdata.*`, `src/scene.*`,
`src/grade.*`, `src/shaders.*`, relevant geometry/lighting tests and shared integration files.

- [x] Compare bridge model placements and real visible/collision triangles to the map bake/preview;
  reproduce missing or distorted bridge decks, supports and building footprints before fixing.
- [x] Use named/model-specific house/building examples; preserve actual roof shapes and placement.
  User clarification: every bridge is inaccurate; the house immediately outside Vercetti Estate
  to the right appears as three rectangles despite a mansion form with pitched roofs and eaves.
- [ ] Inspect estate visual materials/prelighting, native lightmaps and shadow geometry. Reproduce
  the intense interior contrast and front-door removed-pillar shadow at the responsible boundary.
- [ ] Correct source lighting/shadow data or integration with area/material scoping; do not claim
  a post-process contrast adjustment fixes baked or phantom geometry.
- [ ] Review another interior/exterior plus day/night and retain valid shadow sources.

## 5. Integrate, review and verify

- [x] Review each independently tested change and integrate declared sources/hooks/config carefully.
- [x] Run the full suite and production build after all owners freeze; run focused sanitizers and
  independent review of substantive changes against the latest playtest requirements.
- [ ] When the user is finished, back up current state, install, rebuild exact-UUID diagnostics, and
  run controlled native scenes without overwriting saves or mixing user/automation controls.
- [ ] Update per-item acceptance, installed/source versions, log evidence and remaining limits.

## Installed implementation checkpoint — 2026-10-31, 20:47 PDT

Installed library UUID `B46E5CD0-4498-3611-B91F-46F17B5597CA`, 89 hooks / 6 skips.
Fresh `make -j4 test` passes all 81 default executables; `make test-game-assets` passes
both actual-game-asset executables. Production build passes without compiler warnings.
Each independent owner has frozen production sources and received an independent review;
map support clipping, day/night duplicates, shade materials, native IPL transforms and
shared bridge elevation findings were reproduced and corrected before installation.
Evidence lives under `build/feedback-session/` and the current session marker above.

- **Map:** bake now reads native visible DFF meshes instead of collision roofs for 626 placed
  models, including 28 bridge parts: 552,903 triangles, zero collision fallback. The native
  mansion examples retain pitched roofs/eaves; bridge groups share their street reference,
  clip at native water height 6 and select one timed day variant. Native GPU/frame cost and
  installed camera appearances remain to check. CPU-only map sweep worst case was 3.4 ms;
  that measurement excludes native Im2D/GPU cost.
- **Pose/feedback:** actual native aim-pose fixture holds chest/clavicles/arms in one aim frame
  while retaining leg animation. Health and armour loss now reach an overlay drawn after
  native HUD/scopes. SPAS geometry was compared with the native DFF and rebuilt with folded
  stock/perforated shroud and an open ADS channel. Rolled-cabin placement adds bounded terrain
  probes; native flipped-car whiteout is still an acceptance item, not a proven lighting fix.
- **Helicopter:** native RT shadow renderer had another independent belly candidate source;
  the verified optimized one-argument emitter now follows the real lamp. Predictive clearance
  and post-native escape ownership are covered by actual hook fixtures; unavoidable hard
  impacts enter native breakup. Live light alignment, building avoidance and cost remain.
- **Estate:** actual floor/wall/front-step assets contain near-black baked vertex lighting
  and no normals. Six trusted model-table entries receive scoped normals and native lighting
  evaluation, with geometry/GPU dirty state restored. Peer review caught and fixed an invalid
  mesh cache path. Live interior appearance and phantom pillar attribution remain unaccepted.
- **NPC:** native threat perception now includes pedestrians and law drivers; civilian response
  branches by fear/temper/proximity, and police threats preserve native wanted/fight/ram logic.
  Civilian model requests/spawn opportunities no longer lose to police demand every tick, and
  seated traffic occupants no longer satisfy the street-walker demand. New bounded logs
  distinguish walkers, occupants, model/pool gates, patrol activity and station completion.
  Route and native response acceptance remain to check.

The game was closed before installation. A fresh pre-install manifest backs up binaries, map,
INI files, log and both saves; installed library bytes match the fresh build. Both INIs and saves
match the fresh pre-install hashes immediately after installation. No save write was requested.
Diagnostic player-damage and current-car roll setup commands are separate from production;
parser/action/native-symbol checks pass and exact-UUID diagnostics are rebuilt before launch.

## Native check boundary after installation

The installed executable launched, loaded the existing estate save and loaded the new 626-model
map. Initial snapshot: `playtest-native/initial.{json,png}` in the session directory. Exact plugin
UUID telemetry was enabled. Before any scene mutation, Tommy left the estate and entered a car
without matching automation. The user confirmed active play, so root stopped automated input.
No playerhit, carroll, teleport, wanted, camera, clock/weather or owned-NPC command ran this launch.
No restoration/reload/quit was imposed on the user's session. Both saves still match the fresh
pre-install manifest. Native visual/route/performance acceptance remains open; current log activity
is observed gameplay rather than a controlled route. Station reserves initialize 4 per station and
patrol sample positions change; no replacement event was provoked in this launch.

Independent diagnostic review reproduced native helicopters sharing CAutomobile type0. The offline
roll helper now explicitly rejects game::IsAircraft, with Hunter155/Maverick217 regressions. That
setup tooling change does not modify the production library or the running user's game state.

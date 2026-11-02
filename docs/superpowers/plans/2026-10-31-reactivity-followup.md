# Reactivity follow-up implementation plan

> Continue the approved feedback design and the user's explicit playtest corrections. Independent
> solver, native-adapter and player-response owners may work in parallel; root owns impact capture,
> shared integration, backups, installation and live-game automation. Do not repeat design approval.

**Goal:** Stable ground/head constraints, supported living reactions, Tommy's protective contacts,
NPC obstacle separation, and spatially accurate weapon impulses.

**Architecture:** Keep the articulated solver, but give supported contact response its own semantics
instead of feeding every bump into damage-style impulses. Native event capture supplies actual
contact/ray data; player contact posing stays separate from the NPC controller. A final body pose
must satisfy world contact and anatomy together.

**Tech stack:** Existing C++17/arm64 macVC 1.0.5217 plugin and native hook fixtures.

**Spec:** `../specs/2026-10-31-feedback-and-npc-reactivity-design.md`, follow-up playtest requirements.

## Constraints and review focus

- Preserve all existing dirty work; this turn's source/profile/binary snapshot is recorded in
  `build/feedback-session/reactivity-followup-session.txt`.
- No commits, pushes, issue writes or save writes. Keep native health/armour authoritative.
- No worker edits another owner's files. Notify root of ABI, new-source and hook requirements.
- Root alone drives/restarts/installs the game. The user confirmed active play during native checks. Stop automation, inputs, restarts, installation
  and profile restoration until the user finishes; source/offline checks may continue.
- Exercise final contact after constraint projection, small supported bumps versus genuine loss of
  support, native wall/car separation, nested/stale bullet contexts, and player pose-owner transitions.

## 1. Solver stability and supported resistance

**Owner/files:** solver specialist: `src/reactphysics.*`, `src/reactself.*`, `src/reactrig.*`,
`src/reactcontact.*` and their existing tests. Root reviews API changes with adapter owner.

- [x] Reproduce ground loss and head folding with captured-rig/native-contact fixtures; record RED.
- [x] Fix final world/anatomy consistency and measured neck/head pose constraints at the failing seam.
- [x] Reproduce mild foot/car bumps collapsing a grounded survivor; introduce supported resistance,
  protective targets and corrective support, with stronger repeated or unsupported impacts able to fall.
- [x] Preserve directional local weapon impulse support; agree exact solver interface with root.
- [x] Run relevant regressions and sanitizers, report bounds and native acceptance scenarios.

## 2. Native collision ownership and obstacle escape

**Owner/files:** native-adapter specialist: `src/reactions.*`, `tests/test_reactionhook.cpp`.

- [x] Reproduce an NPC running into a wall/car side through the actual native-hook seam.
- [x] Trace original controller velocity/root updates, collision suppression and physical publication.
- [x] Correct ownership/separation so supported survivors stop, slide or regain support without a
  permanent overlap. Preserve legitimate native collision results, health and protected actors.
- [x] Route passive contacts through the solver's supported response and accepted shots through root's
  verified impact context. Forward player contacts to the dedicated player response interface.
- [x] Verify generation/load/recovery exclusions and camera/rig/root coherence; run hook sanitizers.

## 3. Tommy's protective contact response

**Owner/files:** player specialist: new `src/playerreact.*` and focused rules/tests;
`src/player.cpp` for player-specific control/pose integration. Root owns common build/hook files.

- [x] Reproduce the absent player response using a real native-contact/control/pose fixture.
- [x] Add bounded contact-derived flinch/brace/reach while preserving player movement and weapons.
- [x] Coordinate `playerreact::Contact` input with the native-adapter owner; no fake health damage.
- [x] Cover first/third person, swimming/climbing/driving, death/load/outfit reset and pose restoration.
- [x] Report exact source/test integration, run focused verification and sanitizers where useful.

## 4. Accepted weapon impact position and direction

**Owner/files:** root: new `src/shotimpact.*` and impact mapping helper/test as needed;
`src/hooks.*`, `src/heli.cpp`, `src/minimap.cpp`, `Makefile`, patcher if required.

- [x] Trace exact native `DoBulletImpact` → accepted `InflictDamage` call order against the binary.
- [x] Build failing tests for real foot point/ray direction, sequential/nested impacts, rejected damage,
  stale generations and independent helicopter feature settings.
- [x] Capture scoped native impact data with correct hook chaining and no duplicate damage/event.
- [x] Feed local segment/joint forces to the solver; keep explicit fallback only for absent events.
- [ ] Verify installed shots in multiple directions and intact helicopter processing.

## 5. Integrate and replay

- [x] Review each change and run the full test suite/build, relevant sanitizers and independent review.
- [x] Confirm fresh profile/save backup, install final build only after closing the test process, and
  rebuild diagnostic sidecar for the exact ABI/UUID.
- [ ] Replay ground/head, mild foot/car bumps with recovery, genuine trips/falls, wall/car exit escape,
  Tommy's protective contacts, and localized directional weapon impacts. Record visual limits honestly.
- [ ] Restore test settings and confirm save preservation; update the verification ledger.

## 6. Latest playtest: shoulders and window-mode memory

- [x] Reproduce backward shoulder extension on the measured native rig before changing the pose.
- [x] Constrain protective wrist/elbow targets in the current torso frame and fix mirrored/anchored
  first-person shoulders. 864 rig scenarios plus 1,632 actual firearm preparation fixtures pass
  across all 17 assets, hip/aim, contact strengths and directions. Independent review reproduced
  38.014 mm shoulder drift with a strong rear hit and the stubby shotgun; the final common weapon
  frame is now bounded by both fixed shoulder reach envelopes while retaining hand-to-gun grips.
- [x] Diagnose the Mac fullscreen flag separately from GLFW's `VideoMode.Windowed` preference.
  Native `wantNativeFullScreen` starts at 1 and was never saved; exiting the fullscreen Space was lost.
- [x] Add `VideoMode.NativeFullscreen` via the native INI map/writer, restore before initial window
  creation, observe completed Mac fullscreen transitions, and suppress renderer-recreation/shutdown
  exits. Preserve the native default for absent/invalid keys and leave GLFW mode selection unchanged.
- [x] Add an offline handler/notification regression with RED then GREEN, including opposite startup
  choices, renderer replacement, stale/other windows, failed native calls and both quit paths.
- [x] Preview patching the original executable offline: 88 hooks and 6 skips fit. No app file changed.
- [x] Complete independent review and final source/build verification after all changes are frozen.
  All 73 test executables pass; full library build and focused ASan/UBSan runs pass. Review cleared
  the body/anatomy, firearm reach, window lifecycle and native ABI integration.
- [x] Install the shoulder/window fixes and verify real window/fullscreen quit/relaunch cycles.
  The user finished playing and authorized checks. The final build is installed with UUID
  `36AD7B2A-CA8E-31B8-97BB-87C51CDD78F0`, 88 hooks/6 skips. Native Cocoa style was fullscreen
  after a saved-fullscreen quit/relaunch, then windowed after a saved-windowed quit/relaunch;
  renderer `Windowed=1` remained unchanged. The user independently confirmed windowed persistence.
- [ ] Complete controlled native shoulder/fall captures when the user is finished again. The user
  resumed playing during the arm scene and confirmed control of the weapon/camera changes; those
  captures are not accepted as controlled arm-contact or NPC-ground/head tests. Input scripts are
  stopped. Current preferences/saves/binaries were backed up in
  `shoulder-window-before-20261101T020249Z/` beneath the follow-up session.

## Evidence and remaining live checks

The first follow-up build was installed with library UUID `221c3ed9-ed47-37a1-a783-1ad016291da5`,
85 hooks/6 skips and 72 passing C++ test executables. The session marker is
`build/feedback-session/reactivity-followup-session.txt`; it points to backups, diagnostics and captures.

- A controlled on-foot bump kept the NPC at full health, Balanced with balance=1 and a native-owned
  root, then returned to native control. Tommy visibly raised his arms; the user independently
  confirmed that behavior, then identified the shoulder-range problem addressed above.
- Actual native bullet hits localized force to the struck right foot in three incoming directions.
  The requested left-foot target could be intercepted by the nearer right leg; the real native hit
  point/piece correctly won. Opposite Y rays reversed the right-foot impulse. Native damage remained
  authoritative. Owned NPCs were removed before full-fall acceptance captures.
- Full-fall ground/head stability, moving-car resistance, cop escape after exiting cars, genuine
  trip behavior, crowded-scene performance and the newest arm appearance still need controlled live
  acceptance. Tests and query bounds do not establish their native visual/performance acceptance.
- The user confirmed that concurrent movement/weapon changes came from their own active play.
  Do not diagnose those actions as automation faults or restore older profile/save backups over
  subsequent user changes. Clock/weather diagnostic overrides from the earlier checks require
  cleanup when the user is finished; do not run the broad restore command during active play.
  The user subsequently closed the game, clearing those process-local overrides.

## Latest installation / acceptance record

`build/feedback-session/shoulder-window-tests-final.log` records 73 passing test executables;
`shoulder-window-build-final.log` and `shoulder-window-install.log` record the successful build and
88-hook/6-skip installation. The installed library matches the final build. The current two saves and
both INIs matched the fresh backup immediately after installation. Both save hashes still matched
when restart acceptance finished; no test command writes game saves.

`shoulder-window-qa/restart-verification.json` records actual Cocoa style masks 16399 (fullscreen)
and 15 (windowed) after separate graceful quit/relaunch cycles, with renderer `Windowed=1` in both.
The user subsequently confirmed: "the app correctly remembers that I left it as windowed".
An early capture-helper lookup ran before the native window existed; the helper was changed to wait
for a window and frontend readiness. This was a helper failure, not a game crash. One load-menu wait
needed the remaining confirmation keys before the HUD acknowledged the queued status command.

The user resumed playing during the next scene. No further control, camera, weapon, teleport or
restart commands are issued while they play. The game subsequently exited before a cleanup status request could acknowledge; no native
cleanup commands ran. Process-local clock/weather overrides end with that process, and the stale
read-only status request was cancelled. Evidence is in `shoulder-window-qa/runtime-cleanup.json`. Broader ground/head, car-contact and crowd scenarios
remain pending; do not mark them accepted from the uncompleted scene.

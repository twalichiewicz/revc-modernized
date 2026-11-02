# Learjet cabin, boarding and shadow implementation plan

> **For agentic workers:** Implement the independent geometry, boarding and shadow tasks in parallel under the existing execution authorization. Root owns integration, review, builds, runtime input, installation and GitHub. Preserve the dirty shared checkout; no commits or pushes.

**Goal:** Address the user's six Learjet presentation and boarding corrections plus persistent usable-aircraft spawners at all seven airport placements.

**Architecture:** Keep the native CPlane and original source assets. Revise the private model's geometry, use generation-owned native pedestrian animation for boarding, and trace the existing shadow consumer before adding a correction.

**Tech Stack:** C++17, arm64 macVC1.0.5217, RenderWare and original DFF/IFP/COL assets.

**Spec:** The user's current six-point feedback. The gear design is explicitly revised: keep original attached wheel covers, remove additional visible doors/panels and let gaps seal near completion of retraction. Door treads belong in the inside face of the hinged door. The rear cabin needs seats and the same floor level as the cockpit.

## Global constraints

- Native CPlane allocation is0x388 inside vehicle pool stride0x6f0. No Automobile-only field or helper may read beyond that allocation.
- Source static clumps/materials and donor Police Maverick remain immutable; private clones retain independent lifetime and rollback.
- Retain current public Pose/wheel ABI unless root coordinates a necessary change.
- Geometry owner communicates final door/tread route and floor coordinates to boarding owner. Cockpit and passenger floor must meet at the same level; do not raise the passenger floor to hide retracted wheels.
- Preserve collision, input, mission/cutscene and pool-generation ownership. Cancel interrupted boarding without leaving controls, animation callbacks or references owned by a departed actor.
- Root leaves the current player-controlled process untouched during source work. Installation uses a reviewed concrete payload and existing user authorization; check session availability before replacing a running build.

## Review focus

- Full closed/open door sweep: treads, jamb, hull, floor and Tommy's head/feet must clear.
- All-component two-sided depth conflicts, including original asset overlays and donor geometry, at deployed/intermediate/stowed gear states.
- Original attached gear covers remain; extra panels stay absent during ordinary deployment and travel, with flush seals only near final closure.
- Level cabin floor must not expose folded wheels; rear seat rows must clear shell, aisle and door route.
- Boarding abort/reentry, player death, mission takeover, blocked route, world restart, actor replacement and animation removal must remain safe.
- Shadows must reach a real native render/shadow consumer for the owned plane without unsafe car subclass reads or global lighting changes.

## Task 1: Door, gear and cabin geometry

**Files:** `src/airportjetmodel.cpp/.h`, `tests/test_airportjetmodel.cpp`; shared cabin metadata only if needed and coordinated.

- [x] Add real-asset failures for door/wall clipping, rejected extra panels, remaining visible depth conflicts, raised rear floor and missing rear seats. Preserve the rejected baseline evidence before editing.
- [x] Replace the rung/ladder overlay with integrated inner-door treads and verify the complete opening sweep.
- [x] Retain original wheel/fairing geometry, remove added deployed panel mechanisms and seal the actual openings flush only at the end of retraction.
- [x] Match passenger and cockpit floor levels and add fitted rear seats with an aisle and boarding clearance.
- [x] Verify all-component overlap, original source conservation, transparent window backing, full gear/door sweeps, clone cleanup and focused sanitizer checks.

## Task 2: Visible boarding

**Files:** `src/airportjets.cpp/.h`, `tests/test_airportjets.cpp`; a focused boarding module/test may be introduced if the native state machine warrants it.

- [x] Reproduce the current door-open-to-warp shortcut through the installed entry consumer.
- [x] Verify real native animation identifiers and lifetimes against the exact binary/assets. Show reach/open, stepping through the door, movement to the cockpit and sitting before native seat commitment.
- [x] Coordinate motion with actual animation progress and door geometry; preserve native generation and eligibility checks across every callback.
- [x] Cover blocked/cancelled entry, controls release, stale actors, mission takeover, rewind/restart and native ordinary-vehicle fallback. Run focused normal/sanitizer/native-layout checks.

## Task 3: Plane shadow

**Files:** Trace `src/planes.cpp`, `src/airportjets.cpp`, native CPlane/CShadows and RT consumers. A new focused module/test is preferred over unrelated geometry edits if a missing caller needs bridging.

- [x] Build a failing consumer proving where the owned jet drops out of the native shadow path.
- [x] Apply only the validated missing eligibility/submission correction, preserving ordinary planes and native zero/night/interior behavior.
- [x] Verify the actual native ABI and actor allocation bounds; distinguish static submission from visible runtime shadow acceptance.

## Task 4: Persistent usable-aircraft spawners

**Files:** Root integrates lifecycle changes in `src/airportjets.cpp` after boarding freeze, `tests/test_airportjetspawners.cpp`, and relevant asset consumers. A parallel worker prepares and tests an isolated private-copy patch without changing the shared boarding files.

- [x] Reproduce leave-airport/return through the actual Frame/native world callbacks: the old static model currently returns on Retire.
- [x] Claim all seven validated source636 placements as persistent anchors while Planes is enabled. Hide originals independently of actor streaming/obstruction; spawn usable jets when nearby and clear.
- [x] Retire distant unused actors without restoring static scenery, retain retryable anchors, and respawn on return. Protect occupied/foreign/recycled actors and validate anchor pool generation/transform.
- [x] Restore valid original scenery only for explicit feature disable or synchronous world cleanup; keep source/model/TXD ownership and world-list membership balanced.
- [x] Test blocked/pending loads, departure/return, damage/deletion, actor/anchor recycling, all seven placements, rewind and restart/shutdown without duplicate world removals/adds.

## Task 5: Integration and native checks

**Files:** Root-owned Makefile/minimap/patcher/runtime tools, this plan, existing acceptance ledgers and GitHub14.

- [x] Independently review frozen implementation and fix material findings.
- [x] Run the full warning-free default/asset/native build with stable source fingerprints.
- [x] Stage/sign an immutable payload and matching diagnostic sidecar, preserve settings/map/saves and install when the current play session is available.
- [ ] Capture closed/open door, Tommy boarding, rear cabin/floor, deployed/retracting/stowed gear and sunlit ground shadow in-game. Release controls and verify actual pause/quit rather than assuming Escape registered.
- [x] Update issue statuses with separate source, native and remaining user-acceptance evidence.


## Source review checkpoint

This is a GTA: Vice City mod; all Learjet references concern an in-game vehicle.
The seven-anchor lifecycle and native pre-world-shutdown restoration are independently reviewed.
Exact original world cleanup destroys all seven valid hidden sources after restoration; foreign
source generations/transforms are excluded, and model cleanup performs no stale source access.
The patch table now contains130 hooks and6 skips.

Boarding review found and corrected an uncached skeleton-prepare lifetime defect. The guarded
Prepare overload preserves ordinary callers and passed640 cold-cache interruption cases plus
independent normal/sanitizer review. Interrupted owned entry now checks the real three-sphere
pedestrian shape and escape path before restoring collision, preventing self-query rejection;
mission and foreign-animation owners retain their positions. Native-data and actual-COL tests
cover source constants, missing animation, timeout, feature disable, overhead obstruction and
ownership changes during clearance. These are source/consumer results; installed game appearance
and airport-return checks remain pending.


## Native follow-up: crouch walking inside the cabin

Installed8A41166D-C2F7-332C-B9F2-F491D6E3160F passed the full109 default programs,
22 asset/native C++ programs and6 patch-capacity cases with525 stable fingerprints.
All final geometry, guarded preparation and recovery sanitizer checks passed.
The controlled game tour confirmed usable native Learjets at all seven placements and a new
usable vehicle generation after leaving and returning. Native boarding did not reach driver
binding during the35-second check and recovered outside. The user observed successful door
opening/entry followed by sliding inside, and explicitly requests crouch walking to the cockpit
and sitting. This supersedes source-only acceptance of the interior entry route.

- [x] Preserve the tread/doorway entry, then transition to a genuine crouch walking gait only once
  inside. Fit the actual cabin height, animate planted/swing feet in proportion to world travel,
  and blend from the stopped gait into the final seated pose before native driver binding.
- [x] Retain the guarded first-use body preparation and exact temporary-key restoration for all
  newly touched joints/root translation; preserve mission/foreign ownership and safe interruption.
- [x] Add bounded stage/abort-reason evidence to diagnose the native completion failure, then
  verify actual walking cycles, foot planting, head/roof clearance and seated transition using the
  original player rig/animations and an installed game check.
- [ ] Independently review, rebuild/sign with stable fingerprints, preserve saves/map/settings,
  install when available, and complete the remaining native appearance/gear/shadow checks.

## Installed0911 native result

The crouch/continuous-seat correction is installed and independently reviewed. Full combined
Werror verification passed109 default programs,23 asset/native C++ programs plus6 capacity
cases, with526 stable fingerprints.18 focused normal/sanitizer runs pass. Original asset
head-sphere clearance is48mm to the source roof and42.5mm to the generated lining.

The first controlled native sequence completed entry in11.799s and re-entry in11.900s; both
bound the real driver. Throttle travel5.675m, stopped brake, normal exit and re-entry passed
with player/jet health100/1000 unchanged. Wider interior captures show lowered bent-knee
walking before sitting. A later physical-controller overlap invalidated only the second run's
extra ground-controls result; its cleanup wrongly closed the app. The helper now stops keyboard
and quit actions on hardware-axis overlap. Map and both save hashes match the install backup.

Remaining acceptance: cabin/exterior visual polish and a complete installed gear-retraction/
approach presentation. These remain open in issue14; the current boarding defect is addressed.

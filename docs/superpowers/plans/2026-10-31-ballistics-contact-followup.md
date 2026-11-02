# Ballistics and remaining playtest failures implementation plan

> Continue the approved feedback spec and user's latest corrections. Existing Proceed authorization
> covers implementation. Use systematic debugging, test-first regressions and independent parallel
> owners. Root owns GitHub writes, shared integration and all game control/install operations.

**Goal:** Track the latest accepted/rejected behavior accurately and correct visible-target bullet
cutoff, oversized vehicle shot collisions, helicopter lamp placement/damage, estate shadows and
over-sensitive pedestrian contacts at their actual native boundaries.

**Architecture:** Preserve native damage, mission and simulation ownership. Reproduce the complete
native caller/hook/asset seam before changing behavior; shallow geometry/rule fixtures alone do not
establish a visual fix. Source and offline tests remain distinct from installed-game acceptance.

**Tech Stack:** C++17 arm64 macVC 1.0.5217, existing plugin/hooks/RenderWare assets, Makefile and GitHub.

**Spec:** `../specs/2026-10-31-feedback-and-npc-reactivity-design.md`, plus latest user playtest report.

## Constraints and review focus

- Preserve the dirty checkout. Baseline marker: `build/feedback-session/ballistics-contact-session.txt`.
- User explicitly authorizes creating/updating GitHub tickets in this turn; no commits/pushes requested.
- User is actively playing. No worker sends game input, installs, launches, closes, writes saves or
  changes live state. Root keeps automated input stopped until an available test window.
- Root alone edits shared Makefile, patcher, game bindings, minimap/player integration and docs.
- Preserve confirmed map, walking ADS, visible damage overlay and improved combat shotgun, plus
  earlier confirmed crosshairs, cabin lights/grips/radio lock, vertical look and window persistence.
- Review long/near rays, wall/vehicle occlusion, glass/open doors, pedestrians behind cars, actual
  RenderWare callback reachability, low-speed versus repeated/sprint contacts, reciprocal player
  response, alternate heli slots/generations and lamp-break persistence.

## 1. GitHub reconciliation — root

- [x] Read all seven existing issues and search for duplicates of the two newly reported symptoms.
- [x] Preserve original issue content; add current acceptance/status tables and outstanding checks.
- [x] File separate bullet-range and vehicle-hitbox issues if absent, including observable repro and
  acceptance criteria. Keep unresolved aggregate issues open and link relevant tracking.
- [x] Verify the remote updates and retain an audit snapshot of previous/published bodies locally.

## 2. Helicopter lamp — aircraft owner

**Files:** `src/heli.*`, `src/aircraft.*`, related light/hook tests; root integrates shared hooks.

- [x] Reproduce source origin versus actual drawn housing/lens and the exact native RT/render order.
- [x] Reproduce the real shot entry/nearest-hit/can region; verify broken state suppresses every
  actual light producer and survives both heli slots, animation, updates and valid generation changes.
- [x] Fix only proven boundary failures; run focused red/green and sanitizer checks; request review.

## 3. Estate shadows — estate owner

**Files:** `src/estatelighting.*`, related tests. Read other renderer sources but coordinate edits.

- [x] Establish whether the installed correction reaches actual native draws and which source still
  supplies intense shadows: callback ownership, scope/material data, native RT or baked textures.
- [x] Reproduce the user's unchanged symptom at that boundary. Reassess the approach if data shows
  previous corrections do not reach the responsible producer; do not add another contrast tweak.
- [x] Correct a proven cause with actual caller/asset regression, restore GPU/native state and review.
- [ ] Keep phantom-column and final interior visual acceptance open until observed.

## 4. Pedestrian walking/sprinting contact — NPC owner

**Files:** `src/reactcontact.*`, `src/reactimpact.*`, `src/reactions.*`, `src/reactphysics.*`,
`src/playerreact*` only if required, corresponding tests. Root owns `src/player.cpp`.

- [x] Reproduce a normal walking contact causing immediate or accumulated fall through the real
  native contact/damage/reaction path, contrasting sprint, repeated shove and obstruction trip.
- [x] Keep ordinary walking bumps balanced with resistance/recovery; meaningful sprint body impacts
  should affect both Tommy and the NPC using appropriate native player fall ownership.
- [x] Preserve local impacts, legitimate trips, weapons/damage, ground/head constraints and mission
  exclusions. Run captured-rig/native-hook regressions and sanitizers, then request review.

## 5. Bullet cutoff and vehicle shot collision — root

**Files:** inspect native CWeapon firing/range, CWorld LOS and existing camera/shot hooks first;
create a focused `src/ballistics.*` only if a verified native correction requires its own module.

- [x] Capture actual installed weapon ranges and native firing ray construction, actor collision
  eligibility and line-of-sight behavior; build a failing exact-boundary regression for visible targets.
- [x] Compare actual vehicle COL broad/narrow hit geometry to occupant/body triangles, open windows,
  glass and nearby empty space. Preserve real metal cover and first-hit order.
- [x] Fix verified causes without granting through-wall hits or taking over unrelated world collision.
- [x] Run focused ray/native-hook/asset tests and sanitizers, then independent review.

## 6. Integrate and acceptance — root

- [x] Integrate frozen owners, review all changes and run full tests, actual-asset tests and build.
- [x] Back up fresh user state immediately before installation, only when the game is closed.
- [ ] Rebuild exact-UUID diagnostics and run controlled native checks when the user's session allows.
- [x] Reconcile local/remote statuses with evidence; distinguish implemented, confirmed and open.

## Installed core and rejected lighting — 2026-10-31 follow-up

Core plugin `0E855926-330B-3A2C-B260-F1815841377B` was installed with 99 hooks / 6 skips after
85 default tests, three actual-asset modes, warning-free build and independent review. It includes
shot-local native range correction (including deferred sniper deadlines), current rendered car
mesh/occupant collision, walking SetFall ownership, single-hit lamp destruction/private RT caller
correction and an Army Hunter pilot using the native cannon. These are implemented and installed;
new native acceptance remains open.

Fresh state was backed up before installation. Installed executable/library bytes match the signed
immutable `regression-payload` under this session; both INIs and saves were unchanged at installation.
Code signatures verify with `--ignore-resources` for the executables inside the existing app bundle.

The player rejected the new estate appearance as **extreme flat/no lighting**. Actual loaded native
structural draws now execute the relighting path (`flags=0xf`, ambient .216, no RenderWare local
lights). The separate native CPointLights room producer was absent from its replacement. Matching
RT-on/off captures also show a baked floor blob only with RT off, so those captures do not establish
RT as the cause of dark walls or prove the front-column attribution. A new actual-asset regression
reproduces room illumination changing when the camera alone crosses native point-light culling.
The correction in progress consumes existing positive-radius authored room lights; this is not an
accepted estate fix. GitHub #7 records the regression and stays open.

GitHub issue bodies #1, #4–9 were reconciled with the player's accepted/improved/open reports; new
issues #10–14 track range, vehicle collision, Hunter crew and helicopter/plane flight. Original
content and readback audit snapshots are preserved in this session. All unresolved issues stay open.

The old launch helper could start New Game and its opening cutscene; that movement was incorrectly
attributed to possible user input. It now stops at the frontend and requires visually checked menu
navigation. A later movement change was separately confirmed as actual user play. The user explicitly
said **leave the controls to me**: automated input, reload, restore, quit and installation are stopped.
Read-only native source/log and offline testing continue. The current process uses a native-only
sidecar, with hidden plugin telemetry disabled; area-0 vehicle point-light telemetry is not evidence
of estate room lighting.

Aircraft flight integration continues in `2026-10-31-aircraft-flight-followup.md`; those newer modules
are not in the running core build. Final matched estate, range, vehicle, bump/sprint, lamp and military
combat checks await a free test window.

## Staged estate and walking follow-up

Candidate `D7025565-6BED-3A1B-B8D7-0BC7DD0BDEF0` (109 hooks/6 skips) is staged, not installed.
Its root verification passes90default programs, six actual-asset modes and warning-free production
build with stable source hashes. It adds the camera-independent nine authored room lights for the
four estate interior receivers, with original native prelights retained if the current source cannot
be validated. Moving only the camera now leaves real-asset diffuse lighting unchanged; this does not
establish native appearance. GPU/native global light state is restored/preserved in focused fixtures.

The additional first-person walk integration regression reproduces native Walk2 reaching2.90m/s
under the old run curve, crossing the contact walking threshold. The narrow correction preserves the
actual native walking pace and incoming momentum; run/sprint/ADS/crouch/scope/air behavior retains its
ownership. Native movement/contact, invalid timestep and sanitizer regressions pass. Both new source
corrections passed independent review with fresh sanitizers; installed-game acceptance remains open. User controls remain
exclusive; the installed app still byte-matches core0E855926.

Final independent estate/FP walking review is clear of unresolved Important/Critical findings; fresh
ASan/UBSan suites and frozen hashes pass. Evidence: current session
`estate-fpwalk-independent-review.txt`. This is source verification; native acceptance remains open.

## D702 installed and new hallway evidence

The reviewed D702 payload is now installed; receipt and fresh backup are under the current session.
Native office/main-room draws record room_points9. The player sees the new lighting but has not
accepted the whole estate appearance. The player resumed control before matched scene checks.

Actual side-hall assets2471/2472 retain bright baked prelights while four selected room receivers are
relit. Matching carpet vertices0.00011m apart have identical texture/normal/original255prelights but
current replacement55 versus retained255, creating a200/255 seam. Adding hall receivers alone would
leave all45upstairs carpet vertices ambient-flat: the nine physical lamps do not cover them. The
known-RED actual-asset diagnostic and source-only expansion counterexample are documented in
`build/feedback-session/estate-hallway-findings.txt`. Production is unchanged; a consistent treatment
of static indirect/window/ceiling illumination needs architectural review, rather than a brightness
multiplier, invented point lamps or blind scope expansion. Estate visual acceptance stays open.

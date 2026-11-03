# Car Handling and Weight (#37, part 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** GTA IV's sense of weight in Vice City's cars, in the physics itself:
- trucks and buses weigh what such vehicles weigh;
- cars accelerate more slowly and top out lower;
- they turn in with inertia, slide progressively and take longer to stop;
- their bodies roll in corners, dive under braking and squat under power on softer springs.

In first person Tommy's view rolls with the car, while his head levels itself a little. Every car keeps its own character.

**Architecture:**
- **Handling retune:** the game turns each `handling.cfg` line into game units in `cHandlingDataMgr::ConvertDataToGameUnits`. That function also derives turn mass, buoyancy, the top speed (where drag meets the engine) and the gears.
  - The hook retunes a car's record in file units **before** the original runs, so the game derives all of these from the new values itself.
  - After the original, it raises turn mass.
  - It runs once per record, at load, before any car exists.
- **Physical lean:** softer springs with the ride height kept and the damping scaled to match. The game's own suspension then rolls and pitches the body, its occupants and the seat view with it. AI cars lean too.
- **Drive log:** while he drives, every 30 s, the log records the measured top speed, 0–100 km/h time, and body roll and pitch against the road. That's the evidence for tuning.
- **Head balance:** in the first-person seat view of a car, the view is turned back towards level by a fraction of its roll, after a short lag. Quick nose dips are levelled the same way; hills aren't.
- **Structure:** pure models (`handlingmodel`, `drivelog`, `headbalance`) hold every number and are unit-tested. Thin modules (`handling`, `camera.cpp`) bind them to the game.

**Tech Stack:** C++17 plugin (`libVCModernHUD.dylib`); hooks through `tools/patch_macvc.py` and `hooks::Chain`; Makefile tests.

**Spec:** `docs/superpowers/specs/2026-11-02-car-physics-design.md` (part 1).

**The user's direction, from the plan review:**
1. Top speed, acceleration and the rest are adjusted too.
2. Mass is raised by vehicle type.
3. The lean is physical.
4. The car leans in first person, with Tommy balancing his head slightly.
5. The new hook is fine.

## What the game does (macVC 1.0.5217 disassembly)

- **`LoadHandlingData`:**
  - It stores the file's engine acceleration ×0.4 at record +0x80. Everything else is stored as written: mass +0x04, dimensions +0x10, gears byte +0x7e, max velocity +0x84, brake +0x94, suspension +0xb0…+0xc4.
  - It writes the record's id at +0x00, then calls `ConvertDataToGameUnits(this, record)`. That is the only caller.
- **`ConvertDataToGameUnits`:**
  - `turnMass = (dimx² + dimy²) · mass / 12`, ×5 if < 10; `invMass = 1/mass`; collision damage ×2000/mass; `buoyancy = 100/percentSubmerged · 0.008 · mass`.
  - Engine acceleration ×0.0004, then ×0.5 (×0.25 for 4WD) per driven wheel; brake ×0.0004.
  - **Top speed:** it steps v down from max velocity ×(1/180) in steps of 0.01 until `v · (1 − 1/(1 + 0.5 · dimx · dimz / mass · v²)) ≤ accel/6`, where accel is the value ×0.0004 before the per-wheel split. It stores `fMaxVelocity = 1.2 v` (+0x84) and `v` (+0x88), sets the reverse speed (+0x8c), then calls `cTransmission::InitGearRatios`. With this `handling.cfg` (Cerbera's), max velocity is 300 for every car, so drag decides every top speed. For example, Infernus 196 km/h, Admiral 161, Mule 116, Bus 127.
- **`CAutomobile::CAutomobile`:** `m_fAirResistance = dimx · dimz / mass`. So `dimension[2]` is used only for drag and for the `NONPLAYER_STABILISER` flag (0x800), which no car in this file has.
- **Springs (`ApplySpringCollisionAlt`):**
  - Each wheel gives an impulse `0.008 · mass · step · force · (1 − ratio) · bias · 2` along the ground normal.
  - Spring ratios are at car +0x580 (4 floats); wheel colpoints are at +0x4e0, stride 0x28 (point +0x0, normal +0x10).
  - The spring length `upper − lower` is set per car in `SetupSuspensionLines`, when the car is made.
  - So a wheel's rest sag in metres is `(upper − lower)/(4 · force)` for an even bias, and it scales with 1/force.
- **Damping (`ApplySpringDampening`):** impulse `−damping · relative speed · mass · step · 0.53`, so the damping ratio goes as `damping · sqrt(spring length / force)`.
- **Turn mass:** it is a single number, used for yaw, roll and pitch alike.

## Global Constraints

- **Build:** macVC 1.0.5217 only; the plugin already refuses other builds.
- **Cars retuned:** the 71 cars listed in Task 1, each with a type. The RC Bandit, bikes, boats, the train and aircraft (including the Dodo, the Skimmer, helicopters and the airport jets) are untouched, byte for byte.
- **Each car keeps its character:** every factor is the same within a type. So within a type, the order by mass, speed, grip, braking and softness doesn't change. Gears, drive and engine type, steering lock, centre of mass, anti-dive, flags, lights and value are kept.
- **Factors by type (my call; tuned in play from the drive log):**

  | Type | mass | engine accel | top speed | spring stiffness |
  |---|---|---|---|---|
  | Sports | 1.00 | 0.85 | 0.92 | 0.65 |
  | Car | 1.00 | 0.85 | 0.90 | 0.55 |
  | Van | 1.20 | 0.85 | 0.90 | 0.60 |
  | OffRoad | 1.15 | 0.85 | 0.90 | 0.60 |
  | LightTruck | 1.50 | 0.85 | 0.90 | 0.70 |
  | HeavyTruck | 1.60 | 0.85 | 0.85 | 0.75 |
  | Tank | 1.00 | 1.00 | 0.60 | 1.00 |
  | Small | 1.00 | 0.85 | 0.60 | 0.80 |

- **Every type:**
  - turn mass ×1.25;
  - a third less grip is lost when sliding (`tractionLoss = 1 − 0.65 · (1 − tractionLoss)`);
  - peak grip ×0.97;
  - brakes ×0.88;
  - damping ratio ×0.85;
  - **ride height at rest unchanged.**
- **Top speed** is set through drag: `dimension[2] ×= accel · mass / top³`, so the game's own search lands at `top ×` the old speed.
- **Head balance:** level 35% of the view's roll (lag 0.2 s, at most 6°). Level 30% of quick pitch changes (those faster than 1.5 s; lag 0.15 s, at most 3°). Only in a car's seat view (not bikes, boats or aircraft); not while aiming a drive-by; not when the car is rolled over (up.z < 0.35). It eases in and out and never snaps.
- **Settings:** `CarHandling=1` (the retune; 0 = the game's handling, no hook installed) and `HeadBalance=1` (0 = the view rolls with the car fully).
- **Quality bar:** `make test` and `make test-game-assets` pass, `make all` has no warnings, and the patcher run succeeds with the new hook.

## Review Focus

1. **The handling loaded again** (anything re-running `LoadHandlingData`): each conversion starts from freshly parsed values, so the retune never compounds. Pinned in Task 2, `TestReconvertFromFresh`.
2. **Records the hook doesn't know** (bikes, boats, planes, the RC Bandit, an unknown id): passed through bit-identical. Pinned in Task 2, `TestOthersUntouched`.
3. **The softest and the heaviest cars** (Voodoo, golf cart, Coach, Rhino):
   - a wheel's rest sag stays under 55% of its travel;
   - it can still take at least 2.5× its static load before bottoming;
   - its ride height is unchanged to 1 mm.
   - Pinned in Task 1's asset test, `TestEveryCarsSuspension`.
4. **Looking left, right or behind in the seat view:** the levelling uses the view's own roll and pitch, so it levels whichever way he looks; a drive-by resets it. Pinned in Task 4, `TestLevelsAnyView` and `TestInactiveEasesOut`.
5. **A car upside down or on its side in first person:** no correction, and no flip. Pinned in Task 4, `TestRolledCarNoCorrection`.

---

### Task 1: The handling record and its retune (pure)

**Files:**
- Create: `src/handlingmodel.h`, `src/handlingmodel.cpp`
- Test: `tests/test_handlingmodel.cpp` (in `make test`); `tests/test_handling_assets.cpp` (in `make test-game-assets`); Makefile rules.

**Interfaces (produces, `namespace handlingmodel`):**
- `struct Record`: `tHandlingData`, 0xdc bytes, a `static_assert` on each offset.
  - `int32_t id` 0x00; `float mass` 0x04, `invMass` 0x08, `turnMass` 0x0c; `float dimension[3]` 0x10; `float centreOfMass[3]` 0x1c; `int8_t percentSubmerged` 0x28; `float buoyancy` 0x2c, `tractionMultiplier` 0x30.
  - `uint8_t gears[0x48]` 0x34; `uint8_t driveType` 0x7c, `engineType` 0x7d; `int8_t numberOfGears` 0x7e; `uint8_t transmissionFlags` 0x7f; `float engineAcceleration` 0x80, `maxVelocity` 0x84, `maxCruiseVelocity` 0x88, `maxReverseVelocity` 0x8c, `currentVelocity` 0x90.
  - `float brakeDeceleration` 0x94, `brakeBias` 0x98; `int8_t abs` 0x9c; `float steeringLock` 0xa0, `tractionLoss` 0xa4, `tractionBias` 0xa8, `unused` 0xac.
  - `float suspensionForce` 0xb0, `suspensionDamping` 0xb4, `suspensionUpper` 0xb8, `suspensionLower` 0xbc, `suspensionBias` 0xc0, `antiDive` 0xc4, `collisionDamage` 0xc8; `uint32_t flags` 0xcc; `float seatOffset` 0xd0; `int32_t money` 0xd4; `int8_t frontLights` 0xd8, `rearLights` 0xd9.
  - A comment says what each field holds before the conversion (file units; engine acceleration is the file's ×0.4) and after it.
- `enum class Kind : uint8_t { Sports, Car, Van, OffRoad, LightTruck, HeavyTruck, Tank, Small };`
- `struct Factors { float mass, accel, top, springs; };` and `const Factors &For(Kind);`, holding the table in Global Constraints.
- `struct Car { const char *name; Kind kind; };`, `extern const Car kCars[]; extern const int kCarCount;` (71) and `const Car *Find(const char *identifier);` (nullptr if not retuned):
  - **Sports:** PHEONIX HOTRING SABRETUR INFERNUS CHEETAH BANSHEE DELUXO COMET STINGER.
  - **Car:** ADMIRAL SENTINEL MAFIA WASHING LOVEFIST STRETCH ZEBRA CABBIE ROMERO GLENDALE VIRGO IDAHO ESPERANT SABRE1 STALLION VOODOO OCEANIC GREENWOO HERMES CUBAN BLOODRA BLOODRB REGINA PEREN TAXI MANANA BLISTAC POLICE FBICAR.
  - **Van:** MOONBEAM GANGBUR BURRITO PONY RUMPO MRWHOOP.
  - **OffRoad:** SANDKING BFINJECT WALTON BOBCAT LANDSTAL RANCHER DESPERAD PATRIOT FBIRANCH.
  - **LightTruck:** BENSON MULE BOXVILLE YANKEE SECURICA AMBULAN ENFORCER.
  - **HeavyTruck:** LINERUN PACKER BUS FLATBED TRASH COACH FIRETRUK BARRACKS.
  - **Tank:** RHINO.
  - **Small:** GOLFCART BAGGAGE.
- `bool Plausible(const Record &raw);`: a record as parsed, before conversion. True when:
  - `mass` is in [50, 50000];
  - every `dimension` is in (0, 20];
  - `engineAcceleration` is in (0, 20];
  - `maxVelocity` is in [10, 400];
  - `brakeDeceleration` is in [0.5, 15];
  - `tractionMultiplier` is in (0, 3] and `tractionLoss` in (0, 1];
  - `numberOfGears` is in [1, 5];
  - `suspensionForce` is in [0.3, 6], `suspensionDamping` in [0.01, 1], `suspensionUpper > suspensionLower`, and `suspensionBias` in (0, 1);
  - `percentSubmerged` is in [1, 100];
  - `money` is in [0, 1000000];
  - and both lights are in [0, 3].
- `void BeforeConversion(Record &raw, Kind);`: with `f = For(kind)`, `S = upper − lower` and `sag = S / (4 · force)`:
  - `mass *= f.mass`; `engineAcceleration *= f.accel`; `dimension[2] *= f.accel · f.mass / f.top³`;
  - `tractionLoss = 1 − 0.65 · (1 − tractionLoss)`; `tractionMultiplier *= 0.97`; `brakeDeceleration *= 0.88`;
  - when `f.springs < 1`:
    - `suspensionLower −= sag · (1/f.springs − 1)`;
    - `suspensionForce = f.springs · force · S' / S`, with `S'` the new `upper − lower`;
    - `suspensionDamping *= 0.85 · sqrt(f.springs)`.
  - When `f.springs == 1`, only `suspensionDamping *= 0.85`.
- `void AfterConversion(Record &);`: `turnMass *= 1.25`.
- `float DragTopSpeed(const Record &raw);`: km/h, the game's own search (above) on a raw record. It is for tests and the log.

- [ ] **Step 1: Write the failing tests** in `tests/test_handlingmodel.cpp`.
  - **The Infernus fixture, as parsed:**
    - `mass 1500`; `dimension {1.9, 4.5, 1.0}`; `centreOfMass {0, 0.02, 0.20}`; `percentSubmerged 75`;
    - `tractionMultiplier 0.85`, `tractionLoss 0.70`, `tractionBias 0.48`;
    - `numberOfGears 5`, `maxVelocity 300`, `engineAcceleration 12.4`, drive `'R'`, engine `'P'`;
    - `brakeDeceleration 4.5`, `brakeBias 0.65`; `steeringLock 30`;
    - `suspensionForce 1.10`, `suspensionDamping 0.14`, `suspensionUpper 0.15`, `suspensionLower −0.15`, `suspensionBias 0.5`, `antiDive 0.5`;
    - `seatOffset 0.24`; `money 95000`; `flags 0x1000183`; lights 0 and 1.
  - `TestLayout`: `sizeof(Record) == 0xdc`; `offsetof` of `engineAcceleration` 0x80, `tractionLoss` 0xa4, `suspensionForce` 0xb0 and `money` 0xd4.
  - `TestRetuneValues`: Infernus, Sports. Each value to ±1e-4:
    - `engineAcceleration 10.54`; `dimension[2] 1.09158`;
    - `tractionLoss 0.805`, `tractionMultiplier 0.8245`, `brakeDeceleration 3.96`;
    - `suspensionLower −0.186713`, `suspensionForce 0.80250`, `suspensionDamping 0.095941`;
    - mass and the other two dimensions unchanged.
  - `TestRetuneKeepsTheRest`: a `memcmp` before and after, with only the changed fields masked out, finds no other difference. The gears bytes are pattern-filled first.
  - `TestTopSpeedFactor`: for the Infernus (Sports) and a Bus fixture (HeavyTruck: `mass 8000`, dims `{2.3, 9.0, 3.6}`, accel 2.8, force 1.3, damping 0.17, limits 0.40/−0.25), `DragTopSpeed` after `BeforeConversion` is the type's `top ×` before, ±2 km/h (the search's step is 1.8 km/h). The Infernus before is within 2 of 195.6.
  - `TestRideHeightKept`: for both fixtures, `−lower − sag` is the same before and after (±1e-5 m), and so is the bump travel left at rest, `upper − lower − sag`.
  - `TestRollSofter`: the per-metre stiffness `force / (upper − lower)` is the type's `springs ×` before (±1e-4 relative).
  - `TestDampingRatio`: `damping · sqrt((upper − lower)/force)` is ×0.85 before (±1e-4 relative).
  - `TestTank`: a Rhino fixture's suspension force and limits are unchanged and its damping is ×0.85.
  - `TestAfterConversion`: turn mass 2982.5 becomes 3728.125.
  - `TestFind`: `INFERNUS` is Sports, `BUS` HeavyTruck, `RHINO` Tank, `GOLFCART` Small. `RCBANDIT`, `DODO`, `HELI`, `AIRTRAIN`, `DEADDODO`, `TRAIN`, `BIKE`, `SEAPLANE`, `SPARROW` and `HUNTER` return nullptr. `kCarCount == 71`.
  - `TestPlausible`: the Infernus is plausible. It isn't with `mass 0`, with `suspensionUpper < suspensionLower`, with 7 gears, or with `money −1`.
- [ ] **Step 2: Run them.** `make build/test_handlingmodel && build/test_handlingmodel`. Expected: does not compile.
- [ ] **Step 3: Implement `handlingmodel.h/.cpp`.**
- [ ] **Step 4: Run them.** Expected: PASS.
- [ ] **Step 5: Write `tests/test_handling_assets.cpp`.** It parses `$GAMEDATA/data/handling.cfg` plain lines (33 columns, A…g, `atof`). Accel is stored ×0.4 and flags are read as hex, as the loader does.
  - `TestListMatchesFile`: the identifiers from `;Exotic` to `;------- bikes`, minus `AIRTRAIN DEADDODO DODO HELI TRAIN RCBANDIT`, are exactly `kCars`.
  - `TestEveryCarPlausible`: each car is plausible, before and after `BeforeConversion`.
  - `TestEveryCarsSuspension`: after the retune, every car meets the Review Focus 3 limits: rest sag under 0.55 of travel, `4 · force − 1 ≥ 1.5`, ride height to 1 mm.
  - `TestEveryTopSpeed`: each car's top speed is its type's factor of before, ±2 km/h. It prints a table of name, type, before and after.
  - `TestCharacterKept`: within each type, the order by mass, top speed, traction multiplier and brake is the same after as before.
  - `TestNoStabiliser`: no car has flag 0x800, so `dimension[2]` only feeds drag.
  - Run: `make test-game-assets`. Expected: PASS.
- [ ] **Step 6: Commit:** `Car handling model: 71 cars by type, mass, acceleration, top speed through drag, softer springs at the same ride height`.

### Task 2: Retune the game's handling as it converts

**Files:**
- Create: `src/handling.h`, `src/handling.cpp`
- Test: `tests/test_handlinghook.cpp` (`#define dlsym TestSymbol`, then `#include "../src/handling.cpp"`, as `tests/test_rainvehicles.cpp` does).
- Modify:
  - `tools/patch_macvc.py`: add `"_ZN16cHandlingDataMgr22ConvertDataToGameUnitsEP13tHandlingData"` to `DEFAULT_HOOKS`, with a comment.
  - `src/config.h` and `src/config.cpp`: `bool carHandling = true;`, key `CarHandling`, and the template line `CarHandling=1        ; GTA IV-style weight: heavier trucks, slower cars, softer springs that lean, longer braking`.
  - `src/minimap.cpp`: call `handling::Install()` after `rainvehicles::Install()`.
  - `Makefile`: `src/handlingmodel.cpp` and `src/handling.cpp` in `PLUGIN`; the test rule; the `test` list.
  - `README.md`: one line in the features list.

**Interfaces:**
- Consumes: Task 1's `Record`, `Find`, `Plausible`, `BeforeConversion`, `AfterConversion` and `kCarCount`.
- Produces:
  - `bool handling::Install();`: returns true when installed or when `CarHandling=0` (nothing chained). It binds `_ZN16cHandlingDataMgr13GetHandlingIdEPKc` and chains the conversion.
  - `int handling::RetunedCount();`
  - `const char *handling::NameOf(int id);`: the car's identifier, for Task 3's log; nullptr if not retuned.

**The hook `(manager, record)`:**
1. On first use, map each `kCars` name to its id with `GetHandlingId(manager, name)`. An id ≥ 106 means the name is unknown; log it once and skip it.
2. If `record->id` is one of the cars and `Plausible(*record)`, run `BeforeConversion`, the original, then `AfterConversion`, and count it. An implausible record is passed to the original untouched, and the first one is logged.
3. Anything else goes to the original only.
4. When the count reaches the number of known cars, log `car handling: retuned %d cars (by type: weight, acceleration, top speed, grip, brakes, softer springs)`.

- [ ] **Step 1: Write the failing tests.** Stub `hooks::Chain`, the native conversion (a fake that records it was called and marks `invMass = 1/mass`), and `GetHandlingId` by name over a fake name table: Infernus id 4, Bus 58, Dodo 90, RC Bandit 99.
  - `TestRetunesCars`: the Infernus and Bus records reach the native already retuned (its mass, accel and suspension are the model's) and come out with turn mass ×1.25. The native ran once each.
  - `TestOthersUntouched`: the Dodo, the RC Bandit and id 104 reach the native bit-identical to their input, and come out as only the native changed them.
  - `TestImplausiblePassesThrough`: an Infernus with `mass 0` reaches the native unchanged; one log line; `RetunedCount() == 0`.
  - `TestReconvertFromFresh`: converting a fresh Infernus twice gives the same bytes both times.
  - `TestOffInstallsNothing`: with `gConfig.carHandling = false`, `Install()` returns true and `Chain` is never called.
- [ ] **Step 2: Run them.** `make build/test_handlinghook && build/test_handlinghook`. Expected: FAIL.
- [ ] **Step 3: Implement `handling.cpp`** and the wiring listed under Files.
- [ ] **Step 4: Run them.** Expected: PASS.
- [ ] **Step 5: Check the patcher fits every hook.** `python3 -I tools/patch_macvc.py "$HOME/Library/Application Support/macVC/vcmodernhud/macVC.vanilla" <scratchpad>/macVC`. Expected: exit 0, with the new hook listed. If it's full, stop and report.
- [ ] **Step 6: Commit:** `Car handling: the 71 cars retuned as the game converts its handling; CarHandling=1`.

### Task 3: The drive log

**Files:**
- Create: `src/drivelog.h`, `src/drivelog.cpp` (pure)
- Test: `tests/test_drivelog.cpp`
- Modify: `src/handling.cpp` (it chains `_ZN11CAutomobile14ProcessControlEv` and feeds the log; installed only with `CarHandling=1`), `Makefile`.

**Interfaces (`namespace drivelog`):**
- `struct Sample { float t, speed; bool allWheelsDown; float roll, pitch; };`: s; m/s along the car's forward; rad, body against the road; roll > 0 is the right side down, pitch > 0 the nose up.
- `struct Window { float top = 0, zeroTo100 = -1, rollMost = 0, pitchMost = 0; };`
- `struct State { Window window; float startedAt = -1, runFrom = -1; bool armed = false; };`
- `void Add(State &, const Sample &);`:
  - `top` is the most speed seen.
  - A run is armed below 0.5 m/s. It starts timing when speed passes 0.5 m/s, and it stops at 27.78 m/s (100 km/h), keeping the quickest run in the window. Dropping below 0.5 m/s re-arms it.
  - `rollMost` and `pitchMost` are the largest |angle| seen with all wheels down.
- `bool Due(const State &, float t);`: 30 s since the window started. `Window Take(State &, float t);` returns the window and starts the next.
- `void BodyAngles(const float right[3], const float forward[3], const float up[3], const float contact[4][3], float &roll, float &pitch);`: wheels in the game's order (0 front-left, 1 rear-left, 2 front-right, 3 rear-right).
  - Roll is `atan2(d·up, d·right)` for `d` the mean right contact minus the mean left contact.
  - Pitch is the same with front minus rear, along `forward`.

**In game:** after the original `ProcessControl`, only for the car the player ped drives (`api.FindPlayerPed`, `game::PedVehicle`) when its handling id is a retuned car.
- `allWheelsDown` is all four spring ratios at +0x580 below 1. The contacts are at +0x4e0, stride 0x28.
- When `Due`, it logs `car handling: %s last 30 s: top %.0f km/h, 0-100 %s, body roll up to %.1f°, pitch %.1f°`, where the 0–100 field is `%.1f s` or `—`.

- [ ] **Step 1: Write the failing tests.**
  - `TestTopAndZeroTo100`: speed rising 0 → 30 m/s at 5 m/s² gives `top` 30 and `zeroTo100` 5.556 ±0.02 s.
  - `TestRollingStartNoTime`: a run starting at 10 m/s gives `zeroTo100 == −1`.
  - `TestQuickestKept`: two runs (8 s, then 6 s) keep 6.
  - `TestAnglesOnlyAllDown`: a large roll with `allWheelsDown = false` is ignored.
  - `TestBodyAngles`: for a body rolled 4° right-side-down over flat contacts, roll is +4° ±0.01; level gives 0; a nose dipped 2° gives pitch −2°.
  - `TestWindow`: `Due` is false at 29.9 s and true at 30 s, and `Take` resets.
- [ ] **Step 2: Run them.** Expected: does not compile.
- [ ] **Step 3: Implement**, including the `ProcessControl` chain in `handling.cpp`. Add `TestOffInstallsNothing`'s check that `ProcessControl` isn't chained either.
- [ ] **Step 4: Run them**, along with `build/test_handlinghook`. Expected: PASS.
- [ ] **Step 5: Commit:** `Drive log: top speed, 0-100 and body roll and pitch every 30 s, for tuning the handling`.

### Task 4: Tommy balances his head in the seat view

**Files:**
- Create: `src/headbalance.h`, `src/headbalance.cpp` (pure)
- Test: `tests/test_headbalance.cpp`
- Modify:
  - `src/camera.cpp`, `InVehicle`: once the look-about has turned the view, and before `NearClip`, turn it by `TurnCamera(0, turn.pitch, turn.roll)`.
  - `src/config.h` and `src/config.cpp`: `bool headBalance = true;`, key `HeadBalance`, and the line `HeadBalance=1        ; first person in a car: his head levels a little against the car's roll`.
  - `Makefile`.

**Interfaces (`namespace headbalance`):**
- `struct State { float roll = 0, pitch = 0, slowPitch = 0; bool primed = false; };` and `struct Turn { float pitch, roll; };` (rad, for `camera::TurnCamera`).
- `Turn Step(State &, float viewRoll, float viewPitch, bool active, float dt);`, where:
  - `viewRoll = atan2(−s.z, up.z)` with `s = Cross(front, up)` (the screen's right); it is positive when the right of the screen is down.
  - `viewPitch = asin(front.z)`.
  - The camera's rows are read before the turn.
- **Behaviour:**
  - The roll target is `−0.35 · viewRoll`, clamped to ±6°.
  - `slowPitch` low-passes `viewPitch` with τ 1.5 s (primed to it on the first step). The pitch target is `−0.30 · (viewPitch − slowPitch)`, clamped to ±3°.
  - Each turn eases to its target, τ 0.2 s for roll and 0.15 s for pitch. `dt` is clamped to [0, 0.1].
  - When `!active`, both targets are 0, and `slowPitch` keeps following.
- With `camera::Turn`'s sign (right-hand, `up = Turn(up, front, roll)`), a positive `roll` brings a right-side-down view back up, and a positive `pitch` raises the view.

**In camera.cpp:** `active` is `gConfig.headBalance`, and the vehicle is type 0 and not `game::IsAircraft`, and not `driveby::Aiming()`, and the car's up.z ≥ 0.35.

- [ ] **Step 1: Write the failing tests.**
  - `TestLevelsRoll`: a steady view roll of +4° for 2 s gives `roll` → −1.4° ±0.05 (positive right-down roll, turned back by 35%).
  - `TestClampRoll`: a view roll of 30° gives `roll` = −6°.
  - `TestQuickPitchOnly`: a 2° nose dip held for 0.4 s is levelled by about 30% (pitch ≈ +0.6°, ±0.15). A 10° hill held for 10 s ends within 0.1° of 0.
  - `TestLevelsAnyView`: the function only sees the view's angles, so a view looking behind with a +4° roll levels the same way as `TestLevelsRoll`.
  - `TestInactiveEasesOut`: from −1.4°, `active = false` gets within 0.05° of 0 by 1 s, and no step changes it by more than 0.2°.
  - `TestRolledCarNoCorrection`: with `active = false` from the start and a 90° roll, the turn stays 0.
  - `TestFrameRate`: the steady roll result at dt 1/30 and 1/144 agrees to 2%.
- [ ] **Step 2: Run them.** Expected: does not compile.
- [ ] **Step 3: Implement**, and the camera.cpp and config wiring. The camera keeps one `State`, reset when he leaves the seat view.
- [ ] **Step 4: Run them.** Expected: PASS. `make all` has no warnings.
- [ ] **Step 5: Commit:** `Head balance: in first person his head levels a little against the car's roll; HeadBalance=1`.

### Task 5: Whole build, review, install, the play check

- [ ] **Step 1:** `make all 2>&1 | grep -i warning` prints nothing; `make test` and `make test-game-assets` exit 0.
- [ ] **Step 2: Review:** the whole branch, by a fresh reviewer (`superpowers:requesting-code-review`). Fix what it finds.
- [ ] **Step 3: Install (the game must be quit):** `./install.sh`. Expected: `==> Installed.`, with the patcher run listing the conversion hook.
- [ ] **Step 4: First launch:** the log shows `car handling: retuned 71 cars …` and no `missing` lines.
- [ ] **Step 5: Hand the play check to the user and post it on #37:**
  - A hard corner in a sedan, then in the Infernus and a van: the body rolls visibly, the sedan most, and the drive log's roll grows to match.
  - An emergency stop: the nose dives, then rocks back. Full throttle: the tail squats.
  - A truck or bus nudging a car: the car gives way; the truck barely does.
  - Kerbs and jumps: some bottoming out, but not on every kerb.
  - First person in a car: the view leans with the car, and his head levels a little, never seasick, never robotic.
  - Timed work in each type: a street race, a Taxi fare, a Paramedic run, a Firefighter fire. All still makeable.
  - Traffic and police chases: no rollovers in normal driving.
  - Bikes, boats, helicopters and the airport jets: unchanged.
- [ ] **Step 6: Tune** from that play and the drive log: values only in `handlingmodel.cpp`'s table and `headbalance.cpp`. Commit with the log lines quoted.

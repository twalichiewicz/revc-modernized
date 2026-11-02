# Tommy's ragdoll on every knockdown (#25)

Status: draft for approval, 2026-11-02. No code yet.

## Request and confirmed scope

[#25](https://github.com/twalichiewicz/revc-modernized/issues/25): "Tommy being hit by a car uses the
old physics rather than the new Euphoria." Asked on 2026-11-02, the player chose:

- **Every native knockdown on foot ragdolls Tommy**, not only car hits.
- **In first person the camera stays on his head** through the tumble.

## Why it happens today

Tommy has no ragdoll. The articulated controller excludes the player by design
(`src/reactions.cpp` `Eligible`: `ped == api.FindPlayerPed()`), as the 2026-10-31 reactivity spec
required ("not permission to take over the player with the ordinary NPC ragdoll controller").
`src/playerreact.cpp` only layers arm bracing over native animation. `player.cpp`
`Hook_InflictDamage` already calls `reactions::Hit` for Tommy, and it returns at `Eligible`.
So every knockdown plays the stock animation. This spec reverses that exclusion, scoped to
knockdowns.

## The knockdowns covered

Every native knockdown goes through `CPed::SetFall`, which the mod already hooks
(`reactions::Hook_SetFall`). The shipped binary calls it from 15 functions:

| Source | Native caller |
|---|---|
| Vehicles | `CPed::KillPedWithCar`, `CAutomobile::KnockPedOutCar`, `CBike::KnockOffRider`, `CPed::PedSetOutCarCB` |
| Melee | `CWeapon::FireMelee`, `CPed::FightHitPed`, `CPed::StartFightDefend` (2) |
| Guns | `CWeapon::FireShotgun`, `CWeapon::DoBulletImpact` |
| World | `CWorld::TriggerExplosionSectorList`, `CWaterCannon::PushPeds`, `CPed::CollideWithPed` |
| Locomotion | `CPed::ProcessControl` (2), `CPed::PlayFootSteps` (2), `CPed::SetWaitState` |

## Approach

**Reuse the NPC articulated controller for Tommy, admitted only by a knockdown** (recommended).
One solver, the same native fall/get-up holding, corpse and release handoffs, and the hit-volume
work that already applies to NPCs. A separate player ragdoll would duplicate the solver and
diverge. Admitting Tommy for every hit (as NPCs are) would take over his controls for ordinary
flinches; the knockdown gate keeps native control for everything else.

1. **Entry.** When native `SetFall` targets Tommy, start (or continue) his articulated body. The
   impulse is the native velocity the caller sets, plus a direction-matched push for callers that
   only pass a fall direction (melee, trips). Native `SetFall` still runs and is held (duration
   -1), exactly as for NPCs, so the game sees him knocked down.

   Entry cannot rely on `SetFall` alone. In the shipped binary `CPed::KillPedWithCar` has two
   branches: a knockdown (`InflictDamage`, then `SetFall`, at `0x1001ca9c0`/`0x1001ca9d4`), and a
   launch (`InflictDamage`, then `CPhysical::ApplyMoveForce` after the damage callback, at
   `0x1001cabf0`/`0x1001cac84`) with no `SetFall` of its own; that knockdown follows from
   `ProcessControl`. The launch is the case the NPC `pendingVehicle` path already reconciles after
   the callback returns, so Tommy enters through the same damage path for vehicles. Still to verify
   for the other callers: the order of their velocity writes relative to `SetFall`.
2. **During the fall.** Native movement, aiming, firing, weapon switching, crouch, climb, dive and
   swim entry stay off while the body owns his root (to verify: which inputs native
   `CPlayerPed::ProcessControl` still honours in PED_FALL).
   Mod systems that pose or turn him (`aimcam`, `fpshoot`, `fpwalk`/`TurnBody`, `playerreact`,
   viewmodel arms and gun) stand down until he is up.
3. **Getting up.** As for NPCs: physical recovery, then native `SetGetUp` from a root lifted to
   standing height (`StartGetUp`), then control returns.
4. **Death.** If the knockdown kills him, the body keeps its ragdoll through the wasted sequence
   (the corpse retention from 2026-11-02). It is released with the native corpse root
   convention before native resurrection teleports him to the hospital.
5. **Never admitted:** cutscenes, a scripted camera owning the view (the `playerreact` gates), in a
   vehicle, while swimming, or with controls disabled by script. Mission knockdowns are allowed;
   mission ownership of health and death stays native.

## First-person camera (decided 2026-11-02: fully attached)

Today the eye is his body position plus his head's offset from it, eased over 0.28 s, and the view
follows your look input. During a ragdoll:

- The eye sits on his physical head with no easing, and **the view takes the head's full
  orientation**: it rolls and spins with the tumble, as GTA V's first-person ragdoll does. Look
  input is ignored until he is back up.
- `TurnBody` does not turn the ragdolled body toward the view.
- On get-up the view eases from the head's orientation back to your look direction, so control
  returns without a snap.
- The head stays hidden, as it is in first person today.

(The alternative, keeping your look input and only riding on the head's position, was offered and
declined.)

## Risks and checks

- Root ownership: native follow cameras, wanted/witness position and pickups read his root, which
  will sit at pelvis height during the fall (as for NPCs). The get-up and corpse paths already
  restore native conventions.
- Water, ledges and vehicles under him: release to native swimming on water contact; vehicles
  keep the existing dynamic-support rules.
- Saves, missions and the wasted/busted sequences must be unaffected; busted arrests go through
  native arrest, not a knockdown.
- Tests, through the real hooks: one per knockdown family above, first-person eye on the head,
  controls suppressed and restored, death through wasted, cutscene/script/vehicle exclusions,
  mutants for each gate. Then a controlled in-game check per family and your acceptance.

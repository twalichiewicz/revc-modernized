# First-person firing, melee and the minigun (Plan 6)

**Goal:** The player's feedback after playing Plan 5: "the first-person gun animations for shooting are all
pretty flat/lifeless" (research how others make them engaging, then do it); "weapons like the baton/melee
[need] to be held up in a ready position in first person since you can't see them"; "the minigun is being
held too far forward and high".

**Spec:** `docs/superpowers/specs/2026-10-27-first-person-gunplay-design.md`, amended by this feedback: the
gun now kicks visibly in his hands (the spec's "the gun barely kicks, the view jolts" read as flat), and melee
weapons are no longer a non-goal. Research: `docs/research/2026-10-27-viewmodel-fire.md`.

## Global Constraints

- Plans 1–5's constraints. Nothing waits on an animation: a gun fires the frame RT is pulled (Plan 2); a
  melee swing follows the game's own attack (its animation's clock and weapon.dat's fire frame), so the hit
  lands when the game's does.
- The game's own models for melee weapons (from the weapon atomic in his hand), his own arms.
- Don't commit; install only with the game closed.

## Review Focus

1. Every gun kicks its own way and settles back to exactly where it was (`TestPistolSnapsAndSettles`,
   `TestAutomaticClimbs`); aimed, the sights stay on (`TestAimedKicksLess`).
2. The kick never brings his left sleeve into view (his left shoulder stays put: `TestKeepShoulder`; checked
   by render with `VM_SHOT`).
3. A quick gun's bolt is home before its next shot (`TestQuickGunCyclesInTime`); the pump comes after the kick
   and is done before the next shot (`TestPumpCyclesAfterAShot`).
4. A melee swing that starts over (the game loops it while the button's held) never jumps
   (`TestHandNeverJumps`); switching away from a melee weapon never draws a model that's left his hand.
5. The minigun is low at the hip and close, its barrels running in (`TestEveryGunPointsWhereItAims` still holds).

---

### Task 1: The minigun

- `tools/guns/minigun.py`: `FRAMING = ((-0.320, -0.220, 0.330), 8.0, 0.0, 0.3, 0.02)` (was 0.384 m ahead and
  0.224 m below: 16 cm closer, 11 cm lower, turned in 8°); `LEFT_FOREARM = (-0.8, -0.45, -0.6)` (in from the
  lower left, clear of the barrels). Rebuilt with `make gun NAME=minigun` (only the framing's bytes change).

### Task 2: The kick (`vmkick`)

- `vm::Recoil::Shot(KickClass, random[3])`, `Update(KickClass, dt, firing, spin) -> KickMove`: per class, half
  the kick at once and half a spring's push (damping under 1 overshoots); size, roll side and yaw random; a
  burst's first shot bigger; automatics climb on Doom 3's muzzle-rise timer; the minigun buzzes, the
  flamethrower shudders. Tests: `tests/test_vmkick.cpp`.
- `vm::GunKicked(gun, carried, kick)` (vmhands): the gun turned about a point 10 cm back along his forearm;
  his right arm keeps to it. `vm::KeepShoulder(arm, restHand, rest, hand, left)` (vmpose): his left arm
  reaching its kicked hand from its resting shoulder.
- `vmanim`: the kick out on its own (`AnimOutput::kick`, less aimed); slide held 35 ms, home in 50 ms,
  shortened by `AnimInput::interval`; the pump at 0.22 s moving the whole gun (shotgun, SPAS 12); the flash
  a frame long, turned and sized per shot (`flashTurn`, `flashSize`); `light` for the shot's light.
- `fpgun`: the shot's light added over his arms and the gun (255/192/64, out to 0.55 m from the muzzle).

### Task 3: Melee (`vmmelee`)

- `vm::ReadyGrip(MeleeKind)`, `AttackGrip(kind, Attack)`, `MeleeHand::Update` (inertialization of jumps),
  `ArmTo(wrist, ...)` (his arm from his shoulder). Grips tuned by render: `vcmm-viewmodel --melee <model>`
  with `VM_GRIP` / `VM_SWING`. Tests: `tests/test_vmmelee.cpp`.
- `fpgun`: a melee weapon's model read from `CPed::m_pWeaponModel` (+0x290) and drawn in his hand; the
  attack from his animations (205, 206, 209 with `CWeaponInfo` +0x40 / +0x4c; brass knuckles from
  `tFightMoves`).
- `vmanim`: `Carry::kMelee` sprinting; switching away from a melee weapon is at once.

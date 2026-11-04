# Localized mechanical damage (#37 follow-up)

The user sees deformation working but wants damage to affect performance,
eventually with point damage disabling underlying systems like War Thunder.
They selected **mechanical damage first**, leaving crumple tuning unchanged.
This supersedes the earlier physics spec's deferred mechanical-damage non-goal.

## First deliverable

Six independently damaged modules per ordinary car: engine, cooling system and
four wheel assemblies. Confirmed native collision position/direction/severity
determines which volumes receive damage. A cabin/door hit need not harm the
engine. Native punctures remain native; a severely crushed wheel assembly may
also puncture its tyre through the native BurstTyre protection checks.

Engine integrity reduces delivered transmission acceleration, reaching zero
on destruction. A cooling breach leaks coolant; engine operation/load then
raises heat, progressively reducing power. A bent wheel assembly changes that
wheel's alignment and reduces its drive, braking and grip. The native wheel
solver applies these forces at its real contact point, producing pulling/yaw.
No shared handling allocation/configuration is copied or rewritten.

Wheel volumes come from the supported model's native wheel frames (IDs
4,6,1,3 in native LF/LR/RF/RR order); body extents come from its COL bounds.
Engine/cooling volumes are coarse initial authored approximations. Deluxo/Comet
use a rear engine, Infernus/Cheetah/Vice Cheetah a mid-rear engine, other supported
cars a front engine; Comet cooling follows its rear engine, other cooling volumes
are forward. These are implementation choices, not detailed engine reconstructions.
More per-model internals and projectile penetration are subsequent work.

## Collision ownership and performance

Deepen the existing collision collector into `carimpact`: one certified native
impulse owner feeds visual dents and mechanics independently. Native collision
pieces 13–16 are wheel contacts; they are distinct from ApplyDamage component
IDs 1–4. Correct that routing, retain body contacts 1–4, and keep wheel/road
support out of visual deformation. Mechanics remain active with CarDeformation=0.
Native matching/protections, repairs, independent contact episodes, strict
strongest-force ties and RAII actor scopes remain intact.

Initial base dose: impacts above 4.5 m/s, (severity−4.5)/26 capped at 0.55, coupled
along an inward crush segment of 0.08+severity×0.035 m (max 1.0 m), with
0.25 m spread around each volume. One progression per module per 350 ms
contact episode; separate impacts accumulate. Cooling uses 1.35× the base dose
for its more vulnerable heat exchanger. Wheel alignment caps at 0.10 rad;
grip/drive/brake factors at full damage are 0.45/0.65/0.50. At wheel damage
0.75, request a native puncture if still intact. Engine power is 1−0.85×damage,
zero at damage >=0.98, further reduced by heat above 0.65.

Cooling integrates game time in bounded steps; intact cooling stays stable.
Coolant loss rate is 0.025×coolingDamage² per second. Heat follows engine
load and cooling capacity, with no new health/fire/explosion writes. Destruction
of engine or extreme overheating can remove drive torque while the car coasts.
Native engine audio/start flags remain native in this pass.

History is bounded to 256 pool slots. Pool/generation/model/vtable identity
prevents damage transferring to another actor. Damage survives render-clump
replacement on the same physical car, resets on native Fix or world restart,
and never alters tyres' native protection flags. No savegame persistence yet.
Power/wheel changes require a matching live automobile control scope; other
classes, native proofs and unscoped callers pass through unchanged.

## Validation and next fidelity steps

Test localized doses, front/rear-engine separation, per-module episodes, cooling
time integration, native transmission pointer/scratch lifetime, individual wheel
effects and native outputs, repairs/restart/recycling/nesting, disabled features,
actual car layouts and isolated native transmission/wheel consumers. Run the
full suite, sanitizers, one independent review, then install and verify bytes.
Live check: a damaged engine loses acceleration, cooling trouble grows while
running, a damaged corner pulls/slips, and repair restores all of them.

Later fidelity: dedicated brake/steering/drivetrain/fuel/electrical modules,
per-model authored internal volumes, projectile paths/material penetration,
localized leaks/fire and inspection/persistence. Current collision volumes
establish the localized system model without claiming a complete simulator.

# Car parts (#37, part 3)

Continue the approved vehicle-destruction writeup after the user's successful
glass play check. No new design interview is needed: the earlier direction was
to work from the detailed writeup.

## Intent and native baseline

Hoods, trunks and doors should become damaged, then swing, then come off after
a heavier subsequent hit. Bumpers keep the native damaged and detached models;
the game has no bumper hinge to animate. Native `ProcessSwingingDoor`/`CDoor`
retain all motion, including an open hood tearing off at speed. Native loose
objects retain their own collision, mass and disposal.

On macVC 1.0.5217, `CDamageManager::ApplyDamage` advances one state whenever
force times its component-group multiplier exceeds 150. The collision-coefficient
argument is ignored by this function (it is overwritten with the group table);
health damage is separately owned by `VehicleDamage`. Door states are bytes at
DamageManager+9; panel states are nibbles at +0x14. Native doors/bonnet/boot use
states 0 intact, 1 damaged, 2 swinging, 3 missing. Bumper stage 2 retains its
damaged appearance until stage 3. Evidence: `build/car-glass/parts-native.disasm`
and `swinging-native.disasm`, taken from the pristine supported image.

## Approach

Tune only native physical collision calls for parts, rather than altering health
coefficients, replacing door physics or spawning custom loose objects. Two
existing hook chains (`ProcessControl`, `VehicleDamage`) establish a live vehicle
context around the one new hook, `CDamageManager::ApplyDamage`. Scripted damage
calls, bullets, unscoped calls, aircraft, bikes, boats, RC Bandit and Rhino retain
their native behavior.

The eight owned components are bonnet/boot/four doors (native component 5–10)
and the front/rear bumpers (16–17). Scale only the part force passed to native
ApplyDamage: state 0 ×1.00, state 1 ×0.85, state 2 ×0.55. State 3 is already
missing and cannot generate another progression. These factors are an initial
tuning choice, not measured GTA IV constants. The native per-component group
coefficients and each car's collision-force differences remain intact.

One component may advance only once during a contact episode. A new episode
requires at least 350 ms without an ApplyDamage attempt for that component.
Failed small hits do not consume that episode's progression. Every attempted
contact updates the quiet-gap clock, so prolonged scraping does not repeatedly
advance the same part. Other components remain independent.

Only final states after the native vehicle damage call consume progress and
appear in the log: native locks/protections can still veto a swinging door.
Repair clears the history before the native Fix chain. Pool generation, model
and clump changes invalidate history. Backward clocks reset episode timing.
Nested scripted calls cannot attribute their changed parts to an outer collision;
an earlier surviving physical advance still consumes its episode. Repair also
retires damage ownership already on the call stack.
History is bounded to 256 vehicle slots, above the native pool of 110, with no
per-frame allocation. `CarParts=0` is a native pass-through.

Alternatives considered: globally changing damage multipliers also changes
health, fires and mission difficulty; replacing loose parts/hinges duplicates
working native owners. Scoped component tuning preserves those owners.

## Acceptance and limits

- No health, handling, tyre, engine, light, wing or windscreen tuning. No altered
  body collision, AI or mission/script damage APIs.
- Preserve hook chaining, native arguments/results outside the owned scope,
  and all native protections and repair paths.
- Native body-state tests execute the actual ApplyDamage arm64 leaf with its
  verified group table, alongside hook fixtures for collision bursts, distinct
  hits, stronger detachment, nested/recycled identities, clocks, repair and off.
- Full build/test/asset checks and an independent review before installation.
- Live check: repeated separate bumps show damage then swinging; a stronger hit
  releases the part; pressing against a wall does not consume every stage.
- This pass does not add bumper hinges or crumpling. Deformation remains next.

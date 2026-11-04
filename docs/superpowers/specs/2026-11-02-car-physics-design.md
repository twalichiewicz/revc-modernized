# GTA IV-style car physics and destruction (#37) — plan first

You asked for a plan before anything is built. This document lays out what GTA IV-style car physics and
destruction would mean in this game: the options for each part, what each costs, the risks, and a
proposed order. You decide what gets built.

## Intent

Cars in Vice City feel light and arcade-like. They slide on rails, bounce off each other and take damage
as swapped parts. GTA IV's cars have weight: they lean into corners and dip under braking, crumple where
they're hit, and lose doors, bumpers and glass. The goal is that feel, so that crashing a car shows the
crash.

## The four parts

### 1. Handling and weight

- **What it means:** heavier cars, softer suspension, body roll in corners, dive under braking, squat
  under acceleration, and less grip at the limit, so cars slide progressively instead of snapping.
- **What the game has:** its handling data, loaded by `cHandlingDataMgr::LoadHandlingData` into
  `mod_HandlingManager`. Per car: mass, turn mass, centre of mass, traction curve and bias, suspension
  force/damping/limits and anti-dive, brake force, and more.
- **Options:**
  - **(a) Retune the data:** a GTA IV-style set of values per car, applied after the game loads its own.
    Cheapest, reversible, and touches nothing else, but limited: the game's suspension can't truly roll
    the body.
  - **(b) Add a body-roll and pitch layer:** the car's body (not its physics) leans and dips from its
    measured acceleration. That's the look of weight without changing how it drives.
  - **(c) Replace parts of the physics** (tyre model, suspension): the most GTA IV-like and the most
    work, and it risks traffic AI, missions and races tuned for the old handling.
- **Proposed:** (a) + (b) first, measured against missions with tight timings. Consider (c) only after
  that.

### 2. Body deformation

- **What it means:** panels crumple where the car is hit, by how hard, and stay crumpled.
- **What the game has:** damage states per component (doors, panels, bumpers, lights) that swap in
  damaged models (`CDamageManager`, `CAutomobile::VehicleDamage` for each collision).
- **The approach:** deform the car's own mesh at the impact.
  - Each car's vertices near the impact point move inward along the impact, scaled by its force and the
    distance, with a cap so the car never folds through itself.
  - Cars of one model share one mesh, so a damaged car gets its own copy. That means a limit on how many
    deformed cars exist at once (for example, the 16 nearest), with older ones restored as they're
    streamed out.
  - The game's own damage states carry on as today, so missions and repairs keep working. A Pay 'n'
    Spray restores the mesh.
- **Cost and risk:** the largest single part. It needs per-car mesh copies and their memory, a hook on
  collisions with the impact point, and care with the game's damaged-part models. The ray-traced shadows
  must see the deformed mesh, and the input checks just added keep that safe.

### 3. Parts coming off

- **What it means:** bumpers, hoods, trunks and doors hang, swing and come off in heavier crashes.
- **What the game has:** much of this already. Doors and panels progress through damage states, and parts
  fly off as loose objects. The plan here is tuning: when parts loosen and detach, so that it's
  GTA IV-like, plus hanging and swinging stages if the game's states allow.
- **Cost:** small to medium.

### 4. Glass

- **What it means:** windows crack and shatter, from bullets and crashes, with shards falling.
- **What the game has:** the front windscreen already cracks and shatters through native panel damage,
  with falling panes and sound. Side/rear glass is embedded in translucent materials without equivalent
  independent damage. The mod already traces these glass triangles for first-person bullets.
- **The approach:** per car, hide the glass triangles of a window once it's broken (with a cracked stage
  first if possible) and spawn shard particles. The mod's car-bullet collision code (#11) already knows
  where shots hit glass.
- **Cost:** medium.

## Proposed order

1. **Handling (a) + (b):** the biggest change in feel per hour of work, and fully reversible.
2. **Glass:** visible in every gunfight, and moderate work.
3. **Parts coming off:** tuning what the game already has.
4. **Deformation:** the largest; built last, on what the first three teach.

Each part would get its own design and plan when you choose to build it, and its own setting to switch
off.

## Non-goals

- A dragging bumper or detailed material/penetration simulation. Engine/cooling and wheel performance
  damage was subsequently brought into scope by the user's 2026-11-04 clarification; see the
  [mechanical damage design](2026-11-04-car-mechanics-design.md).
- New car models or interiors.
- Changing traffic AI.

## Anti-goals

- Missions or races becoming harder or impossible because of new handling.
- Frame-rate drops in busy traffic.
- Cars folding through themselves, or flickering between damaged and undamaged.
- Losing the game's own damage states, which missions depend on (repairs, wrecks).

## Decisions for you

1. Which parts to build, and whether in the proposed order.
2. **Handling:** keep each car's character (the Infernus still the fastest, the Stallion still the most
   tail-happy) or make them more alike?
3. **Deformation:** how severe? GTA IV's is quite soft. The cap decides how many crashes make a car a
   wreck.

## Progress, 2026-11-04

Handling and physical lean are implemented, with the drive log retained for tuning. The glass pass is
installed; see [glass design](2026-11-04-car-glass-design.md) and
[verification record](../plans/2026-11-04-car-glass.md). After the triangle-strip crash fix, the user
confirmed no crash and visible breakage. The 03:08:27 log records both front side panes of a Deluxo
breaking, with normal gameplay continuing through 03:09:30. Front windscreen damage stays native,
and repairs restore the private side/rear geometry. Other glass presentation checks remain
in the verification record.

The parts pass now tunes eight native components through damaged, swinging and detached stages, with
one progression per contact episode and stronger later hits required. Native health, handling, locks,
hinges, loose objects and repair remain in charge. See [parts design](2026-11-04-car-parts-design.md)
and [verification record](../plans/2026-11-04-car-parts.md). The user confirmed the driver/passenger/front
responded in the Deluxo. Logs also confirm the rear bumper's complete detachment sequence. Their
reported rear/quarter glass misses were reproduced and fixed: the shallow rear glass was excluded
by the roof filter, and fixed quarter windows lacked a severe rear-crash trigger.

Localized visual deformation is implemented and installed behind `CarDeformation`; see
[deformation design](2026-11-04-car-deformation-design.md) and
[verification record](../plans/2026-11-04-car-deformation.md). Private body copies retain dents and
broken glass together and restore on repair. The full suite, native Single collision, real mesh builder,
native dynamic-shadow CPU consumer, sanitizers and independent review pass. Live dent presentation,
rear-glass confirmation and severity tuning remain to be checked, so #37 stays open.

The user subsequently confirmed visible deformation and requested mechanical consequences, selecting
mechanical damage before stronger crumpling. Independent engine/cooling/four-wheel states and native
performance effects are implemented and installed behind `CarMechanics`; see
[mechanical design](2026-11-04-car-mechanics-design.md) and
[verification record](../plans/2026-11-04-car-mechanics.md). Collision routing now correctly distinguishes
body pieces1..4 from tyre pieces13..16. Engine damage reduces drive, cooling breaches leak and overheat,
and wheel assemblies change their own alignment/grip/braking. Native repair restores systems.
Full/native/asset/sanitizer checks pass; independent review has no blocking findings. Initial volumes
are coarse and collision-driven; detailed internals, projectiles, extra systems and persistence are
subsequent work toward the user's longer-term localized-system simulation. Live performance tuning
and a documented slow-update thermal edge remain outstanding.

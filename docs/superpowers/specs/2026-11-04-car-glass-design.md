# Car glass (#37, part 2)

Continue the existing destruction spec, starting with glass. The user already asked to work from that writeup without another design interview. Handling tuning continues separately.

## Native behavior, verified on macVC 1.0.5217

The original spec understates the baseline: windscreens already progress from intact to cracked to missing. `CheckForShootingVehicleOccupant` advances panel 4, skips state 2, calls `SetPanelDamage(19, 4, true)` and plays native audio. `SetPanelDamage` calls `CGlass::CarWindscreenShatters` when the windscreen is removed. The native falling-pane generator is reusable. Side/rear glass has no equivalent per-window state. The mod's `shotcollision` identifies glass but leaves its triangles present.

Evidence: exact-build disassembly at `build/car-glass/native.disasm` and `abi.disasm`; production `src/shotcollision.cpp`. These are static observations, not an in-game visual acceptance.

## Approach

Preserve native windscreen progression. Extend side/rear glass on the existing certified bullet contact, after world closest-hit selection and only in the actual impact callback. A side/rear window shatters on one bullet; the laminated front windscreen retains its native cracked stage. Severe native door/panel damage breaks the associated windows in crashes.

Use private geometry for the damaged car. Rebuild only affected atomics with the broken window's triangles collapsed to zero area, preserving triangle ordinals, opaque triangles, vertex data, materials, bounds and native render callbacks. This removes the glass consistently from raster fragments, rain/bullet intersections and the RT cleaner without shifting certified triangle indices. Keep an original-geometry reference in an unattached atomic; restore it on repair, release it on clump deletion. This avoids changing other cars or the game's model template. Rain and bullet tracing then read the same remaining geometry that renders.

Alternatives considered: material alpha cannot distinguish windows sharing a material and can affect other cars; draw-only hiding would leave rain/bullet collision behind; replacing the native windscreen duplicates existing damage/audio. Private filtered geometry supplies one consistent result to all consumers.

## Constraints and acceptance

- macVC 1.0.5217 only, C++17, existing hook chain.
- `CarGlass=1`; disabled retains native behavior. No handling changes.
- Cars only; aircraft, boats, bikes and RC cars remain native.
- Native health, missions, proof flags, doors, windscreens and repair logic retain ownership.
- Bounded allocation and validated pool generation/clump identity. No material mutation, stale atomic dereferences, repeated shards on an already missing window, or effects from read-only traces.
- Bullet, crash, repair, streaming and allocation failure tests; real car asset checks; full build and existing test suites.
- Live acceptance: side/rear window shot once, another window and nearby same-model car intact; repeated shot through opening; native front crack/shatter; severe crash; rain on remaining glass only; Pay n Spray restores glass; no issue #37 closure until parts/deformation are complete.

## Implementation notes

Native drive-bys and mounted rounds bypass the occupant checker. Existing vehicle/ped damage hook chains consume their certified contacts too, without changing native arguments or results. Crash breakage follows native severe door (status >= 2) or missing rear bumper (panel 6, status 3) callbacks; it does not invent a new collision-damage coefficient. Side windows, quarter glass and extended fixed glazing are grouped into the six cabin regions. Original geometry references use unattached atomics; private copies are limited to 128 damaged cars, 64 affected atomics per car, and 16 MiB including pending copies. Geometry outside those bounds remains native.

The material audit found translucent badges and lamps. Classification additionally requires the measured 20 cm beltline floor and untextured glazing or known glass textures (`white128a/b/c`, `black64a`, taxi/Patriot glass). This deliberately leaves unfamiliar custom textures native. Native drive-by/mounted damage positions are zero vectors; their certified fire scopes retain the actual glass point for effects.

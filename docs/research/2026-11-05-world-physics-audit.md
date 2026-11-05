# Why the current physical systems do not form one world simulation

2026-11-05. Repository baseline: `72e1773`. This is a source audit, not a new playtest or a claim that the installed build meets the proposed design.

The user's requirement is systemic: vehicles, people, weapons, projectiles, structures and the environment exchange physical loads, with material and functional consequences. More shared damage formulas cannot supply the missing dynamics.

## Findings

| Area | What exists | Missing connection |
| --- | --- | --- |
| Rigid vehicle motion | Native `CPhysical` motion/collision; additional force and control hooks in `flightmodel.cpp`, `planeflight.cpp`, `airportjets.cpp`, `handling.cpp`, `carmechanics.cpp`. | No common registry or step that owns all physical bodies, articulated joints, applied loads and resulting topology. |
| Deformation and fracture | `carglass.cpp` privately rebuilds visible meshes; `airframemodel.h` has three structural sections and fourteen functional damage values. | A cut or dent is not generally the result of solving a load-bearing structure. Collision, mass/inertia and aerodynamic geometry can remain different representations. |
| Materials | `materialmodel.h:11-16` uses calibrated damage-unit costs and absorption volumes, shared between some shots and contacts. | These costs are not a constitutive model with mass, stiffness, yield, plastic strain and fracture energy. Sharing this helper is not sharing a physics solver. |
| Doors and loose parts | Native hinges, temporary debris and supplemental current-mesh collision in `panelphysics.cpp` / `panelcollision.cpp`. | Supplemental door contacts still enter the native parent-body contact system. This is not a generally articulated door body participating in the same constraint solve as a walking person or another vehicle. |
| Aircraft | `planeflight.cpp:47-70` computes lift, drag and thrust from velocity and aggregate damage factors. Helicopter forces and rotor failure also have their own owner. | Surviving physical lifting surfaces, changing mass distribution, wind-relative flow and broken structural connections do not universally determine those forces. |
| People | `reactphysics.cpp:12` has a custom 60 Hz body solver; `reactcontact` sweeps it against native collision. Native locomotion and animation also control roots. | It is not one coupled contact/constraint solve with vehicle structures and debris. Two-way momentum and ownership need explicit treatment. |
| Weapons and bullets | Native instant-hit and bullet-record paths, plus shot-specific geometry/penetration hooks. `ballistics.cpp:34-40,76-80` explicitly scopes some changes to the player. | Not every NPC/mission weapon uses one projectile state, physical ammunition description and momentum/energy transfer path. |
| Explosions and wind | Native explosion forces feed some reaction paths; jetwash separately applies velocity changes and queues ped effects (`jetwash.cpp`, `reactions.cpp`). | No universal pressure/flow field, shielding query and force-at-point interface for all recipients. |
| World geometry | Native COLs, imported visible geometry and special upper-tower collision supplements (`towercollision.cpp`, `airworld.cpp`). | No common material/support topology for world objects, nor a universal mutation path when a wall, window or attachment fails. |
| Verification | Numerous isolated native instruction, geometry, ABI and lifecycle fixtures. | They establish useful local contracts. They do not establish emergent cross-system behavior in the complete live world. |

The previous fixes remain useful compatibility knowledge and regression cases. Their feature-specific state and force ownership should not become the foundation of another layer of exceptions.

## Integration constraints

This repository builds a C++17 arm64 dylib. `tools/patch_macvc.py` patches the supported native executable and exports hook entry points. `install.sh` starts from the saved macVC 1.0.5217 image. It does not build the game engine from source.

The [macVC publisher's README](https://github.com/gtamac/macVC/blob/main/README.md) explicitly describes its public repository as download-only, without source. No separate port source checkout was found in the user's immediate `~/GitHub` workspace. This does not prove private source is unavailable elsewhere.

A direct engine-source integration would make global stepping, world streaming, queries and native solver suppression substantially easier to establish. Under the current binary interface, a replacement simulation must prove all of those contracts; merely loading a physics library into the plugin does not establish ownership.

Existing native velocity conversions use a nominal factor of 50, while the ragdoll has a separate 60 Hz step. Those are not evidence that the whole game runs at a fixed 50 Hz. The new design must separate simulation time, native game units, rendered frames and generated display frames.

## Architectural conclusions

1. Give each body one authoritative motion owner per simulation interval. Native collision/gravity and a new solver must not both advance it.
2. Separate gameplay intent from physical state. AI, missions, controls and animation request actions; physical constraints decide feasible movement. Explicit teleports and cinematic kinematic control remain declared commands.
3. Use one physical asset graph to connect structure, mass, attachments and functional systems. Rendering, collision and aerodynamics consume revisions of that graph.
4. Use explicit units and common load accounting. Forces, impulses, energy and injury are related quantities, not interchangeable scalar damage values.
5. Validate interactions in one solver world, then in the real game. Passing isolated callers alone must not close the wing/building, door/person or explosion/debris acceptance cases.

External engine capabilities and limitations are researched separately in `2026-11-05-world-physics-prior-art.md`.

# Car Projectile Systems Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans inline. Track steps with checkboxes.

**Goal:** Certified bullets injure the vehicle modules their material-limited rays intersect.
**Architecture:** projectilemodel integrates surface/module absorption; shotcollision retains and certifies real bullet paths; carmechanics owns revision-checked delivery into its existing state/physics.
**Tech Stack:** C++17, supported macVC1.0.5217 ARM64, existing shot/asset/native fixtures.
**Spec:** docs/superpowers/specs/2026-11-04-car-projectiles-design.md

## Global Constraints

Use native damage units; body/glass/rubber costs12/4/6 divided by incidence>=0.05.
Module resistance/capacity values copied from spec; exact ray boxes, no collision spread.
At most64 surface crossings; bounded256-slot history; reject invalid/overflowed paths.
No damage from queries, duplicate delivery, native-proof bypass, retained mesh pointers or shared handling writes.
CarMechanics controls this phase; native ammo/health/occupant/explosion semantics remain native.

## Review Focus

1. Correct native damage/tyre/weapon metadata authorizes impacts, including normal fire, drive-bys and deferred bullets.
2. Current visible geometry/material/angle and nearer cover govern penetration; no lost energy or damage behind a stopped round.
3. Repair/restart/pool/clump/model/transform changes retire pending shots; unrelated nested calls do not steal them.
4. Receiver works outside control callbacks, preserves native proofs, and composes with existing module physics/repair.
5. Bounded collection handles duplicate triangles, overflow, malformed meshes, grazing shots and unknown producers conservatively.

### Task 1: Material path and receiver

**Files:** src/projectilemodel.h/.cpp, src/mechanicalmodel.h/.cpp, src/carmechanics.h/.cpp, pure/owner/native tests, Makefile.
**Interfaces:** Layer{fraction,incidence,material}; Ray{from,to,limit,budget,layers,count}; Doses(Layout,Ray); BeginBullet(vehicle)->revision; Projectile(vehicle,revision,Ray). Layout gains original body bounds; integrity-only Apply overload.
- [x] Pure/owner regression RED for locality, material/angle ordering, absorption and outside-control delivery.
- [x] Implement bounded path integration and actor-specific revision leases; GREEN including repair/config/proof/recycling and independent rounds.

### Task 2: Certified native shot integration

**Files:** src/shotcollision.cpp, shot tests, native producer evidence, patcher only if accepted tyre boundary needs a new hook.
**Interfaces:** frozen current-ray surfaces and actor snapshot; native accepted damage/burst consumes once; read-only scoped cover query before native damage; actual native weapon/bullet damage metadata.
- [x] RED tests for body/module delivery, cover/material/duplicates, real impact versus read-only calls and all firing paths.
- [x] Implement collection/certification/cover/lifecycle checks; GREEN, native/asset consumers and bounded invalid paths.

### Task 3: Completion

**Files:** README, verification evidence and necessary reviewed fixes.
- [ ] Full all/test/test-game-assets and focused sanitizers; one independent whole-branch review and one fix pass if needed.
- [ ] Install game closed, verify bytes/signatures, record remaining live/fidelity limits and commit.

## Execution record

- Basee0ea95f on local car-projectiles branch. User clarified continuation is vehicle systems; #39 exploration produced no code changes. Terrain correction is recorded as an open replacement requirement, with current class tuning explicitly interim.
- Existing shotcollision owns the native real-impact boundary; carmechanics currently receives only physical collision events. Reuse those owners rather than treating a generic LOS query as a shot.
- Pure model and receiver RED→GREEN: locality, glass/body/obliquity, absorption/conservation, front/rear engine, wheel/caliper overlap; independent rounds, repair/restart/recycling/render deletion and pose/config/proof gates. Projectile integrity does not introduce collision toe or rack-bending impulses.
- Native shot RED→GREEN: accepted damage, duplicate receipt, read-only nested cover, actual drive-by/deferred/mounted producer contracts, unrelated damage/burst rejection, repair between mesh sampling and impact. Native/asset consumers pass, including Police/Sentinel/Deluxo material paths and actual transmission reduction after a penetrating hit.
- Native contracts from pristine 1.0.5217: DoBulletImpact at0x10028ea70 passes zero position into CVehicle::InflictDamage at0x10028f754; drive-by at0x10028d000 uses type42 and FindPlayerPed; mounted round at0x100291464 uses type23/null creator; deferred Update uses unique unchanged0x38-byte record and velocity*timeStep*0.5; BurstTyre at0x10025b1dc is void and accepts native pieces13..16. Raw retained disassembly is in build/car-projectiles/*-native.disasm.
- Initial complete all/test/test-game-assets run passed. Focused model, receiver and shot fixtures passed ASan+UBSan (shot also stock assets). Final lease timing edit is under verification. Scratch patch and capacity regression pass with155 hooks+6 skips; existing DoBulletImpact table entry is reused.

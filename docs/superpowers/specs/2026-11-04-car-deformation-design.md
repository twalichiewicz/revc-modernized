# Car deformation (#37, part 4)

Continue the approved destruction writeup after the user's parts play check.
The user asked to check Deluxo rear/quarter glass, then proceed to deformation;
their earlier direction to work from the detailed writeup still applies.

Deluxo glass: its five rear triangles have a horizontal/vertical normal ratio
of 0.19–0.23, below the old 0.25 roof exclusion. Lower that cutoff to 0.15;
horizontal roofs remain excluded. A missing rear bumper also breaks fixed rear
quarters, which have no rear-door callback on two-door cars. Actual asset and
owner regressions cover both misses.

Deform only body metal near a confirmed native collision. Capture successful
native ApplyCollision/ApplyCollisionAlt outputs, preserving every argument and
result. Store car-local contact position/direction at that instant. The next
scoped physical VehicleDamage must match the car's native strongest-impulse
record (force, other entity, piece and normal), be at most 200 ms old and cause
native health or component damage before the event is consumed as a dent.
Scripted/unscoped calls and nested script/repair invalidation cannot claim it.
All car identities include pool allocation/generation, model and clump.
The single-contact overload has no other-entity argument: it matches force,
piece and normal; the pair/alternate overloads also require entity equality.

Initial tuning (our choice, not measured GTA IV constants): impulse/mass ×50
gives a speed-change severity; start at 3 m/s, depth (severity−3)×0.018 capped
at 0.18 m per impact, radius 1.25 m. Smooth quadratic spatial falloff and a
0.25 m total displacement cap per vertex keep repeated impacts bounded. One
advance per 350 ms contact episode, independently for front/rear/left/right/roof.
Ignore wheel contacts and upward ground support. Glass vertices and wheel,
interior and LOD atomics remain rigid. Native health, collision hulls, handling,
hinges, model swaps, loose objects and mission protections remain authoritative.

Use the existing private glass mesh owner as the single geometry owner for both
effects. Store each part's deformation offsets in its local frame against the
retained pristine geometry. Rebuilding broken panes retains those offsets;
denting retains collapsed glass triangles. Rebuild normals for dented metal and
expand the native bounding sphere conservatively. All affected visible/hidden
damage variants commit together or roll back together. Repairs restore pristine
geometry before the native repair; deletion releases holders without stale
dereferences. Copies use triangle lists, preserving the previous strip-crash fix.

Bound to 16 concurrently deformed cars, 128 total glass/body records, 64 parts
per car and a shared 16 MiB geometry/offset budget. At the dent-car limit, existing
dents persist and new cars wait for streaming/deletion to free a slot; no visible
car is restored merely to make room. CarDeformation=0 disables collision hooks.

The native dynamic shadow collector reads each atomic's current CPU vertices,
so it must receive these same deformed copies. Verify that seam against the
supported executable and exercise real librw rebuilding in the NULL backend.
Unit/owner/native ABI/asset checks cannot establish live dent appearance, hinge
presentation or Metal rendering; those remain explicit live acceptance checks.

Alternatives: a second geometry owner would overwrite glass or dents; changing
collision meshes would alter driving/missions. Shared visual copies preserve
the established owners and keep this feature reversible.

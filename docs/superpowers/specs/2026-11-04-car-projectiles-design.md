# Projectile damage to vehicle systems

Continue the accepted mechanical-first vehicle work after the fourteen-module
collision implementation. The user explicitly selected vehicle systems and
rejected vehicle-class terrain profiles as the intended material model; that
correction is recorded in the utility-traction design. Do not pursue #39 here.

Accepted native bullets should damage the particular components along their
actual ray. A door shot need not hurt an engine; front and rear engines keep
their existing locations. Body/glass/tyre surfaces consume the bullet's budget,
oblique surfaces consume more, and intervening components absorb what remains.
The same persistent module state then controls native drive, cooling, brakes,
steering, fuel and electrical supply. No per-vehicle damage profile or global
health-to-all-modules conversion.

Retain the existing certified shot owner and native damage/tyre boundaries.
Read-only LOS/mesh queries never apply damage. Native accepted vehicle damage
(actual health reduction), or an accepted tyre burst, authorizes one delivery.
Match the pending ray, impact, actor generation, model, clump and transform;
check native proofs/configuration, and invalidate pending work on repair,
restart or recycling. Nested shots and multiple pellets retain separate ray
ownership. Native ammo, world/occupant damage and mesh collision stay native.

Collect at most64 body/glass/tyre surface crossings while the existing mesh
query already traverses the triangles. Store fractions/material/incidence,
not geometry pointers. Reject overflow/invalid geometry for subsystem effects.
Merge coincident triangles from the same material. Clip the internal ray to
the original model bounds and any nearer real world/occupant/other-car cover.
The cover query is read-only, ignores the struck car temporarily and restores
the prior ignore binding without overwriting a nested owner's change.

Use native damage units as an explicit calibrated penetration budget, not
claimed joules (input is capped at1000 damage units). Initial normal-incidence surface costs: body12, glass4,
rubber6; divide by incidence (minimum0.05). Rendered models do not supply
physical sheet thickness or exact internal meshes: these remain authored
approximations, recorded as such. Existing native body/glass/tyre labels are
the material inputs in this phase.

Intersect the fourteen module boxes without the collision model's0.25m crush
spread. Integrate absorption along the ray, sharing deposition between
overlapping coarse volumes. Resistance per metre / full-damage deposition:
engine80/120, cooling8/10, wheels16/40, brakes12/12, steering20/35,
drivetrain70/100, fuel6/12, electrical10/15. Budget is conserved; a stopped
round cannot damage a component farther along the ray. Bullets change module
integrity without applying collision-induced toe/rack bending impulses.

The receiver shares the bounded256-slot history and native repair/reset rules.
It must accept certified shots outside an automobile control callback, while
physics penalties still require the current native actor scope. Collision
contact cooldowns do not swallow separate rounds. Preserve native tyres and
proof checks. Under CarMechanics=0 the new path is inert.

Validate local/oblique/multilayer/occluded paths, energy conservation, front/
rear engines, wheel/caliper separation, independent rounds and actual native
damage producer contracts. Exercise real mesh assets and native physics
consumers, lifecycles, nested calls and read-only cases; full suite, sanitizers,
one independent review, install and byte/signature checks. Live balance still
needs play. Detailed internal authoring, complete world bullet continuation,
ignition/fire response and persistent saves remain later vehicle work.

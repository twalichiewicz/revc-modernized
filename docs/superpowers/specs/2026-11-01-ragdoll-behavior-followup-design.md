# Ragdoll locomotion, recovery and hit detection

The installed solver still caused routine ambulance exits and car navigation to
knock healthy NPCs down, could strand living bodies in recovery, and let shots
miss visible prone bodies. This follow-up keeps the prior collision/anatomy
certificates and fixes the ownership and native integration defects.

Implementation choices are within the user's existing request to design and
implement a comprehensive fix. No additional design approval is required.

Routine nondamaging native contacts now request pose bracing under native
locomotion authority. The accepted animation trajectory supplies center motion;
rejected pre-collision movement cannot become another physical fall. Damage and
external impulses retain physical authority. A separate, optional contact policy
preserves the pure physics contact interface for actual freely moving bodies.
Raising a speed threshold would leave exit/gait support failures and would hide
the ownership mistake. Disabling reactions would discard the requested behavior.

Recovery retains its normal constraint projection. Only an otherwise rejected
Recovering candidate can retry with consistent whole-core attachment gradients,
within the existing world-query budget and the same anatomy/clearance checks.
Increasing iteration counts alone reproduced the deadlock. Routine contacts
cannot reset an ongoing get-up's impact clock. Real impacts still interrupt it.

All native line hits use stack-local copies of the native ten-sphere hit COL,
projected from a validated read-only reaction pose snapshot. Bounds cover that
pose. Native radii, materials, pieces, closest-hit rules and damage remain native.
Neither shared model COL nor animation matrices are overwritten by a query.
The common collision seam covers native AI, player and sniper callers without
depending on the render phase. Expanding only the old box would still miss
between control and rendering; a player-shot-only patch would miss other callers.
Physical sector bounds merge posed extents inside the existing gridfix hook,
before clipping, preserving its native caller checks and off-grid protections.

Regression seams include actual control/contact/world hooks, full-volume native
rig recovery, and executable replay of pristine arm64 world LOS, native ped COL,
box/sphere collision, and Fall/GetUp damage code. Startup and offline replay do
not establish live visual acceptance of ambulance exits or combat behavior.

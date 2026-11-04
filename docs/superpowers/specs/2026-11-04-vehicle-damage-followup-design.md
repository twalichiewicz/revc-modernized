# Vehicle damage follow-up

The user reports premature firefighter exit, ignition from ordinary shooting,
and missing side/rear bullet breakage; requests stronger localized part loss,
material armor for Enforcer157/Securicar158, and the same physical systems in
missions and aircraft. This supersedes earlier blanket mission/aircraft
exclusions. Preserve explicit scripted state changes and native proof flags;
mission membership alone must not disable physical damage. Do not treat armor
as invincibility, fuselage health as engine health, or perforations as decals.

Evidence and deterministic reproductions precede fixes. Native CCarAI reaches
TellOccupantsToLeaveCar at0x100050810 even with a fire at its destination,
then parks in mission11; losing the driver changes vehicle status and stops
the native cannon. Hold only that routine arrival exit for a healthy on-duty
firetruck while a real fire is in cannon range, and release service normally
after extinction. Direct script/damage/flee/player exits remain authoritative;
generic driver intimidation must not take over an active emergency crew.

Cold bullet damage must not start a fire via aggregate health. Ignition needs
the existing localized running hot-engine/fuel-breach or electrical/fuel-breach
interaction; use8 seconds sustained exposure, with explicit native external
fire still immediate. Water suppression/rearm and60s nonexplosive burnout stay.
Logs should identify ref/model and ignition source inputs rather than imply
that low hull health establishes a fuel fire.

Side/rear empty-car shots must use the certified actual native impact, including
DoBulletImpact's zero damage position. Native windscreen occupant prepasses do
not certify every pane; read-only LOS remains inert. Preserve native front
damage stages, exact live atomics, independent panes and repair.

Armor is an authored layer interaction for157/158 body and glazing. Ordinary
gunfire stopped by the layer cannot reduce whole-vehicle health or hurt protected
occupants/modules. Powerful penetrating rounds retain residual budget; exposed
tyres and unarmored openings, collision, fire and blasts remain vulnerable.
Use calibrated native budget units; do not claim a certified ballistic rating.
Repeated localized bullets can progress actual native bonnet/boot/door/bumper
damage and detach those parts; continuous collision episode limits remain for
crashes, while distinct rounds are independent. Do not globally amplify health
damage to simulate part loss.

Aircraft need a separate component layout and skin: common CVehicle identity
is safe, automobile wheel/door/damage-manager offsets are not valid on CPlane.
Trace visible skin, consume material budget, damage components along the ray,
and feed propulsion/control/lift/drag effects into flight. Use private bounded
geometry for actual penetrated skin openings, retaining textures/normals and
restoring on repair; later rays see openings. Keep original meshes, other actors,
native flight controls and explicit mission actions owned by their systems.
Airframe geometry/capacity and component internals must be documented as
approximations; this is a staged physical model, not a complete War Thunder
simulation or full fluid/structural solver.

Prove real/native call contracts, surface/occupant/part/armor locality, crew
retention through water extinction, mission behavior, guarded exact-size planes,
perforation topology and independent flight failures. Run full tests/assets,
sanitizers, one independent review and one tested Important/Critical fix pass;
install with game closed and verify bytes/signatures. Do not close issues or
claim live presentation/navigation from isolated tests.

# Localized brakes, steering, drivetrain, fuel and electrics

Continuation of the accepted mechanical-first work and successful engine,
cooling and wheel drive test. Keep visual deformation tuning unchanged.

Extend the six existing modules to fourteen: four individual brakes, steering
rack, gearbox/drivetrain, fuel tank and electrical supply. Preserve the original
six indexes and certified collision broker, 350 ms per-module contact episodes,
native protections and actor lifecycle. Modules are coarse volumes derived from
the original wheel positions/body bounds, with the existing rear/mid-engine
exceptions. This is a collision-driven foundation, not projectile penetration
or a detailed reconstruction of every car's internals.

Brake volumes sit slightly inboard of each hub. Brake damage independently
reduces that wheel's service braking (1−0.9d, zero at d>=0.98), composing with
existing assembly damage without independently changing grip, drive or toe.
The rack spans the front axle: steering authority 1−0.8d and an impact-side
bias capped at 0.06 radians change the actual front wheel directions. Rear
wheels retain only existing assembly toe. Native actor steering inputs, caller
vectors and wheel output pointers stay intact.

Drivetrain damage reduces delivered drive by 1−0.8d; electrical damage by
1−0.65d, each zero at d>=0.98. These compose with engine and heat penalties
on the native transmission result. No shared handling or gear storage changes.
Electrical supply also scales custom lamp glow and native car coronas/point
lights (1−0.8d, zero at d>=0.98), scoped to the emitting car. Other lights,
native broken-lamp state, cabin containment and CarLightGlow=0 remain correct.

A breached fuel tank leaks 0.012d² of capacity per second, including while
parked with its engine off. Fuel <=0.01 removes drive torque; impact alone
does not empty the tank. Intact cars do not consume fuel in this phase. Repair
restores all systems, fuel/coolant and heat; render streaming never heals them.
No new ignition, explosion, native engine-start or save format behavior.

Fuel and thermal ticking accepts ordinary steps up to one second in bounded
0.05 s substeps; larger gaps and clock rewinds remain discontinuities. This
closes the previous <=250 ms freeze as new reservoirs share the same clock.
Log fuel bands as well as power bands, and named module integrity transitions.

Prove locality, independent failures, delayed depletion and frame-rate agreement
in the pure model; prove native directions/forces, ownership, repairs and light
scoping in production-hook fixtures. Run real native transmission/wheel
consumers, all original asset layouts, full suite, sanitizers, one independent
review and install with byte/signature verification. Live balance remains for
the user's next drive.

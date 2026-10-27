#pragma once
// First person's movement, as GoldSrc (Counter-Strike 1.6) moves a player:
// on the ground friction slows him (PM_Friction), then the stick's wish
// accelerates him towards its speed (PM_Accelerate); in the air he can only
// add a little speed his way (PM_AirAccelerate). Horizontal only, in m/s.
// The constants are CS's own (docs/research/fps-feel-goldsrc.md §1): about
// 0.4 s to full speed and 0.5 s to a stop, moving from the first frame.
// Unit-tested (tests/test_fpmove.cpp).
#include "types.h"

namespace fpmove {

struct Tuning {
	float accel = 5.0f, friction = 4.0f;          // CS's sv_accelerate, sv_friction
	float stopFraction = 0.3f;                    // stop speed as a fraction of top speed (CS: 75 of 250)
	float airAccel = 10.0f, airWishCap = 0.76f;   // sv_airaccelerate; m/s (CS: 30 u/s)
};

// His velocity after dt s on the ground: friction, then acceleration
// towards `wishDir` (a unit vector, or zero) at `wishSpeed` (at most `topSpeed`).
Vec2 Ground(Vec2 velocity, Vec2 wishDir, float wishSpeed, float topSpeed, float dt, const Tuning &t);
// After dt s in the air: no friction, the wish capped at airWishCap.
Vec2 Air(Vec2 velocity, Vec2 wishDir, float wishSpeed, float dt, const Tuning &t);

} // namespace fpmove

#pragma once
#include "types.h"

// Deterministic aircraft clearance/crash rules. Velocities are metres/second.
namespace aircraft {
struct Contact {
	bool hit = false;
	float fraction = 1.0f;
	Vec3 normal{0, 0, 1};
};
using Sweep = Contact (*)(Vec3 from, Vec3 to, float radius, void *context);
struct Pose { Vec3 position{}, right{1,0,0}, forward{0,1,0}, up{0,0,1}; };
struct Motion { Vec3 position{}, velocity{}, normal{}; bool contact = false; };
// Cabin, boom, and rotor extent travel together; blocked movement slides.
Motion Move(const Pose &pose, Vec3 delta, Vec3 velocity, Sweep sweep, void *context);
// Pairwise relative sweep: keep both body and rotor envelopes apart.
Motion Separate(Vec3 from, Vec3 to, Vec3 velocity, Vec3 other, Vec3 otherVelocity, float dt);
// Returns a clear escape direction, including sideways/backwards when ascent is blocked.
Vec3 Escape(const Pose &pose, Vec3 desired, int side, Sweep sweep, void *context, float reach = 6.0f);
float Spotlight(float daylight, float lamp);

enum class Phase { Flying, Descending, Impacted };
enum class Failure { Pilot, Rotor, Hull };
struct Crash {
	Phase phase = Phase::Flying;
	Failure cause = Failure::Hull;
	Vec3 velocity{};
	float elapsed = 0.0f, fallen = 0.0f;
	void Disable(Failure failure, Vec3 momentum);
	// Contact while descending is the only transition that permits breakup.
	Motion Step(const Pose &pose, float dt, Sweep sweep, void *context);
};
} // namespace aircraft

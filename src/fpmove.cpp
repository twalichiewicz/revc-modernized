#include "fpmove.h"

#include <algorithm>
#include <cmath>

namespace fpmove {
namespace {

constexpr float kStill = 0.025f; // m/s: PM_WalkMove's 1 u/s, below which he stops

float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }

// PM_Accelerate: towards `wishDir`, by at most `limit` this step, up to `wishSpeed` along it.
Vec2 Accelerate(Vec2 v, Vec2 wishDir, float wishSpeed, float limit)
{
	const float add = wishSpeed - Dot(v, wishDir);
	if (add <= 0.0f)
		return v;
	const float step = std::min(add, limit);
	return {v.x + wishDir.x * step, v.y + wishDir.y * step};
}

} // namespace

Vec2 Ground(Vec2 v, Vec2 wishDir, float wishSpeed, float topSpeed, float dt, const Tuning &t)
{
	const float speed = std::hypot(v.x, v.y);
	if (speed > 0.0f) {
		const float control = std::max(speed, t.stopFraction * topSpeed);
		const float scale = std::max(0.0f, speed - control * t.friction * dt) / speed;
		v = {v.x * scale, v.y * scale};
	}
	v = Accelerate(v, wishDir, wishSpeed, t.accel * wishSpeed * dt);
	if (std::hypot(v.x, v.y) < kStill)
		v = {0.0f, 0.0f};
	return v;
}

Vec2 Air(Vec2 v, Vec2 wishDir, float wishSpeed, float dt, const Tuning &t)
{
	return Accelerate(v, wishDir, std::min(wishSpeed, t.airWishCap), t.airAccel * wishSpeed * dt);
}

} // namespace fpmove

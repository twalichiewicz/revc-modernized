#include "footplantmodel.h"
#include <algorithm>
#include <cmath>

namespace footplantmodel {
namespace {
// Exponential approach, independent of the frame rate.
float Approach(float now, float target, float dt, float tau)
{
	return dt > 0 ? now + (target - now) * (1 - std::exp(-dt / tau)) : now;
}
} // namespace

Output Step(State &state, const Foot feet[2], float floor, bool active, float dt)
{
	if (!std::isfinite(dt) || dt < 0)
		dt = 0;
	// How far each foot's ground is above (+) or below (-) the floor he's stood on.
	float lift[2] = {0, 0};
	for (int i = 0; i < 2; i++) {
		const Foot &f = feet[i];
		if (!std::isfinite(floor) || !f.hit || !std::isfinite(f.ground) || !std::isfinite(f.animated))
			continue;
		const float d = f.ground - floor;
		if (std::fabs(d) <= kReach)
			lift[i] = d;
	}
	// Hips down by the deepest reach; each foot up from there to its own ground.
	const float drop = std::max(-kMaxDrop, std::min(0.0f, std::min(lift[0], lift[1])));
	state.weight = Approach(state.weight, active ? 1.0f : 0.0f, dt, kFade);
	state.drop = Approach(state.drop, drop, dt, kEase);
	Output out;
	for (int i = 0; i < 2; i++) {
		state.raise[i] = Approach(state.raise[i], std::clamp(lift[i] - drop, 0.0f, kMaxRaise), dt, kEase);
		out.raise[i] = state.raise[i] * state.weight;
	}
	out.drop = state.drop * state.weight;
	return out;
}

namespace {
V3 Sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 Add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 Mul(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float Len(V3 a) { return std::sqrt(Dot(a, a)); }
bool Finite(V3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
} // namespace

bool Leg(V3 hip, V3 knee, V3 foot, V3 target, V3 &newKnee, V3 &newFoot)
{
	if (!Finite(hip) || !Finite(knee) || !Finite(foot) || !Finite(target))
		return false;
	const V3 thigh = Sub(knee, hip), d = Sub(target, hip);
	const float a = Len(thigh), b = Len(Sub(foot, knee)), reach = Len(d);
	if (a < 1e-4f || b < 1e-4f || reach < 1e-5f)
		return false;
	const V3 u = Mul(d, 1 / reach);
	const float len = std::clamp(reach, std::fabs(a - b) + 1e-3f, a + b - 1e-3f);
	// The knee bends the way the animated one does: its offset from the line to the target.
	V3 v = Sub(thigh, Mul(u, Dot(thigh, u)));
	float side = Len(v);
	if (side < 1e-5f) { // a straight leg along the line: bend toward any perpendicular
		v = std::fabs(u.x) < .9f ? V3{1 - u.x * u.x, -u.x * u.y, -u.x * u.z} : V3{-u.y * u.x, 1 - u.y * u.y, -u.y * u.z};
		side = Len(v);
	}
	v = Mul(v, 1 / side);
	const float x = (a * a - b * b + len * len) / (2 * len), y = std::sqrt(std::max(0.0f, a * a - x * x));
	newKnee = Add(hip, Add(Mul(u, x), Mul(v, y)));
	newFoot = Add(hip, Mul(u, len));
	return true;
}

} // namespace footplantmodel

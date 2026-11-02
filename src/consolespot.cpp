#include "consolespot.h"

#include <cmath>

namespace consolespot {
namespace {

// The console: within this of his hips' height (a bench between the seats, a
// sports car's tall tunnel), flat to this over the gun's length, this much
// room over it (the dash's underside), from this far ahead of his hips.
constexpr float kConsoleBelow = 0.2f, kConsoleAbove = 0.22f, kFlat = 0.05f, kRoom = 0.1f;
constexpr float kAhead[] = {0.1f, 0.2f, 0.0f, 0.3f};
// His own seat's inner edge, beside his right thigh: so far in from his hips.
constexpr float kSeatIn = 0.18f, kSeatBelow = 0.2f, kSeatAbove = 0.1f;
constexpr float kSeatAhead[] = {0.05f, 0.15f};
constexpr float kLookFrom = 0.35f, kLookDepth = 0.8f; // over his hips, and down from there
constexpr float kRest = 0.004f;                        // m over the surface: not into it

Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Mul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// Where (x, y) is over the triangle, its height there and how much it faces up (its normal's z).
bool Over(const Triangle &t, float x, float y, float &z, float &up)
{
	const float d = (t.b.y - t.c.y) * (t.a.x - t.c.x) + (t.c.x - t.b.x) * (t.a.y - t.c.y);
	if (std::fabs(d) < 1e-9f)
		return false; // edge on from above
	const float l1 = ((t.b.y - t.c.y) * (x - t.c.x) + (t.c.x - t.b.x) * (y - t.c.y)) / d;
	const float l2 = ((t.c.y - t.a.y) * (x - t.c.x) + (t.a.x - t.c.x) * (y - t.c.y)) / d;
	const float l3 = 1.0f - l1 - l2;
	if (l1 < 0.0f || l2 < 0.0f || l3 < 0.0f)
		return false;
	z = l1 * t.a.z + l2 * t.b.z + l3 * t.c.z;
	const Vec3 n = Cross(Sub(t.b, t.a), Sub(t.c, t.a));
	up = n.z / std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
	return true;
}

// Nothing over (x, y) between z and z + room (a surface facing either way).
bool Clear(const std::vector<Triangle> &body, float x, float y, float z, float room)
{
	for (const Triangle &t : body) {
		float h, up;
		if (Over(t, x, y, h, up) && h > z + 1e-3f && h < z + room)
			return false;
	}
	return true;
}

// A surface at (x, y) within [low, high], flat for the gun's length along `along`, with room over it.
bool Rests(const std::vector<Triangle> &body, float x, float y, float from, float low, float high, Vec3 along,
           float length, float &z)
{
	if (!Down(body, x, y, from, kLookDepth, z) || z < low || z > high || !Clear(body, x, y, z, kRoom))
		return false;
	for (float k : {-0.3f, 0.3f}) {
		float end;
		if (!Down(body, x + along.x * length * k, y + along.y * length * k, from, kLookDepth, end) ||
		    std::fabs(end - z) > kFlat)
			return false;
	}
	return true;
}

} // namespace

bool Down(const std::vector<Triangle> &body, float x, float y, float from, float depth, float &z)
{
	bool found = false;
	for (const Triangle &t : body) {
		float h, up;
		if (Over(t, x, y, h, up) && up > 0.5f && h <= from && h >= from - depth && (!found || h > z)) {
			z = h;
			found = true;
		}
	}
	return found;
}

Spot Find(const std::vector<Triangle> &body, Vec3 seat, Vec3 along, float length)
{
	const float from = seat.z + kLookFrom;
	float z;
	for (float ahead : kAhead)
		if (Rests(body, 0.0f, seat.y + ahead, from, seat.z - kConsoleBelow, seat.z + kConsoleAbove, along, length, z))
			return {{0.0f, seat.y + ahead, z}, along, Kind::kConsole};
	// His seat's inner edge is towards the middle from his hips, whichever side he sits.
	const float in = seat.x < 0.0f ? seat.x + kSeatIn : seat.x - kSeatIn;
	const Vec3 ahead{0.0f, 1.0f, 0.0f};
	for (float y : kSeatAhead)
		if (Rests(body, in, seat.y + y, from, seat.z - kSeatBelow, seat.z + kSeatAbove, ahead, length, z))
			return {{in, seat.y + y, z}, ahead, Kind::kSeat};
	return {{in, seat.y + kSeatAhead[0], seat.z - 0.05f}, ahead, Kind::kGuess};
}

Pose Lay(const Spot &spot, Vec3 boxMin, Vec3 boxMax)
{
	Pose p;
	p.x = spot.along;
	p.y = {0.0f, 0.0f, 1.0f};
	p.z = Cross(p.x, p.y);
	// The box's middle (along and across) over the spot, its underside (its y's least) on it.
	const Vec3 middle{(boxMin.x + boxMax.x) * 0.5f, boxMin.y, (boxMin.z + boxMax.z) * 0.5f};
	const Vec3 placed = Add(Add(Mul(p.x, middle.x), Mul(p.y, middle.y)), Mul(p.z, middle.z));
	p.origin = Sub(Add(spot.at, Vec3{0.0f, 0.0f, kRest}), placed);
	return p;
}

} // namespace consolespot

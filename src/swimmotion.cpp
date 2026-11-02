#include "swimmotion.h"
#include <algorithm>
#include <cmath>
namespace swimmotion
{
namespace
{
CVector Add(CVector a, CVector b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}
CVector Mul(CVector a, float k)
{
	return {a.x * k, a.y * k, a.z * k};
}
float Dot(CVector a, CVector b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
float Length(CVector a)
{
	return std::sqrt(Dot(a, a));
}
bool Blocked(CVector p, CVector motion, const Shape &s, Query query, void *ctx, Contact &hit)
{
	// Spheres overlap along the centre line, covering the head, torso and legs.
	const int count = std::clamp(int(std::ceil(2 * s.halfLength / s.radius)), 1, 80);
	bool blocked = false;
	for (int i = 0; i <= count; ++i) {
		const CVector centre = Add(p, Mul(s.axis, -s.halfLength + 2 * s.halfLength * float(i) / count));
		Contact c;
		if (query(ctx, centre, s.radius, motion, c) && Dot(motion, c.normal) <= 1e-7f) {
			if (!blocked || c.depth > hit.depth)
				hit = c;
			blocked = true;
		}
	}
	return blocked;
}
} // namespace
CVector Move(CVector from, CVector delta, const Shape &shape, Query query, void *ctx)
{
	if (!query || !std::isfinite(Length(delta)))
		return from;
	Shape s = shape;
	s.radius = std::clamp(s.radius, 0.05f, 1.0f);
	const float axisLength = Length(s.axis);
	s.axis = axisLength > 1e-6f ? Mul(s.axis, 1 / axisLength) : CVector{0, 0, 1};
	s.halfLength = std::clamp(s.halfLength, 0.0f, 2.0f);
	CVector at = from;
	int budget = 128;
	for (int plane = 0; plane < 3 && budget > 0; ++plane) {
		const float length = Length(delta);
		if (length < 1e-6f)
			break;
		const int wanted = std::max(1, int(std::ceil(std::min(length, 10000.0f) / (s.radius * .35f))));
		const CVector step = Mul(delta, 1.0f / wanted);
		const int checks = std::min(wanted, budget);
		bool collided = false;
		for (int i = 0; i < checks; ++i) {
			--budget;
			Contact hit;
			if (!Blocked(Add(at, step), step, s, query, ctx, hit)) {
				at = Add(at, step);
				continue;
			}
			float lo = 0, hi = 1;
			for (int n = 0; n < 8; ++n) {
				const float mid = (lo + hi) * .5f;
				Contact sample;
				if (Blocked(Add(at, Mul(step, mid)), step, s, query, ctx, sample))
					hi = mid;
				else
					lo = mid;
			}
			at = Add(at, Mul(step, std::max(0.0f, lo - 0.001f)));
			delta = Mul(step, float(wanted - i) - lo);
			const float n = Length(hit.normal);
			if (n < 1e-6f)
				return at;
			hit.normal = Mul(hit.normal, 1 / n);
			delta = Add(delta, Mul(hit.normal, -std::min(0.0f, Dot(delta, hit.normal))));
			collided = true;
			break;
		}
		if (!collided)
			break;
	}
	return at;
}
} // namespace swimmotion

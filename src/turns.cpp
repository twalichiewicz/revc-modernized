#include "turns.h"

#include <algorithm>
#include <cmath>

namespace turns {
namespace {

// The point `d` metres along the route (its ends beyond them).
Vec2 At(const std::vector<Vec2> &points, const std::vector<float> &along, float d)
{
	for (size_t i = 1; i < points.size(); i++)
		if (d <= along[i]) {
			const float t = std::clamp((d - along[i - 1]) / std::max(along[i] - along[i - 1], 1e-6f), 0.0f, 1.0f);
			return {points[i - 1].x + (points[i].x - points[i - 1].x) * t,
			        points[i - 1].y + (points[i].y - points[i - 1].y) * t};
		}
	return points.back();
}

} // namespace

Turn Next(const std::vector<Vec2> &points, float minAngle, float window, float lookAhead)
{
	if (points.size() < 3)
		return {};
	std::vector<float> along(points.size(), 0.0f);
	for (size_t i = 1; i < points.size(); i++)
		along[i] = along[i - 1] + std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
	for (size_t i = 1; i + 1 < points.size() && along[i] <= lookAhead; i++) {
		const Vec2 a = At(points, along, along[i] - window), b = points[i], c = At(points, along, along[i] + window);
		const float ix = b.x - a.x, iy = b.y - a.y, ox = c.x - b.x, oy = c.y - b.y;
		if (ix * ix + iy * iy < 1e-4f || ox * ox + oy * oy < 1e-4f)
			continue;
		// Anticlockwise (x east, y north) is to the left.
		const float angle = std::atan2(ix * oy - iy * ox, ix * ox + iy * oy);
		if (std::fabs(angle) >= minAngle)
			return {along[i], angle > 0.0f ? kLeft : kRight};
	}
	return {};
}

} // namespace turns

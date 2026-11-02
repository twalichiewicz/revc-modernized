#include "gridclip.h"

#include <algorithm>
#include <cmath>

namespace gridclip {

bool Clip(Vec3 a, Vec3 b, Vec3 &from, Vec3 &to)
{
	if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(a.z) || !std::isfinite(b.x) || !std::isfinite(b.y) ||
	    !std::isfinite(b.z))
		return false; // (a broken point: nothing to look along)
	const float x0 = kMinX + kInside, x1 = kMaxX - kInside, y0 = kMinY + kInside, y1 = kMaxY - kInside;
	const float dx = b.x - a.x, dy = b.y - a.y;
	// Liang-Barsky: how far along the line it comes onto the grid, and leaves it.
	const float p[4] = {-dx, dx, -dy, dy}, q[4] = {a.x - x0, x1 - a.x, a.y - y0, y1 - a.y};
	float t0 = 0.0f, t1 = 1.0f;
	for (int i = 0; i < 4; i++) {
		if (p[i] == 0.0f) {
			if (q[i] < 0.0f)
				return false; // along an edge, off it
			continue;
		}
		const float t = q[i] / p[i];
		if (p[i] < 0.0f)
			t0 = std::max(t0, t);
		else
			t1 = std::min(t1, t);
	}
	if (t0 > t1)
		return false;
	from = {a.x + dx * t0, a.y + dy * t0, a.z + (b.z - a.z) * t0};
	to = {a.x + dx * t1, a.y + dy * t1, a.z + (b.z - a.z) * t1};
	return true;
}

} // namespace gridclip

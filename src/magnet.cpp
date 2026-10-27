#include "magnet.h"

#include <cmath>

namespace magnet {

int Nearest(const std::vector<Vec2> &icons, Vec2 p, float within)
{
	int best = -1;
	float bestDist = within;
	for (size_t i = 0; i < icons.size(); i++) {
		const float d = std::hypot(icons[i].x - p.x, icons[i].y - p.y);
		if (d <= bestDist) {
			best = int(i);
			bestDist = d;
		}
	}
	return best;
}

Vec2 Step(Vec2 cursor, Vec2 move, bool mapMoving, const std::vector<Vec2> &icons, const Tuning &tuning, float dt)
{
	if (move.x != 0.0f || move.y != 0.0f) {
		const float speed = Nearest(icons, cursor, tuning.grip) >= 0 ? tuning.friction : 1.0f;
		return {cursor.x + move.x * speed, cursor.y + move.y * speed};
	}
	const int near = mapMoving ? -1 : Nearest(icons, cursor, tuning.reach);
	if (near < 0)
		return cursor;
	const Vec2 to = icons[near];
	if (std::hypot(to.x - cursor.x, to.y - cursor.y) < 0.5f)
		return to;
	const float k = 1.0f - std::exp(-tuning.rate * dt);
	return {cursor.x + (to.x - cursor.x) * k, cursor.y + (to.y - cursor.y) * k};
}

} // namespace magnet

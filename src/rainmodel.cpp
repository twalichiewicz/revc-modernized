#include "rainmodel.h"
#include <algorithm>
#include <cmath>

namespace rainmodel {

Weather Model::Update(float gameRain, float gameWetRoads, float dt)
{
	w_.rain = std::clamp(gameRain, 0.0f, 1.0f);
	const float target = std::clamp(gameWetRoads, 0.0f, 1.0f);
	w_.wetness += (target - w_.wetness) * (1 - std::exp(-std::max(dt, 0.0f) / kWetEase));
	const float step = std::max(dt, 0.0f), most = std::min(1.0f, w_.rain / kFullRain);
	if (w_.rain <= kRaining)
		w_.puddles -= step / kPuddleDrain;
	else if (w_.puddles < most)
		w_.puddles = std::min(most, w_.puddles + step / kPuddleFill);
	else
		w_.puddles = std::max(most, w_.puddles - step / kPuddleDrain); // the rain eased: down to what it keeps up
	w_.puddles = std::clamp(w_.puddles, 0.0f, 1.0f);
	return w_;
}

int StreakCount(float rain, bool sheltered)
{
	if (sheltered || rain < kRaining)
		return 0;
	return int(std::lround(kMaxStreaks * std::min(1.0f, (rain - kRaining) / (1 - kRaining))));
}

namespace {

uint32_t Hash(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}
double Unit(uint32_t h) { return double(h & 0xffffff) / double(0x1000000); }
// x wrapped into [from, from + size).
double Wrap(double x, double from, double size) { return from + (x - from) - size * std::floor((x - from) / size); }

} // namespace

std::vector<Streak> Streaks(Vec3 camera, Vec2 wind, int count, float seconds, const std::function<bool(Vec3)> &covered)
{
	std::vector<Streak> out;
	out.reserve(size_t(std::max(count, 0)));
	const double fall = std::sqrt(double(wind.x) * wind.x + double(wind.y) * wind.y + double(kFallSpeed) * kFallSpeed);
	const double dx = wind.x / fall, dy = wind.y / fall, dz = -kFallSpeed / fall; // along the fall
	const double x0 = camera.x - kStreakBox / 2, y0 = camera.y - kStreakBox / 2, z0 = camera.z - kStreakBelow;
	for (int i = 0; i < count; i++) {
		const uint32_t a = Hash(uint32_t(i) * 3 + 1), b = Hash(uint32_t(i) * 3 + 2), c = Hash(uint32_t(i) * 3 + 3);
		const double speed = 0.85 + 0.3 * Unit(Hash(a ^ c)); // not all at once, or they fall as a sheet
		const double t = double(seconds) * speed;
		// A place in a world-fixed lattice of boxes, moved by the rain, then the copy in his box.
		const double x = Wrap(Unit(a) * kStreakBox + wind.x * t, x0, kStreakBox);
		const double y = Wrap(Unit(b) * kStreakBox + wind.y * t, y0, kStreakBox);
		const double z = Wrap(Unit(c) * kStreakHeight - kFallSpeed * t, z0, kStreakHeight);
		const Vec3 top{float(x), float(y), float(z)};
		const Vec3 bottom{float(x + dx * kStreakLength), float(y + dy * kStreakLength), float(z + dz * kStreakLength)};
		if (covered(top) || covered(bottom))
			continue;
		out.push_back({top, bottom});
	}
	return out;
}

bool Inside(const Box &b, Vec3 p)
{
	const float dx = p.x - b.centre.x, dy = p.y - b.centre.y, dz = p.z - b.centre.z;
	auto along = [&](Vec3 a) { return std::fabs(dx * a.x + dy * a.y + dz * a.z); };
	return along(b.right) <= b.half.x && along(b.forward) <= b.half.y && along(b.up) <= b.half.z;
}

Vec3 Lit(Vec3 p, Vec3 sky, const std::vector<Lamp> &lamps)
{
	Vec3 c = sky;
	for (const Lamp &l : lamps) {
		if (!(l.radius > 0))
			continue;
		const float d = std::sqrt((p.x - l.pos.x) * (p.x - l.pos.x) + (p.y - l.pos.y) * (p.y - l.pos.y) +
		                          (p.z - l.pos.z) * (p.z - l.pos.z));
		const float k = std::max(0.0f, 1 - d / l.radius);
		c = {c.x + l.colour.x * k * k, c.y + l.colour.y * k * k, c.z + l.colour.z * k * k};
	}
	return c;
}

} // namespace rainmodel

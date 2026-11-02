#include "wetmodel.h"

#include <algorithm>
#include <cmath>

namespace wetmodel {
namespace {

constexpr float kFarthest = 120.0f; // m: lights farther off leave no streak worth drawing
constexpr float kMaxStretch = 10.0f; // a streak is at most this many times as long as it's wide

float Cross(Vec3 o, Vec3 a, float px, float py) { return (a.x - o.x) * (py - o.y) - (a.y - o.y) * (px - o.x); }

} // namespace

std::vector<int> CellsNear(const mapdata::Map &map, Vec3 camera, float radius)
{
	std::vector<int> out;
	for (size_t i = 0; i < map.wet.size(); i++) {
		const float x0 = mapdata::kWorldMin + map.wet[i].cx * mapdata::kRainTile, y0 = mapdata::kWorldMin + map.wet[i].cy * mapdata::kRainTile;
		const float dx = std::max({x0 - camera.x, 0.0f, camera.x - (x0 + mapdata::kRainTile)});
		const float dy = std::max({y0 - camera.y, 0.0f, camera.y - (y0 + mapdata::kRainTile)});
		if (dx * dx + dy * dy <= radius * radius)
			out.push_back(int(i));
	}
	return out;
}

bool WetGroundAt(const mapdata::Map &map, Vec3 p, float &z)
{
	const int cx = int(std::floor((p.x - mapdata::kWorldMin) / mapdata::kRainTile));
	const int cy = int(std::floor((p.y - mapdata::kWorldMin) / mapdata::kRainTile));
	bool found = false;
	for (const mapdata::WetCell &cell : map.wet) {
		if (std::abs(cell.cx - cx) > 1 || std::abs(cell.cy - cy) > 1)
			continue; // a triangle belongs to the cell of its middle, so look next door too
		for (size_t t = 0; t + 2 < cell.tris.size(); t += 3) {
			const Vec3 &a = cell.tris[t], &b = cell.tris[t + 1], &c = cell.tris[t + 2];
			const float area = Cross(a, b, c.x, c.y);
			if (std::fabs(area) < 1e-6f)
				continue;
			const float wa = Cross(b, c, p.x, p.y) / area, wb = Cross(c, a, p.x, p.y) / area, wc = 1 - wa - wb;
			if (wa < -1e-4f || wb < -1e-4f || wc < -1e-4f)
				continue;
			const float h = wa * a.z + wb * b.z + wc * c.z;
			if (h <= p.z && (!found || h > z)) {
				z = h;
				found = true;
			}
		}
	}
	return found;
}

std::vector<Reflection> Reflections(const std::vector<Light> &lights, Vec3 camera, const GroundAt &ground, int cap)
{
	struct Scored {
		float score;
		Reflection r;
	};
	std::vector<Scored> all;
	for (const Light &light : lights) {
		const float bright = std::max({light.colour.x, light.colour.y, light.colour.z});
		const float dx = camera.x - light.pos.x, dy = camera.y - light.pos.y, distance = std::hypot(dx, dy);
		float z = 0;
		if (!(bright > 0) || !(light.radius > 0) || distance > kFarthest || distance < 0.5f || !ground(light.pos, z))
			continue;
		const float ux = dx / distance, uy = dy / distance; // toward him
		const float above = std::max(0.5f, camera.z - z);
		const float width = std::clamp(light.radius * 0.3f, 0.6f, 4.0f);
		// The view's grazing angle at the foot (its cotangent) stretches it.
		const float length = std::min(distance * 0.9f, width * std::clamp(1 + 0.6f * distance / above, 1.0f, kMaxStretch));
		const float back = width * 0.25f, half = width / 2;
		const Vec3 from{light.pos.x - ux * back, light.pos.y - uy * back, z};
		const Vec3 to{light.pos.x + ux * length, light.pos.y + uy * length, z};
		Reflection r;
		r.quad[0] = {from.x - uy * half, from.y + ux * half, z};
		r.quad[1] = {from.x + uy * half, from.y - ux * half, z};
		r.quad[2] = {to.x + uy * half, to.y - ux * half, z};
		r.quad[3] = {to.x - uy * half, to.y + ux * half, z};
		r.colour = light.colour;
		all.push_back({bright * light.radius / (1 + distance), r});
	}
	std::stable_sort(all.begin(), all.end(), [](const Scored &a, const Scored &b) { return a.score > b.score; });
	std::vector<Reflection> out;
	for (size_t i = 0; i < all.size() && int(i) < cap; i++)
		out.push_back(all[i].r);
	return out;
}

} // namespace wetmodel

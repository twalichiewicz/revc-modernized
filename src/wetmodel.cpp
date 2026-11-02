#include "wetmodel.h"

#include <algorithm>
#include <cmath>

namespace wetmodel {
namespace {

constexpr float kFarthest = 120.0f; // m: lights farther off leave no streak worth drawing
constexpr float kMaxStretch = 10.0f; // a streak is at most this many times as long as it's wide

float Cross(Vec3 o, Vec3 a, float px, float py) { return (a.x - o.x) * (py - o.y) - (a.y - o.y) * (px - o.x); }

uint32_t Hash(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}
float Unit(uint32_t h) { return float(h & 0xffffff) / float(0x1000000); }

constexpr float kPuddleLevel = 0.05f; // less is a damp road, no standing water
constexpr int kPuddlesPerCell = 4;
constexpr float kFlat = 0.998f;       // the normal's z: water stands only on the flattest ground
constexpr float kSplashLife = 0.3f;   // s
constexpr int kSplashes = 260, kPerRoof = 7; // in a downpour

// The distance from p to the nearest edge of triangle abc (inside it), in plan.
float EdgeDistance(Vec3 a, Vec3 b, Vec3 c, float px, float py)
{
	const Vec3 v[3] = {a, b, c};
	float d = 1e9f;
	for (int e = 0; e < 3; e++) {
		const Vec3 p = v[e], q = v[(e + 1) % 3];
		const float len = std::hypot(q.x - p.x, q.y - p.y);
		if (len > 1e-6f)
			d = std::min(d, std::fabs(Cross(p, q, px, py)) / len);
	}
	return d;
}

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

namespace {
constexpr float kBin = 2.0f;
// h if p is over triangle abc (in plan), with h its height there.
bool Over(const Vec3 &a, const Vec3 &b, const Vec3 &c, Vec3 p, float &h)
{
	const float area = Cross(a, b, c.x, c.y);
	if (std::fabs(area) < 1e-6f)
		return false;
	const float wa = Cross(b, c, p.x, p.y) / area, wb = Cross(c, a, p.x, p.y) / area, wc = 1 - wa - wb;
	if (wa < -1e-4f || wb < -1e-4f || wc < -1e-4f)
		return false;
	h = wa * a.z + wb * b.z + wc * c.z;
	return true;
}
} // namespace

GroundIndex::GroundIndex(const mapdata::Map &map, Vec3 centre, float reach)
    : x0_(centre.x - reach), y0_(centre.y - reach), side_(std::max(1, int(std::ceil(2 * reach / kBin))))
{
	bins_.resize(size_t(side_) * size_t(side_));
	for (int c : CellsNear(map, centre, reach)) {
		const std::vector<Vec3> &tris = map.wet[size_t(c)].tris;
		for (size_t t = 0; t + 2 < tris.size(); t += 3) {
			const Vec3 &a = tris[t], &b = tris[t + 1], &d = tris[t + 2];
			const int bx0 = std::max(0, int(std::floor((std::min({a.x, b.x, d.x}) - x0_) / kBin)));
			const int bx1 = std::min(side_ - 1, int(std::floor((std::max({a.x, b.x, d.x}) - x0_) / kBin)));
			const int by0 = std::max(0, int(std::floor((std::min({a.y, b.y, d.y}) - y0_) / kBin)));
			const int by1 = std::min(side_ - 1, int(std::floor((std::max({a.y, b.y, d.y}) - y0_) / kBin)));
			if (bx1 < bx0 || by1 < by0)
				continue;
			const uint32_t start = uint32_t(tris_.size());
			tris_.insert(tris_.end(), {a, b, d});
			for (int y = by0; y <= by1; y++)
				for (int x = bx0; x <= bx1; x++)
					bins_[size_t(y) * size_t(side_) + size_t(x)].push_back(start);
		}
	}
}

bool GroundIndex::At(Vec3 p, float &z) const
{
	const int x = int(std::floor((p.x - x0_) / kBin)), y = int(std::floor((p.y - y0_) / kBin));
	if (x < 0 || y < 0 || x >= side_ || y >= side_)
		return false;
	bool found = false;
	for (uint32_t t : bins_[size_t(y) * size_t(side_) + size_t(x)]) {
		float h;
		if (Over(tris_[t], tris_[t + 1], tris_[t + 2], p, h) && h <= p.z && (!found || h > z)) {
			z = h;
			found = true;
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

std::vector<Puddle> Puddles(const mapdata::WetCell &cell, float level)
{
	std::vector<Puddle> out;
	if (!(level > kPuddleLevel))
		return out;
	struct Candidate {
		size_t tri;
		float flat;
	};
	std::vector<Candidate> flat;
	for (size_t t = 0; t + 2 < cell.tris.size(); t += 3) {
		const Vec3 &a = cell.tris[t], &b = cell.tris[t + 1], &c = cell.tris[t + 2];
		const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
		const float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
		const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
		const float z = len > 0 ? std::fabs(nz) / len : 0;
		if (z >= kFlat && len / 2 > 6.0f) // and big enough to hold one
			flat.push_back({t, z});
	}
	std::stable_sort(flat.begin(), flat.end(), [](const Candidate &l, const Candidate &r) { return l.flat > r.flat; });
	const uint32_t seed = Hash(uint32_t(uint16_t(cell.cx)) * 73856093u ^ uint32_t(uint16_t(cell.cy)) * 19349663u);
	const size_t pool = std::min(flat.size(), size_t(kPuddlesPerCell * 3)); // among the flattest
	std::vector<bool> taken(pool, false);
	for (int k = 0; k < kPuddlesPerCell && k < int(pool); k++) {
		size_t pick = size_t(Hash(seed + uint32_t(k) * 2654435761u) % pool);
		while (taken[pick])
			pick = (pick + 1) % pool;
		taken[pick] = true;
		const size_t t = flat[pick].tri;
		const Vec3 &a = cell.tris[t], &b = cell.tris[t + 1], &c = cell.tris[t + 2];
		// Somewhere about its middle, then as big as fits on it.
		const uint32_t h = Hash(seed ^ uint32_t(t) * 2246822519u);
		float wa = 1.0f / 3 + 0.25f * (Unit(h) - 0.5f), wb = 1.0f / 3 + 0.25f * (Unit(Hash(h)) - 0.5f);
		const float wc = 1 - wa - wb;
		const Vec3 p{wa * a.x + wb * b.x + wc * c.x, wa * a.y + wb * b.y + wc * c.y, wa * a.z + wb * b.z + wc * c.z};
		const float fits = EdgeDistance(a, b, c, p.x, p.y) * 0.9f;
		const float want = 0.8f + 1.7f * Unit(Hash(h + 1));
		if (fits > 0.3f)
			out.push_back({p, std::min(want, fits) * std::min(level, 1.0f)});
	}
	return out;
}

std::vector<Splash> Splashes(Vec3 camera, float rain, float seconds, const GroundAt &ground, const std::vector<Roof> &roofs)
{
	std::vector<Splash> out;
	const float amount = std::clamp(rain, 0.0f, 1.0f);
	const int count = int(std::lround(kSplashes * amount)), perRoof = int(std::lround(kPerRoof * amount));
	const int slots = count + perRoof * int(roofs.size());
	for (int i = 0; i < slots; i++) {
		// A slot bursts kSplashLife long, then again somewhere else.
		const float phase = Unit(Hash(uint32_t(i) * 0x9e3779b9u));
		const double cycle = double(seconds) / kSplashLife + phase;
		const uint32_t k = uint32_t(int64_t(std::floor(cycle)));
		const float age = float(cycle - std::floor(cycle));
		const uint32_t h = Hash(uint32_t(i) * 0x85ebca6bu ^ k * 0xc2b2ae35u);
		const float size = 0.18f + 0.22f * Unit(Hash(h + 7));
		if (i >= count) {
			const Roof &r = roofs[size_t((i - count) / std::max(perRoof, 1))];
			const float u = (Unit(h) - 0.5f) * 0.9f, v = (Unit(Hash(h)) - 0.5f) * 1.4f;
			out.push_back({{r.centre.x + r.right.x * u + r.forward.x * v, r.centre.y + r.right.y * u + r.forward.y * v,
			                r.centre.z},
			               size * 0.7f, age});
			continue;
		}
		// Round him, more of them near (where they're seen).
		const float angle = 6.2831853f * Unit(h), reach = 1.5f + (kSplashReach - 1.5f) * Unit(Hash(h));
		const Vec3 at{camera.x + std::cos(angle) * reach, camera.y + std::sin(angle) * reach, camera.z + 3};
		float z = 0;
		if (ground(at, z))
			out.push_back({{at.x, at.y, z}, size, age});
	}
	return out;
}

} // namespace wetmodel

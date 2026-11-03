#include "wetground.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace wetground {
namespace {

constexpr float kCell = mapdata::kRainTile / mapdata::kCoverRes; // 1 m
constexpr int kSub = 2;                                          // samples a side in each: all must be covered
constexpr float kUpright = 0.7f;                                  // |normal z|: ground, not a wall

float Area2(const Face &f, float &nz)
{
	const float ux = f.b.x - f.a.x, uy = f.b.y - f.a.y, uz = f.b.z - f.a.z;
	const float vx = f.c.x - f.a.x, vy = f.c.y - f.a.y, vz = f.c.z - f.a.z;
	const float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz;
	nz = ux * vy - uy * vx;
	return std::sqrt(nx * nx + ny * ny + nz * nz);
}

} // namespace

bool Paved(uint8_t surface) { return surface == 1 || surface == 5 || surface == 34; }

bool Covers(uint8_t surface) { return surface != 12 && surface != 13 && surface != 14 && surface != 16; }

std::vector<mapdata::CoverTile> CoverTiles(const std::vector<Face> &faces)
{
	// The top of what's over each sample point (kSub x kSub a metre); a metre's
	// is the lowest of its samples', so it's only covered where all of it is.
	constexpr int side = mapdata::kCoverRes * kSub;
	std::map<std::pair<int, int>, std::vector<float>> tiles; // samples, by tile
	const int cells = mapdata::kRainTiles * side;
	for (const Face &f : faces) {
		if (!Covers(f.surface))
			continue;
		// Max-z rasterisation: every sample point the face covers.
		auto toCell = [](float w) { return (w - mapdata::kWorldMin) / (kCell / kSub); };
		const float ax = toCell(f.a.x), ay = toCell(f.a.y), bx = toCell(f.b.x), by = toCell(f.b.y);
		const float cx = toCell(f.c.x), cy = toCell(f.c.y);
		const float area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
		if (!std::isfinite(area) || std::fabs(area) < 1e-6f)
			continue;
		const int x0 = std::max(0, int(std::floor(std::min({ax, bx, cx})))), x1 = std::min(cells - 1, int(std::floor(std::max({ax, bx, cx}))));
		const int y0 = std::max(0, int(std::floor(std::min({ay, by, cy})))), y1 = std::min(cells - 1, int(std::floor(std::max({ay, by, cy}))));
		if (x1 < x0 || y1 < y0 || (x1 - x0) * (y1 - y0) > 400 * 400)
			continue;
		for (int y = y0; y <= y1; y++)
			for (int x = x0; x <= x1; x++) {
				const float px = x + 0.5f, py = y + 0.5f;
				const float w0 = ((bx - px) * (cy - py) - (cx - px) * (by - py)) / area;
				const float w1 = ((cx - px) * (ay - py) - (ax - px) * (cy - py)) / area;
				const float w2 = 1.0f - w0 - w1;
				if (w0 < -0.02f || w1 < -0.02f || w2 < -0.02f)
					continue;
				const float z = w0 * f.a.z + w1 * f.b.z + w2 * f.c.z;
				auto [it, fresh] = tiles.try_emplace({y / side, x / side});
				if (fresh)
					it->second.assign(size_t(side) * side, -1e9f);
				float &top = it->second[size_t(y % side) * side + size_t(x % side)];
				top = std::max(top, z);
			}
	}
	std::vector<mapdata::CoverTile> out;
	for (const auto &[key, samples] : tiles) {
		mapdata::CoverTile tile;
		tile.cx = int16_t(key.second);
		tile.cy = int16_t(key.first);
		for (int y = 0; y < mapdata::kCoverRes; y++)
			for (int x = 0; x < mapdata::kCoverRes; x++) {
				float low = 1e9f;
				for (int sy = 0; sy < kSub; sy++)
					for (int sx = 0; sx < kSub; sx++)
						low = std::min(low, samples[size_t(y * kSub + sy) * side + size_t(x * kSub + sx)]);
				tile.top[y * mapdata::kCoverRes + x] =
				    low < -1e8f ? mapdata::kOpenSky : int16_t(std::clamp(std::lround(low * 10), -32767L, 32767L));
			}
		out.push_back(tile);
	}
	return out;
}

std::vector<mapdata::WetCell> WetGround(const std::vector<Face> &faces, const mapdata::Map &covered,
                                        const std::function<bool(Vec3)> &onRoad)
{
	// Points across a triangle (barycentric): its middle and six more about it.
	static const float kSamples[7][3] = {{1 / 3.0f, 1 / 3.0f, 1 / 3.0f}, {0.66f, 0.17f, 0.17f}, {0.17f, 0.66f, 0.17f},
	                                     {0.17f, 0.17f, 0.66f}, {0.5f, 0.25f, 0.25f}, {0.25f, 0.5f, 0.25f}, {0.25f, 0.25f, 0.5f}};
	std::map<std::pair<int, int>, std::vector<std::pair<float, const Face *>>> cells;
	for (const Face &f : faces) {
		float nz;
		const float area2 = Area2(f, nz);
		if (!(area2 > 1e-6f) || -nz / area2 < kUpright)
			continue; // a wall, or a ceiling
		const Vec3 mid{(f.a.x + f.b.x + f.c.x) / 3, (f.a.y + f.b.y + f.c.y) / 3, (f.a.z + f.b.z + f.c.z) / 3};
		if (!Paved(f.surface) && !(onRoad && onRoad(mid)))
			continue; // not paved, and no road on it
		int under = 0;
		for (const auto &w : kSamples) {
			const Vec3 p{w[0] * f.a.x + w[1] * f.b.x + w[2] * f.c.x, w[0] * f.a.y + w[1] * f.b.y + w[2] * f.c.y,
			             w[0] * f.a.z + w[1] * f.b.z + w[2] * f.c.z + kRoofClearance};
			under += mapdata::CoveredAt(covered, p);
		}
		if (under * 2 >= 7)
			continue; // mostly under a roof or a deck
		const int cx = int(std::floor((mid.x - mapdata::kWorldMin) / mapdata::kRainTile));
		const int cy = int(std::floor((mid.y - mapdata::kWorldMin) / mapdata::kRainTile));
		if (cx < 0 || cy < 0 || cx >= mapdata::kRainTiles || cy >= mapdata::kRainTiles)
			continue;
		cells[{cy, cx}].push_back({area2, &f});
	}
	std::vector<mapdata::WetCell> out;
	for (auto &[key, list] : cells) {
		if (list.size() > size_t(kWetTrisPerCell)) {
			std::stable_sort(list.begin(), list.end(), [](const auto &l, const auto &r) { return l.first > r.first; });
			list.resize(size_t(kWetTrisPerCell));
		}
		mapdata::WetCell cell;
		cell.cx = int16_t(key.second);
		cell.cy = int16_t(key.first);
		for (const auto &entry : list)
			cell.tris.insert(cell.tris.end(), {entry.second->a, entry.second->b, entry.second->c});
		out.push_back(std::move(cell));
	}
	return out;
}

} // namespace wetground

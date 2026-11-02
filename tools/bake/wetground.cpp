#include "wetground.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace wetground {
namespace {

constexpr float kCell = mapdata::kRainTile / mapdata::kCoverRes; // 2 m
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
	std::map<std::pair<int, int>, mapdata::CoverTile> tiles;
	const int cells = mapdata::kRainTiles * mapdata::kCoverRes;
	for (const Face &f : faces) {
		if (!Covers(f.surface))
			continue;
		// Max-z rasterisation: every 2 m cell whose centre the face covers.
		auto toCell = [](float w) { return (w - mapdata::kWorldMin) / kCell; };
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
				const int tx = x / mapdata::kCoverRes, ty = y / mapdata::kCoverRes;
				auto [it, fresh] = tiles.try_emplace({ty, tx});
				mapdata::CoverTile &tile = it->second;
				if (fresh) {
					tile.cx = int16_t(tx);
					tile.cy = int16_t(ty);
					std::fill(std::begin(tile.top), std::end(tile.top), mapdata::kOpenSky);
				}
				int16_t &top = tile.top[(y % mapdata::kCoverRes) * mapdata::kCoverRes + x % mapdata::kCoverRes];
				top = std::max(top, int16_t(std::clamp(std::lround(z * 10), -32767L, 32767L)));
			}
	}
	std::vector<mapdata::CoverTile> out;
	for (auto &kv : tiles)
		out.push_back(kv.second);
	return out;
}

std::vector<mapdata::WetCell> WetGround(const std::vector<Face> &faces, const mapdata::Map &covered)
{
	std::map<std::pair<int, int>, std::vector<std::pair<float, const Face *>>> cells;
	for (const Face &f : faces) {
		float nz;
		const float area2 = Area2(f, nz);
		if (!Paved(f.surface) || !(area2 > 1e-6f) || -nz / area2 < kUpright)
			continue; // not paved, a wall, or a ceiling
		const Vec3 mid{(f.a.x + f.b.x + f.c.x) / 3, (f.a.y + f.b.y + f.c.y) / 3, (f.a.z + f.b.z + f.c.z) / 3};
		if (mapdata::CoveredAt(covered, {mid.x, mid.y, mid.z + kRoofClearance}))
			continue; // a roof or a deck over it
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

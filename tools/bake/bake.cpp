// vcmm-bake: builds the minimap's map data from the player's own game files.
//
//   base map   radar art (gta3.img radarNN.txd) -> water/land/park/beach classes,
//              with the art's roads and buildings painted out (they are drawn
//              as vectors instead), upscaled 2x with smoothed edges.
//   roads      car paths from data/maps/paths.ipl with their real widths.
//   buildings  the roof surfaces of every map instance's collision mesh (COL
//              placed by IPL): upward faces that nothing in the same model
//              covers and that stand well above the street, welded into
//              connected roofs whose outline edges become walls.
//
// usage: vcmm-bake <GameData dir> <out map.bin> [debug dir] [--area x y size out.png]...
#include "../../src/mapdata.h"
#include "gamefiles.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <deque>
#include <numeric>
#include <string>
#include <tuple>

using mapdata::Building;

namespace {

// --- base map -------------------------------------------------------------------

constexpr int kArt = 1024; // 8x8 tiles of 128 px

struct Proto {
	uint8_t r, g, b, cls;
};
// Flat colours of the radar art (see tools/vcdata.py histogram).
constexpr Proto kProtos[] = {
    {148, 198, 245, mapdata::kWater},       {120, 168, 208, mapdata::kWater},
    {202, 200, 204, mapdata::kLand},        {152, 152, 152, mapdata::kLand},
    {104, 104, 104, mapdata::kLand},        {24, 126, 72, mapdata::kPark},
    {248, 248, 204, mapdata::kBeach},       {4, 4, 4, mapdata::kArtRoad},
    {247, 247, 247, mapdata::kArtBuilding},
};

bool LoadArt(const ImgArchive &img, std::vector<uint8_t> &rgba)
{
	rgba.assign(size_t(kArt) * kArt * 4, 0);
	for (int t = 0; t < 64; t++) {
		char name[32];
		snprintf(name, sizeof name, "radar%02d.txd", t);
		std::vector<uint8_t> txd, tex;
		int w, h;
		if (!img.Read(name, txd) || !DecodeTxdFirstTexture(txd, w, h, tex) || w != 128 || h != 128)
			return false;
		const int tx = t % 8, ty = t / 8;
		for (int y = 0; y < 128; y++)
			std::copy_n(&tex[size_t(y) * 128 * 4], 128 * 4, &rgba[(size_t(ty * 128 + y) * kArt + tx * 128) * 4]);
	}
	return true;
}

std::vector<uint8_t> ClassifyArt(const std::vector<uint8_t> &rgba)
{
	std::vector<uint8_t> cls(size_t(kArt) * kArt);
	for (size_t i = 0; i < cls.size(); i++) {
		int best = 1 << 30;
		for (const Proto &p : kProtos) {
			const int dr = rgba[i * 4] - p.r, dg = rgba[i * 4 + 1] - p.g, db = rgba[i * 4 + 2] - p.b;
			const int d = dr * dr + dg * dg + db * db;
			if (d < best) {
				best = d;
				cls[i] = p.cls;
			}
		}
	}
	// Painted-out pixels take the class of the nearest real ground (multi-source BFS).
	std::vector<uint8_t> base(cls.size(), 0xff);
	std::deque<int> queue;
	for (size_t i = 0; i < cls.size(); i++)
		if (cls[i] < mapdata::kArtRoad) {
			base[i] = cls[i];
			queue.push_back(int(i));
		}
	while (!queue.empty()) {
		const int i = queue.front();
		queue.pop_front();
		const int x = i % kArt, y = i / kArt;
		const int nb[4] = {x > 0 ? i - 1 : -1, x < kArt - 1 ? i + 1 : -1, y > 0 ? i - kArt : -1,
		                   y < kArt - 1 ? i + kArt : -1};
		for (int n : nb)
			if (n >= 0 && base[n] == 0xff) {
				base[n] = base[i];
				queue.push_back(n);
			}
	}
	for (size_t i = 0; i < cls.size(); i++)
		cls[i] = uint8_t(base[i] | (cls[i] & ~mapdata::kClassMask));
	return cls;
}

// 2x upscale: blur each class's coverage, resample bilinearly, take the winner.
std::vector<uint8_t> UpscaleClasses(const std::vector<uint8_t> &cls, int outSize)
{
	constexpr int kClasses = 4;
	constexpr int kRadius = 2;
	float kernel[2 * kRadius + 1], sum = 0;
	for (int k = -kRadius; k <= kRadius; k++)
		sum += kernel[k + kRadius] = expf(-0.5f * k * k / (0.9f * 0.9f));
	for (float &k : kernel)
		k /= sum;

	std::vector<float> cover(size_t(kClasses) * kArt * kArt), tmp(size_t(kArt) * kArt);
	for (int c = 0; c < kClasses; c++) {
		float *dst = &cover[size_t(c) * kArt * kArt];
		for (int y = 0; y < kArt; y++)
			for (int x = 0; x < kArt; x++) {
				float v = 0;
				for (int k = -kRadius; k <= kRadius; k++) {
					const int xx = std::clamp(x + k, 0, kArt - 1);
					v += kernel[k + kRadius] * ((cls[size_t(y) * kArt + xx] & mapdata::kClassMask) == c);
				}
				tmp[size_t(y) * kArt + x] = v;
			}
		for (int y = 0; y < kArt; y++)
			for (int x = 0; x < kArt; x++) {
				float v = 0;
				for (int k = -kRadius; k <= kRadius; k++)
					v += kernel[k + kRadius] * tmp[size_t(std::clamp(y + k, 0, kArt - 1)) * kArt + x];
				dst[size_t(y) * kArt + x] = v;
			}
	}
	std::vector<uint8_t> out(size_t(outSize) * outSize);
	const float scale = float(kArt) / outSize;
	for (int y = 0; y < outSize; y++)
		for (int x = 0; x < outSize; x++) {
			const float sx = (x + 0.5f) * scale - 0.5f, sy = (y + 0.5f) * scale - 0.5f;
			const int x0 = std::clamp(int(floorf(sx)), 0, kArt - 2), y0 = std::clamp(int(floorf(sy)), 0, kArt - 2);
			const float fx = std::clamp(sx - x0, 0.0f, 1.0f), fy = std::clamp(sy - y0, 0.0f, 1.0f);
			int best = 0;
			float bestV = -1;
			for (int c = 0; c < kClasses; c++) {
				const float *m = &cover[size_t(c) * kArt * kArt];
				const float v = (m[size_t(y0) * kArt + x0] * (1 - fx) + m[size_t(y0) * kArt + x0 + 1] * fx) * (1 - fy) +
				                (m[size_t(y0 + 1) * kArt + x0] * (1 - fx) + m[size_t(y0 + 1) * kArt + x0 + 1] * fx) * fy;
				if (v > bestV) {
					bestV = v;
					best = c;
				}
			}
			const uint8_t flags = cls[size_t(std::min(int(sy + 0.5f), kArt - 1)) * kArt +
			                          std::min(int(sx + 0.5f), kArt - 1)] & ~mapdata::kClassMask;
			out[size_t(y) * outSize + x] = uint8_t(best | flags);
		}
	return out;
}


// --- buildings --------------------------------------------------------------------

constexpr float kCell = 2.0f; // ground grid
constexpr int kGrid = int(mapdata::kWorldSize / kCell); // rows run south -> north
constexpr float kUnset = -1e9f;
constexpr float kMinRoofHeight = 3.5f; // a roof this far above the street makes a building
constexpr float kMinRoofArea = 6.0f;   // m2; smaller pieces are props and ledges
constexpr float kMinRoofWidth = 1.6f;  // average width (2 area / perimeter); thinner is a ledge or parapet

enum SurfaceKind { kSkip, kGround, kSolid };

SurfaceKind Kind(uint8_t surface)
{
	switch (surface) {
	case 1: case 2: case 3: case 4: case 5: case 18: case 19: case 26: case 33: case 34:
		return kGround; // tarmac, grass, gravel, mud, pavement, sand, water, cliff, beach
	case 8: case 12: case 13: case 14: case 16: case 25: case 31:
		return kSkip; // awnings, poles, lamp posts, hydrants, fences, hedges, see-through
	default:
		return kSolid;
	}
}

void Splat(std::vector<float> &grid, int cx, int cy, float z)
{
	if (cx >= 0 && cy >= 0 && cx < kGrid && cy < kGrid) {
		float &v = grid[size_t(cy) * kGrid + cx];
		v = std::max(v, z);
	}
}

// Max-z rasterisation of a world-space triangle (cell centres inside it).
void RasterTriangle(std::vector<float> &grid, Vec3f a, Vec3f b, Vec3f c)
{
	auto toCell = [](float w) { return (w - mapdata::kWorldMin) / kCell; };
	const float ax = toCell(a.x), ay = toCell(a.y), bx = toCell(b.x), by = toCell(b.y), cx = toCell(c.x), cy = toCell(c.y);
	const int x0 = int(floorf(std::min({ax, bx, cx}))), x1 = int(floorf(std::max({ax, bx, cx})));
	const int y0 = int(floorf(std::min({ay, by, cy}))), y1 = int(floorf(std::max({ay, by, cy})));
	if (x1 < 0 || y1 < 0 || x0 >= kGrid || y0 >= kGrid || (x1 - x0) * (y1 - y0) > 400 * 400)
		return;
	const float area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
	if (fabsf(area) < 1e-6f)
		return;
	for (int y = std::max(y0, 0); y <= std::min(y1, kGrid - 1); y++)
		for (int x = std::max(x0, 0); x <= std::min(x1, kGrid - 1); x++) {
			const float px = x + 0.5f, py = y + 0.5f;
			const float w0 = ((bx - px) * (cy - py) - (cx - px) * (by - py)) / area;
			const float w1 = ((cx - px) * (ay - py) - (ax - px) * (cy - py)) / area;
			const float w2 = 1.0f - w0 - w1;
			if (w0 >= -0.02f && w1 >= -0.02f && w2 >= -0.02f)
				Splat(grid, x, y, w0 * a.z + w1 * b.z + w2 * c.z);
		}
}

// Calls f(instanceIndex, a, b, c, surface) for every collision triangle of
// every exterior map instance, in world space. Boxes contribute their top face.
template <typename F>
void ForEachFace(const std::vector<Instance> &insts, const std::map<std::string, ColModel> &cols, F f)
{
	for (size_t idx = 0; idx < insts.size(); idx++) {
		const Instance &in = insts[idx];
		if (in.interior != 0 && in.interior != 13)
			continue; // interiors; 13 means "visible everywhere" and is used for exterior pieces
		auto it = cols.find(in.model);
		if (it == cols.end())
			continue;
		// IPL quaternions are applied inverted (reVC rotates by -2 acos(w)).
		const float x = -in.qx, y = -in.qy, z = -in.qz, w = in.qw;
		const float m[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
		                       {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
		                       {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
		auto xf = [&](Vec3f v) {
			return Vec3f{in.pos.x + m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
			             in.pos.y + m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
			             in.pos.z + m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
		};
		const ColModel &col = it->second;
		for (const ColFace &fc : col.faces)
			if (fc.a < col.verts.size() && fc.b < col.verts.size() && fc.c < col.verts.size())
				f(idx, xf(col.verts[fc.a]), xf(col.verts[fc.b]), xf(col.verts[fc.c]), fc.surface);
		for (const ColBox &b : col.boxes) {
			if ((b.max.x - b.min.x) * (b.max.y - b.min.y) < 16.0f)
				continue; // props
			const Vec3f c[4] = {xf({b.min.x, b.min.y, b.max.z}), xf({b.max.x, b.min.y, b.max.z}),
			                    xf({b.max.x, b.max.y, b.max.z}), xf({b.min.x, b.max.y, b.max.z})};
			f(idx, c[0], c[1], c[2], b.surface);
			f(idx, c[0], c[2], c[3], b.surface);
		}
	}
}

// Street level everywhere: the highest ground-type surface per cell, with
// unknown cells (under buildings, open water) taken from the nearest known one.
std::vector<float> GroundGrid(const std::vector<Instance> &insts, const std::map<std::string, ColModel> &cols)
{
	std::vector<float> g(size_t(kGrid) * kGrid, kUnset);
	ForEachFace(insts, cols, [&](size_t, Vec3f a, Vec3f b, Vec3f c, uint8_t s) {
		if (Kind(s) == kGround)
			RasterTriangle(g, a, b, c);
	});
	std::deque<int> queue;
	for (size_t i = 0; i < g.size(); i++)
		if (g[i] > kUnset)
			queue.push_back(int(i));
	while (!queue.empty()) {
		const int i = queue.front();
		queue.pop_front();
		const int x = i % kGrid, y = i / kGrid;
		const int nb[4] = {x > 0 ? i - 1 : -1, x < kGrid - 1 ? i + 1 : -1, y > 0 ? i - kGrid : -1,
		                   y < kGrid - 1 ? i + kGrid : -1};
		for (int n : nb)
			if (n >= 0 && g[n] <= kUnset) {
				g[n] = g[i];
				queue.push_back(n);
			}
	}
	return g;
}

float GroundAt(const std::vector<float> &g, float x, float y)
{
	const int cx = std::clamp(int((x - mapdata::kWorldMin) / kCell), 0, kGrid - 1);
	const int cy = std::clamp(int((y - mapdata::kWorldMin) / kCell), 0, kGrid - 1);
	return g[size_t(cy) * kGrid + cx];
}

struct Face {
	Vec3f v[3];
};

float Cross2D(Vec3f o, Vec3f a, Vec3f b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

// Height of a face's plane at (x, y) if the point lies inside it from above.
bool HeightAt(const Face &f, float x, float y, float &z)
{
	const float area = Cross2D(f.v[0], f.v[1], f.v[2]);
	if (fabsf(area) < 1e-6f)
		return false;
	const Vec3f p{x, y, 0};
	const float w0 = Cross2D(f.v[1], f.v[2], p) / area, w1 = Cross2D(f.v[2], f.v[0], p) / area, w2 = 1 - w0 - w1;
	if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f)
		return false;
	z = w0 * f.v[0].z + w1 * f.v[1].z + w2 * f.v[2].z;
	return true;
}

struct Extracted {
	std::vector<Building> buildings;
	std::vector<size_t> roofless; // tall instances without any roof surface
};

Extracted ExtractBuildings(const std::vector<Instance> &insts, const std::map<std::string, ColModel> &cols,
                           const std::vector<float> &ground)
{
	std::vector<std::vector<Face>> solid(insts.size());
	ForEachFace(insts, cols, [&](size_t i, Vec3f a, Vec3f b, Vec3f c, uint8_t s) {
		if (Kind(s) == kSolid)
			solid[i].push_back({{a, b, c}});
	});

	Extracted out;
	for (size_t inst = 0; inst < insts.size(); inst++) {
		const std::vector<Face> &faces = solid[inst];
		if (faces.empty())
			continue;
		// 1. Roof faces: facing up/down (winding varies), not covered by
		//    another face of the model, well above the street.
		std::vector<bool> flat(faces.size());
		for (size_t k = 0; k < faces.size(); k++) {
			const Vec3f &a = faces[k].v[0], &b = faces[k].v[1], &c = faces[k].v[2];
			const float nx = (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y);
			const float ny = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z);
			const float nz = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
			const float len = sqrtf(nx * nx + ny * ny + nz * nz);
			flat[k] = len > 1e-6f && fabsf(nz) / len >= 0.45f;
		}
		std::vector<size_t> roofs;
		float tallest = 0;
		for (size_t k = 0; k < faces.size(); k++) {
			const Face &f = faces[k];
			const Vec3f c{(f.v[0].x + f.v[1].x + f.v[2].x) / 3, (f.v[0].y + f.v[1].y + f.v[2].y) / 3,
			              (f.v[0].z + f.v[1].z + f.v[2].z) / 3};
			tallest = std::max(tallest, std::max({f.v[0].z, f.v[1].z, f.v[2].z}) - GroundAt(ground, c.x, c.y));
			if (!flat[k] || c.z - GroundAt(ground, c.x, c.y) < kMinRoofHeight)
				continue;
			bool covered = false;
			for (size_t j = 0; j < faces.size() && !covered; j++) {
				float z;
				covered = j != k && flat[j] && HeightAt(faces[j], c.x, c.y, z) && z > c.z + 0.5f;
			}
			if (!covered)
				roofs.push_back(k);
		}
		if (roofs.empty()) {
			if (tallest > 8.0f)
				out.roofless.push_back(inst);
			continue;
		}

		// 2. Weld vertices and join faces that share an edge.
		std::map<std::tuple<int, int, int>, int> weld;
		std::vector<Vec3f> pos;
		auto vid = [&](Vec3f p) {
			const auto key = std::make_tuple(int(lroundf(p.x * 20)), int(lroundf(p.y * 20)), int(lroundf(p.z * 20)));
			auto [it, added] = weld.try_emplace(key, int(pos.size()));
			if (added)
				pos.push_back(p);
			return it->second;
		};
		std::vector<std::array<int, 3>> tri(roofs.size());
		std::vector<int> parent(roofs.size());
		std::iota(parent.begin(), parent.end(), 0);
		auto find = [&](int x) {
			while (parent[size_t(x)] != x)
				x = parent[size_t(x)] = parent[size_t(parent[size_t(x)])];
			return x;
		};
		std::map<std::pair<int, int>, int> edgeOwner;
		for (size_t r = 0; r < roofs.size(); r++) {
			const Face &f = faces[roofs[r]];
			tri[r] = {vid(f.v[0]), vid(f.v[1]), vid(f.v[2])};
			for (int e = 0; e < 3; e++) {
				const auto key = std::minmax(tri[r][e], tri[r][(e + 1) % 3]);
				auto [it, added] = edgeOwner.try_emplace(key, int(r));
				if (!added)
					parent[size_t(find(int(r)))] = find(it->second);
			}
		}

		// 3. Each connected roof is a building.
		std::map<int, std::vector<size_t>> groups;
		for (size_t r = 0; r < roofs.size(); r++)
			groups[find(int(r))].push_back(r);
		for (const auto &[root, members] : groups) {
			float area = 0;
			for (size_t r : members)
				area += fabsf(Cross2D(pos[size_t(tri[r][0])], pos[size_t(tri[r][1])], pos[size_t(tri[r][2])])) * 0.5f;
			if (area < kMinRoofArea)
				continue;
			std::map<int, uint16_t> local;
			std::vector<float> grounds;
			Building b;
			for (size_t r : members)
				for (int v : tri[r])
					if (local.try_emplace(v, uint16_t(local.size())).second)
						grounds.push_back(GroundAt(ground, pos[size_t(v)].x, pos[size_t(v)].y));
			if (local.size() >= 65535)
				continue;
			std::nth_element(grounds.begin(), grounds.begin() + long(grounds.size() / 2), grounds.end());
			const float street = grounds[grounds.size() / 2];
			b.verts.resize(local.size());
			for (const auto &[v, l] : local)
				b.verts[l] = {pos[size_t(v)].x, pos[size_t(v)].y, std::max(0.5f, pos[size_t(v)].z - street)};
			std::map<std::pair<int, int>, int> uses;
			for (size_t r : members)
				for (int e = 0; e < 3; e++)
					uses[std::minmax(tri[r][e], tri[r][(e + 1) % 3])]++;
			for (size_t r : members) {
				const auto &t = tri[r];
				const bool ccw = Cross2D(pos[size_t(t[0])], pos[size_t(t[1])], pos[size_t(t[2])]) > 0;
				for (int e = 0; e < 3; e++)
					b.tris.push_back(local[t[e]]);
				for (int e = 0; e < 3; e++) {
					const int a = t[e], c = t[(e + 1) % 3];
					if (uses[std::minmax(a, c)] != 1)
						continue; // interior edge
					b.edges.push_back(local[ccw ? a : c]);
					b.edges.push_back(local[ccw ? c : a]);
				}
			}
			float perimeter = 0;
			for (size_t e = 0; e + 1 < b.edges.size(); e += 2)
				perimeter += hypotf(b.verts[b.edges[e]].x - b.verts[b.edges[e + 1]].x,
				                    b.verts[b.edges[e]].y - b.verts[b.edges[e + 1]].y);
			if (perimeter > 0 && 2.0f * area / perimeter < kMinRoofWidth)
				continue;
			out.buildings.push_back(std::move(b));
		}
	}
	printf("buildings: %zu roofs; %zu tall models have no roof surface\n", out.buildings.size(), out.roofless.size());
	return out;
}

std::vector<mapdata::Road> Roads(const std::vector<RoadSegment> &segments)
{
	std::vector<mapdata::Road> roads;
	for (const RoadSegment &s : segments)
		roads.push_back({{s.a.x, s.a.y}, {s.b.x, s.b.y}, s.width});
	return roads;
}

// --- diagnostics ------------------------------------------------------------------

void DebugImages(const std::string &dir, const std::vector<uint8_t> &art, const mapdata::Map &map)
{
	const uint8_t pal[4][3] = {{40, 82, 120}, {70, 76, 84}, {52, 92, 64}, {150, 140, 110}};
	std::vector<uint8_t> img(size_t(map.size) * map.size * 4);
	for (size_t i = 0; i < map.classes.size(); i++) {
		const uint8_t c = map.classes[i];
		const uint8_t *p = pal[c & mapdata::kClassMask];
		const bool road = c & mapdata::kArtRoad;
		img[i * 4] = road ? 200 : p[0];
		img[i * 4 + 1] = road ? 200 : p[1];
		img[i * 4 + 2] = road ? 200 : p[2];
		img[i * 4 + 3] = 255;
	}
	WritePng(dir + "/base.png", map.size, map.size, img);
	(void)art;
}

// Top-down picture of one area: the real collision (ground grey, roads bluish,
// solid coloured by height, roofless tall models magenta), the extracted
// building outlines (red) and the car paths (blue centre, cyan kerbs).
void DebugArea(const std::string &path, float cx, float cy, float size, const std::vector<Instance> &insts,
               const std::map<std::string, ColModel> &cols, const std::vector<float> &ground, const Extracted &ex,
               const std::vector<RoadSegment> &roads)
{
	constexpr int kPx = 1000;
	const float scale = kPx / size, x0 = cx - size / 2, y1 = cy + size / 2;
	std::vector<uint8_t> img(size_t(kPx) * kPx * 4, 0);
	for (size_t i = 3; i < img.size(); i += 4)
		img[i] = 255;
	auto toPx = [&](Vec3f v) { return Vec3f{(v.x - x0) * scale, (y1 - v.y) * scale, v.z}; };
	auto put = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
		if (x >= 0 && y >= 0 && x < kPx && y < kPx) {
			uint8_t *p = &img[(size_t(y) * kPx + x) * 4];
			p[0] = r, p[1] = g, p[2] = b;
		}
	};
	auto fill = [&](Vec3f a, Vec3f b, Vec3f c, uint8_t r, uint8_t g, uint8_t bl) {
		const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
		if (fabsf(area) < 1e-6f)
			return;
		for (int y = std::max(0, int(std::min({a.y, b.y, c.y}))); y <= std::min(kPx - 1, int(std::max({a.y, b.y, c.y}))); y++)
			for (int x = std::max(0, int(std::min({a.x, b.x, c.x}))); x <= std::min(kPx - 1, int(std::max({a.x, b.x, c.x}))); x++) {
				const float px = x + 0.5f, py = y + 0.5f;
				const float w0 = ((b.x - px) * (c.y - py) - (c.x - px) * (b.y - py)) / area;
				const float w1 = ((c.x - px) * (a.y - py) - (a.x - px) * (c.y - py)) / area;
				if (w0 >= 0 && w1 >= 0 && w0 + w1 <= 1)
					put(x, y, r, g, bl);
			}
	};
	auto line = [&](Vec3f a, Vec3f b, int thick, uint8_t r, uint8_t g, uint8_t bl) {
		const int n = int(std::max(fabsf(b.x - a.x), fabsf(b.y - a.y))) + 1;
		for (int k = 0; k <= n; k++)
			for (int dy = -thick / 2; dy <= thick / 2; dy++)
				for (int dx = -thick / 2; dx <= thick / 2; dx++)
					put(int(a.x + (b.x - a.x) * k / n) + dx, int(a.y + (b.y - a.y) * k / n) + dy, r, g, bl);
	};
	struct Tri {
		Vec3f a, b, c;
		float h;
		uint8_t surface;
		bool roofless;
	};
	std::vector<bool> roofless(insts.size(), false);
	for (size_t i : ex.roofless)
		roofless[i] = true;
	std::vector<Tri> tris;
	ForEachFace(insts, cols, [&](size_t i, Vec3f a, Vec3f b, Vec3f c, uint8_t s) {
		const Vec3f pa = toPx(a), pb = toPx(b), pc = toPx(c);
		if (std::max({pa.x, pb.x, pc.x}) < 0 || std::min({pa.x, pb.x, pc.x}) > kPx ||
		    std::max({pa.y, pb.y, pc.y}) < 0 || std::min({pa.y, pb.y, pc.y}) > kPx)
			return;
		const float h = std::max({a.z, b.z, c.z}) - GroundAt(ground, (a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3);
		tris.push_back({pa, pb, pc, h, s, roofless[i]});
	});
	std::sort(tris.begin(), tris.end(), [](const Tri &p, const Tri &q) {
		return std::max({p.a.z, p.b.z, p.c.z}) < std::max({q.a.z, q.b.z, q.c.z});
	});
	for (const Tri &t : tris) {
		const SurfaceKind kind = Kind(t.surface);
		if (kind == kGround) {
			const bool road = t.surface == 1;
			fill(t.a, t.b, t.c, road ? 70 : 52, road ? 70 : 60, road ? 84 : 52);
		} else if (t.roofless) {
			fill(t.a, t.b, t.c, 200, 40, 200);
		} else {
			const float k = std::clamp(t.h / 60.0f, 0.0f, 1.0f); // height above street: green low .. orange high
			fill(t.a, t.b, t.c, uint8_t(90 + 165 * k), uint8_t(140 + 40 * (1 - k)), uint8_t(60 * (1 - k)));
		}
	}
	for (const RoadSegment &r : roads) {
		const Vec3f a = toPx(r.a), b = toPx(r.b);
		const float dx = b.x - a.x, dy = b.y - a.y, len = hypotf(dx, dy);
		if (len > 0) {
			const float nx = -dy / len * r.width * 0.5f * scale, ny = dx / len * r.width * 0.5f * scale;
			line({a.x + nx, a.y + ny, 0}, {b.x + nx, b.y + ny, 0}, 1, 0, 220, 255);
			line({a.x - nx, a.y - ny, 0}, {b.x - nx, b.y - ny, 0}, 1, 0, 220, 255);
		}
		line(a, b, 3, 40, 90, 255);
	}
	for (const Building &bd : ex.buildings)
		for (size_t e = 0; e + 1 < bd.edges.size(); e += 2) {
			const Vec3 p = bd.verts[bd.edges[e]], q = bd.verts[bd.edges[e + 1]];
			line(toPx({p.x, p.y, 0}), toPx({q.x, q.y, 0}), 3, 255, 30, 30);
		}
	WritePng(path, kPx, kPx, img);
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s <GameData dir> <out map.bin> [debug dir] [--area x y size out.png]...\n", argv[0]);
		return 2;
	}
	const std::string gameData = argv[1];
	ImgArchive img;
	if (!img.Open(gameData + "/models/gta3.img")) {
		fprintf(stderr, "cannot open %s/models/gta3.img\n", gameData.c_str());
		return 1;
	}
	std::vector<uint8_t> art;
	if (!LoadArt(img, art)) {
		fprintf(stderr, "radar tiles missing or in an unexpected format\n");
		return 1;
	}
	mapdata::Map map;
	map.size = 2 * kArt;
	map.classes = UpscaleClasses(ClassifyArt(art), map.size);

	std::map<std::string, ColModel> cols;
	for (const std::string &name : img.Names()) {
		std::vector<uint8_t> data;
		if (name.size() > 4 && name.compare(name.size() - 4, 4, ".col") == 0 && img.Read(name, data))
			ParseColArchive(data, cols);
	}
	const std::vector<Instance> insts = LoadInstances(gameData);
	const std::vector<RoadSegment> segments = LoadCarPaths(gameData);
	printf("collision models: %zu, map instances: %zu, road segments: %zu\n", cols.size(), insts.size(),
	       segments.size());
	const std::vector<float> ground = GroundGrid(insts, cols);
	const Extracted ex = ExtractBuildings(insts, cols, ground);
	map.buildings = ex.buildings;
	map.roads = Roads(segments);

	if (!map.Save(argv[2])) {
		fprintf(stderr, "cannot write %s\n", argv[2]);
		return 1;
	}
	if (argc > 3 && argv[3][0] != '-')
		DebugImages(argv[3], art, map);
	for (int i = 3; i + 4 < argc; i++)
		if (std::string(argv[i]) == "--area") {
			DebugArea(argv[i + 4], float(atof(argv[i + 1])), float(atof(argv[i + 2])), float(atof(argv[i + 3])), insts,
			          cols, ground, ex, segments);
			i += 4;
		}
	printf("wrote %s\n", argv[2]);
	return 0;
}

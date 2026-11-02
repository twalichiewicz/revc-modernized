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
// usage: vcmm-bake <GameData dir> <out map.bin> [debug dir] [--streets streets.txt] [--area x y size out.png]...
#include "../../src/mapdata.h"
#include "../../src/radiologos.h"
#include "gamefiles.h"
#include "roofgeometry.h"
#include "streets.h"
#include "visualmodel.h"
#include "wetground.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <deque>
#include <fstream>
#include <numeric>
#include <string>
#include <tuple>
#include <set>
#include <sstream>

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
// every exterior map instance, in world space. Boxes contribute their top face:
// counter-clockwise seen from above, or with kMeshWinding as the meshes wind a
// floor (clockwise), for callers that tell floors from ceilings by winding.
template <bool kMeshWinding = false, typename F>
void ForEachFace(const std::vector<Instance> &insts, const std::map<std::string, ColModel> &cols, F f)
{
	for (size_t idx = 0; idx < insts.size(); idx++) {
		const Instance &in = insts[idx];
		if (in.interior != 0 && in.interior != 13)
			continue; // interiors; 13 means "visible everywhere" and is used for exterior pieces
		auto it = cols.find(in.model);
		if (it == cols.end())
			continue;
		// Ground and visual geometry use the same verified native placement.
		auto xf = [&](Vec3f v) { return visualmodel::Place(v, in); };
		const ColModel &col = it->second;
		for (const ColFace &fc : col.faces)
			if (fc.a < col.verts.size() && fc.b < col.verts.size() && fc.c < col.verts.size())
				f(idx, xf(col.verts[fc.a]), xf(col.verts[fc.b]), xf(col.verts[fc.c]), fc.surface);
		for (const ColBox &b : col.boxes) {
			if ((b.max.x - b.min.x) * (b.max.y - b.min.y) < 16.0f)
				continue; // props
			const Vec3f c[4] = {xf({b.min.x, b.min.y, b.max.z}), xf({b.max.x, b.min.y, b.max.z}),
			                    xf({b.max.x, b.max.y, b.max.z}), xf({b.min.x, b.max.y, b.max.z})};
			if (kMeshWinding) {
				f(idx, c[0], c[2], c[1], b.surface);
				f(idx, c[0], c[3], c[2], b.surface);
			} else {
				f(idx, c[0], c[1], c[2], b.surface);
				f(idx, c[0], c[2], c[3], b.surface);
			}
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

using roofgeometry::Cross2D;
using roofgeometry::Face;

struct Extracted {
	std::vector<Building> buildings;
	std::vector<size_t> sources;
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
			if (!roofgeometry::Covered(faces, flat, k))
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
			out.sources.push_back(inst);
		}
	}
	printf("buildings: %zu roofs; %zu tall models have no roof surface\n", out.buildings.size(), out.roofless.size());
	return out;
}

// Collision supplies street levels and a conservative structural candidate
// set. The visible DFF supplies roofs, eaves, walls and bridge spans themselves.
std::vector<Building> VisualBuildings(const std::vector<Instance> &insts,
                                    const std::map<std::string, ColModel> &cols,
                                    const std::vector<float> &ground, const Extracted &fallback, ImgArchive &img,
                                    float water = 0)
{
	std::set<size_t> candidates(fallback.sources.begin(), fallback.sources.end());
	candidates.insert(fallback.roofless.begin(), fallback.roofless.end());
	std::vector<bool> bridge(insts.size());
	for (size_t i = 0; i < insts.size(); i++) {
		bridge[i] = insts[i].model.find("bridg") != std::string::npos;
		if (bridge[i] && insts[i].model.rfind("lod", 0) != 0 && (insts[i].interior == 0 || insts[i].interior == 13))
			candidates.insert(i);
	}
	std::vector<float> deck(insts.size(), 1e9f);
	ForEachFace(insts, cols, [&](size_t i, Vec3f a, Vec3f b, Vec3f c, uint8_t surface) {
		if (!bridge[i] || Kind(surface) != kGround || surface == 26) return;
		const float ux=b.x-a.x, uy=b.y-a.y, uz=b.z-a.z, vx=c.x-a.x, vy=c.y-a.y, vz=c.z-a.z;
		const float nx=uy*vz-uz*vy, ny=uz*vx-ux*vz, nz=ux*vy-uy*vx;
		if (std::fabs(nz) < .45f*std::sqrt(nx*nx+ny*ny+nz*nz)) return;
		const float low = std::min({a.z,b.z,c.z});
		if (low > .1f) deck[i] = std::min(deck[i], low);
	});
	std::map<std::string, visualmodel::Mesh> cache;
	std::set<std::string> failed;
	std::map<size_t, Building> visual;
	struct Prepared {size_t instance;const visualmodel::Mesh *mesh;float street;int bridgePart;};
	std::vector<Prepared> prepared;
	std::vector<visualmodel::BridgePart> parts;
	size_t triangles = 0, bridges = 0;
	for (size_t i : candidates) {
		const Instance &instance = insts[i];
		if (instance.model.rfind("lod", 0) == 0 || failed.count(instance.model) || !visualmodel::ActiveAt(instance,12)) continue;
		auto found = cache.find(instance.model);
		if (found == cache.end()) {
			std::vector<uint8_t> bytes;visualmodel::Mesh mesh;std::string why;
			if (!img.Read(instance.model+".dff", bytes) || !visualmodel::Read(bytes, mesh, why)) {
				failed.insert(instance.model);continue;
			}
			found = cache.emplace(instance.model, std::move(mesh)).first;
		}
		std::vector<float> levels;
		for (size_t v=0;v<found->second.vertices.size();v+=16) {
			const Vec3f point=visualmodel::Place(found->second.vertices[v],instance);
			levels.push_back(GroundAt(ground,point.x,point.y));
		}
		if(levels.empty())continue;
		std::nth_element(levels.begin(),levels.begin()+long(levels.size()/2),levels.end());
		const float street = bridge[i] && deck[i] < 1e8f ? deck[i]-.02f : levels[levels.size()/2];
		int part=-1;
		if(bridge[i]) {
			Vec2 lo{1e9f,1e9f},hi{-1e9f,-1e9f};
			for(Vec3f p:found->second.vertices) {p=visualmodel::Place(p,instance);lo={std::min(lo.x,p.x),std::min(lo.y,p.y)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y)};}
			part=int(parts.size());parts.push_back({lo,hi,street,deck[i]<1e8f});
		}
		prepared.push_back({i,&found->second,street,part});
	}
	visualmodel::AlignBridges(parts);
	for(const Prepared &p:prepared) {
		const size_t i=p.instance;
		const float street=p.bridgePart>=0?parts[size_t(p.bridgePart)].ground:p.street;
		Building building;
		if(!visualmodel::Build(*p.mesh,insts[i],street,building,bridge[i],water))continue;
		if(bridge[i]) {building.flags|=mapdata::kBridge;++bridges;}
		triangles+=building.tris.size()/3;visual.emplace(i,std::move(building));
	}
	std::vector<Building> result;
	for (const auto &[i, building] : visual) result.push_back(building);
	size_t retained=0;
	for(size_t i=0;i<fallback.buildings.size();i++)
		if(!visual.count(fallback.sources[i])&&visualmodel::ActiveAt(insts[fallback.sources[i]],12)) {result.push_back(fallback.buildings[i]);++retained;}
	printf("visual map: %zu placed models (%zu bridge pieces), %zu triangles; %zu collision roofs retained\n",
	       visual.size(),bridges,triangles,retained);
	return result;
}

// What buildings show over the street that their collision leaves out (porches,
// eaves, balconies): their visible triangles, for the rain's cover. Buildings
// are the models with solid collision; not the far versions, nor trees and
// bushes (rain falls through their leaves).
std::vector<wetground::Face> BuildingVisuals(const std::vector<Instance> &insts, const std::map<std::string, ColModel> &cols,
                                             ImgArchive &img)
{
	std::vector<wetground::Face> out;
	std::map<std::string, visualmodel::Mesh> cache;
	std::set<std::string> skip;
	auto leafy = [](const std::string &n) {
		return n.find("lod") != std::string::npos || n.find("veg") != std::string::npos || n.find("palm") != std::string::npos ||
		       n.find("tree") != std::string::npos || n.find("bush") != std::string::npos;
	};
	size_t models = 0;
	for (const Instance &in : insts) {
		if ((in.interior != 0 && in.interior != 13) || skip.count(in.model) || leafy(in.model) || !visualmodel::ActiveAt(in, 12))
			continue;
		auto col = cols.find(in.model);
		if (col == cols.end() || std::none_of(col->second.faces.begin(), col->second.faces.end(),
		                                       [](const ColFace &f) { return Kind(f.surface) == kSolid; })) {
			skip.insert(in.model);
			continue;
		}
		auto it = cache.find(in.model);
		if (it == cache.end()) {
			std::vector<uint8_t> bytes;
			visualmodel::Mesh mesh;
			std::string why;
			if (!img.Read(in.model + ".dff", bytes) || !visualmodel::Read(bytes, mesh, why)) {
				skip.insert(in.model);
				continue;
			}
			it = cache.emplace(in.model, std::move(mesh)).first;
			++models;
		}
		for (const visualmodel::Face &f : it->second.faces) {
			const Vec3f a = visualmodel::Place(it->second.vertices[f.a], in), b = visualmodel::Place(it->second.vertices[f.b], in),
			            c = visualmodel::Place(it->second.vertices[f.c], in);
			out.push_back({{a.x, a.y, a.z}, {b.x, b.y, b.z}, {c.x, c.y, c.z}, 0});
		}
	}
	printf("rain: %zu building models' visible triangles in the cover (%zu)\n", models, out.size());
	return out;
}

float SeaLevel(const std::string &gameData)
{
	std::ifstream file(gameData+"/data/water.dat");std::string row;
	while(std::getline(file,row)) {
		std::replace(row.begin(),row.end(),',',' ');std::istringstream values(row);
		float level,left,bottom,right,top;
		if(values>>level>>left>>bottom>>right>>top)
			if(left<=0&&right>=0&&bottom<=0&&top>=0&&std::isfinite(level))return level;
	}
	return 0;
}

// Each end gets a round joint so bends close up. Where roads of different
// widths meet (junctions, lane changes) it is only as wide as the narrowest of
// them, so it never bulges out of a road.
std::vector<mapdata::Road> Roads(const std::vector<RoadSegment> &segments)
{
	constexpr float kSameNode = 1.0f; // neighbouring tiles' shared nodes are a little apart
	std::vector<mapdata::Road> roads;
	for (const RoadSegment &s : segments)
		roads.push_back({{s.a.x, s.a.y}, {s.b.x, s.b.y}, s.width, s.width, s.width});
	struct End {
		Vec2 pos;
		float width;
		float *join;
	};
	std::vector<End> ends;
	for (mapdata::Road &r : roads) {
		ends.push_back({r.a, r.width, &r.joinA});
		ends.push_back({r.b, r.width, &r.joinB});
	}
	for (const End &e : ends)
		for (const End &other : ends)
			if (fabsf(e.pos.x - other.pos.x) < kSameNode && fabsf(e.pos.y - other.pos.y) < kSameNode)
				*e.join = std::min(*e.join, other.width);
	return roads;
}

// --- diagnostics ------------------------------------------------------------------

// Street names' coverage (#35), one picture per navigation area (data/navig.zon):
// roads coloured by street, unnamed ones red; a legend (colour, name) beside
// it; and the named road length against all of it, printed.
void StreetPictures(const std::string &dir, const std::string &gameData, const mapdata::Map &map)
{
	std::ifstream zon(gameData + "/data/navig.zon");
	for (std::string line; std::getline(zon, line);) {
		char name[32] = {};
		int type = 0;
		float x1, y1, z1, x2, y2, z2;
		if (sscanf(line.c_str(), " %31[^,], %d, %f, %f, %f, %f, %f, %f", name, &type, &x1, &y1, &z1, &x2, &y2, &z2) != 8)
			continue;
		const float w = x2 - x1, h = y2 - y1, scale = 1200.0f / std::max(w, h);
		const int pw = std::max(1, int(w * scale)), ph = std::max(1, int(h * scale));
		std::vector<uint8_t> img(size_t(pw) * ph * 4, 0);
		for (size_t i = 0; i < img.size(); i += 4)
			img[i] = img[i + 1] = img[i + 2] = 32, img[i + 3] = 255;
		auto colour = [&](uint16_t street, uint8_t rgb[3]) {
			if (street == mapdata::Map::kNoStreet) {
				rgb[0] = 255, rgb[1] = 40, rgb[2] = 40;
				return;
			}
			uint32_t hash = 2166136261u;
			for (char c : map.streets[street])
				hash = (hash ^ uint8_t(c)) * 16777619u;
			rgb[0] = uint8_t(60 + hash % 160), rgb[1] = uint8_t(90 + (hash >> 8) % 160), rgb[2] = uint8_t(90 + (hash >> 16) % 160);
		};
		std::map<uint16_t, bool> seen;
		for (size_t r = 0; r < map.roads.size(); r++) {
			const mapdata::Road &road = map.roads[r];
			const uint16_t street = r < map.roadStreet.size() ? map.roadStreet[r] : mapdata::Map::kNoStreet;
			uint8_t rgb[3];
			colour(street, rgb);
			const float len = hypotf(road.b.x - road.a.x, road.b.y - road.a.y);
			const int half = std::max(1, int(road.width * scale * 0.5f));
			for (float t = 0; t <= 1.0f && len > 0; t += 0.5f / (len * scale + 1)) {
				const int px = int((road.a.x + (road.b.x - road.a.x) * t - x1) * scale);
				const int py = int((y2 - (road.a.y + (road.b.y - road.a.y) * t)) * scale);
				for (int dy = -half; dy <= half; dy++)
					for (int dx = -half; dx <= half; dx++)
						if (px + dx >= 0 && py + dy >= 0 && px + dx < pw && py + dy < ph) {
							uint8_t *p = &img[(size_t(py + dy) * pw + px + dx) * 4];
							p[0] = rgb[0], p[1] = rgb[1], p[2] = rgb[2];
						}
			}
			const Vec2 mid{(road.a.x + road.b.x) / 2, (road.a.y + road.b.y) / 2};
			if (street != mapdata::Map::kNoStreet && mid.x >= x1 && mid.x <= x2 && mid.y >= y1 && mid.y <= y2)
				seen[street] = true;
		}
		WritePng(dir + "/" + name + ".png", pw, ph, img);
		std::ofstream legend(dir + "/" + std::string(name) + ".txt");
		for (const auto &s : seen) {
			uint8_t rgb[3];
			colour(s.first, rgb);
			char hex[8];
			snprintf(hex, sizeof hex, "%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
			legend << hex << " " << map.streets[s.first] << "\n";
		}
		const streets::Coverage c = streets::Measure(map.roads, map.roadStreet, {x1, y1}, {x2, y2});
		printf("streets: %-7s %6.0f of %6.0f m named (%.0f%%)\n", name, c.named, c.total,
		       c.total > 0 ? 100.0f * c.named / c.total : 0.0f);
	}
}

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

// The radio wheel's station logos: 512x512 in the frontend's textures, shrunk
// to radiologos::kSize with alpha-weighted averaging (so edges don't darken).
void BakeRadioLogos(const std::string &gameData, const std::string &out)
{
	std::vector<uint8_t> txd;
	if (!ReadGameFile(gameData + "/models/fronten2.txd", txd)) {
		printf("radio logos: models/fronten2.txd not found\n");
		return;
	}
	constexpr int n = radiologos::kSize;
	radiologos::Logos logos(radiologos::kCount);
	int found = 0;
	for (int i = 0; i < radiologos::kCount; i++) {
		int w, h;
		std::vector<uint8_t> rgba;
		if (!DecodeTxdTexture(txd, radiologos::kTextures[i], w, h, rgba) || w < n || h < n || w % n || h % n)
			continue;
		const int fx = w / n, fy = h / n;
		logos[i].resize(size_t(n) * n * 4);
		for (int y = 0; y < n; y++)
			for (int x = 0; x < n; x++) {
				float sum[4] = {};
				for (int j = 0; j < fy; j++)
					for (int k = 0; k < fx; k++) {
						const uint8_t *p = &rgba[(size_t(y * fy + j) * w + x * fx + k) * 4];
						for (int c = 0; c < 3; c++)
							sum[c] += p[c] * p[3];
						sum[3] += p[3];
					}
				uint8_t *o = &logos[i][(size_t(y) * n + x) * 4];
				for (int c = 0; c < 3; c++)
					o[c] = sum[3] > 0 ? uint8_t(sum[c] / sum[3] + 0.5f) : 0;
				o[3] = uint8_t(sum[3] / (fx * fy) + 0.5f);
			}
		found++;
	}
	if (!radiologos::Save(out, logos))
		fprintf(stderr, "cannot write %s\n", out.c_str());
	else
		printf("radio logos: %d of %d\n", found, radiologos::kCount);
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s <GameData dir> <out map.bin> [debug dir] [--streets streets.txt] [--area x y size out.png]...\n", argv[0]);
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
	map.buildings = VisualBuildings(insts, cols, ground, ex, img, SeaLevel(gameData));
	map.roads = Roads(segments);
	for (const RoadSegment &s : segments) { // each road's height at its two ends (street names)
		map.roadZ.push_back(s.a.z);
		map.roadZ.push_back(s.b.z);
	}
	map.roadStreet.assign(map.roads.size(), mapdata::Map::kNoStreet);
	for (int i = 3; i + 1 < argc; i++)
		if (std::string(argv[i]) == "--streets") {
			std::ifstream in(argv[i + 1]);
			std::vector<streets::Outline> outlines;
			std::string error;
			if (!in || !streets::ParseStreets(std::string(std::istreambuf_iterator<char>(in), {}), outlines, error)) {
				fprintf(stderr, "%s: %s\n", argv[i + 1], in ? error.c_str() : "cannot read");
				return 1;
			}
			std::vector<uint16_t> toName;
			map.streets = streets::UniqueNames(outlines, toName);
			map.roadStreet = streets::Assign(outlines, map.roads);
			for (uint16_t &s : map.roadStreet)
				if (s != mapdata::Map::kNoStreet)
					s = toName[s];
			const auto named = std::count_if(map.roadStreet.begin(), map.roadStreet.end(),
			                                 [](uint16_t s) { return s != mapdata::Map::kNoStreet; });
			printf("streets: %zu, %ld of %zu roads named\n", outlines.size(), long(named), map.roads.size());
		}
	{ // The rain (#32): the paved ground that gets wet and the cover over everything, from the collision.
		std::vector<wetground::Face> faces;
		ForEachFace<true>(insts, cols, [&](size_t, Vec3f a, Vec3f b, Vec3f c, uint8_t s) {
			faces.push_back({{a.x, a.y, a.z}, {b.x, b.y, b.z}, {c.x, c.y, c.z}, s});
		});
		std::vector<wetground::Face> over = faces;
		const std::vector<wetground::Face> shown = BuildingVisuals(insts, cols, img);
		over.insert(over.end(), shown.begin(), shown.end());
		map.covers = wetground::CoverTiles(over);
		map.IndexCovers();
		map.wet = wetground::WetGround(faces, map);
		size_t tris = 0;
		for (const mapdata::WetCell &cell : map.wet)
			tris += cell.tris.size() / 3;
		printf("rain: %zu wet cells, %zu wet triangles; cover heights over %zu tiles\n", map.wet.size(), tris,
		       map.covers.size());
	}

	if (!map.Save(argv[2])) {
		fprintf(stderr, "cannot write %s\n", argv[2]);
		return 1;
	}
	const std::string out = argv[2];
	BakeRadioLogos(gameData, out.substr(0, out.find_last_of('/') + 1) + "radio.bin");
	for (int i = 3; i + 1 < argc; i++)
		if (std::string(argv[i]) == "--streets-debug")
			StreetPictures(argv[i + 1], gameData, map);
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

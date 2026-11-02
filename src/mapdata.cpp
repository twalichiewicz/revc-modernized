#include "mapdata.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace mapdata {
namespace {

constexpr char kMagic[8] = {'V', 'C', 'M', 'M', 'M', 'A', 'P', '6'};

template <typename T>
bool Read(FILE *f, T &v) { return fread(&v, sizeof v, 1, f) == 1; }

template <typename T>
bool ReadVec(FILE *f, std::vector<T> &v, size_t n)
{
	v.resize(n);
	return fread(v.data(), sizeof(T), n, f) == n;
}

template <typename T>
void Write(FILE *f, const T &v) { fwrite(&v, sizeof v, 1, f); }

template <typename T>
void WriteVec(FILE *f, const std::vector<T> &v) { fwrite(v.data(), sizeof(T), v.size(), f); }

} // namespace

float BuildingHeightScale(float requested, float tallest, float maximum)
{
	return tallest > 0.0f ? std::max(0.0f, std::min(requested, maximum / tallest)) : std::max(0.0f, requested);
}

void Map::IndexCovers()
{
	coverIndex.assign(size_t(kRainTiles) * kRainTiles, -1);
	for (size_t i = 0; i < covers.size(); i++)
		if (covers[i].cx >= 0 && covers[i].cy >= 0 && covers[i].cx < kRainTiles && covers[i].cy < kRainTiles)
			coverIndex[size_t(covers[i].cy) * kRainTiles + size_t(covers[i].cx)] = int32_t(i);
}

bool CoveredAt(const Map &map, Vec3 p)
{
	if (map.coverIndex.empty() || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
		return false;
	const float u = (p.x - kWorldMin) / kRainTile, v = (p.y - kWorldMin) / kRainTile;
	if (u < 0 || v < 0 || u >= kRainTiles || v >= kRainTiles)
		return false;
	const int32_t tile = map.coverIndex[size_t(int(v)) * kRainTiles + size_t(int(u))];
	if (tile < 0)
		return false;
	const int x = std::min(kCoverRes - 1, int((u - std::floor(u)) * kCoverRes));
	const int y = std::min(kCoverRes - 1, int((v - std::floor(v)) * kCoverRes));
	const int16_t top = map.covers[size_t(tile)].top[y * kCoverRes + x];
	return top != kOpenSky && p.z < top * 0.1f - kCoverMargin;
}

bool Map::Load(const std::string &path)
{
	*this = {};
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	char magic[8];
	uint32_t mapSize = 0, count = 0;
	bool ok = Read(f, magic) && memcmp(magic, kMagic, 7) == 0 && magic[7] >= '3' && magic[7] <= '6' &&
	          Read(f, mapSize) && mapSize > 0 && mapSize <= 8192 &&
	          ReadVec(f, classes, size_t(mapSize) * mapSize) && Read(f, count) && count <= 100000;
	const bool visual = ok && magic[7] >= '4', named = ok && magic[7] >= '5', rainy = ok && magic[7] >= '6';
	size = int(mapSize);
	for (uint32_t i = 0; ok && i < count; i++) {
		Building b;
		uint16_t nv, nt, ne;
		ok = (!visual || Read(f, b.flags)) && !(b.flags & ~(kVisualMesh | kBridge)) &&
		     Read(f, nv) && Read(f, nt) && Read(f, ne) && ReadVec(f, b.verts, nv) && ReadVec(f, b.tris, size_t(nt) * 3) &&
		     ReadVec(f, b.edges, size_t(ne) * 2);
		for (uint16_t index : b.tris)
			ok = ok && index < b.verts.size();
		for (uint16_t index : b.edges)
			ok = ok && index < b.verts.size();
		if (!ok || b.verts.empty())
			continue;
		b.lo = b.hi = {b.verts[0].x, b.verts[0].y};
		for (const Vec3 &v : b.verts) {
			ok = ok && std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
			b.lo = {std::min(b.lo.x, v.x), std::min(b.lo.y, v.y)};
			b.hi = {std::max(b.hi.x, v.x), std::max(b.hi.y, v.y)};
			b.top = std::max(b.top, v.z);
		}
		buildings.push_back(std::move(b));
	}
	ok = ok && Read(f, count) && count <= 1000000 && ReadVec(f, roads, count);
	if (ok && named) {
		uint32_t names = 0;
		ok = Read(f, names) && names < Map::kNoStreet;
		for (uint32_t i = 0; ok && i < names; i++) {
			uint8_t length = 0;
			std::string name;
			ok = Read(f, length) && length > 0;
			name.resize(length);
			ok = ok && fread(name.data(), 1, length, f) == length;
			streets.push_back(std::move(name));
		}
		ok = ok && ReadVec(f, roadStreet, roads.size()) && ReadVec(f, roadZ, roads.size() * 2);
		for (uint16_t street : roadStreet)
			ok = ok && (street == Map::kNoStreet || street < streets.size());
		for (float z : roadZ)
			ok = ok && std::isfinite(z);
	} else if (ok) {
		roadStreet.assign(roads.size(), Map::kNoStreet);
		roadZ.assign(roads.size() * 2, 0.0f);
	}
	if (ok && rainy) {
		uint32_t cells = 0, tiles = 0;
		ok = Read(f, cells) && cells <= uint32_t(kRainTiles * kRainTiles);
		for (uint32_t i = 0; ok && i < cells; i++) {
			WetCell cell;
			uint32_t points = 0;
			ok = Read(f, cell.cx) && Read(f, cell.cy) && Read(f, points) && points % 3 == 0 && points <= 300000 &&
			     ReadVec(f, cell.tris, points);
			for (const Vec3 &v : cell.tris)
				ok = ok && std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
			wet.push_back(std::move(cell));
		}
		ok = ok && Read(f, tiles) && tiles <= uint32_t(kRainTiles * kRainTiles) && ReadVec(f, covers, tiles);
	}
	fclose(f);
	if (!ok) {
		*this = {};
		return false;
	}
	if (!covers.empty())
		IndexCovers();
	return true;
}

bool Map::Save(const std::string &path) const
{
	FILE *f = fopen(path.c_str(), "wb");
	if (!f)
		return false;
	Write(f, kMagic);
	Write(f, uint32_t(size));
	WriteVec(f, classes);
	Write(f, uint32_t(buildings.size()));
	for (const Building &b : buildings) {
		Write(f, b.flags);
		Write(f, uint16_t(b.verts.size()));
		Write(f, uint16_t(b.tris.size() / 3));
		Write(f, uint16_t(b.edges.size() / 2));
		WriteVec(f, b.verts);
		WriteVec(f, b.tris);
		WriteVec(f, b.edges);
	}
	Write(f, uint32_t(roads.size()));
	WriteVec(f, roads);
	// Street names: written as they are (Load checks them). A road without one,
	// or without heights, is written as none / 0.
	Write(f, uint32_t(streets.size()));
	for (const std::string &name : streets) {
		const uint8_t length = uint8_t(std::min<size_t>(name.size(), 255));
		Write(f, length);
		fwrite(name.data(), 1, length, f);
	}
	for (size_t i = 0; i < roads.size(); i++)
		Write(f, i < roadStreet.size() ? roadStreet[i] : Map::kNoStreet);
	for (size_t i = 0; i < roads.size() * 2; i++)
		Write(f, i < roadZ.size() ? roadZ[i] : 0.0f);
	Write(f, uint32_t(wet.size()));
	for (const WetCell &cell : wet) {
		Write(f, cell.cx);
		Write(f, cell.cy);
		Write(f, uint32_t(cell.tris.size() / 3 * 3));
		fwrite(cell.tris.data(), sizeof(Vec3), cell.tris.size() / 3 * 3, f);
	}
	Write(f, uint32_t(covers.size()));
	WriteVec(f, covers);
	const bool ok = ferror(f) == 0;
	fclose(f);
	return ok;
}

} // namespace mapdata

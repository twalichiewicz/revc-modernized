#include "mapdata.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace mapdata {
namespace {

constexpr char kMagic[8] = {'V', 'C', 'M', 'M', 'M', 'A', 'P', '2'};

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

bool Map::Load(const std::string &path)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	char magic[8];
	uint32_t mapSize = 0, count = 0;
	bool ok = Read(f, magic) && memcmp(magic, kMagic, 8) == 0 && Read(f, mapSize) && mapSize > 0 && mapSize <= 8192 &&
	          ReadVec(f, classes, size_t(mapSize) * mapSize) && Read(f, count);
	size = int(mapSize);
	for (uint32_t i = 0; ok && i < count; i++) {
		Building b;
		uint16_t nv, nt, ne;
		ok = Read(f, nv) && Read(f, nt) && Read(f, ne) && ReadVec(f, b.verts, nv) && ReadVec(f, b.tris, size_t(nt) * 3) &&
		     ReadVec(f, b.edges, size_t(ne) * 2);
		if (!ok || b.verts.empty())
			continue;
		b.lo = b.hi = {b.verts[0].x, b.verts[0].y};
		for (const Vec3 &v : b.verts) {
			b.lo = {std::min(b.lo.x, v.x), std::min(b.lo.y, v.y)};
			b.hi = {std::max(b.hi.x, v.x), std::max(b.hi.y, v.y)};
			b.top = std::max(b.top, v.z);
		}
		buildings.push_back(std::move(b));
	}
	ok = ok && Read(f, count) && ReadVec(f, roads, count);
	fclose(f);
	if (!ok) {
		*this = {};
		return false;
	}
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
		Write(f, uint16_t(b.verts.size()));
		Write(f, uint16_t(b.tris.size() / 3));
		Write(f, uint16_t(b.edges.size() / 2));
		WriteVec(f, b.verts);
		WriteVec(f, b.tris);
		WriteVec(f, b.edges);
	}
	Write(f, uint32_t(roads.size()));
	WriteVec(f, roads);
	const bool ok = ferror(f) == 0;
	fclose(f);
	return ok;
}

} // namespace mapdata

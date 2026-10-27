#include "dff.h"

#include <cstring>

namespace {

struct Chunk {
	uint32_t type, size, version;
	size_t data; // where its body starts
};

bool At(const std::vector<uint8_t> &b, size_t p, Chunk &c)
{
	if (p + 12 > b.size())
		return false;
	memcpy(&c.type, &b[p], 4);
	memcpy(&c.size, &b[p + 4], 4);
	memcpy(&c.version, &b[p + 8], 4);
	c.data = p + 12;
	return c.data + c.size <= b.size();
}

std::vector<Chunk> Children(const std::vector<uint8_t> &b, size_t start, size_t end)
{
	std::vector<Chunk> out;
	Chunk c;
	for (size_t p = start; p + 12 <= end && At(b, p, c); p = c.data + c.size)
		out.push_back(c);
	return out;
}

template <typename T>
T Get(const std::vector<uint8_t> &b, size_t p)
{
	T v{};
	if (p + sizeof(T) <= b.size())
		memcpy(&v, &b[p], sizeof(T));
	return v;
}

uint32_t Library(uint32_t version)
{
	return version & 0xffff0000u ? (((version >> 14) & 0x3ff00u) + 0x30000u) | ((version >> 16) & 0x3fu) : version << 8;
}

} // namespace

bool ReadSkinnedDff(const std::vector<uint8_t> &b, vm::SkinnedMesh &out, std::string &why)
{
	Chunk clump;
	if (!At(b, 0, clump) || clump.type != 0x10) {
		why = "not a clump";
		return false;
	}
	std::vector<int> hanim; // the skin's bones, in order: their tags
	size_t geometry = 0, geometrySize = 0;
	for (const Chunk &c : Children(b, clump.data, clump.data + clump.size)) {
		if (c.type == 0x0e) { // the frame list: each frame's extension may hold HAnim data
			const auto kids = Children(b, c.data, c.data + c.size);
			if (kids.empty())
				continue;
			const uint32_t frames = Get<uint32_t>(b, kids[0].data);
			for (uint32_t i = 0; i < frames && 1 + i < kids.size(); i++)
				for (const Chunk &e : Children(b, kids[1 + i].data, kids[1 + i].data + kids[1 + i].size))
					if (e.type == 0x11e) {
						const int32_t nodes = Get<int32_t>(b, e.data + 8);
						for (int32_t k = 0; k < nodes; k++)
							hanim.push_back(Get<int32_t>(b, e.data + 20 + size_t(k) * 12));
					}
		} else if (c.type == 0x1a) {
			for (const Chunk &g : Children(b, c.data, c.data + c.size))
				if (g.type == 0x0f && !geometry) {
					geometry = g.data;
					geometrySize = g.size;
				}
		}
	}
	if (!geometry || hanim.empty()) {
		why = hanim.empty() ? "no HAnim hierarchy" : "no geometry";
		return false;
	}
	const auto kids = Children(b, geometry, geometry + geometrySize);
	size_t q = kids[0].data;
	const uint32_t flags = Get<uint32_t>(b, q);
	const int32_t triangles = Get<int32_t>(b, q + 4), vertices = Get<int32_t>(b, q + 8);
	q += 16;
	if (Library(kids[0].version) < 0x34000)
		q += 12; // ambient, specular, diffuse
	if (flags & 0x08)
		q += 4 * size_t(vertices); // prelit
	const uint32_t uvSets = (flags >> 16) & 0xff ? (flags >> 16) & 0xff : (flags & 0x04 ? 1 : 0);
	out.vertices.assign(size_t(vertices), vm::SkinVertex{});
	for (uint32_t s = 0; s < uvSets; s++)
		for (int32_t i = 0; i < vertices; i++, q += 8)
			if (s == 0) {
				out.vertices[i].u = Get<float>(b, q);
				out.vertices[i].v = Get<float>(b, q + 4);
			}
	out.triangles.resize(size_t(triangles));
	for (int32_t i = 0; i < triangles; i++, q += 8) {
		// Stored as v2, v1, material, v3.
		vm::Triangle &t = out.triangles[i];
		t.v[1] = Get<uint16_t>(b, q);
		t.v[0] = Get<uint16_t>(b, q + 2);
		t.material = Get<uint16_t>(b, q + 4);
		t.v[2] = Get<uint16_t>(b, q + 6);
	}
	q += 16; // bounding sphere
	const uint32_t hasVertices = Get<uint32_t>(b, q), hasNormals = Get<uint32_t>(b, q + 4);
	q += 8;
	for (int32_t i = 0; hasVertices && i < vertices; i++, q += 12)
		out.vertices[i].pos = {Get<float>(b, q), Get<float>(b, q + 4), Get<float>(b, q + 8)};
	for (int32_t i = 0; hasNormals && i < vertices; i++, q += 12)
		out.vertices[i].normal = {Get<float>(b, q), Get<float>(b, q + 4), Get<float>(b, q + 8)};
	bool skinned = false;
	for (size_t k = 1; k < kids.size(); k++) {
		if (kids[k].type != 0x03)
			continue;
		for (const Chunk &e : Children(b, kids[k].data, kids[k].data + kids[k].size)) {
			if (e.type != 0x116)
				continue;
			const uint8_t bones = b[e.data], used = b[e.data + 1];
			size_t o = e.data + 4 + used;
			for (int32_t i = 0; i < vertices; i++, o += 4)
				for (int c = 0; c < 4; c++)
					out.vertices[i].bone[c] = b[o + c];
			for (int32_t i = 0; i < vertices; i++, o += 16)
				for (int c = 0; c < 4; c++)
					out.vertices[i].weight[c] = Get<float>(b, o + 4 * c);
			out.bind.clear();
			for (int i = 0; i < bones; i++) {
				if (Library(kids[0].version) < 0x34003)
					o += 4; // 0xDEADDEAD
				float m[16];
				memcpy(m, &b[o], 64);
				o += 64;
				out.bind.push_back(vm::Inverse(vm::FromRw(m)));
			}
			skinned = true;
		}
	}
	if (!skinned || !hasNormals) {
		why = skinned ? "no normals" : "no skin";
		return false;
	}
	out.tags.assign(hanim.begin(), hanim.end());
	if (out.tags.size() != out.bind.size()) {
		why = "the skin's bones and the hierarchy's don't match";
		return false;
	}
	return true;
}

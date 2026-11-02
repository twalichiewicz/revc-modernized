#pragma once
#include "gamefiles.h"
#include "../../src/mapdata.h"
#include <array>

namespace visualmodel {
struct Material {
	std::string texture;
	uint8_t alpha = 255;
};
struct Face {
	uint32_t a, b, c, material;
};
struct Mesh {
	std::vector<Vec3f> vertices;
	std::vector<Face> faces;
	std::vector<Material> materials;
};
// Flatten visible DFF atomics with their complete frame hierarchy. Collision
// boxes omit roof pitches, eaves, bridge decks and rails and cannot supply this.
bool Read(const std::vector<uint8_t> &data, Mesh &out, std::string &why);
Vec3f Place(Vec3f local, const Instance &instance);
bool Build(const Mesh &mesh, const Instance &instance, float ground, mapdata::Building &out, bool bridge = false, float water = 0);
bool ActiveAt(const Instance &instance, int hour);
struct BridgePart { Vec2 lo,hi;float ground;bool deck; };
void AlignBridges(std::vector<BridgePart> &parts);
}

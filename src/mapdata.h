#pragma once
// Baked map data (tools/bake): a cleaned land/water class map, the car road
// network with real widths, and 3D buildings taken from the collision meshes.
// All in world coordinates. No game dependencies.
#include "types.h"

#include <string>
#include <vector>

namespace mapdata {

constexpr float kWorldMin = -2000.0f; // the radar art covers [-2000, 2000] on both axes
constexpr float kWorldSize = 4000.0f;

enum : uint8_t {
	kWater = 0,
	kLand = 1,
	kPark = 2,
	kBeach = 3,
	kClassMask = 0x0f,
	kArtRoad = 0x10,     // the radar art drew a road here (class holds what lies under it)
	kArtBuilding = 0x20, // ...or a building
};

// The roof surface of a building as in its collision mesh, plus the outline
// edges that get walls down to the ground.
struct Building {
	std::vector<Vec3> verts;       // world XY; z = height above the surrounding ground
	std::vector<uint16_t> tris;    // roof triangles
	std::vector<uint16_t> edges;   // outline edges as pairs; the roof lies to their left
	float top = 0;                 // highest vertex
	Vec2 lo{}, hi{};               // XY bounds
};

struct Road {
	Vec2 a, b;
	float width;        // kerb to kerb
	float joinA, joinB; // the round joint at each end: as wide as the narrowest road meeting there
};

struct Map {
	int size = 0;                 // classes is size x size; row 0 is the north edge
	std::vector<uint8_t> classes;
	std::vector<Building> buildings;
	std::vector<Road> roads;

	bool Load(const std::string &path);
	bool Save(const std::string &path) const;
};

} // namespace mapdata

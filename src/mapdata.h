#pragma once
// Baked map data (tools/bake): a cleaned land/water class map, the car road
// network with real widths, and placed visual building/bridge meshes.
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

enum : uint8_t { kVisualMesh = 1, kBridge = 2 };
// Placed visual triangles, or a collision-roof fallback with outline extrusion.
struct Building {
	uint8_t flags = 0;
	std::vector<Vec3> verts;       // world XY; z = height above the surrounding ground
	std::vector<uint16_t> tris;    // roof triangles
	std::vector<uint16_t> edges;   // outline edges as pairs; the roof lies to their left
	float top = 0;                 // highest vertex
	Vec2 lo{}, hi{};               // XY bounds
};

// One common height scale for the visible block: tall roofs fit the map
// camera without flattening different storeys onto the same height plane.
float BuildingHeightScale(float requestedScale, float tallest, float maximum);

struct Road {
	Vec2 a, b;
	float width;        // kerb to kerb
	float joinA, joinB; // the round joint at each end: as wide as the narrowest road meeting there
};

// The rain (#32, map.bin 6: wet ground; 7: cover at 1 m), by 64 m tile of the
// world (column, row from kWorldMin).
constexpr float kRainTile = 64.0f;
constexpr int kRainTiles = int(kWorldSize / kRainTile) + 1;
constexpr int kCoverRes = 64;            // cover heights per tile side: every metre
constexpr int16_t kOpenSky = -32768;      // nothing solid there at all
constexpr float kCoverMargin = 0.25f;     // m: a point this close under the top is on it, not under it
// The paved ground that gets wet: triangles (three points each), world space.
struct WetCell {
	int16_t cx = 0, cy = 0;
	std::vector<Vec3> tris;
};
// The top of everything solid over each metre of a tile (a roof, an awning, a
// bridge deck, or the ground itself), in decimetres; kOpenSky for none. A
// metre counts as under a roof only if all of it is (rain reaches the wall).
struct CoverTile {
	int16_t cx = 0, cy = 0;
	int16_t top[kCoverRes * kCoverRes];
};

struct Map {
	int size = 0;                 // classes is size x size; row 0 is the north edge
	std::vector<uint8_t> classes;
	std::vector<Building> buildings;
	std::vector<Road> roads;
	// Street names (#35, map.bin 5): each road's street (an index into
	// `streets`, kNoStreet for none) and its height at a and b (two a road).
	// Older files load with none, every road kNoStreet and its heights 0.
	static constexpr uint16_t kNoStreet = 0xffff;
	std::vector<std::string> streets;
	std::vector<uint16_t> roadStreet;
	std::vector<float> roadZ;
	// The rain (#32, map.bin 6 and 7). Older files load with neither; a 6 without the cover.
	std::vector<WetCell> wet;
	std::vector<CoverTile> covers;
	std::vector<int32_t> coverIndex; // kRainTiles^2, -1 for no tile; IndexCovers() builds it (Load does)

	void IndexCovers();
	bool Load(const std::string &path);
	bool Save(const std::string &path) const;
};

// Whether something solid is over `p` (no rain falls there): false with no cover data.
bool CoveredAt(const Map &map, Vec3 p);

} // namespace mapdata

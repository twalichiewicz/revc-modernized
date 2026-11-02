#pragma once
// The rain's world (#32), from the game's collision: the paved ground that
// gets wet, and the top of the solid things over every 2 m (where no rain falls).
#include "../../src/mapdata.h"

#include <vector>

namespace wetground {

struct Face {
	Vec3 a, b, c;
	uint8_t surface; // the game's eSurfaceType
};

constexpr int kWetTrisPerCell = 3000;

// Tarmac, pavement and the beach's concrete (surface.dat names adhesion groups,
// not surfaces: the ids are the game's eSurfaceType).
bool Paved(uint8_t surface);
// What keeps rain off: anything solid but poles, lamp posts, hydrants and wire fences.
bool Covers(uint8_t surface);

std::vector<mapdata::CoverTile> CoverTiles(const std::vector<Face> &faces);
// Paved faces facing up (the collision winds a floor clockwise seen from above;
// the other way is a ceiling), with no roof over them in `covered` (cover at
// least kRoofClearance up: a bench or a wall beside it doesn't keep it dry),
// by 64 m cell; the biggest kWetTrisPerCell per cell.
constexpr float kRoofClearance = 2.5f;
std::vector<mapdata::WetCell> WetGround(const std::vector<Face> &faces, const mapdata::Map &covered);

} // namespace wetground

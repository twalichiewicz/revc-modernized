#pragma once
// The minimap picture itself, independent of the game: base map, roads at
// their real widths, 3D buildings (depth-tested) and the GPS route, seen
// through a Projector. The plugin feeds it CRadar's state; tools/preview feeds
// it test positions.
#include "canvas.h"
#include "mapdata.h"
#include "projection.h"

#include <vector>

struct Route {
	std::vector<Vec2> points; // world XY, along road centres
	CRGBA colour;
};

struct SceneInput {
	// CRadar's view this frame: rotation, range (world units per ground unit), centre.
	float cos = 1, sin = 0, range = 120;
	Vec2 origin{};
	// Layers; any may be null.
	bool background = true;      // false: the caller already filled the rect
	void *baseTexture = nullptr; // BuildBaseImage() covering the whole world
	const std::vector<mapdata::Road> *roads = nullptr;
	const std::vector<mapdata::Building> *buildings = nullptr;
	const Route *route = nullptr;
	float buildingScale = 1.0f; // multiplier on real building heights
};

// Draws the minimap inside proj's rectangle, below where the blips go.
void DrawScene(Canvas &canvas, const Projector &proj, const SceneInput &in);

// High-contrast "N" pinned to the minimap edge in the direction of north.
void DrawNorthMarker(Canvas &canvas, const Projector &proj, const SceneInput &in);

// Colours the baked class map (RGBA, size x size, north row first). Road pixels
// of the radar art away from the real roads (runways, car parks) stay as
// tarmac; the rest give way to the vector roads.
std::vector<uint8_t> BuildBaseImage(const mapdata::Map &map);

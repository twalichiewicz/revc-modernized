#pragma once
// Wet streets (#32), without the game: which of the baked wet ground to draw
// near the camera, and the streaks the lights leave on it (in place of the
// screen-space reflections the renderer can't do: it has no depth texture).
#include "mapdata.h"

#include <functional>
#include <vector>

namespace wetmodel {

struct Light {
	Vec3 pos;
	Vec3 colour; // 0..1 and up
	float radius;
};
// On the ground: the two corners at the light's foot, then the two toward the camera.
struct Reflection {
	Vec3 quad[4];
	Vec3 colour;
};

// Wet cells (indices into map.wet) within `radius` of the camera, across the ground.
std::vector<int> CellsNear(const mapdata::Map &map, Vec3 camera, float radius = 150);
// The highest wet ground under `p` (at or below it); false where there's none.
bool WetGroundAt(const mapdata::Map &map, Vec3 p, float &z);
using GroundAt = std::function<bool(Vec3 below, float &z)>;
// One streak per light on the wet ground under it: from just behind its foot
// toward the camera, longer the more grazing the view; the `cap` nearest and
// brightest.
std::vector<Reflection> Reflections(const std::vector<Light> &lights, Vec3 camera, const GroundAt &ground, int cap = 24);

} // namespace wetmodel

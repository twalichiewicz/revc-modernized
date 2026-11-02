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
// The same answer as WetGroundAt, quickly, within `reach` of a point: the
// near wet triangles binned on a 2 m grid (for the many splashes each frame).
class GroundIndex {
public:
	GroundIndex(const mapdata::Map &map, Vec3 centre, float reach);
	bool At(Vec3 p, float &z) const;

private:
	float x0_, y0_;
	int side_;
	std::vector<Vec3> tris_;
	std::vector<std::vector<uint32_t>> bins_; // triangle starts in tris_
};
// One streak per light on the wet ground under it: from just behind its foot
// toward the camera, longer the more grazing the view; the `cap` nearest and
// brightest.
std::vector<Reflection> Reflections(const std::vector<Light> &lights, Vec3 camera, const GroundAt &ground, int cap = 24);

// Puddles: a few still spots per cell, on its flattest triangles and wholly on
// one (they never hang over a kerb), the same each time (seeded by the cell's
// place), as big as `level` (rainmodel's puddles, 0..1) lets them be.
struct Puddle {
	Vec3 centre;
	float radius;
};
std::vector<Puddle> Puddles(const mapdata::WetCell &cell, float level);
// Whether there's anything wet to draw: the ground, or puddles still draining after it's dried.
bool Shows(float wetness, float puddles);

// Splashes: drops bursting on the wet ground (and on car roofs) round the
// camera, each for a moment, as many as the rain is heavy.
constexpr float kSplashReach = 25.0f; // m
struct Roof {
	Vec3 centre, right, forward; // the middle of a car's roof and its axes
};
struct Splash {
	Vec3 pos;
	float size; // m
	float age;  // 0..1 through its life
};
// `covered` keeps them off the roofs of cars under bridges and awnings (the
// wet ground is already only where the rain falls).
std::vector<Splash> Splashes(Vec3 camera, float rain, float seconds, const GroundAt &ground,
                             const std::vector<Roof> &roofs = {}, const std::function<bool(Vec3)> &covered = {});

} // namespace wetmodel

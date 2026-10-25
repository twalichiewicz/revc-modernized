#pragma once
// The next turn along a route (world XY points, from where you are): where
// the road bends by at least an angle, measured over a stretch either side of
// each point so a junction rounded off over a few nodes reads as one turn and
// a long bend doesn't read as one at all. For the car's blinkers.
#include "types.h"

#include <vector>

namespace turns {

constexpr int kLeft = -1, kRight = 1;

struct Turn {
	float distance = 0; // along the route, m
	int side = 0;       // kLeft, kRight, or 0 for none
};

// The first bend of at least `minAngle` (radians) within `lookAhead` metres,
// directions taken `window` metres before and after each point.
Turn Next(const std::vector<Vec2> &points, float minAngle, float window, float lookAhead);

} // namespace turns

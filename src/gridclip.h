#pragma once
// The world's grid of sectors (CWorld::ms_aSectors: 80 by 80, 50 m each,
// x -2400 to 1600, y -2000 to 2000), and a line cut to the part of it on the
// grid (gridfix.cpp). No game in it; unit-tested (tests/test_gridclip.cpp).
#include "types.h"

namespace gridclip {

constexpr float kMinX = -2400.0f, kMaxX = 1600.0f, kMinY = -2000.0f, kMaxY = 2000.0f;
constexpr float kInside = 0.5f; // m in from the edge, clear of its last sector's far side

// The part of a to b on the grid (in from its edge), in `from` and `to`; false if none of it is.
bool Clip(Vec3 a, Vec3 b, Vec3 &from, Vec3 &to);

} // namespace gridclip

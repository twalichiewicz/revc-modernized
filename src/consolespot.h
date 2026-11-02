#pragma once
// Where a gun lies in a car while he drives (seatgun.cpp): on the console
// between the front seats if the car's model has one beside him (flat for the
// gun's length, with room over it), else on the inner edge of his own seat.
// From the body's triangles in the car's frame (x right, y ahead, z up) and
// the driver's seat (ped_frontseat on his side: his hips). And how the gun
// lies there: on its side, along the way it points, resting on the surface.
// No game in it; unit-tested (tests/test_consolespot.cpp).
#include "types.h"

#include <vector>

namespace consolespot {

struct Triangle {
	Vec3 a, b, c;
};

enum class Kind { kConsole, kSeat, kGuess };
struct Spot {
	Vec3 at;    // on the surface, under the gun's middle
	Vec3 along; // the way it lies: as asked on the console, straight ahead on his seat (by his thigh)
	Kind kind;
};

// The highest surface facing up straight under (x, y), from `from` down to
// `from - depth`: its height in `z`; false if there's none.
bool Down(const std::vector<Triangle> &body, float x, float y, float from, float depth, float &z);

// Where it lies, `length` m long: on the console `along` (unit, flat).
Spot Find(const std::vector<Triangle> &body, Vec3 seat, Vec3 along, float length);

// The gun's frame resting at `spot`: its model's x (the barrel) along the
// spot's way, its y (its side) up, its box's middle over the spot and its
// lowest point on it. Columns x, y, z and the origin.
struct Pose {
	Vec3 x, y, z, origin;
};
Pose Lay(const Spot &spot, Vec3 boxMin, Vec3 boxMax);

} // namespace consolespot

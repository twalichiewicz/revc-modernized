#pragma once
// Seeing the road from the driver's seat in first person. His eye is where
// the game sits his head (CPed::SetPedPositionInCar puts him on the car's
// ped_frontseat, CAR_sit holds his head 0.53 m over it), and in a lot of the
// game's cars that has the hood or the dash at his eyes: nothing of the road
// for a long way ahead. Seen from it straight ahead (a few degrees either
// way), the view out is a window between the hood and dash below and the
// roof's front edge above; his eye goes up until the hood's this far under
// level, short of the roof over him and of the roof's edge coming down over
// the road. From the body's opaque triangles (no glass), in the car's frame
// (x right, y ahead, z up). No game in it; unit-tested (tests/test_seatview.cpp).
#include "types.h"

#include <vector>

namespace seatview {

struct Triangle {
	Vec3 a, b, c;
};

constexpr float kAcross = 12.0f;   // degrees either side of straight ahead he looks along
constexpr float kBelow = 7.0f;     // degrees: the hood and dash under level at least, as in a real car
constexpr float kAbove = 4.0f;     // degrees: the roof's front edge over level at least
constexpr float kHighest = 0.25f;  // m he's raised at most
constexpr float kHeadroom = 0.06f; // m kept between his eye and the roof over it

// The view out ahead (degrees from level): `low` the hood line, `high` the
// roof's edge, the worst across the way ahead. None: low over high.
struct Window {
	float low, high;
};
Window Ahead(const std::vector<Triangle> &body, Vec3 eye, float lift);

// How far (m) to raise his eye.
float Lift(const std::vector<Triangle> &body, Vec3 eye);

} // namespace seatview

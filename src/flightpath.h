#pragma once
// The airliners' flight path (data/paths/flight.dat: the three jumbos taking
// off from Escobar International, round the city and back down onto the
// runway) raised above the volumetric clouds' 2600m top. The game flew them
// at 345-450m. The airborne vertical envelope is eased away from the original
// departure and arrival; the runway and the first and last of the climb stay
// (a node under 14 m is on the ground to the game, which puts it at 14 m as
// it loads the path: those stay put). The game times the flight by the
// path's length over the ground, so nothing else changes. No game in it;
// unit-tested (tests/test_flightpath.cpp).
#include "types.h"

#include <vector>

namespace flightpath {

constexpr float kGround = 14.0f;  // m: at or under this a node is on the runway (CPlane::InitPlanes sets them to it)
constexpr float kClimb = 0.40f;   // maximum vertical envelope per metre of horizontal route
constexpr float kDescent = 0.38f;
constexpr float kPeak = 3800.0f;  // m: actual flight.dat peaks above 3000m after easing

// The nodes of a closed loop, raised in place.
void Raise(std::vector<Vec3> &nodes);

} // namespace flightpath

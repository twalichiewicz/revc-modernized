#pragma once
#include "game.h"
namespace swimmotion
{
struct Contact {
	CVector normal{};
	float depth = 0;
};
// Returns an overlap and its outward normal/depth. Tests use real geometry;
// the runtime adapter uses CWorld collision against solid world and vehicles.
using Query = bool (*)(void *, CVector, float, CVector motion, Contact &);
struct Shape {
	CVector axis{0, 0, 1};
	float radius = 0.30f, halfLength = 0.60f;
};
// Sweep the body's whole extent, then slide at contacts. Bounded work; very
// large movement is stopped at the last checked point rather than teleported.
CVector Move(CVector from, CVector delta, const Shape &, Query, void *);
} // namespace swimmotion

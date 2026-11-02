#include "carlightrules.h"
#include <cassert>
#include <cstdio>
int main()
{
	using namespace carlightrules;
	assert(SuppressCabinLight(true, true, 0x256e9c));
	assert(SuppressCabinLight(true, true, 0x257178));
	assert(!SuppressCabinLight(false, true, 0x256e9c));
	assert(!SuppressCabinLight(true, false, 0x256e9c));
	assert(!SuppressCabinLight(true, true, 0x256e1c)); // headlights stay active
	assert(SuppressCabinCorona(true, true, 14));
	assert(SuppressCabinCorona(true, true, 15));
	assert(!SuppressCabinCorona(true, true, 13));
	assert(!SuppressCabinCorona(false, true, 14));
	assert(InteriorAlpha(128) == 48 && InteriorAlpha(24) == 24);
	assert(InteriorAlpha(0) == 0 && InteriorAlpha(255) == 255);
	const auto lens = LensCoordinates({1, -2, 0.6f}, {1, -2, 0.6f});
	assert(lens.x == 0 && lens.y == 0 && lens.z == 0);
	const auto cabin = LensCoordinates({1, 0, 0.6f}, {1, -2, 0.6f});
	assert(cabin.y > 1);
	puts("all car light rule tests passed");
}

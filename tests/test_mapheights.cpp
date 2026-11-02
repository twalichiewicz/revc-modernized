#include "mapdata.h"
#include <cassert>
#include <cmath>
#include <cstdio>
int main()
{
	const float scale = mapdata::BuildingHeightScale(1.0f / 120, 400, 1.2f);
	assert(400 * scale <= 1.20001f);
	assert(std::fabs((400 * scale) / (200 * scale) - 2) < 0.0001f);
	assert(300 * scale > 200 * scale && 200 * scale > 100 * scale);
	assert(mapdata::BuildingHeightScale(1.0f / 120, 50, 1.2f) == 1.0f / 120);
	assert(mapdata::BuildingHeightScale(0, 400, 1.2f) == 0);
	assert(mapdata::BuildingHeightScale(0.01f, 0, 1.2f) == 0.01f);
	puts("all map height tests passed");
}

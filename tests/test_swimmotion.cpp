#include "swimmotion.h"
#include <cmath>
#include <cstdio>
static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)
struct World {
	bool floor = false, wall = false, slope = false;
};
static bool WorldQuery(void *ctx, CVector p, float r, CVector, swimmotion::Contact &hit)
{
	auto &w = *static_cast<World *>(ctx);
	if (w.wall && p.x + r > 1 && p.x - r < 1.05f) {
		hit = {{-1, 0, 0}, p.x + r - 1};
		return true;
	}
	if (w.floor && p.z - r < 0) {
		hit = {{0, 0, 1}, r - p.z};
		return true;
	}
	if (w.slope && (p.z - p.x) * 0.70710678f < r) {
		hit = {{-0.70710678f, 0, 0.70710678f}, r - (p.z - p.x) * 0.70710678f};
		return true;
	}
	return false;
}
int main()
{
	using namespace swimmotion;
	Shape shape;
	shape.axis = {0, 1, 0};
	World empty;
	auto free = Move({0, 0, 1}, {0.4f, 0.3f, -0.2f}, shape, WorldQuery, &empty);
	CHECK(std::fabs(free.x - .4f) < 1e-5f && std::fabs(free.z - .8f) < 1e-5f);
	World wall;
	wall.wall = true;
	auto stop = Move({0, 0, 1}, {5, 0, 0}, shape, WorldQuery, &wall);
	CHECK(stop.x < .701f && stop.x > .65f);
	auto slide = Move({0, 0, 1}, {2, 1, 0}, shape, WorldQuery, &wall);
	CHECK(slide.x < .701f && slide.y > .99f);
	World floor;
	floor.floor = true;
	auto dive = Move({0, 0, 1}, {0, 0, -3}, shape, WorldQuery, &floor);
	CHECK(dive.z >= .299f && dive.z < .32f);
	shape.axis = {0, 0, 1};
	auto tread = Move({0, 0, 2}, {0, 0, -3}, shape, WorldQuery, &floor);
	CHECK(tread.z >= .899f && tread.z < .92f); // feet, not just hips
	shape.axis = {1, 0, 0};
	auto longBody = Move({0, 0, 2}, {2, 0, 0}, shape, WorldQuery, &wall);
	CHECK(longBody.x < .101f); // head/body extent
	World slope;
	slope.slope = true;
	shape.axis = {0, 1, 0};
	auto incline = Move({0, 0, 1}, {2, 0, -1}, shape, WorldQuery, &slope);
	CHECK(incline.z - incline.x > .423f);
	auto huge = Move({0, 0, 5}, {1000, 0, 0}, shape, WorldQuery, &empty);
	CHECK(huge.x < 100); // bounded collision work does not tunnel on a hitch
	if (failures)
		return 1;
	std::puts("all swimming motion tests passed");
}

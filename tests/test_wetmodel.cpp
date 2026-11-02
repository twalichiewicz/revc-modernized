// Wet streets (#32), without the game: which wet ground is drawn near the
// camera, and the streaks the lights leave on it.
#include "wetmodel.h"
#include <cmath>
#include <cstdio>

using namespace wetmodel;
static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)

// A wet cell holding a square of ground at height z.
static mapdata::WetCell Cell(float x0, float y0, float x1, float y1, float z)
{
	mapdata::WetCell c;
	c.cx = int16_t(std::floor((x0 - mapdata::kWorldMin) / mapdata::kRainTile));
	c.cy = int16_t(std::floor((y0 - mapdata::kWorldMin) / mapdata::kRainTile));
	c.tris = {{x0, y0, z}, {x0, y1, z}, {x1, y1, z}, {x0, y0, z}, {x1, y1, z}, {x1, y0, z}};
	return c;
}

static const GroundAt kFlat = [](Vec3, float &z) {
	z = 0;
	return true;
};

static float Length(const Reflection &r)
{
	const float fx = (r.quad[0].x + r.quad[1].x) / 2, fy = (r.quad[0].y + r.quad[1].y) / 2;
	const float tx = (r.quad[2].x + r.quad[3].x) / 2, ty = (r.quad[2].y + r.quad[3].y) / 2;
	return std::hypot(tx - fx, ty - fy);
}

static void TestCellsNear()
{
	mapdata::Map m;
	m.wet = {Cell(0, 0, 10, 10, 0), Cell(130, 0, 140, 10, 0), Cell(400, 0, 410, 10, 0), Cell(-300, -300, -290, -290, 0)};
	const std::vector<int> near = CellsNear(m, {5, 5, 2}, 150);
	CHECK(near.size() == 2 && near[0] == 0 && near[1] == 1);
	CHECK(CellsNear(m, {405, 5, 2}, 150).size() == 1);
}

static void TestWetGroundAt()
{
	mapdata::Map m;
	m.wet = {Cell(0, 0, 10, 10, 3), Cell(0, 0, 10, 10, 9)}; // a road, and a deck over it
	float z = 0;
	CHECK(WetGroundAt(m, {5, 5, 12}, z) && z == 9); // under a lamp on the deck
	CHECK(WetGroundAt(m, {5, 5, 8}, z) && z == 3);  // under a lamp below it
	CHECK(!WetGroundAt(m, {20, 5, 12}, z));         // not over wet ground
}

static void TestReflectionPointsToCamera()
{
	const std::vector<Reflection> r = Reflections({{{10, 0, 6}, {1, 1, 1}, 8}}, {0, 0, 2}, kFlat);
	CHECK(r.size() == 1);
	if (r.empty())
		return;
	CHECK(r[0].quad[0].x > r[0].quad[3].x && r[0].quad[1].x > r[0].quad[2].x); // the far end is the camera's side
	CHECK(r[0].quad[0].x > 9 && r[0].quad[3].x > 0.5f);                        // from about under the light, short of him
	CHECK(std::fabs(r[0].quad[0].y + r[0].quad[1].y) < 1e-3f);                // straight at him
	CHECK(r[0].quad[0].z == 0 && r[0].quad[2].z == 0);                         // on the ground
}

static void TestReflectionLongerAtGrazing()
{
	const std::vector<Light> light{{{30, 0, 6}, {1, 1, 1}, 8}};
	const std::vector<Reflection> low = Reflections(light, {0, 0, 1.6f}, kFlat), high = Reflections(light, {0, 0, 25}, kFlat);
	CHECK(low.size() == 1 && high.size() == 1);
	if (low.size() == 1 && high.size() == 1)
		CHECK(Length(low[0]) > 2 * Length(high[0]));
}

static void TestReflectionCap()
{
	std::vector<Light> lights;
	for (int i = 0; i < 60; ++i) // in a row going away, the third one dim
		lights.push_back({{4.0f + i * 1.5f, 3, 6}, i == 2 ? Vec3{0.02f, 0.02f, 0.02f} : Vec3{1, 1, 1}, 8});
	const std::vector<Reflection> r = Reflections(lights, {0, 0, 2}, kFlat, 24);
	CHECK(r.size() == 24);
	bool nearest = true, dim = false;
	for (const Reflection &x : r) {
		nearest &= x.quad[0].x < 4.0f + 25 * 1.5f + 1; // the 25 nearest, less the dim one
		dim |= x.colour.x < 0.1f;
	}
	CHECK(nearest && !dim);
}

static void TestNoWetMeshDrawsNothing()
{
	const mapdata::Map m;
	CHECK(CellsNear(m, {0, 0, 0}, 150).empty());
	const GroundAt none = [&](Vec3 p, float &z) { return WetGroundAt(m, p, z); };
	CHECK(Reflections({{{10, 0, 6}, {1, 1, 1}, 8}}, {0, 0, 2}, none).empty());
}

int main()
{
	TestCellsNear();
	TestWetGroundAt();
	TestReflectionPointsToCamera();
	TestReflectionLongerAtGrazing();
	TestReflectionCap();
	TestNoWetMeshDrawsNothing();
	if (failures)
		return 1;
	std::puts("wetmodel: all wet street tests passed");
}

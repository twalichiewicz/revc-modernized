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

static void TestGroundIndexAgrees()
{
	mapdata::Map m;
	m.wet = {Cell(0, 0, 10, 10, 3), Cell(0, 0, 10, 10, 9), Cell(30, 0, 50, 20, 1), Cell(400, 0, 410, 10, 0)};
	const GroundIndex index(m, {5, 5, 2}, 60);
	bool agree = true;
	for (float x = -5; x < 60; x += 0.7f)
		for (float y = -5; y < 25; y += 0.9f)
			for (float z : {2.0f, 12.0f}) {
				float a = -1, b = -1;
				const bool fa = WetGroundAt(m, {x, y, z}, a), fb = index.At({x, y, z}, b);
				agree &= fa == fb && (!fa || a == b);
			}
	float z = 0;
	CHECK(agree && !index.At({405, 5, 2}, z)); // beyond its reach: nothing
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

// A cell of road: a grid of flat squares, and a slope.
static mapdata::WetCell Street()
{
	mapdata::WetCell c;
	c.cx = 31;
	c.cy = 20;
	const float x0 = -16, y0 = -720;
	for (int i = 0; i < 6; i++)
		for (int j = 0; j < 6; j++) {
			const float x = x0 + i * 10.0f, y = y0 + j * 10.0f;
			c.tris.insert(c.tris.end(), {{x, y, 5}, {x, y + 10, 5}, {x + 10, y + 10, 5}, {x, y, 5}, {x + 10, y + 10, 5}, {x + 10, y, 5}});
		}
	c.tris.insert(c.tris.end(), {{x0, y0 + 60, 5}, {x0, y0 + 63, 9}, {x0 + 3, y0 + 63, 9}}); // a ramp
	return c;
}

// Its distance inside the triangle it's on (negative: outside every one).
static float Inside(const mapdata::WetCell &c, Vec3 p)
{
	float best = -1;
	for (size_t t = 0; t + 2 < c.tris.size(); t += 3) {
		const Vec3 v[3] = {c.tris[t], c.tris[t + 1], c.tris[t + 2]};
		float d = 1e9f;
		for (int e = 0; e < 3; e++) {
			const Vec3 a = v[e], b = v[(e + 1) % 3];
			const float ex = b.x - a.x, ey = b.y - a.y, len = std::hypot(ex, ey);
			const float side = ((p.x - a.x) * ey - (p.y - a.y) * ex) / len;
			const float inward = ((v[(e + 2) % 3].x - a.x) * ey - (v[(e + 2) % 3].y - a.y) * ex) > 0 ? side : -side;
			d = std::min(d, inward);
		}
		best = std::max(best, d);
	}
	return best;
}

static void TestPuddlesStable()
{
	const mapdata::WetCell c = Street(), copy = Street();
	const std::vector<Puddle> a = Puddles(c, 1), b = Puddles(copy, 1);
	CHECK(!a.empty() && a.size() == b.size());
	bool same = true, onFlat = true;
	for (size_t i = 0; i < a.size() && i < b.size(); i++) {
		same &= a[i].centre.x == b[i].centre.x && a[i].centre.y == b[i].centre.y && a[i].radius == b[i].radius;
		onFlat &= a[i].centre.z == 5 && Inside(c, a[i].centre) >= a[i].radius; // all of it on one flat triangle
	}
	CHECK(same && onFlat);
	mapdata::WetCell other = Street();
	other.cx = 32; // another place: other spots
	const std::vector<Puddle> o = Puddles(other, 1);
	CHECK(!o.empty() && (o[0].centre.x != a[0].centre.x || o[0].centre.y != a[0].centre.y));
}

static void TestPuddlesGrowWithLevel()
{
	const std::vector<Puddle> half = Puddles(Street(), 0.5f), full = Puddles(Street(), 1);
	CHECK(half.size() == full.size() && !full.empty());
	bool grew = true;
	for (size_t i = 0; i < half.size() && i < full.size(); i++)
		grew &= half[i].centre.x == full[i].centre.x && std::fabs(full[i].radius - 2 * half[i].radius) < 1e-4f;
	CHECK(grew);
}

static void TestNoPuddlesWhenDry()
{
	CHECK(Puddles(Street(), 0).empty() && Puddles(Street(), 0.02f).empty());
	mapdata::WetCell steep;
	steep.tris = {{0, 0, 0}, {0, 10, 4}, {10, 10, 4}}; // a ramp holds no water
	CHECK(Puddles(steep, 1).empty());
}

static void TestSplashesScaleWithRain()
{
	const Vec3 eye{0, 0, 2};
	const std::vector<Splash> heavy = Splashes(eye, 1, 7.3f, kFlat), light = Splashes(eye, 0.3f, 7.3f, kFlat),
	                          none = Splashes(eye, 0, 7.3f, kFlat);
	CHECK(heavy.size() > light.size() && !light.empty() && none.empty());
	bool near = true, alive = true;
	for (const Splash &s : heavy) {
		near &= std::hypot(s.pos.x - eye.x, s.pos.y - eye.y) <= kSplashReach && s.pos.z == 0;
		alive &= s.age >= 0 && s.age < 1 && s.size > 0;
	}
	CHECK(near && alive);
	// Each lives a moment: a little later most are new ones elsewhere.
	const std::vector<Splash> later = Splashes(eye, 1, 7.8f, kFlat);
	int same = 0;
	for (size_t i = 0; i < heavy.size() && i < later.size(); i++)
		same += heavy[i].pos.x == later[i].pos.x && heavy[i].pos.y == later[i].pos.y;
	CHECK(later.size() == heavy.size() && same < int(heavy.size()) / 4);
	// On a car roof too.
	const std::vector<Splash> roofs = Splashes(eye, 1, 7.3f, kFlat, {{{5, 0, 1.4f}, {1, 0, 0}, {0, 1, 0}}});
	bool onRoof = false;
	for (const Splash &s : roofs)
		onRoof |= s.pos.z == 1.4f && std::fabs(s.pos.x - 5) < 0.6f;
	CHECK(roofs.size() > heavy.size() && onRoof);
	// Nowhere wet: none.
	CHECK(Splashes(eye, 1, 7.3f, [](Vec3, float &) { return false; }).empty());
}

int main()
{
	TestCellsNear();
	TestWetGroundAt();
	TestGroundIndexAgrees();
	TestReflectionPointsToCamera();
	TestReflectionLongerAtGrazing();
	TestReflectionCap();
	TestNoWetMeshDrawsNothing();
	TestPuddlesStable();
	TestPuddlesGrowWithLevel();
	TestNoPuddlesWhenDry();
	TestSplashesScaleWithRain();
	if (failures)
		return 1;
	std::puts("wetmodel: all wet street tests passed");
}

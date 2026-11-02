// Unit tests for the airliners' flight path raised into the clouds: make test
#include "flightpath.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

// A loop like flight.dat's: down the runway, a climb to 345 m, round the city
// at that height for 15 km, down at 17% onto the runway, along it back to the start.
static std::vector<Vec3> Loop()
{
	std::vector<Vec3> p;
	float x = 0.0f;
	auto at = [&](float z) { p.push_back({x, 0.0f, z}); };
	for (; x < 600.0f; x += 60.0f)
		at(-5.0f);
	for (float z = 10.0f; z < 345.0f; x += 60.0f, z += 60.0f * 0.13f)
		at(z);
	for (float end = x + 15000.0f; x < end; x += 120.0f)
		at(345.0f);
	for (float z = 345.0f; z > 14.0f; x += 60.0f, z -= 60.0f * 0.17f)
		at(z);
	for (float end = x + 800.0f; x < end; x += 80.0f)
		at(-5.0f);
	return p;
}

static void TestRaisedIntoTheClouds()
{
	const std::vector<Vec3> before = Loop();
	std::vector<Vec3> after = before;
	flightpath::Raise(after);
	float highest = 0.0f;
	for (size_t i = 0; i < after.size(); i++) {
		CHECK(after[i].z >= before[i].z - 1e-3f); // never lower
		CHECK(after[i].x == before[i].x && after[i].y == before[i].y);
		if (before[i].z < flightpath::kGround)
			CHECK(after[i].z == before[i].z); // the runway as it was
		highest = std::fmax(highest, after[i].z);
	}
	CHECK(highest > 3000.0f && highest <= flightpath::kPeak + 1e-3f); // clear the actual 2600m cloud top
}

// The climb and the descent no steeper than a little over the ways asked,
// and smooth: no sudden change of slope from one node to the next that the
// old path didn't have there (it went from climbing to level at once).
static void TestClimbsAndComesDownSmoothly()
{
	const std::vector<Vec3> before = Loop();
	std::vector<Vec3> p = before;
	flightpath::Raise(p);
	float lastSlope = 0.0f, lastBefore = 0.0f;
	bool flying = false;
	for (size_t i = 1; i < p.size(); i++) {
		const float run = p[i].x - p[i - 1].x, slope = (p[i].z - p[i - 1].z) / run;
		const float was = (before[i].z - before[i - 1].z) / run;
		const bool air = p[i].z >= flightpath::kGround && p[i - 1].z >= flightpath::kGround;
		if (air) {
			CHECK(slope <= flightpath::kClimb * 1.3f && slope >= -std::fmax(.17f,flightpath::kDescent)*1.3f);
			if (flying)
				CHECK(std::fabs(slope - lastSlope) / run < 0.001f || // a turn over a few hundred metres
				      std::fabs(slope - lastSlope) <= std::fabs(was - lastBefore) + 1e-3f);
		}
		flying = air;
		lastSlope = slope;
		lastBefore = was;
	}
}

// As the game has the path once it's loaded: its runway at 14 m (CPlane::InitPlanes
// puts every node under 14 m there). Raised the same, the runway left at 14 m.
static void TestAsTheGameHasIt()
{
	std::vector<Vec3> before = Loop();
	for (Vec3 &p : before)
		if (p.z < flightpath::kGround)
			p.z = flightpath::kGround;
	std::vector<Vec3> after = before;
	flightpath::Raise(after);
	float highest = 0.0f;
	for (size_t i = 0; i < after.size(); i++) {
		if (before[i].z <= flightpath::kGround)
			CHECK(after[i].z == flightpath::kGround);
		highest = std::fmax(highest, after[i].z);
	}
	CHECK(highest > 3000.0f);
}

// A path that never leaves the ground (or a broken one) is left alone.
static void TestLeavesTheGroundAlone()
{
	std::vector<Vec3> p = {{0, 0, -5}, {100, 0, -5}, {200, 0, -5}};
	flightpath::Raise(p);
	CHECK(p[1].z == -5.0f);
	std::vector<Vec3> tiny = {{0, 0, 50}};
	flightpath::Raise(tiny);
	CHECK(tiny[0].z == 50.0f);
}

int main()
{
	TestRaisedIntoTheClouds();
	TestClimbsAndComesDownSmoothly();
	TestAsTheGameHasIt();
	TestLeavesTheGroundAlone();
	if (gFailures)
		return 1;
	printf("flightpath: all passed\n");
	return 0;
}

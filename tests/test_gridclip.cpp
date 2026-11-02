// Unit tests for lines cut to the world's grid of sectors: make test
#include "gridclip.h"

#include <cmath>
#include <cstdio>
#include <limits>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

static bool OnIt(Vec3 p)
{
	return p.x >= gridclip::kMinX + gridclip::kInside - 1e-2f && p.x <= gridclip::kMaxX - gridclip::kInside + 1e-2f &&
	       p.y >= gridclip::kMinY + gridclip::kInside - 1e-2f && p.y <= gridclip::kMaxY - gridclip::kInside + 1e-2f;
}

// On the grid both ends: as it was.
static void TestOnTheGrid()
{
	Vec3 f, t;
	CHECK(gridclip::Clip({0, 0, 0}, {100, -50, 5}, f, t) && f.x == 0 && f.y == 0 && t.x == 100 && t.y == -50 && t.z == 5);
}

// From the airport to an airliner off the grid's south edge (y -2206): cut at the edge, on the same line.
static void TestCutAtTheEdge()
{
	Vec3 f, t;
	const Vec3 a{-1300, -1800, 10}, b{-1369, -2206, 300};
	CHECK(gridclip::Clip(a, b, f, t) && OnIt(t) && f.y == a.y);
	CHECK(std::fabs(t.y - (gridclip::kMinY + gridclip::kInside)) < 0.01f);
	const float k = (t.y - a.y) / (b.y - a.y);
	CHECK(std::fabs(t.x - (a.x + (b.x - a.x) * k)) < 0.01f && std::fabs(t.z - (a.z + (b.z - a.z) * k)) < 0.01f);
	CHECK(gridclip::Clip(b, a, f, t) && OnIt(f) && t.y == a.y); // the other way round
}

// Across the whole grid and out both sides; wholly off it; along its edge, off it.
static void TestThroughAndOff()
{
	Vec3 f, t;
	CHECK(gridclip::Clip({-3000, 0, 0}, {2000, 10, 0}, f, t) && OnIt(f) && OnIt(t));
	CHECK(!gridclip::Clip({0, -2500, 0}, {100, -2600, 0}, f, t));
	CHECK(!gridclip::Clip({-2600, -100, 0}, {-2600, 100, 0}, f, t));
}

static void TestNaN()
{
	Vec3 f, t;
	const float nan = std::numeric_limits<float>::quiet_NaN();
	CHECK(!gridclip::Clip({nan, 0, 0}, {0, 0, 0}, f, t));
}

int main()
{
	TestOnTheGrid();
	TestCutAtTheEdge();
	TestThroughAndOff();
	TestNaN();
	if (gFailures)
		return 1;
	printf("gridclip: all passed\n");
	return 0;
}

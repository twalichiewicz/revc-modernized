// The ray-traced shadows' dynamic triangles, checked before the GPU sees them:
// clean ones pass untouched; one with a non-finite vertex, a vertex out of the
// world, or an edge no moving thing has is left out, and reported.
#include "rtgeometry.h"
#include <cmath>
#include <cstdio>
#include <limits>

using namespace rtgeometry;
static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)

static void Triangle(std::vector<float> &v, float x, float y, float z, float size = 1)
{
	const float t[9] = {x, y, z, x + size, y, z, x, y + size, z};
	v.insert(v.end(), t, t + 9);
}

int main()
{
	// Clean: untouched, and described.
	std::vector<float> tris, out{42};
	Triangle(tris, -10, 5, 2);
	Triangle(tris, 30, -4, 8, 2);
	Report r;
	CHECK(!Clean(tris.data(), 2, out, r));
	CHECK(out.size() == 1 && out[0] == 42); // not copied
	CHECK(r.triangles == 2 && r.dropped == 0 && r.firstBad == -1);
	CHECK(r.lo[0] == -10 && r.lo[1] == -4 && r.lo[2] == 2 && r.hi[0] == 32 && r.hi[1] == 6 && r.hi[2] == 8);
	CHECK(std::fabs(r.longestEdge - std::sqrt(8.0f)) < 1e-5f);

	// A NaN vertex, an infinite one, one out of the world, a spike: each left out.
	const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
	for (int kind = 0; kind < 4; ++kind) {
		std::vector<float> mixed;
		Triangle(mixed, 1, 1, 1);
		Triangle(mixed, 2, 2, 2);
		Triangle(mixed, 3, 3, 3);
		float &bad = mixed[9 + 4]; // the second triangle's second vertex, y
		bad = kind == 0 ? nan : kind == 1 ? -inf : kind == 2 ? 25000.0f : 2 + 500.0f;
		Report m;
		std::vector<float> kept;
		CHECK(Clean(mixed.data(), 3, kept, m));
		CHECK(kept.size() == 18 && kept[0] == 1 && kept[9] == 3);
		CHECK(m.triangles == 3 && m.dropped == 1 && m.firstBad == 1);
		CHECK(m.bad[0] == 2 && (kind == 0 ? std::isnan(m.bad[4]) : m.bad[4] == bad));
		CHECK(m.hi[0] == 4 && m.lo[0] == 1); // the kept ones' bounds
	}
	// Whole and small, but out of the world (its entity's matrix gone wrong).
	std::vector<float> far;
	Triangle(far, 0, 0, 0);
	Triangle(far, 30000, -30000, 5);
	Report f;
	std::vector<float> near;
	CHECK(Clean(far.data(), 2, near, f) && f.dropped == 1 && f.firstBad == 1 && near.size() == 9);
	// Big but real: a jumbo's wing (70 m), far out over the sea, high up.
	std::vector<float> plane;
	Triangle(plane, -2400, 1800, 3500, 70);
	Report p;
	CHECK(!Clean(plane.data(), 1, out, p) && p.dropped == 0);
	// All bad: nothing left to trace.
	std::vector<float> none;
	Triangle(none, nan, 0, 0);
	Report n;
	CHECK(Clean(none.data(), 1, out, n) && out.empty() && n.dropped == 1);
	// Nothing given.
	Report z;
	CHECK(!Clean(nullptr, 0, out, z) && z.triangles == 0);

	// Collapsed to a point (an object whose matrix is all zeros puts every one of
	// its triangles at the world's origin): no area, no shadow; left out, counted.
	std::vector<float> zero;
	Triangle(zero, 5, 5, 5);
	for (int i = 0; i < 3; ++i) zero.insert(zero.end(), 9, 0.0f);
	const float point[9] = {7, 7, 7, 7, 7, 7, 7, 7, 7};
	zero.insert(zero.end(), point, point + 9);
	Triangle(zero, 6, 6, 6);
	Report c;
	std::vector<float> area;
	CHECK(Clean(zero.data(), 6, area, c));
	CHECK(c.collapsed == 4 && c.atOrigin == 3 && c.dropped == 0 && area.size() == 18 && area[9] == 6);
	CHECK(c.hi[0] == 7 && c.lo[0] == 5); // the origin's not in the kept bounds
	CHECK(c.point[0] == 7 && c.point[1] == 7 && c.point[2] == 7); // the first collapsed elsewhere
	// A sliver (two corners the same) still has its place: kept.
	std::vector<float> sliver{1, 1, 1, 1, 1, 1, 2, 1, 1};
	Report s;
	CHECK(!Clean(sliver.data(), 1, out, s) && s.collapsed == 0);

	// The trace's view: the camera, the far plane's corners, the sun, its
	// parameters, and each light (four float4s): every one finite.
	float view[22 + 16 * 3] = {};
	for (int i = 0; i < 22 + 48; ++i) view[i] = float(i) * .5f - 3;
	CHECK(BadViewField(view, view + 22, 3) == -1);
	view[0] = nan; // the camera
	CHECK(BadViewField(view, view + 22, 3) == 0);
	view[0] = 1;
	view[22 + 16 * 2 + 5] = inf; // the third light's beam
	CHECK(BadViewField(view, view + 22, 3) == 22 + 16 * 2 + 5);
	CHECK(BadViewField(view, view + 22, 2) == -1); // past the lights it has: not looked at

	if (failures)
		return 1;
	std::puts("rtgeometry: all ray-traced geometry tests passed");
}

// Unit tests for the minimap projection: make test
#include "projection.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

static bool Near(float a, float b, float eps = 0.01f) { return fabsf(a - b) <= eps; }

static Projector Make(float tilt = 50.0f)
{
	ViewParams p;
	p.tiltDeg = tilt;
	Projector proj;
	proj.Setup(p, 100.0f, 500.0f, 310.0f, 200.0f); // 1.55:1 rect
	return proj;
}

static void TestPlayerAnchor()
{
	Projector proj = Make();
	Vec2 px;
	CHECK(proj.GroundToScreen({0, 0}, px));
	CHECK(Near(px.x, 100.0f + 155.0f));
	CHECK(Near(px.y, 500.0f + 100.0f + 0.45f * 100.0f)); // below the centre
}

static void TestCornersMapToRect()
{
	Projector proj = Make();
	const Vec2 expected[4] = {{100, 700}, {410, 700}, {410, 500}, {100, 500}};
	for (int i = 0; i < 4; i++) {
		Vec2 px;
		CHECK(proj.GroundToScreen(proj.Corners()[i], px));
		CHECK(Near(px.x, expected[i].x, 0.05f) && Near(px.y, expected[i].y, 0.05f));
	}
}

static void TestRoundTrip()
{
	Projector proj = Make();
	for (float nx = -1.5f; nx <= 1.5f; nx += 0.25f)
		for (float ny = -1.0f; ny <= 1.0f; ny += 0.25f) {
			Vec2 g = proj.NormToGround({nx, ny});
			Vec2 px;
			CHECK(proj.GroundToScreen(g, px));
			CHECK(Near(px.x, 255.0f + nx * 100.0f, 0.05f));
			CHECK(Near(px.y, 600.0f - ny * 100.0f, 0.05f));
		}
}

static void TestPerspectiveShrinksDistance()
{
	// The same ground step covers fewer pixels far ahead than near the player.
	Projector proj = Make();
	Vec2 a, b, c, d;
	proj.GroundToScreen({0, 0}, a);
	proj.GroundToScreen({0.5f, 0}, b);
	proj.GroundToScreen({0, 1.5f}, c);
	proj.GroundToScreen({0.5f, 1.5f}, d);
	CHECK(fabsf(d.x - c.x) < fabsf(b.x - a.x));
	// More ground is visible ahead than behind.
	CHECK(proj.Corners()[2].y > -2.0f * proj.Corners()[0].y);
}

static void TestFlatIsOrthographic()
{
	Projector proj = Make(0.0f);
	Vec2 a, b;
	proj.GroundToScreen({0, 0}, a);
	proj.GroundToScreen({1, 0}, b);
	CHECK(Near(b.x - a.x, ViewParams{}.zoom * 100.0f)); // zoom * half-height
	CHECK(Near(proj.Corners()[0].x, proj.Corners()[3].x)); // no perspective: straight sides
}

static void TestInsideAndClamp()
{
	Projector proj = Make();
	CHECK(proj.Inside({0, 0}));
	CHECK(!proj.Inside({0, 50}));
	const Vec2 far[] = {{0, 50}, {40, 3}, {-30, -30}, {0, -9}, {5, 60}};
	for (Vec2 g : far) {
		Vec2 c = proj.ClampToEdge(g);
		Vec2 px;
		CHECK(proj.GroundToScreen(c, px));
		bool onEdge = Near(px.x, 100.0f, 0.1f) || Near(px.x, 410.0f, 0.1f) || Near(px.y, 500.0f, 0.1f) ||
		              Near(px.y, 700.0f, 0.1f);
		CHECK(onEdge);
		CHECK(fabsf(c.x * g.y - c.y * g.x) < 1e-3f * (fabsf(g.x) + fabsf(g.y))); // same ray
		CHECK(c.x * g.x + c.y * g.y > 0);
	}
	Vec2 in = proj.ClampToEdge({0.1f, 0.2f});
	CHECK(Near(in.x, 0.1f) && Near(in.y, 0.2f));
}

static void TestClipPolygon()
{
	Projector proj = Make();
	ClipVert out[8];

	ClipVert small[4] = {{{-.1f, -.1f}, {1, 1}}, {{.1f, -.1f}, {2, 1}}, {{.1f, .1f}, {2, 2}}, {{-.1f, .1f}, {1, 2}}};
	CHECK(proj.ClipPolygon(small, 4, out) == 4);

	ClipVert away[4] = {{{20, 20}, {}}, {{21, 20}, {}}, {{21, 21}, {}}, {{20, 21}, {}}};
	CHECK(proj.ClipPolygon(away, 4, out) == 0);

	// A huge square clipped to the view becomes the view itself; payload = 2 * ground.
	ClipVert huge[4] = {{{-50, -50}, {-100, -100}}, {{50, -50}, {100, -100}}, {{50, 50}, {100, 100}},
	                    {{-50, 50}, {-100, 100}}};
	int n = proj.ClipPolygon(huge, 4, out);
	CHECK(n == 4);
	for (int i = 0; i < n; i++) {
		CHECK(!proj.Inside({out[i].g.x * 1.01f, out[i].g.y * 1.01f}) || proj.Inside(out[i].g));
		CHECK(Near(out[i].world.x, 2 * out[i].g.x, 0.02f) && Near(out[i].world.y, 2 * out[i].g.y, 0.02f));
	}
}

static void TestClipSegment()
{
	Projector proj = Make();
	Vec2 a{0, 0}, b{0, 100};
	CHECK(proj.ClipSegment(a, b));
	CHECK(Near(a.y, 0) && Near(b.y, proj.Corners()[2].y, 0.01f));
	Vec2 c{30, 30}, d{40, 30};
	CHECK(!proj.ClipSegment(c, d));
}

static void TestHeight()
{
	Projector proj = Make();
	Vec2 ground, raised, flat;
	CHECK(proj.Project({0.3f, 0.8f}, 0.0f, ground));
	proj.GroundToScreen({0.3f, 0.8f}, flat);
	CHECK(Near(ground.x, flat.x) && Near(ground.y, flat.y));
	CHECK(proj.Project({0.3f, 0.8f}, 0.4f, raised));
	CHECK(raised.y < ground.y - 5.0f); // a roof sits higher on screen than its footprint
	// Raised points come closer to the camera, and the camera itself has depth 0.
	CHECK(proj.Depth({0.3f, 0.8f}, 0.4f) < proj.Depth({0.3f, 0.8f}, 0.0f));
	CHECK(Near(proj.Depth(proj.CameraGround(), proj.CameraHeight()), 0.0f, 1e-4f));
	CHECK(proj.Depth({0, 2}, 0) > proj.Depth({0, 0}, 0));
}

int main()
{
	TestHeight();
	TestPlayerAnchor();
	TestCornersMapToRect();
	TestRoundTrip();
	TestPerspectiveShrinksDistance();
	TestFlatIsOrthographic();
	TestInsideAndClamp();
	TestClipPolygon();
	TestClipSegment();

	Projector proj = Make();
	const Vec2 *c = proj.Corners();
	printf("default view: %.2f ranges ahead, %.2f behind, half-width %.2f near / %.2f far, max %.2f\n", c[2].y,
	       -c[0].y, c[1].x, c[2].x, proj.MaxVisibleDistance());
	if (gFailures) {
		printf("%d check(s) failed\n", gFailures);
		return EXIT_FAILURE;
	}
	printf("all projection tests passed\n");
	return EXIT_SUCCESS;
}

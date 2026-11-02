// Unit tests for seeing over a car's hood from the driver's seat: make test
#include "seatview.h"

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

using seatview::Triangle;

// A closed box's twelve triangles.
static void Box(std::vector<Triangle> &body, Vec3 lo, Vec3 hi)
{
	auto p = [&](int i) { return Vec3{i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z}; };
	const int quads[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
	for (const auto &q : quads) {
		body.push_back({p(q[0]), p(q[1]), p(q[2])});
		body.push_back({p(q[0]), p(q[2]), p(q[3])});
	}
}

constexpr Vec3 kEye{-0.4f, 0.0f, 1.0f};
constexpr float kDeg = 3.14159265f / 180.0f;

// A car (x right, y ahead, z up): its hood from 0.8 to 2.4 m ahead of his
// eyes, its top `hood` m under them; the roof over him `roof` m over them,
// its front edge 0.5 m ahead (the windscreen between, glass: not there).
static std::vector<Triangle> Car(float hood, float roof)
{
	std::vector<Triangle> body;
	Box(body, {-0.9f, 0.8f, 0.3f}, {0.9f, 2.4f, kEye.z - hood});
	Box(body, {-0.9f, -1.2f, kEye.z + roof}, {0.9f, 0.5f, kEye.z + roof + 0.05f});
	return body;
}

// The window ahead: down to the hood's front edge, up to the roof's.
static void TestTheWindowAhead()
{
	const seatview::Window w = seatview::Ahead(Car(0.1f, 0.3f), kEye, 0.0f);
	const float hoodLine = std::atan2(-0.1f, 2.4f) / kDeg, roofLine = std::atan2(0.3f, 0.5f) / kDeg;
	CHECK(std::fabs(w.low - hoodLine) < 1.0f);
	CHECK(w.high > roofLine - 4.0f && w.high < roofLine + 1.0f); // (its edge comes lower to the side)
}

// The player's report: in a lot of cars he was staring at the hood. Raised,
// it's under him enough to see the road over it.
static void TestRaisedOverTheHood()
{
	const std::vector<Triangle> car = Car(0.08f, 0.4f);
	CHECK(seatview::Ahead(car, kEye, 0.0f).low > -seatview::kBelow);
	const float lift = seatview::Lift(car, kEye);
	CHECK(lift > 0.05f && lift <= seatview::kHighest);
	CHECK(seatview::Ahead(car, kEye, lift).low <= -seatview::kBelow + 0.3f);
	// And no more than it takes.
	CHECK(seatview::Ahead(car, kEye, lift - 0.02f).low > -seatview::kBelow);
}

// A hood up over his eyes (he couldn't see the road at all): raised too.
static void TestAHoodOverHisEyes()
{
	const std::vector<Triangle> car = Car(-0.02f, 0.45f);
	CHECK(seatview::Lift(car, kEye) > 0.1f);
}

// One he sees over already stays as it is.
static void TestClearAlready() { CHECK(seatview::Lift(Car(0.4f, 0.3f), kEye) == 0.0f); }

// Never into the roof: he keeps his headroom, however much the hood wants.
static void TestTheRoofStopsIt()
{
	const std::vector<Triangle> car = Car(0.05f, 0.12f);
	const float lift = seatview::Lift(car, kEye);
	CHECK(lift <= 0.12f - seatview::kHeadroom + 1e-3f);
}

// Nor so high the roof's front edge comes down over the road ahead.
static void TestTheRoofsEdgeStaysUp()
{
	std::vector<Triangle> car;
	Box(car, {-0.9f, 0.8f, 0.3f}, {0.9f, 2.4f, kEye.z - 0.02f});      // a hood at his eyes
	Box(car, {-0.9f, 0.1f, kEye.z + 0.25f}, {0.9f, 1.4f, kEye.z + 0.3f}); // a roof running far ahead, low
	const float lift = seatview::Lift(car, kEye);
	CHECK(seatview::Ahead(car, kEye, lift).high >= seatview::kAbove - 0.3f);
}

// Nothing there (a car the body wasn't found in): left be.
static void TestNoBody() { CHECK(seatview::Lift({}, kEye) == 0.0f); }

// A convertible: only the windscreen's frame over the hood. Never raised over
// its top, where the frame would run across the road ahead.
static void TestNotOverTheFrame()
{
	std::vector<Triangle> car;
	Box(car, {-0.9f, 0.8f, 0.3f}, {0.9f, 2.4f, kEye.z - 0.03f});              // a hood just under his eyes
	Box(car, {-0.9f, 0.55f, kEye.z + 0.22f}, {0.9f, 0.6f, kEye.z + 0.27f});    // the frame's top
	const float lift = seatview::Lift(car, kEye);
	const seatview::Window w = seatview::Ahead(car, kEye, lift);
	CHECK(w.low < 0.0f && w.high > seatview::kAbove - 0.3f);
}

int main()
{
	TestTheWindowAhead();
	TestRaisedOverTheHood();
	TestAHoodOverHisEyes();
	TestClearAlready();
	TestTheRoofStopsIt();
	TestTheRoofsEdgeStaysUp();
	TestNoBody();
	TestNotOverTheFrame();
	if (gFailures)
		printf("%d failure(s)\n", gFailures);
	else
		printf("seatview: all tests passed\n");
	return gFailures ? 1 : 0;
}

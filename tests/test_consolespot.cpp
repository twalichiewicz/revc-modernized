// Unit tests for where a gun lies in a car while he drives: make test
#include "consolespot.h"

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

using consolespot::Kind;
using consolespot::Triangle;

// A flat rectangle at height z, facing up (or down: its underside).
static void Slab(std::vector<Triangle> &body, float x0, float x1, float y0, float y1, float z, bool up = true)
{
	const Vec3 a{x0, y0, z}, b{x1, y0, z}, c{x1, y1, z}, d{x0, y1, z};
	if (up) {
		body.push_back({a, b, c});
		body.push_back({a, c, d});
	} else {
		body.push_back({a, c, b});
		body.push_back({a, d, c});
	}
}

static float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

constexpr Vec3 kSeat{-0.46f, 0.0f, -0.15f};
constexpr Vec3 kAlong{0.34202f, 0.93969f, 0.0f}; // 20 degrees in from straight ahead

// A car: the floor, both seats' cushions, and maybe a console between them.
static std::vector<Triangle> Car(bool console, float consoleTop = -0.12f)
{
	std::vector<Triangle> body;
	Slab(body, -0.8f, 0.8f, -1.0f, 1.2f, -0.45f);          // the floor
	Slab(body, -0.72f, -0.22f, -0.3f, 0.3f, kSeat.z - 0.05f); // his seat
	Slab(body, 0.22f, 0.72f, -0.3f, 0.3f, kSeat.z - 0.05f);   // the passenger's
	if (console)
		Slab(body, -0.12f, 0.12f, -0.3f, 0.6f, consoleTop);
	return body;
}

static void TestDownFindsTheHighestFacingUp()
{
	std::vector<Triangle> body;
	Slab(body, -1, 1, -1, 1, 0.0f);
	Slab(body, -1, 1, -1, 1, 0.5f);
	Slab(body, -1, 1, -1, 1, 0.3f, false); // facing down: not one to rest on
	float z;
	CHECK(consolespot::Down(body, 0, 0, 1.0f, 2.0f, z) && std::fabs(z - 0.5f) < 1e-5f);
	CHECK(consolespot::Down(body, 0, 0, 0.4f, 2.0f, z) && std::fabs(z) < 1e-5f); // from under the top
	CHECK(!consolespot::Down(body, 0, 0, 0.4f, 0.2f, z));                       // not that deep
	CHECK(!consolespot::Down(body, 2, 0, 1.0f, 2.0f, z));                       // off its edge
}

static void TestOnTheConsole()
{
	const consolespot::Spot s = consolespot::Find(Car(true), kSeat, kAlong, 0.3f);
	CHECK(s.kind == Kind::kConsole && Dot(s.along, kAlong) > 0.9999f);
	CHECK(std::fabs(s.at.x) < 1e-5f && std::fabs(s.at.z + 0.12f) < 1e-4f);
	CHECK(s.at.y > kSeat.y - 0.01f && s.at.y < kSeat.y + 0.35f); // beside him, a little ahead
}

// No console (a bench, or bucket seats with the floor between): his seat's inner edge.
static void TestOnHisSeatWithoutAConsole()
{
	const consolespot::Spot s = consolespot::Find(Car(false), kSeat, kAlong, 0.3f);
	CHECK(s.kind == Kind::kSeat && s.along.y > 0.9999f); // by his thigh, straight ahead
	CHECK(std::fabs(s.at.x - (kSeat.x + 0.18f)) < 1e-4f && std::fabs(s.at.z - (kSeat.z - 0.05f)) < 1e-4f);
}

// A console too low to reach (the floor's tunnel) or too high (the dash) isn't one.
static void TestConsoleOutOfReach()
{
	CHECK(consolespot::Find(Car(true, -0.4f), kSeat, kAlong, 0.3f).kind == Kind::kSeat);
	CHECK(consolespot::Find(Car(true, 0.3f), kSeat, kAlong, 0.3f).kind == Kind::kSeat);
}

// No room over it (under the dash): not there.
static void TestNeedsRoomOverIt()
{
	std::vector<Triangle> body = Car(true);
	Slab(body, -0.3f, 0.3f, -0.5f, 0.8f, -0.06f, false); // a shelf's underside 6 cm over the console
	CHECK(consolespot::Find(body, kSeat, kAlong, 0.3f).kind != Kind::kConsole);
}

// A narrow ridge the gun would balance on isn't flat for its length.
static void TestNeedsToBeFlat()
{
	std::vector<Triangle> body;
	Slab(body, -0.8f, 0.8f, -1.0f, 1.2f, -0.45f);
	Slab(body, -0.72f, -0.22f, -0.3f, 0.3f, kSeat.z - 0.05f);
	Slab(body, -0.12f, 0.12f, 0.05f, 0.15f, -0.1f); // 10 cm long
	CHECK(consolespot::Find(body, kSeat, kAlong, 0.3f).kind == Kind::kSeat);
}

// The passenger's side mirrors it.
static void TestEitherSide()
{
	const Vec3 seat{0.46f, 0.0f, -0.15f};
	const consolespot::Spot s = consolespot::Find(Car(false), seat, kAlong, 0.3f);
	CHECK(s.kind == Kind::kSeat && std::fabs(s.at.x - 0.28f) < 1e-4f);
}

// Nothing at all: a guess beside him, at his seat's height.
static void TestGuessWithNothing()
{
	const consolespot::Spot s = consolespot::Find({}, kSeat, kAlong, 0.3f);
	CHECK(s.kind == Kind::kGuess && std::fabs(s.at.z - (kSeat.z - 0.05f)) < 1e-4f);
}

// Laid there, it points along the way given, on its side, its lowest point
// just on the surface and its middle over the spot.
static void TestLaysItFlatOnTheSpot()
{
	const consolespot::Spot spot{{0.0f, 0.1f, -0.12f}, kAlong, Kind::kConsole};
	const Vec3 lo{-0.1f, -0.03f, -0.17f}, hi{0.38f, 0.06f, 0.15f}; // an Uzi's box
	const consolespot::Pose p = consolespot::Lay(spot, lo, hi);
	CHECK(Dot(p.x, kAlong) > 0.9999f && p.y.z > 0.9999f);
	CHECK(std::fabs(Dot(p.x, p.z)) < 1e-5f && std::fabs(Dot(p.y, p.z)) < 1e-5f);
	auto world = [&](Vec3 m) {
		return Vec3{p.origin.x + p.x.x * m.x + p.y.x * m.y + p.z.x * m.z, p.origin.y + p.x.y * m.x + p.y.y * m.y + p.z.y * m.z,
		            p.origin.z + p.x.z * m.x + p.y.z * m.y + p.z.z * m.z};
	};
	float lowest = 1e9f;
	for (int i = 0; i < 8; i++)
		lowest = std::fmin(lowest, world({i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z}).z);
	CHECK(lowest >= spot.at.z && lowest < spot.at.z + 0.01f);
	const Vec3 mid = world({(lo.x + hi.x) / 2, lo.y, (lo.z + hi.z) / 2});
	CHECK(std::fabs(mid.x - spot.at.x) < 1e-4f && std::fabs(mid.y - spot.at.y) < 1e-4f);
}

int main()
{
	TestDownFindsTheHighestFacingUp();
	TestOnTheConsole();
	TestOnHisSeatWithoutAConsole();
	TestConsoleOutOfReach();
	TestNeedsRoomOverIt();
	TestNeedsToBeFlat();
	TestEitherSide();
	TestGuessWithNothing();
	TestLaysItFlatOnTheSpot();
	if (gFailures)
		return 1;
	printf("consolespot: all passed\n");
	return 0;
}

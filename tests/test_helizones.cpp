// Unit tests for where a shot meets the police helicopter: make test
#include "helizones.h"

#include <cmath>
#include <cstdio>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

using helizones::Part;

static Vec3 Towards(Vec3 from, Vec3 to)
{
	const Vec3 d{to.x - from.x, to.y - from.y, to.z - from.z};
	const float l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
	return {d.x / l, d.y / l, d.z / l};
}

static helizones::Parts All()
{
	helizones::Parts p;
	const auto lamp=helizones::Aim({0.0f,10.0f,-35.0f});
	p.lamp=lamp.middle;p.lampForward=lamp.forward;
	p.havePilot = true;
	return p;
}

// A shot at something from below and in front (where he stands on the ground).
static helizones::Hit Shoot(Vec3 at, const helizones::Parts &parts, Vec3 from = {6.0f, 25.0f, -30.0f})
{
	return helizones::Trace(from, Towards(from, at), 100.0f, parts);
}

static void TestBodyAsBefore()
{
	const helizones::Hit h = Shoot({0.0f, -0.5f, 0.35f}, All(), {20.0f, -0.5f, 0.35f}); // the cabin's side, level
	CHECK(h.part == Part::kBody && std::fabs(h.at.x - 1.15f) < 0.01f && h.normal.x > 0.99f);
	CHECK(Shoot({0.0f, -5.0f, 1.2f}, All(), {20.0f, -5.0f, 1.2f}).part == Part::kBody); // the tail boom
	CHECK(Shoot({30.0f, 0.0f, 0.0f}, All(), {20.0f, 0.0f, -30.0f}).part == Part::kNone); // a miss
}

static void TestTheLamp()
{
	const helizones::Parts parts = All();
	const helizones::Hit h = Shoot(parts.lamp, parts);
	CHECK(h.part == Part::kLamp);
	helizones::Parts broken = parts;
	broken.haveLamp = false; // shot away: the body behind it
	CHECK(Shoot(parts.lamp, broken).part == Part::kBody);
}

// The drawn barrel is longer and narrower than the former 0.156 m sphere.
// Side-on rays at its bezel reach the can; empty air beside it remains empty.
static void TestVisibleCan()
{
	helizones::Parts parts;const auto lamp=helizones::Aim({-.35f,2.25f,-40});
	parts.lamp=lamp.middle;parts.lampForward=lamp.forward;
	const auto edge=helizones::Trace({20,2.25f,-.9032f},{-1,0,0},25,parts);
	CHECK(edge.part==Part::kLamp && edge.at.x<-.21f && edge.at.x>-.24f);
	CHECK(helizones::Trace({-.35f,2.39f,-30},{0,0,1},40,parts).part!=Part::kLamp);
	// A nearer solid cabin still occludes the lamp from the opposite side.
	CHECK(helizones::Trace({-.35f,2.25f,20},{0,0,-1},30,parts).part==Part::kBody);
}

// The lamp looks at its spot, but never up into the body.
static void TestLampAims()
{
	const helizones::Lamp down = helizones::Aim({-0.35f, 2.25f, -40.0f});
	CHECK(down.forward.z < -0.999f);
	const helizones::Lamp ahead = helizones::Aim({-0.35f, 200.0f, -5.0f});
	CHECK(ahead.forward.y > 0.95f && ahead.forward.z < -0.1f);
	const helizones::Lamp up = helizones::Aim({0.0f, 10.0f, 30.0f});
	CHECK(up.forward.z < 0.0f && up.forward.y > 0.9f);
	CHECK(down.middle.z < helizones::kLampPivot.z); // it hangs from its mount
}

static void TestTheMast()
{
	const helizones::Hit h = Shoot({0.0f, 0.0f, 2.0f}, All(), {30.0f, 0.0f, 2.0f});
	CHECK(h.part == Part::kMast && std::fabs(h.at.x - helizones::kMastRadius) < 0.01f);
}

static void TestTheTailRotor()
{
	const Vec3 left{-20.0f, -6.76f, 1.1f};
	CHECK(Shoot({-0.15f, -6.76f, 1.1f}, All(), left).part == Part::kTailRotor);        // its hub, from its side
	CHECK(Shoot({-0.15f, -7.3f, 0.6f}, All(), {-20.0f, -7.3f, 0.6f}).part == Part::kTailRotor); // a blade's sweep
	helizones::Parts gone = All();
	gone.haveTailRotor = false;
	CHECK(Shoot({-0.15f, -6.76f, 1.1f}, gone, left).part == Part::kBody);
}

// Through the windscreen at him: the pilot. Through the cabin's side behind the glass: the body.
static void TestThePilot()
{
	const helizones::Parts parts = All();
	const helizones::Hit h = Shoot(helizones::kPilotChest, parts, {0.42f, 30.0f, 0.4f});
	CHECK(h.part == Part::kPilot);
	helizones::Parts empty = parts;
	empty.havePilot = false;
	CHECK(Shoot(helizones::kPilotChest, empty, {0.42f, 30.0f, 0.4f}).part == Part::kBody);
	// From behind, through the cabin and its back wall: not the glass, so not him.
	CHECK(Shoot(helizones::kPilotChest, parts, {0.42f, -1.0f, 30.0f}).part == Part::kBody);
	// Through the glass but wide of him.
	CHECK(Shoot({-0.5f, 1.5f, 0.35f}, parts, {-0.5f, 30.0f, 0.35f}).part == Part::kBody);
}

// A gunner's shot out of his door (from inside the cabin) hits none of it.
static void TestFromInside()
{
	CHECK(helizones::Trace({0.6f, 0.3f, 0.4f}, Towards({0.6f, 0.3f, 0.4f}, {0.0f, 10.0f, -35.0f}), 100.0f, All()).part ==
	      Part::kNone);
}

int main()
{
	TestBodyAsBefore();
	TestTheLamp();
	TestVisibleCan();
	TestLampAims();
	TestTheMast();
	TestTheTailRotor();
	TestThePilot();
	TestFromInside();
	if (gFailures)
		return 1;
	printf("helizones: all passed\n");
	return 0;
}

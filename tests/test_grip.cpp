// Unit tests for a cop hanging on to Tommy's car door: make test
#include "grip.h"

#include <cstdio>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

using grip::AtDoor;
using grip::Now;

// How long he hangs on at this speed and turn (s), at 60 frames a second; how it ends.
static float HangsOn(float speed, float turn, Now &end)
{
	grip::Hold h;
	float t = 0.0f;
	for (int i = 0; i < 60 * 20; i++) {
		end = h.Update(speed, turn, 1.0f / 60.0f);
		t += 1.0f / 60.0f;
		if (end != Now::kHolding)
			return t;
	}
	return t;
}

// Stopped or barely rolling the door opens, as it did; moving, he can only
// catch hold of it; too fast, not even that. (The game opened it up to 10 m/s.)
static void TestAtTheDoor()
{
	CHECK(grip::Reach(0.0f) == AtDoor::kOpen);
	CHECK(grip::Reach(1.0f) == AtDoor::kOpen);
	CHECK(grip::Reach(3.0f) == AtDoor::kGrab);
	CHECK(grip::Reach(9.9f) == AtDoor::kGrab); // where the game would have pulled him out
	CHECK(grip::Reach(14.0f) == AtDoor::kMiss);
}

static void TestLetsGoSoonerTheFaster()
{
	Now end;
	const float jog = HangsOn(5.0f, 0.0f, end);
	CHECK(end == Now::kLetsGo && jog > 1.2f && jog < 2.5f);
	const float fast = HangsOn(10.0f, 0.0f, end);
	CHECK(end == Now::kLetsGo && fast > 0.3f && fast < 0.8f && fast < jog);
}

static void TestTurningShakesHimOff()
{
	Now end;
	const float straight = HangsOn(6.0f, 0.0f, end), turning = HangsOn(6.0f, 1.5f, end);
	CHECK(end == Now::kLetsGo && turning < straight * 0.5f);
}

// Hanging on as the car slows to a crawl: then he opens it.
static void TestOpensWhenItSlows()
{
	grip::Hold h;
	for (int i = 0; i < 30; i++)
		CHECK(h.Update(4.0f, 0.0f, 1.0f / 60.0f) == Now::kHolding);
	Now now = Now::kHolding;
	int frames = 0;
	while (now == Now::kHolding && frames < 600) {
		now = h.Update(0.5f, 0.0f, 1.0f / 60.0f);
		frames++;
	}
	CHECK(now == Now::kOpens && frames >= int(grip::kOpensAfter * 60.0f) - 1 && frames <= int(grip::kOpensAfter * 60.0f) + 2);
	CHECK(h.Strain() < 0.4f); // and his grip came back a little meanwhile
}

// Quick on the gas again before he's opened it: he's back to hanging on.
static void TestSpeedingUpAgain()
{
	grip::Hold h;
	for (int i = 0; i < 10; i++)
		h.Update(0.5f, 0.0f, 1.0f / 60.0f);
	CHECK(h.Update(5.0f, 0.0f, 1.0f / 60.0f) == Now::kHolding);
	for (int i = 0; i < 15; i++)
		CHECK(h.Update(0.5f, 0.0f, 1.0f / 60.0f) == Now::kHolding); // the crawl starts over
}

int main()
{
	TestAtTheDoor();
	TestLetsGoSoonerTheFaster();
	TestTurningShakesHimOff();
	TestOpensWhenItSlows();
	TestSpeedingUpAgain();
	if (gFailures)
		return 1;
	printf("grip: all passed\n");
	return 0;
}

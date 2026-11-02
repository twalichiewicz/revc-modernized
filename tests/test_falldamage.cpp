// Unit tests for how much a landing hurts: make test
#include "falldamage.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

using falldamage::Damage;
using falldamage::Games;

// How fast a ped lands (m/s) from a drop of `metres`, as the game moves him:
// a frame at a time (50 a second), 0.008 a frame of gravity, and
// CPhysical::ApplyAirResistance's drag (a ped's air resistance, 0.4 / 70).
static float Landing(float metres)
{
	float z = 0.0f, v = 0.0f;
	while (z < metres) {
		v += 0.008f;
		v /= 1.0f + 0.5f * (0.4f / 70.0f) * v * v;
		z += v;
	}
	return v * 50.0f;
}

static float From(float metres, bool soft = false) { return Damage(Landing(metres), 0.0f, soft); }

// A storey's drop, or a hop off a wall, is nothing (the game's own line: it
// deals nothing under 12.5 m/s).
static void TestASmallDropIsNothing()
{
	CHECK(From(2.0f) == 0.0f);
	CHECK(From(3.0f) == 0.0f);
	CHECK(From(4.0f) < 5.0f);
	CHECK(Damage(0.0f, 7.0f, false) == 0.0f); // landing running
}

// The player's report: off the fourth storey and barely hurt. The game took
// a third of his health for it (and his armour took that).
static void TestTheFourthStorey()
{
	CHECK(Games(Landing(12.0f), 0.0f, false) < 35.0f); // as it was
	CHECK(From(12.0f) >= 70.0f && From(12.0f) < 100.0f); // a fourth-floor window: he only just lives
	CHECK(From(14.0f) >= 100.0f);                         // its roof: a full health's worth
}

// Two storeys hurt.
static void TestTwoStoreys() { CHECK(From(6.0f) >= 15.0f && From(6.0f) < 40.0f); }

// Five storeys and up nobody lives through, whatever his health.
static void TestFiveStoreysKill()
{
	CHECK(From(16.0f) >= falldamage::kDead);
	CHECK(From(30.0f) >= falldamage::kDead); // out of a police helicopter
	CHECK(From(60.0f) >= falldamage::kDead);
}

// Never kinder than the game's, and worse the faster.
static void TestWorseThanTheGameAndRising()
{
	float last = 0.0f;
	for (float m = 1.0f; m <= 60.0f; m += 0.5f) {
		const float v = Landing(m);
		CHECK(Damage(v, 0.0f, false) >= Games(v, 0.0f, false));
		CHECK(Damage(v, 0.0f, true) >= Games(v, 0.0f, true));
		CHECK(Damage(v, 0.0f, false) >= last);
		last = Damage(v, 0.0f, false);
	}
	// Thrown along the ground (a car, an explosion): the game's, at least.
	CHECK(Damage(0.0f, 25.0f, false) >= Games(0.0f, 25.0f, false) && Damage(0.0f, 25.0f, false) > 0.0f);
}

// Sand, grass, a bush: softer, but not from any height.
static void TestSoftGround()
{
	CHECK(From(10.0f, true) < From(10.0f) * 0.75f);
	CHECK(From(10.0f, true) > 0.0f);
	CHECK(From(20.0f, true) >= 100.0f);
}

// The drop the log names, from the speed.
static void TestTheDropNamed()
{
	for (float m : {5.0f, 12.0f, 30.0f})
		CHECK(std::fabs(falldamage::Drop(Landing(m)) - m) < 0.5f);
}

int main()
{
	TestASmallDropIsNothing();
	TestTheFourthStorey();
	TestTwoStoreys();
	TestFiveStoreysKill();
	TestWorseThanTheGameAndRising();
	TestSoftGround();
	TestTheDropNamed();
	if (gFailures)
		printf("%d failure(s)\n", gFailures);
	else
		printf("falldamage: all tests passed\n");
	return gFailures ? 1 : 0;
}

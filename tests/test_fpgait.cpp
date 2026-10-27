// Unit tests for first person's gait decisions (what moves him, when, how fast; who's an enemy): make test
#include "fpgait.h"

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

using fpgait::Mode;

static bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// The keyboard's keys land on the D-pad fields (255 down), as CPad::GetPedWalkLeftRight / UpDown read them.
static void TestPushFromKeys()
{
	const Vec2 ahead = fpgait::Push(0, 0, 255, 0, 0, 0);
	CHECK(Near(ahead.x, 0.0f, 1e-4f) && ahead.y > 0.95f);
	const Vec2 left = fpgait::Push(0, 0, 0, 0, 255, 0);
	CHECK(left.x < -0.95f && Near(left.y, 0.0f, 1e-4f));
	const Vec2 diagonal = fpgait::Push(0, 0, 255, 0, 0, 255);
	CHECK(Near(std::hypot(diagonal.x, diagonal.y), 1.0f, 1e-3f)); // not faster on the diagonal
}

static void TestPushFromStick()
{
	const Vec2 p = fpgait::Push(64, -96, 0, 0, 0, 0); // up is negative on the pad
	CHECK(Near(p.x, 0.5f, 1e-3f) && Near(p.y, 0.75f, 1e-3f));
	const Vec2 full = fpgait::Push(128, -128, 0, 0, 0, 0); // a corner: no longer than a full push
	CHECK(Near(std::hypot(full.x, full.y), 1.0f, 1e-3f));
	const Vec2 both = fpgait::Push(20, 0, 0, 0, 0, 255); // the D-pad's further: it wins, as the game's does
	CHECK(both.x > 0.95f);
}

static fpgait::Situation Walking()
{
	fpgait::Situation s;
	s.state = 1; // PED_IDLE
	s.standing = true;
	s.inTheAir = false;
	s.inControl = true;
	s.controlsDisabled = false;
	s.objective = 0;
	return s;
}

static void TestDrivesHimselfOnTheGround()
{
	fpgait::Situation s = Walking();
	CHECK(fpgait::MoveMode(s) == Mode::kGround);
	s.state = 0x10; // shooting
	CHECK(fpgait::MoveMode(s) == Mode::kGround);
	s.state = 0x16; // aiming
	CHECK(fpgait::MoveMode(s) == Mode::kGround);
	s.state = 0x24; // answering the phone
	CHECK(fpgait::MoveMode(s) == Mode::kGround);
}

static void TestLeavesTheGameItsOwn()
{
	fpgait::Situation seek = Walking();
	seek.state = 0x18; // PED_SEEK_CAR: walking to a car's door
	seek.objective = 2;
	CHECK(fpgait::MoveMode(seek) == Mode::kNone);
	fpgait::Situation scripted = Walking();
	scripted.objective = 25;
	CHECK(fpgait::MoveMode(scripted) == Mode::kNone);
	fpgait::Situation disabled = Walking();
	disabled.controlsDisabled = true;
	CHECK(fpgait::MoveMode(disabled) == Mode::kNone);
	fpgait::Situation fight = Walking();
	fight.state = 0x11; // fists: the stick picks punches
	CHECK(fpgait::MoveMode(fight) == Mode::kNone);
	fpgait::Situation knocked = Walking();
	knocked.state = 0x2a; // PED_FALL
	knocked.inControl = false;
	CHECK(fpgait::MoveMode(knocked) == Mode::kNone);
}

static void TestAirControlInJumpsAndFalls()
{
	fpgait::Situation jump = Walking();
	jump.state = 0x29; // PED_JUMP: not "in control", still his
	jump.inControl = false;
	CHECK(fpgait::MoveMode(jump) == Mode::kAir);
	fpgait::Situation off = Walking();
	off.standing = false;
	off.inTheAir = true;
	off.inControl = false;
	CHECK(fpgait::MoveMode(off) == Mode::kAir);
}

static void TestTopSpeeds()
{
	fpgait::Gait g{};
	CHECK(Near(fpgait::TopSpeed(g), 6.4f, 1e-4f));
	g.twoHanded = true;
	CHECK(Near(fpgait::TopSpeed(g), 5.0f, 1e-4f));
	g.sprinting = true;
	CHECK(Near(fpgait::TopSpeed(g), 9.8f, 1e-4f));
	g.aiming = true; // aiming wins over the sprint
	CHECK(Near(fpgait::TopSpeed(g), 0.52f * 5.0f, 1e-4f));
	g.ducking = true;
	CHECK(Near(fpgait::TopSpeed(g), 1.6f, 1e-4f));
}

static void TestEnemies()
{
	CHECK(fpgait::Enemy(8, true, false, false, false));    // out to kill him on foot
	CHECK(fpgait::Enemy(9, true, false, false, false));    // ... by any means
	CHECK(fpgait::Enemy(0, false, true, false, false));    // his gun on him
	CHECK(!fpgait::Enemy(14, true, false, false, false));  // following him (a mission partner)
	CHECK(!fpgait::Enemy(10, true, false, false, false));  // fleeing from him
	CHECK(!fpgait::Enemy(0, false, false, true, false));   // a cop, not after him
	CHECK(fpgait::Enemy(0, false, false, true, true));     // a cop while he's wanted
}

// In the mod's sniper scope he's in the game's sniper state, and creeps by the
// stick; in that state otherwise (the game's own scope), he doesn't.
static void TestScopedSniperStateCreeps()
{
	fpgait::Situation s = Walking();
	s.state = 0xc; // PED_SNIPER_MODE
	CHECK(fpgait::MoveMode(s) == fpgait::Mode::kNone);
	s.scoped = true;
	CHECK(fpgait::MoveMode(s) == fpgait::Mode::kGround);
}

int main()
{
	TestScopedSniperStateCreeps();
	TestPushFromKeys();
	TestPushFromStick();
	TestDrivesHimselfOnTheGround();
	TestLeavesTheGameItsOwn();
	TestAirControlInJumpsAndFalls();
	TestTopSpeeds();
	TestEnemies();
	if (gFailures) {
		printf("%d gait test(s) failed\n", gFailures);
		return 1;
	}
	printf("all gait tests passed\n");
	return 0;
}

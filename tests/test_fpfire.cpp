// Unit tests for first person's firing rules: the trigger, the view punch, the spread: make test
#include "fpfire.h"

#include <algorithm>
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

using namespace fpfire;

// Shots in `seconds` with RT held throughout, at `fps`.
static int Held(bool automatic, float interval, float seconds, float fps, float timeScale = 1.0f)
{
	Trigger trigger;
	int shots = 0;
	const float dt = 1.0f / fps;
	for (int frame = 0; frame < int(std::lround(seconds * fps)); frame++)
		shots += trigger.Update(true, true, interval, automatic, dt * timeScale);
	return shots;
}

static void TestFirstShotOnThePress()
{
	Trigger trigger;
	CHECK(trigger.Update(false, true, 0.1f, true, 1.0f / 60.0f) == 0);
	CHECK(trigger.Update(true, true, 0.1f, true, 1.0f / 60.0f) == 1); // the frame it's pulled
}

static void TestSemiAutoNeedsARelease()
{
	CHECK(Held(false, 0.2f, 1.0f, 60.0f) == 1);
	Trigger trigger;
	int shots = 0;
	for (int i = 0; i < 60; i++) // pulled and let go every 0.1 s, a 0.2 s cadence: 5 shots a second
		shots += trigger.Update(i % 6 < 3, true, 0.2f, false, 1.0f / 60.0f);
	CHECK(shots == 5);
}

static void TestAutomaticCadence() { CHECK(Held(true, 0.1f, 1.0f, 60.0f) == 10); }

static void TestCadenceAtAnyFrameRate()
{
	CHECK(Held(true, 0.1f, 1.0f, 20.0f) == 10);
	CHECK(Held(true, 0.1f, 1.0f, 144.0f) == 10);
	CHECK(std::abs(Held(true, 0.03f, 1.0f, 20.0f) - 33) <= 1); // a minigun's, faster than the frame rate (frames to t 0.95)
}

static void TestCadenceOnGameTime() { CHECK(Held(true, 0.1f, 1.0f, 60.0f, 0.5f) == 5); }

static void TestNoShotWhileNotReady()
{
	Trigger trigger;
	int shots = 0;
	for (int i = 0; i < 30; i++)
		shots += trigger.Update(true, false, 0.1f, true, 1.0f / 60.0f); // switching, or reloading
	CHECK(shots == 0);
	CHECK(trigger.Update(true, true, 0.1f, true, 1.0f / 60.0f) == 1); // ready, still held: it fires
}

static void TestPunchReturns()
{
	Punch punch;
	punch.Kick(Kind::kPistol, 1, 0.5f);
	CHECK(std::fabs(punch.pitch - 2.0f) < 1e-4f);
	float lowest = punch.pitch;
	for (float t = 0.0f; t < 0.5f; t += 1.0f / 60.0f) {
		punch.Decay(1.0f / 60.0f);
		lowest = std::fmin(lowest, punch.pitch);
	}
	CHECK(punch.pitch < 0.01f && lowest >= 0.0f);
}

static void TestPunchGrowsInABurst()
{
	Punch punch;
	float last = 0.0f;
	for (int shot = 1; shot <= 3; shot++) { // (the fourth reaches the cap)
		const float before = punch.pitch;
		punch.Kick(Kind::kRifle, shot, 0.5f);
		CHECK(punch.pitch - before > last - 1e-5f); // each kick at least as big as the last
		last = punch.pitch - before;
	}
	for (int shot = 5; shot < 30; shot++)
		punch.Kick(Kind::kRifle, shot, 0.5f);
	CHECK(punch.pitch <= 3.5f + 1e-4f && std::fabs(punch.yaw) <= 2.25f + 1e-4f); // the M4A1's caps
}

static void TestFirstShotExact()
{
	Spread spread;
	CHECK(spread.Angle(Kind::kRifle, Moving::kStill, false, false) == 0.0f);
	CHECK(spread.Angle(Kind::kPistol, Moving::kWalking, false, false) == 0.0f);
	CHECK(spread.Angle(Kind::kRifle, Moving::kAir, false, false) > 0.0f);
}

static void TestSpreadGrowsAndRecovers()
{
	Spread spread;
	float last = 0.0f;
	for (int i = 0; i < 6; i++) {
		spread.Shot();
		const float a = spread.Angle(Kind::kRifle, Moving::kStill, false, false);
		CHECK(a >= last);
		last = a;
		spread.Update(0.1f);
	}
	CHECK(last > 0.0f && spread.Burst() == 6);
	spread.Update(0.5f); // a pause
	CHECK(spread.Burst() == 0 && spread.Angle(Kind::kRifle, Moving::kStill, false, false) == 0.0f);
}

static void TestMovingWidens()
{
	Spread spread;
	spread.Shot();
	spread.Shot();
	const float still = spread.Angle(Kind::kSmg, Moving::kStill, false, false);
	CHECK(spread.Angle(Kind::kSmg, Moving::kRunning, false, false) > still);
	CHECK(spread.Angle(Kind::kSmg, Moving::kAir, false, false) > spread.Angle(Kind::kSmg, Moving::kRunning, false, false));
	CHECK(spread.Angle(Kind::kSmg, Moving::kStill, true, false) < still);
	CHECK(spread.Angle(Kind::kSmg, Moving::kStill, false, true) < still);
}

static void TestKinds()
{
	CHECK(KindOf(17) == Kind::kPistol && KindOf(18) == Kind::kPistol);
	CHECK(KindOf(19) == Kind::kShotgun && KindOf(21) == Kind::kShotgun);
	CHECK(KindOf(23) == Kind::kSmg && KindOf(25) == Kind::kSmg && KindOf(26) == Kind::kRifle);
	CHECK(KindOf(32) == Kind::kHeavy && KindOf(33) == Kind::kHeavy);
	CHECK(!Automatic(Kind::kPistol) && !Automatic(Kind::kShotgun) && Automatic(Kind::kSmg) && Automatic(Kind::kHeavy));
}

// Switching guns with RT still held: a pistol or shotgun needs a fresh pull; an automatic carries on.
static void TestSwitchWithTheTriggerHeld()
{
	Trigger semi;
	semi.Reset(true);
	CHECK(semi.Update(true, true, 0.2f, false, 1.0f / 60.0f) == 0);
	semi.Update(false, true, 0.2f, false, 1.0f / 60.0f);
	CHECK(semi.Update(true, true, 0.2f, false, 1.0f / 60.0f) == 1);
	Trigger automatic;
	automatic.Reset(true);
	CHECK(automatic.Update(true, true, 0.1f, true, 1.0f / 60.0f) == 1);
}

// Let go while it can't fire (the dive, third person): the next pull fires.
static void TestReleaseWhileNotReadyRearms()
{
	Trigger trigger;
	CHECK(trigger.Update(true, true, 0.2f, false, 1.0f / 60.0f) == 1);
	for (int i = 0; i < 30; i++)
		trigger.Update(i < 10, false, 0.2f, false, 1.0f / 60.0f); // held, then let go, while not ready
	CHECK(trigger.Update(true, true, 0.2f, false, 1.0f / 60.0f) == 1);
}

// Each shot jolts the camera a little (the force his body takes): a quick
// shake, gone in a tenth of a second or so; more for a shotgun than a pistol.
static void TestJoltShakesAndSettles()
{
	fpfire::Jolt still;
	CHECK(still.yaw == 0.0f && still.pitch == 0.0f && still.roll == 0.0f);
	auto peak = [](fpfire::Kind kind) {
		fpfire::Jolt j;
		j.Kick(kind, 0.3f);
		float most = 0.0f;
		for (int i = 0; i < 6; i++) {
			j.Decay(1.0f / 120.0f);
			most = std::max({most, std::fabs(j.yaw), std::fabs(j.pitch), std::fabs(j.roll)});
		}
		return most;
	};
	const float pistol = peak(fpfire::Kind::kPistol), shotgun = peak(fpfire::Kind::kShotgun);
	CHECK(pistol > 0.2f && pistol < 1.0f && shotgun > pistol && shotgun < 2.0f);
	fpfire::Jolt j;
	j.Kick(fpfire::Kind::kRifle, 0.7f);
	for (int i = 0; i < 30; i++)
		j.Decay(1.0f / 120.0f); // 0.25 s
	CHECK(std::fabs(j.yaw) < 0.03f && std::fabs(j.pitch) < 0.03f && std::fabs(j.roll) < 0.03f);
}

int main()
{
	TestJoltShakesAndSettles();
	TestSwitchWithTheTriggerHeld();
	TestReleaseWhileNotReadyRearms();
	TestFirstShotOnThePress();
	TestSemiAutoNeedsARelease();
	TestAutomaticCadence();
	TestCadenceAtAnyFrameRate();
	TestCadenceOnGameTime();
	TestNoShotWhileNotReady();
	TestPunchReturns();
	TestPunchGrowsInABurst();
	TestFirstShotExact();
	TestSpreadGrowsAndRecovers();
	TestMovingWidens();
	TestKinds();
	if (gFailures) {
		printf("%d fpfire test(s) failed\n", gFailures);
		return 1;
	}
	printf("all fpfire tests passed\n");
	return 0;
}

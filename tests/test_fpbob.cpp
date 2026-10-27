// Unit tests for first person's bob (Counter-Strike's V_CalcBob): make test
#include "fpbob.h"

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

static const float kRun = 6.4f;

static void TestStillIsFlat()
{
	fpbob::State s;
	for (int i = 0; i < 100; i++)
		CHECK(fpbob::Bob(s, 0.0f, 1.0f / 60.0f, true) == 0.0f);
}

static void TestRunRange()
{
	fpbob::State s;
	float lo = 1.0f, hi = -1.0f;
	for (int i = 0; i < 96; i++) { // one 0.8 s cycle at 120 Hz
		const float b = fpbob::Bob(s, kRun, 1.0f / 120.0f, true);
		lo = std::min(lo, b);
		hi = std::max(hi, b);
	}
	CHECK(std::fabs(lo - (-0.0256f)) <= 2e-3f);
	CHECK(std::fabs(hi - 0.064f) <= 2e-3f);
}

static void TestPeriod()
{
	fpbob::State s;
	float at[200];
	for (int i = 0; i < 200; i++)
		at[i] = fpbob::Bob(s, kRun, 0.01f, true);
	for (int i = 0; i + 80 < 200; i++) // 80 steps of 0.01 s: one cycle
		CHECK(std::fabs(at[i] - at[i + 80]) <= 1e-3f);
}

static void TestFrozenInAir()
{
	fpbob::State s;
	float last = 0.0f;
	for (int i = 0; i < 18; i++) // 0.3 s
		last = fpbob::Bob(s, kRun, 1.0f / 60.0f, true);
	for (int i = 0; i < 3; i++)
		CHECK(fpbob::Bob(s, kRun, 1.0f / 60.0f, false) == last);
}

// The eye's height over steps: eased on the ground, never far behind, exact in the air.
static void TestStepsEaseOnTheGround()
{
	fpbob::Steps steps;
	CHECK(steps.Height(10.0f, 1.0f / 60.0f, true) == 10.0f); // the first frame: where he is
	const float z = steps.Height(10.3f, 1.0f / 60.0f, true);
	CHECK(z > 10.0f && z < 10.3f);
	float last = z;
	for (int i = 0; i < 30; i++)
		last = steps.Height(10.3f, 1.0f / 60.0f, true);
	CHECK(std::fabs(last - 10.3f) < 0.01f);
}

static void TestStepsNeverFarBehind()
{
	fpbob::Steps steps;
	steps.Height(0.0f, 1.0f / 60.0f, true);
	const float z = steps.Height(-2.0f, 1.0f / 60.0f, true);
	CHECK(z >= -2.0f && z <= -2.0f + 0.45f + 1e-4f);
}

static void TestStepsExactInTheAir()
{
	fpbob::Steps steps;
	steps.Height(0.0f, 1.0f / 60.0f, true);
	CHECK(steps.Height(1.5f, 1.0f / 60.0f, false) == 1.5f);
}

// The view's largest turn over a stride (two steps) at a speed, straight ahead.
static fpbob::Turn Most(float speed)
{
	fpbob::Turn most{};
	for (int i = 0; i < 200; i++) {
		const fpbob::Turn t = fpbob::Sway(i / 100.0f, speed, 0.0f);
		most = {std::max(most.pitch, std::fabs(t.pitch)), std::max(most.roll, std::fabs(t.roll)), std::max(most.yaw, std::fabs(t.yaw))};
	}
	return most;
}

// Standing still, the view doesn't move.
static void TestSwayStillIsZero()
{
	const fpbob::Turn still = Most(0.0f);
	CHECK(still.pitch == 0.0f && still.roll == 0.0f && still.yaw == 0.0f);
}

// Walking it barely moves; running, more; sprinting most (under a degree).
static void TestSwayGrowsWithSpeed()
{
	const fpbob::Turn walk = Most(1.8f), run = Most(4.5f), sprint = Most(7.0f);
	CHECK(walk.pitch > 0.0f && walk.pitch < 0.08f && walk.roll < 0.08f);
	CHECK(run.pitch > 2.0f * walk.pitch && run.roll > 2.0f * walk.roll && run.yaw > 2.0f * walk.yaw);
	CHECK(sprint.pitch > 1.5f * run.pitch && sprint.roll > 1.5f * run.roll);
	CHECK(sprint.pitch < 0.5f && sprint.roll < 0.5f && sprint.yaw < 0.5f); // a third of what read as too much
}

// Strafing rolls the view over that way, as Half-Life's (its cl_rollangle 2
// at cl_rollspeed 200 units, 5.08 m/s) cut to a third with the rest, and no more.
static void TestStrafeRolls()
{
	CHECK(std::fabs(fpbob::Sway(0.0f, 0.0f, 5.08f).roll - 0.667f) < 1e-3f);
	CHECK(std::fabs(fpbob::Sway(0.0f, 0.0f, -2.54f).roll + 0.333f) < 1e-3f);
	CHECK(std::fabs(fpbob::Sway(0.0f, 0.0f, 9.0f).roll - 0.667f) < 1e-3f);
}

// Looking down, the eye goes forward past his chest (and a little down), so
// he sees his body and legs, not his neck; none looking ahead or up.
static void TestLookDownPush()
{
	const float deg = 3.14159265f / 180.0f;
	for (float pitch : {30.0f, 0.0f, -15.0f}) {
		const fpbob::Push p = fpbob::LookDown(std::sin(pitch * deg));
		CHECK(p.ahead == 0.0f && p.down == 0.0f);
	}
	const fpbob::Push most = fpbob::LookDown(-1.0f);
	CHECK(std::fabs(most.ahead - 0.16f) < 1e-4f && std::fabs(most.down - 0.05f) < 1e-4f);
	float last = 0.0f;
	for (float pitch = -20.0f; pitch >= -90.0f; pitch -= 5.0f) {
		const float ahead = fpbob::LookDown(std::sin(pitch * deg)).ahead;
		CHECK(ahead >= last);
		last = ahead;
	}
}

// His steps' phase is his legs': the walk, run or sprint animation blended in
// most, its place through its loop (a stride, two steps).
static void TestStridePhaseFromTheAnimation()
{
	float phase = -1.0f;
	const fpbob::Stride none[3] = {{0, 0.2f, 1.0f}, {0, 0.1f, 0.6f}, {0, 0, 0.5f}};
	CHECK(!fpbob::StridePhase(none, 3, phase));
	const fpbob::Stride running[3] = {{0.2f, 0.9f, 1.0f}, {0.8f, 0.3f, 0.6f}, {0, 0, 0.5f}};
	CHECK(fpbob::StridePhase(running, 3, phase) && std::fabs(phase - 1.0f) < 1e-5f);
	const fpbob::Stride broken[1] = {{1.0f, 0.3f, 0.0f}}; // no length: none
	CHECK(!fpbob::StridePhase(broken, 1, phase));
}

int main()
{
	TestStridePhaseFromTheAnimation();
	TestLookDownPush();
	TestSwayStillIsZero();
	TestSwayGrowsWithSpeed();
	TestStrafeRolls();
	TestStepsEaseOnTheGround();
	TestStepsNeverFarBehind();
	TestStepsExactInTheAir();
	TestStillIsFlat();
	TestRunRange();
	TestPeriod();
	TestFrozenInAir();
	if (gFailures) {
		printf("%d bob test(s) failed\n", gFailures);
		return 1;
	}
	printf("all bob tests passed\n");
	return 0;
}

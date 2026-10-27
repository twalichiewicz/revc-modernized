// Unit tests for first-person movement (GoldSrc's friction and acceleration): make test
#include "fpmove.h"

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

static const fpmove::Tuning kCS;
static const float kRun = 6.4f;

static float Speed(Vec2 v) { return std::hypot(v.x, v.y); }

// Steps Ground at `hz` from `v`, wishing along `dir` at `wish`, until `done` holds or `limit` s
// pass; returns the time it first held (or -1).
template <typename Done> static float Run(Vec2 v, Vec2 dir, float wish, float hz, float limit, Done done)
{
	const float dt = 1.0f / hz;
	for (float t = dt; t <= limit + 1e-6f; t += dt) {
		v = fpmove::Ground(v, dir, wish, kRun, dt, kCS);
		if (done(v))
			return t;
	}
	return -1.0f;
}

static void TestMovesOnFirstFrame()
{
	const Vec2 v = fpmove::Ground({0, 0}, {0, 1}, kRun, kRun, 1.0f / 60.0f, kCS);
	CHECK(v.y > 0.2f);
}

static void TestReachesTopSpeedLikeCS()
{
	const float t90 = Run({0, 0}, {0, 1}, kRun, 60.0f, 2.0f, [](Vec2 v) { return Speed(v) >= 0.9f * kRun; });
	const float tFull = Run({0, 0}, {0, 1}, kRun, 60.0f, 2.0f, [](Vec2 v) { return Speed(v) >= 0.995f * kRun; });
	CHECK(t90 >= 0.30f && t90 <= 0.36f);
	CHECK(tFull >= 0.37f && tFull <= 0.44f);
}

static void TestStopsLikeCS()
{
	const float tStop = Run({0, kRun}, {0, 0}, 0.0f, 60.0f, 2.0f, [](Vec2 v) { return Speed(v) <= 0.025f; });
	CHECK(tStop >= 0.45f && tStop <= 0.60f);
	const float tZero = Run({0, kRun}, {0, 0}, 0.0f, 60.0f, 2.0f, [](Vec2 v) { return Speed(v) == 0.0f; });
	CHECK(tZero == tStop);
}

static void TestRateIndependent()
{
	const auto reached = [](Vec2 v) { return Speed(v) >= 0.9f * kRun; };
	const float slow = Run({0, 0}, {0, 1}, kRun, 30.0f, 2.0f, reached);
	const float fast = Run({0, 0}, {0, 1}, kRun, 100.0f, 2.0f, reached);
	CHECK(slow > 0.0f && fast > 0.0f);
	CHECK(std::fabs(slow - fast) < 0.04f);
}

static void TestDiagonalNotFaster()
{
	const float k = 1.0f / std::sqrt(2.0f);
	Vec2 v{0, 0};
	for (int i = 0; i < 120; i++)
		v = fpmove::Ground(v, {k, k}, kRun, kRun, 1.0f / 60.0f, kCS);
	CHECK(Speed(v) <= kRun + 1e-3f);
}

static void TestReversesDirection()
{
	Vec2 v{0, kRun};
	for (int i = 0; i < 60; i++)
		v = fpmove::Ground(v, {0, -1}, kRun, kRun, 1.0f / 60.0f, kCS);
	CHECK(v.y < -6.0f);
}

static void TestNoWishDecaysSmoothly()
{
	const float dt = 1.0f / 60.0f;
	Vec2 v{0, kRun};
	for (int i = 0; i < 60; i++) {
		const Vec2 next = fpmove::Ground(v, {0, 0}, 0.0f, kRun, dt, kCS);
		CHECK(Speed(v) - Speed(next) <= kCS.friction * kRun * dt + 1e-4f);
		v = next;
	}
}

static void TestAirAddsOnlyUpToCap()
{
	Vec2 v{0, kRun};
	for (int i = 0; i < 30; i++)
		v = fpmove::Air(v, {1, 0}, kRun, 1.0f / 60.0f, kCS);
	CHECK(std::fabs(v.x) <= 0.76f + 1e-3f);
	CHECK(std::fabs(v.y - kRun) <= 1e-4f);
}

int main()
{
	TestMovesOnFirstFrame();
	TestReachesTopSpeedLikeCS();
	TestStopsLikeCS();
	TestRateIndependent();
	TestDiagonalNotFaster();
	TestReversesDirection();
	TestNoWishDecaysSmoothly();
	TestAirAddsOnlyUpToCap();
	if (gFailures) {
		printf("%d movement test(s) failed\n", gFailures);
		return 1;
	}
	printf("all movement tests passed\n");
	return 0;
}

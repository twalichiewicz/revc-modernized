// Unit tests for finding the next turn on a GPS route: make test
#include "turns.h"

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

static const float kDeg = 3.14159265f / 180.0f;
static turns::Turn Next(const std::vector<Vec2> &points, float lookAhead = 100.0f)
{
	return turns::Next(points, 40.0f * kDeg, 12.0f, lookAhead);
}

static void TestStraightRoadHasNoTurn()
{
	CHECK(Next({{0, 0}, {0, 40}, {0, 80}, {0, 120}}).side == 0);
}

// North, then east at 50 m: a right turn (the world's x is east, y north).
static void TestRightTurn()
{
	const turns::Turn t = Next({{0, 0}, {0, 50}, {50, 50}});
	CHECK(t.side == turns::kRight);
	CHECK(std::fabs(t.distance - 50.0f) < 0.5f);
}

static void TestLeftTurn()
{
	const turns::Turn t = Next({{0, 0}, {0, 50}, {-50, 50}});
	CHECK(t.side == turns::kLeft);
	CHECK(std::fabs(t.distance - 50.0f) < 0.5f);
}

// A junction rounded off over a few nodes still reads as one turn, near the corner.
static void TestRoundedCorner()
{
	const turns::Turn t = Next({{0, 0}, {0, 40}, {2, 47}, {7, 50}, {40, 50}});
	CHECK(t.side == turns::kRight);
	CHECK(t.distance > 35.0f && t.distance < 50.0f);
}

// A long bend (60 degrees over about 200 m) is not a turn.
static void TestGentleCurveIsNotATurn()
{
	std::vector<Vec2> arc;
	const float r = 200.0f;
	for (int deg = 0; deg <= 60; deg += 3)
		arc.push_back({r - r * std::cos(deg * kDeg), r * std::sin(deg * kDeg)});
	CHECK(Next(arc).side == 0);
}

static void TestTurnPastTheLookAheadIsIgnored()
{
	CHECK(Next({{0, 0}, {0, 150}, {50, 150}}, 100.0f).side == 0);
}

// The first of two turns is the one to signal.
static void TestFirstTurnWins()
{
	const turns::Turn t = Next({{0, 0}, {0, 30}, {-40, 30}, {-40, 80}});
	CHECK(t.side == turns::kLeft);
	CHECK(std::fabs(t.distance - 30.0f) < 0.5f);
}

static void TestTooShortARoute()
{
	CHECK(Next({{0, 0}, {0, 10}}).side == 0);
	CHECK(Next({}).side == 0);
}

int main()
{
	TestStraightRoadHasNoTurn();
	TestRightTurn();
	TestLeftTurn();
	TestRoundedCorner();
	TestGentleCurveIsNotATurn();
	TestTurnPastTheLookAheadIsIgnored();
	TestFirstTurnWins();
	TestTooShortARoute();
	if (gFailures) {
		printf("%d turn test(s) failed\n", gFailures);
		return 1;
	}
	printf("all turn tests passed\n");
	return 0;
}

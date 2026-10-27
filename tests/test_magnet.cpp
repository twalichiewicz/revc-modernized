// Unit tests for the pause map cursor's pull towards icons: make test
#include "magnet.h"

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

static const magnet::Tuning kTuning{40.0f, 20.0f, 0.4f, 14.0f};
static const float kFrame = 1.0f / 60.0f;

static float Dist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

static void TestNothingWithinReach()
{
	CHECK(magnet::Nearest({{100, 100}, {300, 50}}, {0, 0}, 40.0f) == -1);
	CHECK(magnet::Nearest({}, {0, 0}, 40.0f) == -1);
}

static void TestNearestOfTwoWithinReach()
{
	CHECK(magnet::Nearest({{30, 0}, {0, 12}, {500, 500}}, {0, 0}, 40.0f) == 1);
}

// Left alone 30 px from an icon, the cursor closes in each frame and is on it within half a second.
static void TestIdleCursorIsPulledOntoANearIcon()
{
	const std::vector<Vec2> icons{{130, 100}};
	Vec2 cursor{100, 100};
	const Vec2 first = magnet::Step(cursor, {0, 0}, false, icons, kTuning, kFrame);
	CHECK(Dist(first, icons[0]) < 30.0f);
	for (int i = 0; i < 30; i++)
		cursor = magnet::Step(cursor, {0, 0}, false, icons, kTuning, kFrame);
	CHECK(Dist(cursor, icons[0]) < 0.5f);
}

static void TestIdleCursorFarFromIconsStays()
{
	const Vec2 cursor = magnet::Step({100, 100}, {0, 0}, false, {{200, 100}}, kTuning, kFrame);
	CHECK(cursor.x == 100.0f && cursor.y == 100.0f);
}

// The pull never carries the cursor past the icon.
static void TestPullDoesNotOvershoot()
{
	const Vec2 cursor = magnet::Step({100, 100}, {0, 0}, false, {{130, 100}}, kTuning, 1.0f);
	CHECK(cursor.x <= 130.0f && cursor.x > 100.0f);
}

static void TestNoPullWhileTheMapMoves()
{
	const Vec2 cursor = magnet::Step({100, 100}, {0, 0}, true, {{130, 100}}, kTuning, kFrame);
	CHECK(cursor.x == 100.0f && cursor.y == 100.0f);
}

// Over an icon the stick moves the cursor slower, but still moves it.
static void TestStickIsSlowerOverAnIcon()
{
	const Vec2 cursor = magnet::Step({100, 100}, {10, 0}, false, {{105, 100}}, kTuning, kFrame);
	CHECK(cursor.x > 100.0f && cursor.x < 110.0f);
	CHECK(std::fabs(cursor.x - 104.0f) < 0.01f);
}

// Away from the icons the stick moves the cursor its full amount, unpulled.
static void TestStickMovesFullyAwayFromIcons()
{
	const Vec2 cursor = magnet::Step({100, 100}, {10, -5}, false, {{135, 100}}, kTuning, kFrame);
	CHECK(cursor.x == 110.0f && cursor.y == 95.0f);
}

int main()
{
	TestNothingWithinReach();
	TestNearestOfTwoWithinReach();
	TestIdleCursorIsPulledOntoANearIcon();
	TestIdleCursorFarFromIconsStays();
	TestPullDoesNotOvershoot();
	TestNoPullWhileTheMapMoves();
	TestStickIsSlowerOverAnIcon();
	TestStickMovesFullyAwayFromIcons();
	if (gFailures) {
		printf("%d magnet test(s) failed\n", gFailures);
		return 1;
	}
	printf("all magnet tests passed\n");
	return 0;
}

// Unit tests for first person's four-line crosshair: its gap from the spread: make test
#include "fpcross.h"

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

static void TestGapAtZeroSpread() { CHECK(std::fabs(fpcross::Gap(0.0f, 0.5f, 1080.0f) - fpcross::MinGap(1080.0f)) < 1e-4f); }

static void TestGapFollowsSpread()
{
	const float least = fpcross::MinGap(1080.0f);
	const float one = fpcross::Gap(0.01f, 0.5f, 1080.0f) - least, two = fpcross::Gap(0.02f, 0.5f, 1080.0f) - least;
	CHECK(one > 0.0f && std::fabs(two / one - 2.0f) < 0.01f);
	// The spread's edge on screen: tan(angle) over tan(half the field of view), of half the screen.
	CHECK(std::fabs(one - std::tan(0.01f) / 0.5f * 540.0f) < 1e-3f);
}

static void TestScalesWithTheScreen()
{
	CHECK(std::fabs(fpcross::MinGap(2160.0f) - 2.0f * fpcross::MinGap(1080.0f)) < 1e-4f);
	CHECK(std::fabs(fpcross::Length(2160.0f) - 2.0f * fpcross::Length(1080.0f)) < 1e-4f);
}

static bool Whole(float v) { return v == std::floor(v); }

// The four lines are whole-pixel rectangles (crisp, never smeared over two
// pixels), each its thickness across and its length along.
static void TestLinesOnWholePixels()
{
	for (float h : {720.0f, 1080.0f, 1440.0f, 2160.0f})
		for (float cx : {640.0f, 960.5f, 1279.3f}) {
			fpcross::Rect r[4];
			fpcross::Lines(cx, cx * 0.5625f, 7.3f, h, r);
			for (const fpcross::Rect &q : r)
				CHECK(Whole(q.x) && Whole(q.y) && Whole(q.w) && Whole(q.h) && q.w >= 1.0f && q.h >= 1.0f);
			const float t = std::round(fpcross::Thickness(h)), len = std::round(fpcross::Length(h));
			CHECK(r[0].w == t && r[0].h == len && r[1].w == t && r[1].h == len);   // up, down
			CHECK(r[2].w == len && r[2].h == t && r[3].w == len && r[3].h == t);   // left, right
		}
}

// They sit evenly about the centre: up as far as down, left as right, each across it.
static void TestLinesSymmetric()
{
	fpcross::Rect r[4];
	fpcross::Lines(960.0f, 540.0f, 6.0f, 1080.0f, r);
	const float upGap = 540.0f - (r[0].y + r[0].h), downGap = r[1].y - 540.0f;
	const float leftGap = 960.0f - (r[2].x + r[2].w), rightGap = r[3].x - 960.0f;
	CHECK(upGap == downGap && leftGap == rightGap && upGap == leftGap && upGap == 6.0f);
	CHECK(r[0].x + r[0].w / 2 == 960.0f && r[2].y + r[2].h / 2 == 540.0f);
}

// Every crosshair's dark edge: whole pixels, one at 1080, two at 2160, never none.
static void TestEdge() { CHECK(fpcross::Edge(1080) == 1.0f && fpcross::Edge(2160) == 2.0f && fpcross::Edge(720) == 1.0f); }

int main()
{
	TestEdge();
	TestLinesOnWholePixels();
	TestLinesSymmetric();
	TestGapAtZeroSpread();
	TestGapFollowsSpread();
	TestScalesWithTheScreen();
	if (gFailures) {
		printf("%d fpcross test(s) failed\n", gFailures);
		return 1;
	}
	printf("all fpcross tests passed\n");
	return 0;
}

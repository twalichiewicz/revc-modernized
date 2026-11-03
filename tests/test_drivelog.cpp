// The drive log (#37), without the game: top speed, 0-100 km/h from rest and the
// body's roll and pitch against the road, over 30 s windows.
#include "drivelog.h"
#include <cmath>
#include <cstdio>

using namespace drivelog;
static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)
#define NEAR(a, b, e) CHECK(std::fabs((a) - (b)) <= (e))

constexpr float kDt = 1.0f / 60.0f, kDeg = 3.14159265f / 180.0f;

// From rest (or `from` m/s), `accel` m/s^2 up to `to` m/s, then held for a second.
static float Drive(State &s, float t, float from, float accel, float to)
{
	for (float v = from; v < to; v += accel * kDt, t += kDt)
		Add(s, {t, v, true, 0, 0});
	for (int i = 0; i < 60; ++i, t += kDt)
		Add(s, {t, to, true, 0, 0});
	return t;
}

static void TestTopAndZeroTo100()
{
	State s;
	for (int i = 0; i < 30; ++i)
		Add(s, {i * kDt, 0, true, 0, 0}); // at rest a moment
	Drive(s, 30 * kDt, 0, 5, 30);
	NEAR(s.window.top, 30.0f, 0.1f);
	NEAR(s.window.zeroTo100, 5.556f, 0.02f);
}

static void TestRollingStartNoTime()
{
	State s;
	Drive(s, 0, 10, 5, 30);
	CHECK(s.window.zeroTo100 == -1);
}

static void TestQuickestKept()
{
	State s;
	float t = Drive(s, 0, 0, 27.78f / 8, 28); // 8 s
	for (int i = 0; i < 30; ++i, t += kDt)
		Add(s, {t, 0, true, 0, 0}); // stopped again
	Drive(s, t, 0, 27.78f / 6, 28);   // 6 s
	NEAR(s.window.zeroTo100, 6.0f, 0.05f);
}

static void TestAnglesOnlyAllDown()
{
	State s;
	Add(s, {0, 10, false, 20 * kDeg, 10 * kDeg});
	CHECK(s.window.rollMost == 0 && s.window.pitchMost == 0);
	Add(s, {kDt, 10, true, -3 * kDeg, 2 * kDeg});
	NEAR(s.window.rollMost, 3 * kDeg, 1e-6f);
	NEAR(s.window.pitchMost, 2 * kDeg, 1e-6f);
}

static void TestBodyAngles()
{
	// Flat ground; the wheels: 0 front-left, 1 rear-left, 2 front-right, 3 rear-right.
	const float contact[4][3] = {{-0.8f, 1.3f, 0}, {-0.8f, -1.3f, 0}, {0.8f, 1.3f, 0}, {0.8f, -1.3f, 0}};
	float roll, pitch;
	const float level[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
	BodyAngles(level[0], level[1], level[2], contact, roll, pitch);
	NEAR(roll, 0.0f, 1e-6f);
	NEAR(pitch, 0.0f, 1e-6f);
	const float r = 4 * kDeg; // the body rolled 4 degrees, its right side down
	const float right[3] = {std::cos(r), 0, -std::sin(r)}, up[3] = {std::sin(r), 0, std::cos(r)};
	BodyAngles(right, level[1], up, contact, roll, pitch);
	NEAR(roll, 4 * kDeg, 0.01f * kDeg);
	NEAR(pitch, 0.0f, 1e-5f);
	const float p = 2 * kDeg; // the nose dipped 2 degrees
	const float fwd[3] = {0, std::cos(p), -std::sin(p)}, up2[3] = {0, std::sin(p), std::cos(p)};
	BodyAngles(level[0], fwd, up2, contact, roll, pitch);
	NEAR(pitch, -2 * kDeg, 0.01f * kDeg);
	NEAR(roll, 0.0f, 1e-5f);
}

static void TestWindow()
{
	State s;
	Add(s, {100, 5, true, 0, 0});
	CHECK(!Due(s, 129.9f));
	CHECK(Due(s, 130.0f));
	const Window w = Take(s, 130.0f);
	CHECK(w.top == 5);
	CHECK(s.window.top == 0 && s.window.zeroTo100 == -1);
	CHECK(!Due(s, 159.9f) && Due(s, 160.0f));
}

int main()
{
	TestTopAndZeroTo100();
	TestRollingStartNoTime();
	TestQuickestKept();
	TestAnglesOnlyAllDown();
	TestBodyAngles();
	TestWindow();
	if (failures)
		std::printf("%d failure(s)\n", failures);
	else
		std::printf("drive log: all passed\n");
	return failures ? 1 : 0;
}

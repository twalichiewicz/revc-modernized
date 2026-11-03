// Tommy balancing his head in a car's seat view (#37), without the game: the
// view turned back a little towards level against its roll, and against quick
// pitching (not hills), after a short lag.
#include "headbalance.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace headbalance;
static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)
#define NEAR(a, b, e) CHECK(std::fabs((a) - (b)) <= (e))

constexpr float kDeg = 3.14159265f / 180.0f, kDt = 1.0f / 60.0f;

static Turn Hold(State &s, float roll, float pitch, bool active, float seconds, float dt = kDt)
{
	Turn t{};
	for (float time = 0; time < seconds - 1e-6f; time += dt)
		t = Step(s, roll, pitch, active, dt);
	return t;
}

static void TestLevelsRoll()
{
	State s;
	const Turn t = Hold(s, 4 * kDeg, 0, true, 2);
	NEAR(t.roll, -1.4f * kDeg, 0.05f * kDeg); // a right-side-down view turned back 35%
	NEAR(t.pitch, 0.0f, 1e-6f);
}

static void TestClampRoll()
{
	State s;
	const Turn t = Hold(s, 30 * kDeg, 0, true, 2);
	NEAR(t.roll, -6 * kDeg, 1e-4f);
}

static void TestQuickPitchOnly()
{
	State s;
	Hold(s, 0, 0, true, 1); // level a moment
	float most = 0;
	for (float time = 0; time < 0.4f; time += kDt)
		most = std::max(most, Step(s, 0, -2 * kDeg, true, kDt).pitch); // a 2 degree nose dip
	CHECK(most >= 0.4f * kDeg && most <= 0.6f * kDeg);              // levelled about a quarter
	State hill;
	Hold(hill, 0, 0, true, 1);
	const Turn t = Hold(hill, 0, 10 * kDeg, true, 10); // a long climb isn't levelled
	NEAR(t.pitch, 0.0f, 0.1f * kDeg);
}

static void TestLevelsAnyView()
{
	// Looking behind, the view's own roll is what it sees: the same turn back.
	State s;
	const Turn t = Hold(s, 4 * kDeg, 0, true, 2);
	State behind;
	const Turn b = Hold(behind, 4 * kDeg, 0, true, 2);
	CHECK(t.roll == b.roll);
}

static void TestInactiveEasesOut()
{
	State s;
	Turn t = Hold(s, 4 * kDeg, 0, true, 2);
	float last = t.roll, biggest = 0;
	for (float time = 0; time < 1.0f; time += kDt) {
		t = Step(s, 4 * kDeg, 0, false, kDt);
		biggest = std::max(biggest, std::fabs(t.roll - last));
		last = t.roll;
	}
	NEAR(t.roll, 0.0f, 0.05f * kDeg);
	CHECK(biggest <= 0.2f * kDeg);
}

static void TestRolledCarNoCorrection()
{
	State s;
	const Turn t = Hold(s, 90 * kDeg, 0, false, 2);
	CHECK(t.roll == 0 && t.pitch == 0);
}

static void TestFrameRate()
{
	State slow, fast;
	const Turn a = Hold(slow, 4 * kDeg, 0, true, 0.3f, 1.0f / 30), b = Hold(fast, 4 * kDeg, 0, true, 0.3f, 1.0f / 144);
	CHECK(std::fabs(a.roll - b.roll) <= 0.02f * std::fabs(b.roll));
}

int main()
{
	TestLevelsRoll();
	TestClampRoll();
	TestQuickPitchOnly();
	TestLevelsAnyView();
	TestInactiveEasesOut();
	TestRolledCarNoCorrection();
	TestFrameRate();
	if (failures)
		std::printf("%d failure(s)\n", failures);
	else
		std::printf("head balance: all passed\n");
	return failures ? 1 : 0;
}

// Unit tests for first person's right stick, aim scaling and aim slowdown: make test
#include "fplook.h"

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

static const fplook::Tuning kT;
static const float kFrame = 1.0f / 60.0f;
static const float kDeg = 3.14159265f / 180.0f;

static bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

static void TestDeadZone()
{
	fplook::Stick s;
	CHECK(s.Update(0.10f, 0.0f, kFrame, kT).yaw == 0.0f);
	fplook::Stick s2;
	const float yaw = s2.Update(0.13f, 0.0f, kFrame, kT).yaw;
	CHECK(yaw > 0.0f && yaw < 0.01f);
}

static void TestCurve()
{
	fplook::Stick s;
	CHECK(Near(s.Update(0.12f + 0.5f * 0.83f, 0.0f, kFrame, kT).yaw, 0.25f * 4.19f, 1e-3f));
	fplook::Stick full;
	CHECK(Near(full.Update(1.0f, 0.0f, kFrame, kT).yaw, 4.19f, 1e-3f));
}

static void TestPitchUsesItsOwnRate()
{
	fplook::Stick s;
	CHECK(Near(s.Update(0.0f, 1.0f, kFrame, kT).pitch, 2.53f, 1e-3f));
}

// Held at the rim, the yaw is still its plain top speed at the delay, then 50% more once ramped.
static float HoldFor(fplook::Stick &s, float x, float seconds)
{
	float yaw = 0.0f;
	const int frames = int(std::lround(seconds / kFrame));
	for (int i = 0; i < frames; i++)
		yaw = s.Update(x, 0.0f, kFrame, kT).yaw;
	return yaw;
}

static void TestRimBoostRamps()
{
	fplook::Stick s;
	CHECK(Near(HoldFor(s, 1.0f, 0.05f), 4.19f, 0.01f));
	fplook::Stick r;
	CHECK(Near(HoldFor(r, 1.0f, 0.05f + 0.35f), 4.19f * 1.5f, 0.02f));
	HoldFor(r, 0.9f, 0.2f); // off the rim: the boost starts again from nothing
	CHECK(Near(r.Update(1.0f, 0.0f, kFrame, kT).yaw, 4.19f, 0.01f));
}

static void TestAimScale()
{
	const float expected = std::tan(26.0f * kDeg) / std::tan(35.0f * kDeg);
	CHECK(Near(fplook::AimScale(70.0f, 52.0f, 0.45f), expected, 1e-3f));
	CHECK(Near(fplook::AimScale(70.0f, 52.0f, 0.9f), 2.0f * expected, 2e-3f));
}

static void TestOffAngle()
{
	const CVector eye{0, 0, 0}, ahead{0, 1, 0};
	CHECK(fplook::OffAngle(eye, ahead, {0, 10, -1}, {0, 10, 0.8f}, 0.35f) <= 0.0f);
	const float off = fplook::OffAngle(eye, ahead, {1, 10, -1}, {1, 10, 0.8f}, 0.35f);
	CHECK(Near(off, std::atan(1.0f / 10.0f) - std::atan(0.35f / 10.0f), 2e-3f));
}

static void TestSlowdown()
{
	CHECK(Near(fplook::Slowdown(0.0f, 10.0f, false, 0.5f), 0.5f, 1e-4f));
	CHECK(Near(fplook::Slowdown(0.0f, 10.0f, true, 0.5f), 0.4f, 1e-4f));
	CHECK(fplook::Slowdown(1.5f * kDeg, 10.0f, false, 0.5f) == 1.0f);
	CHECK(fplook::Slowdown(3.0f * kDeg, 10.0f, false, 0.5f) == 1.0f);
	CHECK(fplook::Slowdown(0.0f, 10.0f, false, 0.9f) == 1.0f); // a flick isn't slowed
	CHECK(fplook::Slowdown(0.0f, 70.0f, false, 0.5f) == 1.0f);
	const float at40 = fplook::Slowdown(0.0f, 40.0f, false, 0.5f);
	CHECK(at40 > 0.5f && at40 < 1.0f);
	const float at1 = fplook::Slowdown(0.0f, 1.0f, false, 0.5f);
	CHECK(at1 > 0.5f && at1 < 1.0f); // weaker point-blank
}

static void TestAssistInAtOnceOutSlowly()
{
	fplook::Assist a;
	CHECK(a.Update(0.5f, kFrame) == 0.5f);
	const float partway = a.Update(1.0f, 0.075f);
	CHECK(partway > 0.6f && partway < 0.9f);
	CHECK(a.Update(1.0f, 0.125f) == 1.0f);
}

int main()
{
	TestDeadZone();
	TestCurve();
	TestPitchUsesItsOwnRate();
	TestRimBoostRamps();
	TestAimScale();
	TestOffAngle();
	TestSlowdown();
	TestAssistInAtOnceOutSlowly();
	if (gFailures) {
		printf("%d look test(s) failed\n", gFailures);
		return 1;
	}
	printf("all look tests passed\n");
	return 0;
}

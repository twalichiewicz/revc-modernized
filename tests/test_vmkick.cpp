// Unit tests for the guns' kick in his hands (snap and settle, by class; bursts; buzz): make test
#include "vmkick.h"

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

using namespace vm;

constexpr float kMiddle[3] = {0.5f, 0.75f, 0.5f}; // its usual size, over to the right, no yaw

struct Trace {
	float peak = -1e9f, peakAt = 0.0f, low = 1e9f, at1s = 0.0f, backPeak = 0.0f;
};

// One shot from rest, followed for 1.5 s at `fps`: its pitch's peak (and when), its least after that, and where it is at 1 s.
static Trace Follow(KickClass k, float fps = 60.0f)
{
	Recoil r;
	r.Shot(k, kMiddle);
	Trace t;
	const float dt = 1.0f / fps;
	for (float time = 0.0f; time < 1.5f; time += dt) {
		const KickMove m = r.Update(k, time == 0.0f ? 0.0f : dt, false, 0.0f);
		if (m.pitch > t.peak) {
			t.peak = m.pitch;
			t.peakAt = time;
		}
		if (time > t.peakAt)
			t.low = std::min(t.low, m.pitch);
		if (std::fabs(time - 1.0f) < dt * 0.5f)
			t.at1s = m.pitch;
		t.backPeak = std::max(t.backPeak, -m.offset.y);
	}
	return t;
}

static void TestTheClasses()
{
	CHECK(KickClassFor(17) == KickClass::kPistol && KickClassFor(18) == KickClass::kRevolver &&
	      KickClassFor(19) == KickClass::kShotgun && KickClassFor(23) == KickClass::kSmg && KickClassFor(25) == KickClass::kHeavySmg &&
	      KickClassFor(26) == KickClass::kRifle && KickClassFor(31) == KickClass::kFlame && KickClassFor(32) == KickClass::kLmg &&
	      KickClassFor(33) == KickClass::kMinigun && KickClassFor(30) == KickClass::kNone);
}

// A pistol flips well up at once, peaks within a few frames, drops back past
// where it was, and has settled within a second.
static void TestPistolSnapsAndSettles()
{
	Recoil r;
	r.Shot(KickClass::kPistol, kMiddle);
	const KickMove now = r.Update(KickClass::kPistol, 0.0f, false, 0.0f);
	CHECK(now.pitch > 6.0f); // half of it the frame it fires
	const Trace t = Follow(KickClass::kPistol);
	CHECK(t.peak > 11.0f && t.peak < 18.0f && t.peakAt < 0.08f);
	CHECK(t.low < -0.08f * t.peak); // the overshoot
	CHECK(std::fabs(t.at1s) < 0.5f);
}

// A revolver goes higher and comes down heavily, without a wobble.
static void TestRevolverHeavier()
{
	const Trace p = Follow(KickClass::kPistol), r = Follow(KickClass::kRevolver);
	CHECK(r.peak > 1.3f * p.peak);
	CHECK(r.low > -0.03f * r.peak);
}

// A shotgun shoves straight back more than it flips.
static void TestShotgunShovesBack()
{
	const Trace s = Follow(KickClass::kShotgun), p = Follow(KickClass::kPistol);
	CHECK(s.backPeak > 0.028f && s.backPeak > 2.0f * p.backPeak);
	CHECK(s.peak < 0.6f * p.peak);
}

// The same however fast the frames come.
static void TestAnyFrameRate()
{
	const Trace a = Follow(KickClass::kPistol, 30.0f), b = Follow(KickClass::kPistol, 144.0f);
	CHECK(std::fabs(a.peak - b.peak) < 0.12f * b.peak);
	CHECK(std::fabs(a.at1s - b.at1s) < 0.3f);
}

// Each shot its own: its size, and the side it rolls to.
static void TestShotsDiffer()
{
	Recoil a, b;
	const float small[3] = {0.0f, 0.25f, 0.0f}, big[3] = {1.0f, 0.75f, 1.0f};
	a.Shot(KickClass::kRifle, small);
	b.Shot(KickClass::kRifle, big);
	const KickMove ma = a.Update(KickClass::kRifle, 0.0f, false, 0.0f), mb = b.Update(KickClass::kRifle, 0.0f, false, 0.0f);
	CHECK(mb.pitch > 1.3f * ma.pitch);
	CHECK(ma.roll * mb.roll < 0.0f);
	CHECK(ma.yaw * mb.yaw < 0.0f);
}

// A burst's first shot kicks harder than the ones after.
static void TestFirstShotBigger()
{
	Recoil r;
	r.Shot(KickClass::kSmg, kMiddle);
	const float first = r.Update(KickClass::kSmg, 0.0f, true, 0.0f).pitch;
	const float before = r.Update(KickClass::kSmg, 0.067f, true, 0.0f).pitch;
	r.Shot(KickClass::kSmg, kMiddle);
	const float second = r.Update(KickClass::kSmg, 0.0f, true, 0.0f).pitch - before;
	CHECK(first > 1.35f * second);
}

// Held, an automatic climbs and backs up into his shoulder; let go, it comes back.
static void TestAutomaticClimbs()
{
	Recoil r;
	const float dt = 1.0f / 60.0f, interval = 0.067f;
	float next = 0.0f, early = 0.0f, late = 0.0f, lateBack = 0.0f;
	int earlyN = 0, lateN = 0;
	for (float t = 0.0f; t < 1.2f; t += dt) {
		if (t >= next) {
			r.Shot(KickClass::kSmg, kMiddle);
			next += interval;
		}
		const KickMove m = r.Update(KickClass::kSmg, dt, true, 0.0f);
		if (t < 0.2f) {
			early += m.pitch;
			earlyN++;
		} else if (t > 0.9f) {
			late += m.pitch;
			lateBack += -m.offset.y;
			lateN++;
		}
	}
	CHECK(late / float(lateN) > early / float(earlyN) + 1.0f);
	CHECK(lateBack / float(lateN) > 0.008f);
	KickMove m{};
	for (float t = 0.0f; t < 1.2f; t += dt)
		m = r.Update(KickClass::kSmg, dt, false, 0.0f);
	CHECK(std::fabs(m.pitch) < 0.2f && std::fabs(m.offset.y) < 0.001f);
}

// A minigun buzzes as it fires, a little as its barrels spin, not at all still.
static void TestMinigunBuzzes()
{
	Recoil r;
	auto range = [&](bool firing, float spin) {
		float lo = 1e9f, hi = -1e9f;
		for (int i = 0; i < 120; i++) {
			const float roll = r.Update(KickClass::kMinigun, 1.0f / 120.0f, firing, spin).roll;
			if (i >= 60) {
				lo = std::min(lo, roll);
				hi = std::max(hi, roll);
			}
		}
		return hi - lo;
	};
	const float firing = range(true, 1.0f), spinning = range(false, 1.0f), still = range(false, 0.0f);
	CHECK(firing > 1.0f && spinning > 0.1f && spinning < 0.5f * firing && still < 0.05f);
}

int main()
{
	TestTheClasses();
	TestPistolSnapsAndSettles();
	TestRevolverHeavier();
	TestShotgunShovesBack();
	TestAnyFrameRate();
	TestShotsDiffer();
	TestFirstShotBigger();
	TestAutomaticClimbs();
	TestMinigunBuzzes();
	if (gFailures) {
		printf("%d vmkick test(s) failed\n", gFailures);
		return 1;
	}
	printf("all vmkick tests passed\n");
	return 0;
}

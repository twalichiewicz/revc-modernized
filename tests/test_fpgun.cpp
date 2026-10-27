// Unit tests for when first person draws the new viewmodel, and when his arms are rebuilt: make test
#include "vmuse.h"

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

using namespace vm;

constexpr int32_t kIdle = 1, kFall = 0x2a, kGetUp = 0x2b, kDie = 0x36, kDead = 0x37;

static Moment Standing()
{
	Moment m;
	m.firstPersonOnFoot = true;
	m.enabled = true;
	m.covered = true;
	m.armsOk = true;
	m.pedState = kIdle;
	return m;
}

static void TestCoveredGunUsesNew() { CHECK(Choose(Standing()) == Use::kNew); }

static void TestUncoveredGunFallsBack()
{
	Moment m = Standing();
	m.covered = false; // the Uzi, in this round
	CHECK(Choose(m) == Use::kOld);
	m = Standing();
	m.armsOk = false; // his model didn't split
	CHECK(Choose(m) == Use::kOld);
}

static void TestStatesThatFallBack()
{
	for (int32_t state : {kFall, kGetUp, kDie, kDead}) {
		Moment m = Standing();
		m.pedState = state;
		CHECK(Choose(m) == Use::kOld);
	}
	Moment m = Standing();
	m.swimming = true;
	CHECK(Choose(m) == Use::kOld);
	m = Standing();
	m.climbing = true;
	CHECK(Choose(m) == Use::kOld);
	// The shootdodge's dive and landing aren't among them, the landing's get-up
	// included: the new gun stays, its firing, the dive standing for spread,
	// and its crosshair.
	m = Standing();
	m.pedState = kGetUp;
	m.dodging = true;
	CHECK(Choose(m) == Use::kNew);
}

static void TestSwitchedOff()
{
	Moment m = Standing();
	m.enabled = false;
	CHECK(Choose(m) == Use::kOld);
	m = Standing();
	m.firstPersonOnFoot = false;
	CHECK(Choose(m) == Use::kNone);
}

static void TestArmsCacheKey()
{
	int a = 0, b = 0;
	const ModelKey key{&a, 0, 1, 1153, 1355};
	CHECK(SameModel(key, ModelKey{&a, 0, 1, 1153, 1355}));
	CHECK(!SameModel(key, ModelKey{&b, 0, 1, 1153, 1355}));  // a new geometry (a clothes change)
	CHECK(!SameModel(key, ModelKey{&a, 0, 2, 1153, 1355}));  // another model index
	CHECK(!SameModel(key, ModelKey{&a, &b, 1, 1153, 1355})); // another texture
	// Every outfit is the same model index; a new one loaded where the old one was is told by its size.
	CHECK(!SameModel(key, ModelKey{&a, 0, 1, 1294, 1392}));
	CHECK(!SameModel(ModelKey{}, ModelKey{}));   // nothing yet: never the same
}

// The drawing pass must take each frame's viewmodel: if it doesn't (its hook
// never reached, or it can't see the camera) his arms would be hidden with
// nothing drawn, so after a few frames the new viewmodel stands down.
static void TestDrawingMissedStandsDown()
{
	Heartbeat beat;
	CHECK(beat.Beat(true) && beat.Beat(true));
	CHECK(beat.Beat(false) && beat.Beat(false)); // a frame or two missed: carry on
	CHECK(!beat.Beat(false));                    // three running: stand down
	CHECK(!beat.Beat(true));                     // and stay down
}

// A shot seen from the clip going down, only frame to frame: coming back to
// first person after firing in third isn't a shot.
static void TestNoPhantomShot()
{
	ShotSeen seen;
	CHECK(!seen.Update(17, 7, 100));
	CHECK(seen.Update(17, 6, 101));  // fired
	CHECK(!seen.Update(17, 6, 102));
	CHECK(!seen.Update(17, 3, 200)); // back after frames away: not a shot
	CHECK(!seen.Update(18, 2, 201)); // another gun
	CHECK(seen.Update(18, 1, 202));
}

// The reload's progress from when it started: the fast-reload perk's quarter-length reload runs 0 to 1 too.
static void TestReloadProgressFromItsStart()
{
	ReloadClock clock;
	CHECK(clock.Progress(true, 1000, 1250) == 0.0f); // started now, 250 ms to go
	CHECK(std::fabs(clock.Progress(true, 1125, 1250) - 0.5f) < 1e-4f);
	CHECK(clock.Progress(true, 1300, 1250) == 1.0f);
	CHECK(clock.Progress(false, 1400, 1250) == 0.0f);
	CHECK(clock.Progress(true, 2000, 3000) == 0.0f); // a new reload
}

int main()
{
	TestDrawingMissedStandsDown();
	TestNoPhantomShot();
	TestReloadProgressFromItsStart();
	TestCoveredGunUsesNew();
	TestUncoveredGunFallsBack();
	TestStatesThatFallBack();
	TestSwitchedOff();
	TestArmsCacheKey();
	if (gFailures) {
		printf("%d fpgun test(s) failed\n", gFailures);
		return 1;
	}
	printf("all fpgun tests passed\n");
	return 0;
}

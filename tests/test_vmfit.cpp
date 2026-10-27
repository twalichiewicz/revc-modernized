// Unit tests for fitting his fingers to a gun without going into it: make test
#include "vmfit.h"
#include "vmtestarm.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

using namespace vm;

// A closed box as a gun's mesh (its faces outward), all on the body.
static void AddBox(Gun &gun, Vec3 lo, Vec3 hi)
{
	const uint16_t base = uint16_t(gun.vertices.size());
	for (int i = 0; i < 8; i++)
		gun.vertices.push_back({{i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z}, {0, 0, 1}, 0, 0, 0});
	const int quads[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
	for (const auto &q : quads)
		for (int t : {0, 1, 2, 0, 2, 3})
			gun.indices.push_back(uint16_t(base + q[t]));
}

static Anchor MakeAnchor(const char *name, Vec3 pos, Vec3 slack = {0, 0, 0})
{
	Anchor a{};
	strncpy(a.name, name, 15);
	a.pos = pos;
	a.part = kNoPart;
	a.slack = slack;
	return a;
}

static void TestWindingInsideAndOut()
{
	Gun gun;
	AddBox(gun, {0, 0, 0}, {1, 2, 3});
	CHECK(std::fabs(Winding(gun, {0.5f, 1.0f, 1.5f}) - 1.0f) < 1e-3f);
	CHECK(std::fabs(Winding(gun, {1.5f, 1.0f, 1.5f})) < 1e-3f);
	AddBox(gun, {0.5f, 0, 0}, {2, 2, 3}); // overlapping: still inside where either is
	CHECK(Winding(gun, {0.75f, 1.0f, 1.5f}) > 0.5f);
	CHECK(Winding(gun, {1.75f, 1.0f, 1.5f}) > 0.5f);
}

// The middle finger reaching straight ahead, a block right where its middle
// joint would go: the fit bends it out another way, its tip still on the anchor.
static void TestFitAvoidsTheGun()
{
	const Arms arms = Articulate(vmtest::Body());
	const Arm &arm = arms.right;
	Gun gun;
	gun.hold = Inverse(arm.bind[kHand]); // the gun's frame is his skin space
	const Frame hand = arm.bind[kHand];
	const Frame carry = Compose(hand, Inverse(arm.bind[kHand]));
	const int b = DigitBase(kDigitMiddle);
	const Vec3 knuckle = Apply(carry, arm.bind[b].pos);
	const float reach = Length(Sub(arm.bind[b + 1].pos, arm.bind[b].pos)) + Length(Sub(arm.tip[kDigitMiddle], arm.bind[b + 1].pos));
	const Vec3 target = Add(knuckle, Mul(Normal(hand.x), 0.8f * reach));
	gun.anchors.push_back(MakeAnchor("r_middle", target));
	// Where the unfitted pose puts its middle joint.
	Vec3 anchors[kDigits] = {}, none[kDigits] = {};
	bool want[kDigits] = {};
	anchors[kDigitMiddle] = target;
	want[kDigitMiddle] = true;
	Frame out[kArmBones];
	Pose(arm, hand, anchors, want, ArmAngles{}, false, out, none);
	const Vec3 bend = Apply(out[b], arm.bind[b + 1].pos);
	AddBox(gun, Sub(bend, {0.012f, 0.012f, 0.012f}), Add(bend, {0.012f, 0.012f, 0.012f}));
	const char *const names[kDigits] = {"r_index", "r_middle", "r_ring", "r_pinky", "r_thumb"};
	CHECK(Inside(arm, gun, hand, anchors, want, none, kDigitMiddle) > 0); // as it was, into the block
	const Fit fit = FitHand(arm, gun, names, false);
	CHECK(fit.reach[kDigitMiddle] && !fit.reach[kDigitIndex]);
	CHECK(fit.inside[kDigitMiddle] == 0);
	CHECK(Length(Sub(fit.anchor[kDigitMiddle], target)) < 1e-6f); // no slack: the anchor stays
}

// An anchor its finger can't reach clean slides along its slack until it can.
static void TestSlackMovesTheAnchor()
{
	const Arms arms = Articulate(vmtest::Body());
	const Arm &arm = arms.right;
	Gun gun;
	gun.hold = Inverse(arm.bind[kHand]);
	const Frame hand = arm.bind[kHand];
	const int b = DigitBase(kDigitRing);
	const Vec3 knuckle = arm.bind[b].pos;
	const float reach = Length(Sub(arm.bind[b + 1].pos, knuckle)) + Length(Sub(arm.tip[kDigitRing], arm.bind[b + 1].pos));
	// A target inside a block: no bend keeps the tip out of it, till it slides out past the block's face.
	const Vec3 dir = Normal(hand.x), target = Add(knuckle, Mul(dir, 0.7f * reach));
	AddBox(gun, Sub(target, {0.01f, 0.01f, 0.01f}), Add(target, {0.01f, 0.01f, 0.01f}));
	gun.anchors.push_back(MakeAnchor("r_ring", target, Mul(Normal(hand.y), -0.03f)));
	const char *const names[kDigits] = {"r_index", "r_middle", "r_ring", "r_pinky", "r_thumb"};
	const Fit fit = FitHand(arm, gun, names, false);
	CHECK(fit.reach[kDigitRing]);
	CHECK(Length(Sub(fit.anchor[kDigitRing], target)) > 0.009f);
	CHECK(fit.inside[kDigitRing] < 3);
}

int main()
{
	TestWindingInsideAndOut();
	TestFitAvoidsTheGun();
	TestSlackMovesTheAnchor();
	if (gFailures) {
		printf("%d vmfit test(s) failed\n", gFailures);
		return 1;
	}
	printf("all vmfit tests passed\n");
	return 0;
}

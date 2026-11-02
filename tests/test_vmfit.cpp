// Unit tests for fitting his fingers to a gun without going into it: make test
#include "vmfit.h"
#include "vmtestarm.h"

#include <algorithm>
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

// The angle (degrees) at a digit's middle joint as `fit` poses it, and as it rests.
static float MiddleJoint(const Arm &arm, const Frame &hand, const Fit &fit, int digit, bool left, float &rest)
{
	Frame out[kArmBones];
	Pose(arm, hand, fit.anchor, fit.reach, ArmAngles{}, left, out, fit.bend);
	const int b = DigitBase(digit);
	const Frame carry = Compose(hand, Inverse(arm.bind[kHand]));
	auto angle = [](Vec3 k, Vec3 m, Vec3 t) {
		return std::acos(std::max(-1.0f, std::min(1.0f, Dot(Normal(Sub(k, m)), Normal(Sub(t, m)))))) * 57.2958f;
	};
	rest = angle(Apply(carry, arm.bind[b].pos), Apply(carry, arm.bind[b + 1].pos), Apply(carry, arm.tip[digit]));
	return angle(Apply(out[b], arm.bind[b].pos), Apply(out[b], arm.bind[b + 1].pos), Apply(out[b + 1], arm.tip[digit]));
}

// How far `p` is from the box's surface (0 on it or inside).
static float OffBox(Vec3 p, Vec3 lo, Vec3 hi)
{
	const float dx = std::max({lo.x - p.x, 0.0f, p.x - hi.x}), dy = std::max({lo.y - p.y, 0.0f, p.y - hi.y}),
	            dz = std::max({lo.z - p.z, 0.0f, p.z - hi.z});
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// The player's report: his left fingers round a rifle's handguard were a
// mess. Their anchors were low on its far side, nearer his knuckles than his
// fingertips rest, and reaching them folded each finger's middle joint shut
// (the ring finger's to 18 degrees round the M4's), its mesh crushing in on
// itself. Held under a handguard, a finger closes round it as it rests, till
// it meets it: its shape kept, its tip on the guard.
static void TestLeftFingersWrapTheHandguard()
{
	const Arms arms = Articulate(vmtest::Body());
	const Arm &arm = arms.left;
	Gun gun;
	gun.twoHanded = true;
	gun.holdLeft = Inverse(arm.bind[kHand]); // the gun's frame is his skin space
	const Frame hand = arm.bind[kHand];
	// The handguard over his palm, short of his knuckles; his fingers' hooks hang loose round its far side.
	const Vec3 lo = Apply(hand, {0.02f, 0.036f, -0.06f}), hi = Apply(hand, {0.085f, 0.06f, 0.06f});
	AddBox(gun, {std::min(lo.x, hi.x), std::min(lo.y, hi.y), std::min(lo.z, hi.z)},
	       {std::max(lo.x, hi.x), std::max(lo.y, hi.y), std::max(lo.z, hi.z)});
	const Vec3 boxLo{std::min(lo.x, hi.x), std::min(lo.y, hi.y), std::min(lo.z, hi.z)},
	           boxHi{std::max(lo.x, hi.x), std::max(lo.y, hi.y), std::max(lo.z, hi.z)};
	const int b = DigitBase(kDigitMiddle);
	const float z = Apply(Inverse(hand), arm.bind[b].pos).z;
	gun.anchors.push_back(MakeAnchor("l_middle", Apply(hand, {0.093f, 0.056f, z}))); // low on the far side
	const char *const names[kDigits] = {"l_index", "l_middle", "l_ring", "l_pinky", "l_thumb"};
	const Fit fit = FitHand(arm, gun, names, true);
	float rest = 0.0f;
	const float bent = MiddleJoint(arm, hand, fit, kDigitMiddle, true, rest);
	CHECK(fit.reach[kDigitMiddle]);
	CHECK(fit.inside[kDigitMiddle] == 0);
	CHECK(std::fabs(bent - rest) < 15.0f);                               // its shape kept, not folded shut
	CHECK(OffBox(fit.anchor[kDigitMiddle], boxLo, boxHi) < 0.008f);     // its tip on the guard
	CHECK(Length(Sub(fit.anchor[kDigitMiddle], Apply(hand, {0.093f, 0.056f, z}))) > 0.005f); // not where it was told
}

int main()
{
	TestWindingInsideAndOut();
	TestFitAvoidsTheGun();
	TestSlackMovesTheAnchor();
	TestLeftFingersWrapTheHandguard();
	if (gFailures) {
		printf("%d vmfit test(s) failed\n", gFailures);
		return 1;
	}
	printf("all vmfit tests passed\n");
	return 0;
}

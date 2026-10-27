// Unit tests for posing the viewmodel's arm: the hand on the gun, the arm angled off it, fingers to anchors: make test
#include "vmpose.h"
#include "vmtestarm.h"

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

static const Arm &TheArm()
{
	static const Arms arms = Articulate(vmtest::Body());
	return arms.right;
}

static bool Near(Vec3 a, Vec3 b, float eps) { return Length(Sub(a, b)) < eps; }
static bool IsIdentity(const Frame &f)
{
	const Frame i = Identity();
	return Near(f.x, i.x, 1e-4f) && Near(f.y, i.y, 1e-4f) && Near(f.z, i.z, 1e-4f) && Near(f.pos, i.pos, 1e-4f);
}

// A hand somewhere in the world: turned and moved off its bind frame.
static Frame SomeHand(const Arm &arm)
{
	return Compose(TurnAbout(Normal({0.3f, -0.5f, 0.8f}), 1.1f, {0, 0, 0}), arm.bind[kHand]);
}

static Frame Moved(const Frame &f, Vec3 by)
{
	Frame out = f;
	out.pos = Add(out.pos, by);
	return out;
}

static void TestBindPoseIsRest()
{
	const Arm &arm = TheArm();
	const Vec3 anchors[kDigits] = {};
	const bool reach[kDigits] = {};
	Frame out[kArmBones];
	Pose(arm, arm.bind[kHand], anchors, reach, ArmAngles{0.0f, 0.0f, 0.0f, 0.0f}, false, out);
	for (int b = 0; b < kArmBones; b++)
		CHECK(IsIdentity(out[b]));
}

static void TestHandFollowsGun()
{
	const Arm &arm = TheArm();
	Gun gun;
	gun.hold = {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, {0.02f, 0.03f, -0.06f}};
	const Frame gunWorld = Moved(TurnAbout({0, 0, 1}, 0.4f, {0, 0, 0}), {10, 20, 3});
	const Frame hand = HandFor(gun, gunWorld);
	CHECK(Near(Apply(Compose(hand, gun.hold), {0.1f, 0.2f, 0.3f}), Apply(gunWorld, {0.1f, 0.2f, 0.3f}), 1e-4f));
	const Vec3 anchors[kDigits] = {};
	const bool reach[kDigits] = {};
	Frame out[kArmBones];
	Pose(arm, hand, anchors, reach, ArmAngles{}, false, out);
	CHECK(Near(Apply(out[kHand], arm.bind[kHand].pos), hand.pos, 1e-4f));
	CHECK(Near(Rotate(out[kHand], arm.bind[kHand].x), hand.x, 1e-4f));
}

static void TestForearmAngles()
{
	const Arm &arm = TheArm();
	const Frame hand = Moved(SomeHand(arm), {1, 2, 3});
	const Vec3 anchors[kDigits] = {};
	const bool reach[kDigits] = {};
	Frame out[kArmBones];
	const ArmAngles angles{};
	Pose(arm, hand, anchors, reach, angles, false, out);
	const float d = 3.14159265f / 180.0f;
	const Vec3 want = Normal(Sub(Sub(Mul(Normal(hand.x), -1.0f), Mul(Normal(hand.z), std::tan(angles.down * d))),
	                            Mul(Normal(hand.y), std::tan(angles.out * d))));
	const Vec3 elbow = Apply(out[kFore], arm.bind[kFore].pos);
	const float forearm = Length(Sub(arm.bind[kFore].pos, arm.bind[kHand].pos));
	CHECK(Near(Normal(Sub(elbow, hand.pos)), want, 1e-4f));
	CHECK(std::fabs(Length(Sub(elbow, hand.pos)) - forearm) < 1e-4f);
	// The left arm goes out the other way.
	Pose(arm, hand, anchors, reach, angles, true, out);
	const Vec3 leftWant = Normal(Add(Sub(Mul(Normal(hand.x), -1.0f), Mul(Normal(hand.z), std::tan(angles.down * d))),
	                                 Mul(Normal(hand.y), std::tan(angles.out * d))));
	CHECK(Near(Normal(Sub(Apply(out[kFore], arm.bind[kFore].pos), hand.pos)), leftWant, 1e-4f));
}

// Each digit's knuckle, bend and tip where the hand carries them, and its reach.
struct Joints {
	Vec3 knuckle, bend, tip, pole;
	float reach;
};
static Joints Carried(const Arm &arm, const Frame &hand, int d)
{
	const Frame r = Compose(hand, Inverse(arm.bind[kHand]));
	const int base = DigitBase(d);
	Joints g{Apply(r, arm.bind[base].pos), Apply(r, arm.bind[base + 1].pos), Apply(r, arm.tip[d]), Rotate(r, arm.pole[d]), 0};
	g.reach = Length(Sub(g.bend, g.knuckle)) + Length(Sub(g.tip, g.bend));
	return g;
}

static void TestFingertipsReachAnchors()
{
	const Arm &arm = TheArm();
	const Frame hand = SomeHand(arm);
	Vec3 anchors[kDigits];
	bool reach[kDigits];
	for (int d = 0; d < kDigits; d++) {
		const Joints g = Carried(arm, hand, d);
		// Curled in: towards the tip, but only 85% of the finger's length away.
		anchors[d] = Add(g.knuckle, Mul(Normal(Sub(g.tip, g.knuckle)), 0.85f * g.reach));
		reach[d] = true;
	}
	Frame out[kArmBones];
	Pose(arm, hand, anchors, reach, ArmAngles{}, false, out);
	for (int d = 0; d < kDigits; d++)
		CHECK(Near(Apply(out[DigitBase(d) + 1], arm.tip[d]), anchors[d], 0.001f));
}

static void TestFingersBendTheRightWay()
{
	const Arm &arm = TheArm();
	const Frame hand = SomeHand(arm);
	Vec3 anchors[kDigits];
	bool reach[kDigits];
	for (int d = 0; d < kDigits; d++) {
		const Joints g = Carried(arm, hand, d);
		anchors[d] = Add(g.knuckle, Mul(Normal(Sub(g.tip, g.knuckle)), 0.7f * g.reach));
		reach[d] = true;
	}
	Frame out[kArmBones];
	Pose(arm, hand, anchors, reach, ArmAngles{}, false, out);
	for (int d = 0; d < kDigits; d++) {
		const Joints g = Carried(arm, hand, d);
		const Vec3 bend = Apply(out[DigitBase(d)], arm.bind[DigitBase(d) + 1].pos);
		const Vec3 line = Normal(Sub(anchors[d], g.knuckle)), kb = Sub(bend, g.knuckle);
		const Vec3 off = Sub(kb, Mul(line, Dot(kb, line)));
		CHECK(Dot(off, g.pole) > 0.0f);
	}
}

static void TestUnreachableAnchor()
{
	const Arm &arm = TheArm();
	const Frame hand = SomeHand(arm);
	Vec3 anchors[kDigits] = {};
	bool reach[kDigits] = {};
	const Joints g = Carried(arm, hand, kDigitMiddle);
	const Vec3 far = Add(g.knuckle, {1.0f, 0.0f, 0.0f});
	anchors[kDigitMiddle] = far;
	reach[kDigitMiddle] = true;
	Frame out[kArmBones];
	Pose(arm, hand, anchors, reach, ArmAngles{}, false, out);
	const Vec3 tip = Apply(out[DigitBase(kDigitMiddle) + 1], arm.tip[kDigitMiddle]);
	CHECK(std::isfinite(tip.x) && std::isfinite(tip.y) && std::isfinite(tip.z));
	CHECK(Near(Normal(Sub(tip, g.knuckle)), Normal(Sub(far, g.knuckle)), 1e-3f));
	CHECK(std::fabs(Length(Sub(tip, g.knuckle)) - g.reach) < 0.0025f);
}

// A finger reaching straight ahead of its knuckle (along the hand, as the
// index does for the trigger) still bends in its own curl plane, its middle
// joint out on the back of the hand's side, never off sideways or palm-ward.
static void TestReachingStraightAheadKeepsTheCurlPlane()
{
	const Arm &arm = TheArm();
	const Frame hand = SomeHand(arm);
	const Frame toHand = Inverse(hand);
	Vec3 anchors[kDigits] = {};
	bool reach[kDigits] = {};
	for (int d = 0; d < 4; d++) {
		const Joints g = Carried(arm, hand, d);
		anchors[d] = Add(g.knuckle, Mul(Normal(hand.x), 0.8f * g.reach));
		reach[d] = true;
	}
	Frame out[kArmBones];
	Pose(arm, hand, anchors, reach, ArmAngles{}, false, out);
	for (int d = 0; d < 4; d++) {
		const Joints g = Carried(arm, hand, d);
		const Vec3 k = Apply(toHand, g.knuckle), bend = Apply(toHand, Apply(out[DigitBase(d)], arm.bind[DigitBase(d) + 1].pos));
		CHECK(std::fabs(bend.z - k.z) < 0.005f);
		CHECK(bend.y < k.y);
	}
}

// An anchor's bend hint (as a gun gives its trigger finger: along the frame,
// over the guard) puts the middle joint out that way instead.
static void TestBendHintChoosesTheSide()
{
	const Arm &arm = TheArm();
	const Frame hand = SomeHand(arm);
	Vec3 anchors[kDigits] = {}, bends[kDigits] = {};
	bool reach[kDigits] = {};
	const Joints g = Carried(arm, hand, kDigitIndex);
	anchors[kDigitIndex] = Add(g.knuckle, Mul(Normal(hand.x), 0.8f * g.reach));
	reach[kDigitIndex] = true;
	bends[kDigitIndex] = Normal(hand.z); // across the hand: off the curl plane, on purpose
	Frame out[kArmBones];
	Pose(arm, hand, anchors, reach, ArmAngles{}, false, out, bends);
	const Vec3 bend = Apply(out[DigitBase(kDigitIndex)], arm.bind[DigitBase(kDigitIndex) + 1].pos);
	const Vec3 line = Normal(Sub(anchors[kDigitIndex], g.knuckle)), kb = Sub(bend, g.knuckle);
	const Vec3 off = Normal(Sub(kb, Mul(line, Dot(kb, line))));
	CHECK(Dot(off, Normal(hand.z)) > 0.99f);
	CHECK(Length(Sub(Apply(out[DigitBase(kDigitIndex) + 1], arm.tip[kDigitIndex]), anchors[kDigitIndex])) < 0.001f);
}

// A two-handed gun says which way his left forearm runs from the wrist (its
// hand's on the handguard, turned across it, so the hand's own axis can't).
static void TestForearmOverride()
{
	const Arm &arm = TheArm();
	const Frame hand = Moved(SomeHand(arm), {1, 2, 3});
	const Vec3 anchors[kDigits] = {};
	const bool reach[kDigits] = {};
	ArmAngles angles;
	angles.forearm = Normal({-0.3f, -1.0f, -0.5f});
	Frame out[kArmBones];
	Pose(arm, hand, anchors, reach, angles, true, out);
	const Vec3 elbow = Apply(out[kFore], arm.bind[kFore].pos);
	CHECK(Near(Normal(Sub(elbow, hand.pos)), angles.forearm, 1e-4f));
	// And the upper arm likewise.
	angles.upper = Normal({-0.6f, -0.6f, 0.5f});
	Pose(arm, hand, anchors, reach, angles, true, out);
	const Vec3 shoulder = Apply(out[kUpper], arm.bind[kUpper].pos);
	CHECK(Near(Normal(Sub(shoulder, Apply(out[kFore], arm.bind[kFore].pos))), angles.upper, 1e-4f));
}

// The directions Pose works out for an arm, taken from one hand frame and
// given back as overrides, pose it the same: so the arm can follow the gun as
// framed while only the hand follows its kick, tilt and sway.
static void TestArmDirectionsAsPose()
{
	const Arm &arm = TheArm();
	const Frame hand = Moved(SomeHand(arm), {1, 2, 3});
	const Vec3 anchors[kDigits] = {};
	const bool reach[kDigits] = {};
	for (bool left : {false, true}) {
		Frame plain[kArmBones], overridden[kArmBones];
		Pose(arm, hand, anchors, reach, ArmAngles{}, left, plain);
		ArmAngles angles;
		ArmDirections(hand, ArmAngles{}, left, angles.forearm, angles.upper);
		Pose(arm, hand, anchors, reach, angles, left, overridden);
		for (int b : {kUpper, kFore})
			CHECK(Near(Apply(plain[b], arm.bind[kUpper].pos), Apply(overridden[b], arm.bind[kUpper].pos), 1e-4f));
	}
}

int main()
{
	TestArmDirectionsAsPose();
	TestForearmOverride();
	TestBendHintChoosesTheSide();
	TestReachingStraightAheadKeepsTheCurlPlane();
	TestBindPoseIsRest();
	TestHandFollowsGun();
	TestForearmAngles();
	TestFingertipsReachAnchors();
	TestFingersBendTheRightWay();
	TestUnreachableAnchor();
	if (gFailures) {
		printf("%d vmpose test(s) failed\n", gFailures);
		return 1;
	}
	printf("all vmpose tests passed\n");
	return 0;
}

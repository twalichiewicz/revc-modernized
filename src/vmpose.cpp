#include "vmpose.h"

#include <algorithm>
#include <cmath>

namespace vm {
namespace {

constexpr float kDeg = 3.14159265f / 180.0f;
constexpr float kSlack = 0.001f; // m: a finger never quite straight, nor folded flat

// Two joints, base at `knuckle` and the next at `bend`, swung so the tip
// (where the carried pose has it at `tip`) reaches `target`. A finger bends
// about its `hinge` (the hand's z: it curls from along the hand towards the
// palm), so its middle joint stands out on the back of its curl, whichever
// way it reaches; the thumb (no hinge) bends out to its `pole` side. A
// `hint` (nonzero) overrules both. Returns the base's turn and the tip's
// (both about points).
void Reach(Vec3 knuckle, Vec3 bend, Vec3 tip, Vec3 pole, Vec3 hinge, Vec3 hint, Vec3 target, Frame &base, Frame &end)
{
	const float l1 = Length(Sub(bend, knuckle)), l2 = Length(Sub(tip, bend));
	const Vec3 dir = Normal(Sub(target, knuckle));
	const float d = std::clamp(Length(Sub(target, knuckle)), std::fabs(l1 - l2) + kSlack, l1 + l2 - kSlack);
	Vec3 side = Sub(hint, Mul(dir, Dot(hint, dir)));
	if (Length(side) < 0.2f * std::max(Length(hint), 1e-6f))
		side = Cross(dir, hinge);
	if (Length(side) < 0.2f) // no hinge, or reaching along it: out to the pole's side
		side = Sub(pole, Mul(dir, Dot(pole, dir)));
	if (Length(side) < 1e-6f) { // and the pole along the line: the bend's own side
		const Vec3 kb = Sub(bend, knuckle);
		side = Sub(kb, Mul(dir, Dot(kb, dir)));
	}
	side = Normal(side);
	const float c = std::clamp((l1 * l1 + d * d - l2 * l2) / (2.0f * l1 * d), -1.0f, 1.0f);
	const Vec3 newBend = Add(knuckle, Add(Mul(dir, l1 * c), Mul(side, l1 * std::sqrt(1.0f - c * c))));
	base = Swing(Sub(bend, knuckle), Sub(newBend, knuckle), knuckle);
	const Vec3 swungTip = Apply(base, tip);
	end = Compose(Swing(Sub(swungTip, newBend), Sub(Add(knuckle, Mul(dir, d)), newBend), newBend), base);
}

} // namespace

void ArmDirections(const Frame &hand, const ArmAngles &angles, bool left, Vec3 &forearm, Vec3 &upper)
{
	const Vec3 hx = Normal(hand.x), hy = Normal(hand.y), hz = Normal(hand.z);
	const float outward = left ? 1.0f : -1.0f; // away from his middle: -y on the right hand
	forearm = Length(angles.forearm) > 0.0f ? Normal(angles.forearm)
	                                        : Normal(Add(Sub(Mul(hx, -1.0f), Mul(hz, std::tan(angles.down * kDeg))),
	                                                     Mul(hy, outward * std::tan(angles.out * kDeg))));
	upper = Length(angles.upper) > 0.0f ? Normal(angles.upper)
	                                    : Normal(Add(Add(forearm, Mul(hy, outward * angles.upperOut)), Mul(hz, angles.upperUp)));
}

void Pose(const Arm &arm, const Frame &handWorld, const Vec3 anchors[kDigits], const bool reach[kDigits],
          const ArmAngles &angles, bool left, Frame out[kArmBones], const Vec3 *bends)
{
	// Everything carried with the hand as his bind pose has it.
	const Frame carry = Compose(handWorld, Inverse(arm.bind[kHand]));
	for (int b = 0; b < kArmBones; b++)
		out[b] = carry;
	const Vec3 hz = Normal(handWorld.z);
	const Vec3 wrist = handWorld.pos;
	// The forearm back from the wrist, down and out a little; the upper arm back from the elbow, out and up: past the camera.
	Vec3 back, upper;
	ArmDirections(handWorld, angles, left, back, upper);
	out[kFore] = Compose(Swing(Sub(Apply(carry, arm.bind[kFore].pos), wrist), back, wrist), carry);
	const Vec3 elbow = Apply(out[kFore], arm.bind[kFore].pos);
	out[kUpper] = Compose(Swing(Sub(Apply(out[kFore], arm.bind[kUpper].pos), elbow), upper, elbow), out[kFore]);
	// The fingers and thumb to their anchors.
	for (int d = 0; d < kDigits; d++) {
		if (!reach[d])
			continue;
		const int b = DigitBase(d);
		Frame base, end;
		const Vec3 hinge = d == kDigitThumb ? Vec3{0, 0, 0} : hz;
		Reach(Apply(carry, arm.bind[b].pos), Apply(carry, arm.bind[b + 1].pos), Apply(carry, arm.tip[d]),
		      Rotate(carry, arm.pole[d]), hinge, bends ? bends[d] : Vec3{0, 0, 0}, anchors[d], base, end);
		out[b] = Compose(base, carry);
		out[b + 1] = Compose(end, carry);
	}
}

} // namespace vm

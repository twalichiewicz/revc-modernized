#include "vmfit.h"

#include <cmath>
#include <cstring>

namespace vm {
namespace {

constexpr float kSlackSteps[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
// What a vertex inside the gun costs against turning the middle joint (a
// degree) and sliding the anchor (all of its slack): a pad's vertex or two
// on what it presses is better than a finger swung round the far side.
constexpr float kInsideCost = 10.0f, kTurnCost = 0.5f, kSlideCost = 10.0f;
constexpr float kTurns[] = {0.0f, 20.0f, -20.0f, 40.0f, -40.0f, 60.0f, -60.0f, 90.0f, -90.0f}; // degrees
// His left hand closing round what it holds: each digit turned whole about its
// knuckle from this open (degrees, from as it rests) towards this closed, in
// these steps, then to the degree.
constexpr float kOpen = -60.0f, kClosed = 90.0f, kStep = 4.0f;
constexpr Vec3 kPalm{0.045f, 0.03f, 0.0f}; // his palm's middle, in his hand bone's frame (kit.py's PALM_IN_HAND)

// The side a digit's middle joint goes out to unless told otherwise (as Reach in vmpose.cpp has it).
Vec3 PlainSide(const Arm &arm, const Frame &hand, int digit, Vec3 anchor, Vec3 hint)
{
	const Frame carry = Compose(hand, Inverse(arm.bind[kHand]));
	const Vec3 knuckle = Apply(carry, arm.bind[DigitBase(digit)].pos), dir = Normal(Sub(anchor, knuckle));
	Vec3 side = Sub(hint, Mul(dir, Dot(hint, dir)));
	if (Length(side) < 0.2f * std::max(Length(hint), 1e-6f))
		side = digit == kDigitThumb ? Vec3{0, 0, 0} : Cross(dir, Normal(hand.z));
	if (Length(side) < 0.2f) {
		const Vec3 pole = Rotate(carry, arm.pole[digit]);
		side = Sub(pole, Mul(dir, Dot(pole, dir)));
	}
	return Normal(side);
}

// A digit as it rests (the hand at `hand`), turned `deg` about `axis` through
// its knuckle: where its tip goes (the anchor) and its middle joint's side.
void Turned(const Arm &arm, const Frame &hand, int digit, Vec3 axis, float deg, Vec3 &tip, Vec3 &side)
{
	const Frame carry = Compose(hand, Inverse(arm.bind[kHand]));
	const int b = DigitBase(digit);
	const Vec3 knuckle = Apply(carry, arm.bind[b].pos);
	const Frame turn = TurnAbout(axis, deg * 3.14159265f / 180.0f, knuckle);
	const Vec3 bend = Apply(turn, Apply(carry, arm.bind[b + 1].pos));
	tip = Apply(turn, Apply(carry, arm.tip[digit]));
	const Vec3 dir = Normal(Sub(tip, knuckle)), off = Sub(bend, knuckle);
	side = Sub(off, Mul(dir, Dot(off, dir)));
	side = Length(side) > 1e-3f ? Normal(side) : Normal(Rotate(turn, Rotate(carry, arm.pole[digit])));
}

// His left hand on a handguard (or whatever it holds): a digit as it rests,
// closed round it whole, turned about its knuckle the way it curls (the
// thumb: towards its anchor) from open till it meets the gun, and held just
// short of that. Its shape is kept: reaching an anchor nearer his knuckles
// than his fingertips rest, two joints can only fold the middle one shut (the
// ring finger to 18 degrees round the M4's handguard), its mesh crushing in on
// itself. False if it's in the gun even open, or meets nothing.
bool Wrap(const Arm &arm, const Gun &gun, const Frame &hand, int digit, Vec3 anchor, Vec3 &tip, Vec3 &side, int &inside)
{
	const Frame carry = Compose(hand, Inverse(arm.bind[kHand]));
	const int b = DigitBase(digit);
	const Vec3 knuckle = Apply(carry, arm.bind[b].pos), rest = Apply(carry, arm.tip[digit]);
	// A finger curls about the hand's hinge (its z, as Pose bends it), closing towards his palm; the thumb
	// (straight) turns towards its anchor.
	Vec3 axis = digit == kDigitThumb ? Cross(Sub(rest, knuckle), Sub(anchor, knuckle)) : Normal(hand.z);
	if (Length(axis) < 1e-6f)
		return false;
	axis = Normal(axis);
	const Vec3 palm = Apply(hand, kPalm);
	if (digit != kDigitThumb && Dot(Cross(axis, Sub(rest, knuckle)), Sub(palm, rest)) < 0.0f)
		axis = Mul(axis, -1.0f);
	Vec3 anchors[kDigits] = {}, bends[kDigits] = {};
	bool reach[kDigits] = {};
	reach[digit] = true;
	auto in = [&](float deg) {
		Turned(arm, hand, digit, axis, deg, anchors[digit], bends[digit]);
		return Inside(arm, gun, hand, anchors, reach, bends, digit, true);
	};
	if (in(kOpen) > 0)
		return false;
	float clear = kOpen, deg = kOpen + kStep;
	for (; deg <= kClosed && in(deg) == 0; deg += kStep)
		clear = deg;
	if (deg > kClosed)
		return false; // nothing to close on
	for (float fine = clear + 1.0f; fine < deg && in(fine) == 0; fine += 1.0f)
		clear = fine;
	Turned(arm, hand, digit, axis, clear, tip, side);
	anchors[digit] = tip;
	bends[digit] = side;
	inside = Inside(arm, gun, hand, anchors, reach, bends, digit, true);
	return true;
}

} // namespace

float Winding(const Gun &gun, Vec3 p)
{
	double total = 0.0;
	for (size_t i = 0; i + 2 < gun.indices.size(); i += 3) {
		const Vec3 a = Sub(gun.vertices[gun.indices[i]].pos, p), b = Sub(gun.vertices[gun.indices[i + 1]].pos, p),
		           c = Sub(gun.vertices[gun.indices[i + 2]].pos, p);
		const double la = Length(a), lb = Length(b), lc = Length(c);
		const double det = Dot(a, Cross(b, c));
		const double div = la * lb * lc + Dot(a, b) * lc + Dot(b, c) * la + Dot(c, a) * lb;
		total += 2.0 * std::atan2(det, div);
	}
	return float(total / (4.0 * 3.14159265358979));
}

int Inside(const Arm &arm, const Gun &gun, const Frame &hand, const Vec3 anchors[kDigits], const bool reach[kDigits],
           const Vec3 bends[kDigits], int digit, bool left)
{
	Frame out[kArmBones];
	Pose(arm, hand, anchors, reach, ArmAngles{}, left, out, bends);
	const int base = DigitBase(digit);
	int inside = 0;
	for (const SkinVertex &v : arm.vertices) {
		if (v.bone[0] != base && v.bone[0] != base + 1)
			continue;
		Vec3 p{0, 0, 0};
		for (int k = 0; k < 4; k++)
			if (v.weight[k] > 0.0f)
				p = Add(p, Mul(Apply(out[v.bone[k]], v.pos), v.weight[k]));
		inside += Winding(gun, p) > 0.5f;
	}
	return inside;
}

Fit FitHand(const Arm &arm, const Gun &gun, const char *const names[kDigits], bool left)
{
	Fit fit{};
	const Frame hand = left ? Inverse(gun.holdLeft) : HandFor(gun, Identity()); // in the gun's frame
	for (int d = 0; d < kDigits; d++) {
		const Anchor *a = FindAnchor(gun, names[d]);
		fit.reach[d] = a != nullptr;
		fit.part[d] = a ? a->part : kNoPart;
		if (!a)
			continue;
		if (left && Wrap(arm, gun, hand, d, a->pos, fit.anchor[d], fit.bend[d], fit.inside[d]))
			continue;
		// Tried one digit at a time, the others resting.
		Vec3 anchors[kDigits] = {}, bends[kDigits] = {};
		bool reach[kDigits] = {};
		reach[d] = true;
		const bool slack = Length(a->slack) > 0.0f;
		float best = 1e30f;
		for (float s : kSlackSteps) {
			if (s > 0.0f && !slack)
				break;
			anchors[d] = Add(a->pos, Mul(a->slack, s));
			const Vec3 side = PlainSide(arm, hand, d, anchors[d], a->bend);
			const Frame carry = Compose(hand, Inverse(arm.bind[kHand]));
			const Vec3 dir = Normal(Sub(anchors[d], Apply(carry, arm.bind[DigitBase(d)].pos)));
			for (float turn : kTurns) {
				bends[d] = Rotate(TurnAbout(dir, turn * 3.14159265f / 180.0f, {0, 0, 0}), side);
				const int inside = Inside(arm, gun, hand, anchors, reach, bends, d, left);
				const float score = float(inside) * kInsideCost + s * kSlideCost + std::fabs(turn) * kTurnCost;
				if (score < best) {
					best = score;
					fit.anchor[d] = anchors[d];
					fit.bend[d] = bends[d];
					fit.inside[d] = inside;
				}
			}
			if (best < kInsideCost)
				break; // clean: no need to slide it further
		}
	}
	return fit;
}

} // namespace vm

#include "vmmelee.h"

#include <algorithm>
#include <cmath>

namespace vm {
namespace {

constexpr float kWindUp = 0.7f;      // of the time to the hit spent drawing back (the rest held, cocked)
constexpr float kInto = 0.07f;       // s: from cocked through the hit
constexpr float kFollow = 0.14f;     // s after the hit: followed through
constexpr float kBack = 0.3f;        // s after that: ready again
constexpr float kSettle = 20.0f;     // rad/s: a jump in the hand's way fades in about a quarter second
constexpr float kBuzz = 0.004f, kBuzzRate = 6.0f; // m: the chainsaw shaking as it cuts
constexpr Vec3 kShoulder{0.19f, -0.06f, -0.24f};  // m: his right shoulder from the eye
constexpr Vec3 kElbowOut{0.6f, -0.2f, -1.0f};       // the way his elbow goes: out and down

// A swing's grips: drawn back (cocked), at the hit, and followed through.
struct Arc {
	Grip cocked, hit, through;
};

float Smooth(float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}
float EaseIn(float t) // quickening: into the hit
{
	t = std::clamp(t, 0.0f, 1.0f);
	return t * t;
}
float EaseOut(float t) // slowing: out of it
{
	t = std::clamp(t, 0.0f, 1.0f);
	return 1.0f - (1.0f - t) * (1.0f - t);
}

// The kinds' grips (m; directions; tuned by render with vcmm-viewmodel
// --melee). Ready, then each swing: the first from up on his right down
// across to his left; a knife's or blade's second (a combo) back the other
// way and its third over the top; a club's, bat's or chainsaw's second at
// someone on the ground (the game's GROUND_2ND); a fist's jab, hook and
// uppercut.
struct Moves {
	Grip ready;
	Arc swings[3];
};
const Moves &MovesFor(MeleeKind kind)
{
	static const Moves kMoves[] = {
	    // Knuckles: a fist up on the right, the duster turned to show; a jab straight out, a hook across, an uppercut.
	    {{{0.15f, 0.32f, -0.09f}, {-0.7f, 0.4f, 0.6f}, {-0.2f, 0.6f, -0.8f}},
	     {{{{0.17f, 0.24f, -0.11f}, {-0.7f, 0.4f, 0.6f}, {-0.2f, 0.6f, -0.8f}},
	       {{0.06f, 0.52f, -0.04f}, {-0.6f, 0.3f, 0.75f}, {-0.1f, 0.8f, -0.6f}},
	       {{0.05f, 0.50f, -0.05f}, {-0.6f, 0.3f, 0.75f}, {-0.1f, 0.8f, -0.6f}}},
	      {{{0.26f, 0.26f, -0.08f}, {-0.5f, 0.5f, 0.7f}, {-0.5f, 0.6f, -0.6f}},
	       {{0.00f, 0.42f, -0.04f}, {-0.3f, 0.2f, 0.95f}, {-0.9f, 0.4f, -0.2f}},
	       {{-0.08f, 0.36f, -0.07f}, {-0.2f, 0.1f, 1.0f}, {-1.0f, 0.2f, -0.1f}}},
	      {{{0.12f, 0.26f, -0.24f}, {-0.7f, 0.4f, 0.6f}, {-0.2f, 0.9f, -0.3f}},
	       {{0.05f, 0.40f, 0.00f}, {-0.6f, 0.2f, 0.75f}, {-0.1f, 0.4f, 0.9f}},
	       {{0.05f, 0.36f, 0.04f}, {-0.6f, 0.2f, 0.75f}, {-0.1f, 0.4f, 0.9f}}}}},
	    // Knife: low on the right, its point in towards the middle; a stab, a slash across, a stab down.
	    {{{0.18f, 0.40f, -0.18f}, {-0.55f, 1.0f, 0.3f}, {0.0f, -0.2f, 1.0f}},
	     {{{{0.20f, 0.28f, -0.19f}, {-0.45f, 1.0f, 0.3f}, {0.0f, -0.2f, 1.0f}},
	       {{0.07f, 0.58f, -0.10f}, {-0.3f, 1.0f, 0.1f}, {0.0f, -0.1f, 1.0f}},
	       {{0.06f, 0.56f, -0.11f}, {-0.3f, 1.0f, 0.05f}, {0.0f, -0.1f, 1.0f}}},
	      {{{0.28f, 0.30f, -0.12f}, {0.6f, 0.6f, 0.4f}, {0.0f, -0.4f, 1.0f}},
	       {{0.04f, 0.46f, -0.12f}, {-0.8f, 0.8f, 0.0f}, {0.0f, 0.0f, 1.0f}},
	       {{-0.12f, 0.34f, -0.18f}, {-1.0f, 0.2f, -0.2f}, {0.0f, 0.2f, 1.0f}}},
	      {{{0.14f, 0.26f, 0.02f}, {-0.2f, 0.6f, -1.0f}, {0.0f, 1.0f, 0.3f}},
	       {{0.06f, 0.46f, -0.14f}, {-0.2f, 0.8f, -1.0f}, {0.0f, 1.0f, 0.5f}},
	       {{0.05f, 0.40f, -0.26f}, {-0.2f, 0.5f, -1.0f}, {0.0f, 1.0f, 0.4f}}}}},
	    // A blade: up on the right, leaning in; slashed down across, back across, chopped down.
	    {{{0.18f, 0.36f, -0.15f}, {-0.3f, 0.65f, 0.7f}, {-0.3f, 0.8f, -0.5f}},
	     {{{{0.26f, 0.22f, -0.04f}, {0.35f, -0.45f, 0.8f}, {-0.1f, 0.5f, 1.0f}},
	       {{0.03f, 0.46f, -0.12f}, {-0.9f, 0.5f, -0.15f}, {0.0f, 1.0f, 0.0f}},
	       {{-0.12f, 0.34f, -0.26f}, {-0.6f, -0.1f, -0.8f}, {0.0f, 1.0f, -0.3f}}},
	      {{{-0.10f, 0.32f, -0.16f}, {-0.9f, 0.1f, 0.4f}, {-0.2f, 1.0f, 0.0f}},
	       {{0.10f, 0.46f, -0.10f}, {0.9f, 0.5f, 0.1f}, {0.0f, 1.0f, 0.0f}},
	       {{0.28f, 0.32f, -0.12f}, {0.8f, -0.2f, -0.3f}, {0.2f, 1.0f, 0.0f}}},
	      {{{0.12f, 0.22f, 0.06f}, {0.0f, -0.6f, 1.0f}, {0.0f, 0.6f, 1.0f}},
	       {{0.04f, 0.46f, -0.10f}, {0.0f, 1.0f, 0.1f}, {0.0f, -0.1f, 1.0f}},
	       {{0.04f, 0.36f, -0.30f}, {0.0f, 0.5f, -0.85f}, {0.0f, 0.8f, 0.6f}}}}},
	    // A club: as a blade (leaning in less: a hammer's head off the crosshair), its second (on someone
	    // down) brought down in front of him.
	    {{{0.19f, 0.36f, -0.15f}, {-0.18f, 0.6f, 0.78f}, {-0.3f, 0.8f, -0.5f}},
	     {{{{0.22f, 0.26f, -0.06f}, {0.3f, -0.4f, 0.85f}, {-0.1f, 0.5f, 1.0f}},
	       {{0.03f, 0.46f, -0.12f}, {-0.9f, 0.5f, -0.15f}, {0.0f, 1.0f, 0.0f}},
	       {{-0.12f, 0.34f, -0.26f}, {-0.6f, -0.1f, -0.8f}, {0.0f, 1.0f, -0.3f}}},
	      {{{0.20f, 0.30f, 0.02f}, {0.1f, -0.5f, 1.0f}, {0.0f, 0.6f, 1.0f}},
	       {{0.06f, 0.48f, -0.16f}, {-0.1f, 0.8f, -0.6f}, {0.0f, 0.6f, 0.8f}},
	       {{0.05f, 0.38f, -0.30f}, {-0.1f, 0.3f, -1.0f}, {0.0f, 1.0f, 0.3f}}},
	      {{{-0.10f, 0.32f, -0.16f}, {-0.9f, 0.1f, 0.4f}, {-0.2f, 1.0f, 0.0f}},
	       {{0.10f, 0.46f, -0.10f}, {0.9f, 0.5f, 0.1f}, {0.0f, 1.0f, 0.0f}},
	       {{0.28f, 0.32f, -0.12f}, {0.8f, -0.2f, -0.3f}, {0.2f, 1.0f, 0.0f}}}}},
	    // Long (bat, golf club): cocked over his right shoulder; swung flat across, brought down on
	    // someone on the ground, and back across.
	    {{{0.26f, 0.40f, -0.24f}, {0.45f, -0.25f, 0.85f}, {-0.3f, 1.0f, 0.0f}},
	     {{{{0.30f, 0.26f, -0.12f}, {0.6f, -0.7f, 0.4f}, {-0.2f, 0.4f, 1.0f}},
	       {{0.04f, 0.46f, -0.18f}, {-1.0f, 0.6f, 0.05f}, {0.0f, 1.0f, 0.0f}},
	       {{-0.16f, 0.32f, -0.26f}, {-0.8f, -0.3f, -0.3f}, {0.2f, 1.0f, 0.0f}}},
	      {{{0.22f, 0.28f, 0.00f}, {0.2f, -0.6f, 0.8f}, {0.0f, 0.6f, 1.0f}},
	       {{0.06f, 0.48f, -0.18f}, {-0.1f, 0.8f, -0.6f}, {0.0f, 0.6f, 0.8f}},
	       {{0.05f, 0.38f, -0.32f}, {-0.1f, 0.3f, -1.0f}, {0.0f, 1.0f, 0.3f}}},
	      {{{-0.14f, 0.30f, -0.18f}, {-1.0f, 0.0f, 0.2f}, {0.0f, 1.0f, 0.0f}},
	       {{0.10f, 0.46f, -0.14f}, {1.0f, 0.6f, 0.05f}, {0.0f, 1.0f, 0.0f}},
	       {{0.30f, 0.28f, -0.20f}, {0.8f, -0.3f, -0.1f}, {0.0f, 1.0f, 0.0f}}}}},
	    // Chainsaw: low at his hip, its bar running in towards the middle; thrust in.
	    {{{0.28f, 0.06f, -0.26f}, {0.215f, 0.512f, 0.836f}, {-0.53f, 0.78f, -0.34f}},
	     {{{{0.29f, 0.02f, -0.27f}, {0.215f, 0.512f, 0.836f}, {-0.53f, 0.78f, -0.34f}},
	       {{0.22f, 0.20f, -0.22f}, {0.2f, 0.5f, 0.84f}, {-0.45f, 0.82f, -0.3f}},
	       {{0.22f, 0.20f, -0.22f}, {0.2f, 0.5f, 0.84f}, {-0.45f, 0.82f, -0.3f}}},
	      {{{0.29f, 0.02f, -0.27f}, {0.215f, 0.512f, 0.836f}, {-0.53f, 0.78f, -0.34f}},
	       {{0.22f, 0.20f, -0.22f}, {0.2f, 0.5f, 0.84f}, {-0.45f, 0.82f, -0.3f}},
	       {{0.22f, 0.20f, -0.22f}, {0.2f, 0.5f, 0.84f}, {-0.45f, 0.82f, -0.3f}}},
	      {{{0.29f, 0.02f, -0.27f}, {0.215f, 0.512f, 0.836f}, {-0.53f, 0.78f, -0.34f}},
	       {{0.22f, 0.20f, -0.22f}, {0.2f, 0.5f, 0.84f}, {-0.45f, 0.82f, -0.3f}},
	       {{0.22f, 0.20f, -0.22f}, {0.2f, 0.5f, 0.84f}, {-0.45f, 0.82f, -0.3f}}}}},
	};
	return kMoves[int(kind)];
}

} // namespace

bool IsMelee(int weapon) { return weapon >= 1 && weapon <= 11; }

MeleeKind MeleeKindFor(int weapon)
{
	switch (weapon) {
	case 1: return MeleeKind::kKnuckles;
	case 2: case 5: return MeleeKind::kKnife;       // screwdriver, knife
	case 8: case 9: case 10: return MeleeKind::kBlade; // cleaver, machete, katana
	case 3: case 6: return MeleeKind::kLong;        // golf club, bat
	case 11: return MeleeKind::kChainsaw;
	default: return MeleeKind::kClub;               // nightstick, hammer
	}
}

Frame GripFrame(const Grip &g)
{
	const Vec3 z = Normal(g.along);
	Vec3 x = Sub(g.knuckles, Mul(z, Dot(g.knuckles, z)));
	x = Length(x) > 1e-4f ? Normal(x) : Normal(Cross({0, 0, 1}, z));
	return {x, Cross(z, x), z, g.wrist};
}

Grip Mix(const Grip &a, const Grip &b, float t)
{
	return {Lerp(a.wrist, b.wrist, t), Normal(Lerp(Normal(a.along), Normal(b.along), t)),
	        Normal(Lerp(Normal(a.knuckles), Normal(b.knuckles), t))};
}

Grip ReadyGrip(MeleeKind kind) { return MovesFor(kind).ready; }

void ArmTo(Vec3 wrist, float forearm, float upperArm, Vec3 &foreWay, Vec3 &upperWay)
{
	const Vec3 reach = Sub(wrist, kShoulder);
	const Vec3 along = Normal(reach);
	const float l1 = upperArm, l2 = forearm;
	const float d = std::clamp(Length(reach), std::fabs(l1 - l2) + 0.01f, l1 + l2 - 0.002f);
	const float c = std::clamp((l1 * l1 + d * d - l2 * l2) / (2.0f * l1 * d), -1.0f, 1.0f);
	Vec3 out = Sub(Normal(kElbowOut), Mul(along, Dot(Normal(kElbowOut), along)));
	out = Length(out) > 1e-4f ? Normal(out) : Vec3{1, 0, 0};
	const Vec3 elbow = Add(kShoulder, Add(Mul(along, l1 * c), Mul(out, l1 * std::sqrt(1.0f - c * c))));
	foreWay = Normal(Sub(elbow, wrist));
	upperWay = Normal(Sub(kShoulder, elbow));
}

Grip AttackGrip(MeleeKind kind, const Attack &attack)
{
	const Moves &m = MovesFor(kind);
	if (attack.which < 1 || attack.which > 3)
		return m.ready;
	const Arc &a = m.swings[attack.which - 1];
	const float t = attack.time, hit = std::max(attack.strike, kInto + 0.02f);
	const float cocked = hit - kInto; // drawn back by here, then through
	if (t < cocked)
		return Mix(m.ready, a.cocked, Smooth(t / (cocked * kWindUp)));
	if (t < hit)
		return Mix(a.cocked, a.hit, EaseIn((t - cocked) / kInto));
	if (t < hit + kFollow)
		return Mix(a.hit, a.through, EaseOut((t - hit) / kFollow));
	return Mix(a.through, m.ready, Smooth((t - hit - kFollow) / kBack));
}

Grip MeleeHand::Update(MeleeKind kind, const Attack &attack, float dt)
{
	const Grip to = AttackGrip(kind, attack);
	// A new swing, or the same one started over (the game loops it while the
	// button's held), or one cut short: what's left of the hand's way from
	// where it was fades (critically damped), the swing itself played as it is.
	const bool jumped = attack.which != last_.which || attack.time < last_.time - 1e-4f;
	const bool first = !started_;
	if (first) {
		gap_ = gapSpeed_ = Grip{};
		started_ = true;
	} else if (jumped) {
		gap_ = {Sub(at_.wrist, to.wrist), Sub(at_.along, Normal(to.along)), Sub(at_.knuckles, Normal(to.knuckles))};
		gapSpeed_ = speed_;
	} else if (dt > 0.0f) {
		const float w = kSettle, c = 2.0f * w;
		const int steps = int(std::ceil(dt * 240.0f));
		const float h = dt / float(steps);
		auto fade = [&](Vec3 &x, Vec3 &v) {
			for (int i = 0; i < steps; i++) {
				v = Add(v, Mul(Sub(Mul(x, -w * w), Mul(v, c)), h));
				x = Add(x, Mul(v, h));
			}
		};
		fade(gap_.wrist, gapSpeed_.wrist);
		fade(gap_.along, gapSpeed_.along);
		fade(gap_.knuckles, gapSpeed_.knuckles);
	}
	last_ = attack;
	const Grip was = at_;
	at_ = {Add(to.wrist, gap_.wrist), Normal(Add(Normal(to.along), gap_.along)), Normal(Add(Normal(to.knuckles), gap_.knuckles))};
	if (first)
		speed_ = Grip{};
	else if (dt > 0.0f)
		speed_ = {Mul(Sub(at_.wrist, was.wrist), 1.0f / dt), Mul(Sub(at_.along, was.along), 1.0f / dt),
		          Mul(Sub(at_.knuckles, was.knuckles), 1.0f / dt)};
	Grip out = at_;
	if (kind == MeleeKind::kChainsaw) { // it shakes running, more as it cuts
		buzz_ += ((attack.which > 0 ? 1.0f : 0.25f) - buzz_) * std::min(1.0f, kBuzzRate * dt);
		phase_ = std::fmod(phase_ + dt * 60.0f, 1000.0f);
		const Vec3 shake{std::sin(phase_ * 1.7f), std::sin(phase_ * 2.3f), std::sin(phase_ * 3.1f)};
		out.wrist = Add(out.wrist, Mul(shake, kBuzz * buzz_));
	}
	return out;
}

} // namespace vm

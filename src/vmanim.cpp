#include "vmanim.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace vm {
namespace {

constexpr float kDrawTime = 0.35f, kHolsterTime = 0.2f;
constexpr float kDrawDrop = 0.25f, kDrawTurn = -35.0f;   // m below, degrees down, at the start of a draw
constexpr float kKickBack = 0.01f, kKickTurn = 1.5f, kKickFade = 14.0f; // m, degrees, per second (small: his body takes most, the view jolts)
constexpr float kSlideTravel = 0.022f, kSlideHeld = 0.015f, kSlideHome = 0.05f; // m; back the frame it fires, held, then home (s)
constexpr float kFlashTime = 0.05f;
constexpr float kTriggerTravel = 0.004f, kTriggerRate = 40.0f;
constexpr float kSprintRate = 6.0f, kSprintOutRate = 18.0f; // in, and out quickly (a shot leaves from a gun that's up)
// Sprinting, by kind: the carry (m; degrees up, over to the right, muzzle
// left) and its swing once a stride (`swing`, sin) and each step (`bounce`,
// 0 at a footfall to -1 between).
struct CarryPose {
	Move held;
	Move swing;
	Vec3 bounce;
	float bouncePitch;
};
constexpr CarryPose kCarries[] = {
    // One-handed (a pistol or SMG): up forward of his ear, the muzzle near straight up, at the view's right edge
    // (tuned by render: pulled in closer it filled the view); it bobs with him.
    {{{0.09f, 0.0f, 0.10f}, 80.0f, 15.0f, -10.0f}, {{0.0f, 0.008f, 0.0f}, 3.0f, 2.0f, 0.0f}, {0.0f, 0.0f, 0.015f}, 4.0f},
    // Two-handed: up across his chest, canted hard in; swaying a stride, bouncing a step.
    {{{-0.06f, -0.02f, 0.03f}, 5.0f, -40.0f, 35.0f}, {{0.02f, 0.0f, 0.0f}, 0.0f, 4.0f, 4.0f}, {0.0f, 0.0f, 0.015f}, 2.0f},
    // Heavy: at the hip, a little across and in; half the swing.
    {{{-0.03f, 0.0f, 0.0f}, -5.0f, -12.0f, 15.0f}, {{0.01f, 0.0f, 0.0f}, 0.0f, 2.0f, 2.0f}, {0.0f, 0.0f, 0.008f}, 1.0f},
};
constexpr float kTiltRoll = 25.0f, kTiltPitch = 15.0f, kTiltLift = 0.06f, kTiltIn = 0.06f; // reloading: up and in, the magazine well to the eye
constexpr float kMagazineTravel = 0.12f, kMagazineAway = 0.3f; // m: out of the well; then away out of view
// Through a reload (0..1): the hand to the magazine, the magazine out, away,
// swapped (out of view), back up, in; the hand back.
constexpr float kHandTo = 0.1f, kOutFrom = 0.18f, kOut = 0.3f, kAway = 0.38f, kSwapped = 0.42f, kUp = 0.52f,
                kIn = 0.65f, kHandBack = 0.7f, kHandHome = 0.8f;
constexpr float kPumpTravel = 0.07f, kPumpWait = 0.03f, kPumpBack = 0.1f, kPumpHome = 0.15f; // m, s
constexpr float kBobRun = 4.5f, kBobMost = 2.2f;          // m/s the bob's sized for; its most, sprinting
constexpr float kBobSide = 0.012f, kBobDip = 0.01f, kBobPitch = 1.5f, kBobRoll = 2.0f, kBobYaw = 1.0f; // m, degrees, running
constexpr float kChamber = 60.0f, kCylinderRate = 30.0f;  // degrees a shot; how fast it turns there
constexpr float kSpinTop = 1800.0f, kSpinRate = 4.0f, kSpinHeld = 0.15f; // degrees a second; easing; s after a shot

float Smooth(float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}
float Span(float p, float a, float b) { return Smooth((p - a) / (b - a)); } // 0 before a, 1 after b

} // namespace

AnimOutput Anim::Update(const AnimInput &in)
{
	AnimOutput out{};
	const float dt = in.dt;
	// What's on screen: an uncovered gun shows his real arms at once; a switch
	// between covered guns holsters the old one, then draws the new.
	if (!in.covered) {
		shown_ = -1;
		holster_ = -1.0f;
		out.visible = false;
		out.weapon = in.weapon;
		out.magazineShown = true;
		sprint_ = 0.0f;
		return out;
	}
	if (shown_ < 0) {
		shown_ = in.weapon;
		draw_ = 0.0f;
	} else if (in.weapon != shown_ && holster_ < 0.0f) {
		holster_ = 0.0f;
	}
	float down = 0.0f; // 0 up .. 1 all the way down
	if (holster_ >= 0.0f) {
		holster_ += dt;
		down = Smooth(holster_ / kHolsterTime);
		if (holster_ >= kHolsterTime) {
			holster_ = -1.0f;
			shown_ = in.weapon;
			draw_ = 0.0f;
			down = 1.0f;
			empty_ = false;
			sinceShot_ = 1e9f;
			kick_ = 0.0f;
		}
	} else {
		down = 1.0f - Smooth(draw_);
		draw_ = std::min(1.0f, draw_ + dt / kDrawTime);
	}
	out.visible = true;
	out.weapon = shown_;
	const bool current = shown_ == in.weapon && holster_ < 0.0f;
	out.ready = current && draw_ >= 1.0f;

	// Firing: a kick, the slide back and home, a flash.
	if (current && in.shot) {
		kick_ = 1.0f;
		sinceShot_ = 0.0f;
	} else {
		sinceShot_ += dt;
		kick_ *= std::exp(-kKickFade * dt);
	}
	// The shot that empties it locks the slide back, till rounds go in.
	if (current && in.clip > 0)
		empty_ = false;
	else if (current && in.shot)
		empty_ = true;
	const float s = sinceShot_;
	float slide = s < kSlideHeld ? 1.0f : s < kSlideHeld + kSlideHome ? 1.0f - (s - kSlideHeld) / kSlideHome : 0.0f;
	if (empty_)
		slide = 1.0f; // locked back
	out.flash = s < kFlashTime ? 1.0f : 0.0f;
	// A pump racked after the shot; a cylinder's next chamber; a minigun's barrels spinning while it fires.
	out.pumpBack = kPumpTravel * (s < kPumpWait ? 0.0f
	                              : s < kPumpWait + kPumpBack ? Smooth((s - kPumpWait) / kPumpBack)
	                                                          : 1.0f - Smooth((s - kPumpWait - kPumpBack) / kPumpHome));
	if (current && in.shot)
		cylinderTo_ += kChamber;
	cylinder_ += (cylinderTo_ - cylinder_) * std::min(1.0f, kCylinderRate * dt);
	if (cylinderTo_ >= 3600.0f) {
		cylinderTo_ -= 3600.0f;
		cylinder_ -= 3600.0f;
	}
	out.cylinderTurn = cylinder_;
	spin_ += ((in.triggerHeld || s < kSpinHeld ? kSpinTop : 0.0f) - spin_) * std::min(1.0f, kSpinRate * dt);
	barrels_ = std::fmod(barrels_ + spin_ * dt, 360.0f);
	out.barrelsTurn = barrels_;
	trigger_ += ((in.triggerHeld || s < kFlashTime ? 1.0f : 0.0f) - trigger_) * std::min(1.0f, kTriggerRate * dt);
	out.triggerBack = kTriggerTravel * trigger_;

	// Reloading: tilt in, the magazine out and a new one in, the slide let go, tilt out.
	float tilt = 0.0f;
	out.magazineShown = true;
	if (current && in.reloading) {
		if (!wasReloading_)
			emptyAtReload_ = empty_ || in.clip == 0;
		const float p = in.reloadProgress;
		tilt = Span(p, 0.0f, 0.15f) * (1.0f - Span(p, 0.85f, 1.0f));
		if (p < kOut)
			out.magazineDrop = kMagazineTravel * Span(p, kOutFrom, kOut);
		else if (p < kAway)
			out.magazineDrop = kMagazineTravel + kMagazineAway * Span(p, kOut, kAway);
		else if (p < kSwapped)
			out.magazineDrop = kMagazineTravel + kMagazineAway;
		else if (p < kUp)
			out.magazineDrop = kMagazineTravel + kMagazineAway * (1.0f - Span(p, kSwapped, kUp));
		else
			out.magazineDrop = kMagazineTravel * (1.0f - Span(p, kUp, kIn));
		out.magazineShown = p < kAway || p >= kSwapped;
		if (emptyAtReload_)
			slide = 1.0f - Span(p, 0.75f, 0.85f);
		empty_ = false;
	}
	wasReloading_ = current && in.reloading;
	out.slideBack = kSlideTravel * slide;
	out.boltBack = out.slideBack;

	// Sprinting (not aimed, not reloading, the gun up): carried by the kind of
	// gun on screen, swinging with his stride; gone as the sights come up.
	const bool carrying = in.sprinting && in.aimed < 0.5f && current && !in.reloading;
	sprint_ += ((carrying ? 1.0f : 0.0f) - sprint_) * std::min(1.0f, (carrying ? kSprintRate : kSprintOutRate) * dt);
	// With the sprint eased to within a hair, it's there.
	const float sprint = sprint_ > 0.999f ? 1.0f : sprint_ < 0.001f ? 0.0f : sprint_;
	const float carried = sprint * std::clamp(1.0f - in.aimed, 0.0f, 1.0f);
	const CarryPose &c = kCarries[int(CarryFor(shown_))];
	const float pi = 3.14159265f, stride = std::sin(pi * in.stepPhase), step = (std::cos(2.0f * pi * in.stepPhase) - 1.0f) * 0.5f;
	out.carry.offset = Mul(Add(Add(c.held.offset, Mul(c.swing.offset, stride)), Mul(c.bounce, -step)), carried);
	out.carry.pitch = (c.held.pitch + c.swing.pitch * stride + c.bouncePitch * step) * carried;
	out.carry.roll = (c.held.roll + c.swing.roll * stride) * carried;
	out.carry.yaw = (c.held.yaw + c.swing.yaw * stride) * carried;
	// Moving, it bobs with his steps: not aimed, and handed to the carry's swing sprinting.
	const GunSway bob = GunBob(in.stepPhase, in.stepSpeed);
	const float bobbing = std::clamp(1.0f - in.aimed, 0.0f, 1.0f) * (1.0f - sprint);

	out.offset = Add({-kTiltIn * tilt, -kKickBack * kick_, -kDrawDrop * down + kTiltLift * tilt}, Mul(bob.offset, bobbing));
	out.pitch = kDrawTurn * down + kKickTurn * kick_ + kTiltPitch * tilt + bob.pitch * bobbing;
	out.roll = kTiltRoll * tilt + bob.roll * bobbing;
	out.yaw = bob.yaw * bobbing;
	return out;
}

GunSway GunBob(float phase, float speed)
{
	const float k = speed / kBobRun, size = std::min(k * k, kBobMost);
	// Dipping at each side, about where it's held (averaging out: not low and pointing down).
	const float pi = 3.14159265f, stride = std::sin(pi * phase), dip = std::cos(2.0f * pi * phase) * 0.5f;
	return {{kBobSide * size * stride, 0.0f, kBobDip * size * dip}, kBobPitch * size * dip, kBobRoll * size * stride,
	        kBobYaw * size * stride};
}

Carry CarryFor(int weapon)
{
	switch (weapon) {
	case 17: case 18: case 22: case 23: case 24: // Colt, Python, Tec-9, Uzi, Ingram
		return Carry::kOneHanded;
	case 30: case 31: case 32: case 33:          // rocket launcher, flamethrower, M60, minigun
		return Carry::kHeavy;
	default:
		return Carry::kTwoHanded;
	}
}

ReloadHand HandForReload(bool reloading, float progress, bool twoHanded)
{
	const float p = progress;
	if (twoHanded) {
		const float at = reloading ? Span(p, kHandTo, kOutFrom) * (1.0f - Span(p, kHandBack, kHandHome)) : 0.0f;
		return {at, 0.0f, true};
	}
	if (!reloading || p < kAway || p >= kHandHome)
		return {0.0f, 0.0f, false};
	return {1.0f, Span(p, kHandBack - 0.02f, kHandHome), true};
}

Frame PartMove(const Part &part, const AnimOutput &out, float scale)
{
	const std::string_view name = part.name;
	float by = 0.0f, turn = 0.0f;
	if (name == "slide")
		by = out.slideBack;
	else if (name == "bolt")
		by = out.boltBack;
	else if (name == "pump")
		by = out.pumpBack;
	else if (name == "trigger")
		by = out.triggerBack;
	else if (name == "magazine")
		by = out.magazineDrop;
	else if (name == "cylinder")
		turn = out.cylinderTurn;
	else if (name == "barrels")
		turn = out.barrelsTurn;
	const Vec3 axis = Normal(part.axis);
	if (turn != 0.0f && Length(axis) > 0.0f)
		return TurnAbout(axis, turn * 3.14159265f / 180.0f, part.pivot);
	Frame f = Identity();
	f.pos = Mul(axis, by * scale);
	return f;
}

} // namespace vm

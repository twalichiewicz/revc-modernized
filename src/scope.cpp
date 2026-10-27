#include "scope.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace scope {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kStops[] = {3.0f, 8.0f}; // magnification of the game's 70° view
constexpr float kZoomRate = 12.0f;       // per second, easing to a stop
constexpr float kSway = 0.35f, kSwayMoving = 1.5f;              // degrees standing; times, creeping
constexpr float kSwayYawHz = 0.27f, kSwayPitchHz = 0.41f;        // two slow sines
constexpr float kHoldFor = 4.5f, kGaspFor = 1.0f, kGasp = 4.5f, kRefill = 3.0f; // s, s, times the sway, s
constexpr float kSteadyRate = 8.0f;      // per second, into and out of a held breath
constexpr float kBolt = 0.9f;            // s the scope's down after a shot
constexpr float kOverlayRate = 8.0f;     // per second, fading in
constexpr float kSpread = 0.03f, kCreep = 1.5f; // radians at a creep's pace (m/s)

float FovFor(float magnification) { return 2.0f * std::atan(std::tan(35.0f * kPi / 180.0f) / magnification) * 180.0f / kPi; }

} // namespace

Angles SniperAngles(Dir f)
{
	constexpr float kHighest = 60.0f * kPi / 180.0f, kLowest = -89.5f * kPi / 180.0f; // CCam::Process_Sniper's
	return {std::clamp(std::asin(std::clamp(f.z, -1.0f, 1.0f)), kLowest, kHighest), std::atan2(f.y, f.x)};
}

Angles FollowAngles(Dir f, float level)
{
	constexpr float kHighest = 45.0f * kPi / 180.0f, kLowest = -89.5f * kPi / 180.0f; // Process_FollowPedWithMouse's
	return {std::clamp(std::asin(std::clamp(f.z, -1.0f, 1.0f)) - level, kLowest, kHighest), std::atan2(-f.y, -f.x)};
}

Output Scope::Update(const Input &in)
{
	Output out{};
	const float dt = in.dt;
	if (!in.wanted) { // let go: everything back
		stop_ = 0;
		fov_ = 0.0f;
		bolt_ = overlay_ = gasp_ = 0.0f;
		steady_ = 1.0f;
		breath_ = std::min(1.0f, breath_ + dt / kRefill);
		out.fov = 70.0f;
		out.breath = breath_;
		return out;
	}
	t_ += dt;
	// Zoom: RB steps through the stops; the view eases there.
	if (in.zoomPressed)
		stop_ = (stop_ + 1) % int(std::size(kStops));
	const float to = FovFor(kStops[stop_]);
	fov_ = fov_ <= 0.0f ? to : fov_ + (to - fov_) * std::min(1.0f, kZoomRate * dt);
	// Breath: held, it drains and the sway eases out; out of it, a gasp; then it refills.
	letGo_ = letGo_ || !in.breathHeld;
	const bool holding = in.breathHeld && breath_ > 0.0f && gasp_ <= 0.0f && letGo_;
	if (holding) {
		breath_ = std::max(0.0f, breath_ - dt / kHoldFor);
		if (breath_ <= 0.0f) {
			gasp_ = kGaspFor;
			letGo_ = false; // (not again till L3's let go)
		}
	} else if (gasp_ > 0.0f) {
		gasp_ = std::max(0.0f, gasp_ - dt);
	} else {
		breath_ = std::min(1.0f, breath_ + dt / kRefill);
	}
	const float steadyTo = holding ? 0.0f : gasp_ > 0.0f ? 1.0f + (kGasp - 1.0f) * gasp_ / kGaspFor : 1.0f;
	steady_ += (steadyTo - steady_) * std::min(1.0f, kSteadyRate * dt);
	// Sway: two slow sines, more creeping.
	moving_ += (std::min(in.moving / kCreep, 1.0f) - moving_) * std::min(1.0f, 6.0f * dt);
	const float amplitude = kSway * (1.0f + (kSwayMoving - 1.0f) * moving_) * steady_;
	out.sway = amplitude;
	out.swayYaw = amplitude * std::sin(2.0f * kPi * kSwayYawHz * t_);
	out.swayPitch = amplitude * 0.7f * std::sin(2.0f * kPi * kSwayPitchHz * t_ + 1.0f);
	// A shot: the scope drops while the bolt cycles.
	if (in.shot && in.boltAction)
		bolt_ = kBolt;
	else
		bolt_ = std::max(0.0f, bolt_ - dt);
	// Faded in in the game's view; while the bolt's dropping it, as it was till the view goes.
	overlay_ = !in.inView ? 0.0f : bolt_ > 0.0f ? overlay_ : std::min(1.0f, overlay_ + kOverlayRate * dt);
	out.fov = fov_;
	out.breath = breath_;
	out.holding = holding;
	out.gasping = gasp_ > 0.0f;
	out.bolting = bolt_ > 0.0f;
	out.spread = kSpread * std::min(in.moving / kCreep, 1.0f);
	out.overlay = overlay_;
	return out;
}

} // namespace scope

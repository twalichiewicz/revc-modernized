#include "fplook.h"

#include <algorithm>
#include <cmath>

namespace fplook {
namespace {

constexpr float kPi = 3.14159265f;
// Aim slowdown (docs/research/fps-feel-controller.md, "Numbers to start from").
constexpr float kStrengthHip = 0.5f, kStrengthAim = 0.6f;
constexpr float kZone = 1.5f * kPi / 180.0f; // fades out this far outside the target
constexpr float kFlick = 0.85f;              // a push this far isn't slowed
constexpr float kNear = 3.0f, kFull = 20.0f, kFar = 60.0f; // m
constexpr float kLetGo = 4.0f;               // per second

float Dot(const CVector &a, const CVector &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
CVector Sub(const CVector &a, const CVector &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
CVector Along(const CVector &a, const CVector &d, float s) { return {a.x + d.x * s, a.y + d.y * s, a.z + d.z * s}; }

} // namespace

Rates Stick::Update(float x, float y, float dt, const Tuning &t)
{
	const float m = std::hypot(x, y);
	if (m >= t.full)
		rimTime_ += dt;
	else
		rimTime_ = 0.0f;
	if (m <= t.deadZone)
		return {};
	const float n = std::min(1.0f, (m - t.deadZone) / (t.full - t.deadZone));
	const float speed = std::pow(n, t.power) / m;
	const float ramp = std::clamp((rimTime_ - t.rimDelay) / t.rimRamp, 0.0f, 1.0f);
	return {x * speed * t.yawRate * (1.0f + t.rimYaw * ramp), y * speed * t.pitchRate * (1.0f + t.rimPitch * ramp)};
}

float AimScale(float fovHipDeg, float fovAimDeg, float aimSensitivity, float aimSensitivityDefault)
{
	const float half = kPi / 360.0f;
	return std::tan(fovAimDeg * half) / std::tan(fovHipDeg * half) * aimSensitivity / aimSensitivityDefault;
}

float OffAngle(const CVector &eye, const CVector &dir, const CVector &feet, const CVector &head, float radius)
{
	// The capsule's axis at its point nearest the line of sight.
	const CVector axis = Sub(head, feet), r = Sub(eye, feet);
	const float b = Dot(dir, axis), c = Dot(axis, axis), d = Dot(dir, r), e = Dot(axis, r);
	const float denom = c - b * b; // |dir| = 1
	float s = denom > 1e-6f ? (e - b * d) / denom : 0.0f;
	s = std::clamp(s, 0.0f, 1.0f);
	const CVector w = Sub(Along(feet, axis, s), eye);
	const float ahead = Dot(w, dir), dist = std::sqrt(Dot(w, w));
	if (ahead <= 0.0f || dist < 1e-4f)
		return kPi;
	const float off = std::sqrt(std::max(0.0f, Dot(w, w) - ahead * ahead));
	return std::atan2(off, ahead) - std::atan(radius / dist);
}

float Slowdown(float offAngle, float distance, bool aiming, float push)
{
	if (push >= kFlick || offAngle >= kZone || distance >= kFar)
		return 1.0f;
	const float zone = offAngle <= 0.0f ? 1.0f : 1.0f - offAngle / kZone;
	float range = 1.0f;
	if (distance < kNear)
		range = 0.5f + 0.5f * distance / kNear;
	else if (distance > kFull)
		range = (kFar - distance) / (kFar - kFull);
	return 1.0f - (aiming ? kStrengthAim : kStrengthHip) * zone * range;
}

float Assist::Update(float target, float dt)
{
	if (target < value_)
		value_ = target;
	else
		value_ += std::min(target - value_, kLetGo * dt);
	return value_;
}

} // namespace fplook

#pragma once
// First person's right stick, as modern console shooters turn
// (docs/research/fps-feel-controller.md): a small round dead zone, a squared
// response, 240 / 145 degrees a second at full push and up to half as much
// again held at the rim (after a moment, ramping in); aiming slower in
// proportion to the zoom; and aim slowdown, the turn slowed a little while
// the crosshair is over a target (never pulled towards it), applied at once
// and let go gently. Unit-tested (tests/test_fplook.cpp).
#include "game.h"

namespace fplook {

struct Tuning {
	float deadZone = 0.12f, full = 0.95f, power = 2.0f;
	float yawRate = 4.19f, pitchRate = 2.53f; // rad/s at full push: 240 and 145 deg/s
	float rimYaw = 0.5f, rimPitch = 0.2f;     // more at the rim, as a fraction
	float rimDelay = 0.05f, rimRamp = 0.35f;  // s
};

struct Rates {
	float yaw = 0.0f, pitch = 0.0f; // rad/s, signed as the stick's x and y
};

// The right stick's turn rates; it keeps only the rim boost's timer. dt in real seconds.
class Stick {
public:
	Rates Update(float x, float y, float dt, const Tuning &t);

private:
	float rimTime_ = 0.0f;
};

// Aiming's turn scale: tan(fovAim/2) / tan(fovHip/2) (degrees in), times the
// aim sensitivity setting over its default.
float AimScale(float fovHipDeg, float fovAimDeg, float aimSensitivity, float aimSensitivityDefault = 0.45f);
// How far (radians) the ray from `eye` along `dir` (a unit vector) passes
// outside a target: a vertical capsule from `feet` to `head` of `radius`.
// 0 or less is on it.
float OffAngle(const CVector &eye, const CVector &dir, const CVector &feet, const CVector &head, float radius);
// The turn rate's multiplier (1: none) for a target `offAngle` radians
// outside the crosshair, `distance` m off; `push` is the right stick's (0..1).
float Slowdown(float offAngle, float distance, bool aiming, float push);

// The multiplier applied: a stronger slowdown at once, let go at 4 a second
// (about 0.15 s from a typical 0.5).
class Assist {
public:
	float Update(float target, float dt);

private:
	float value_ = 1.0f;
};

} // namespace fplook

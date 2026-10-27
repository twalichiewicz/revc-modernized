#include "fpfire.h"

#include <algorithm>
#include <cmath>

namespace fpfire {
namespace {

constexpr int kMostShotsAFrame = 3;
constexpr float kLeastInterval = 0.01f; // s
constexpr float kDue = 1e-5f;           // s: due, within float rounding (0.1 - 2 x 0.05 isn't quite 0)

// KickBack's arguments (research: docs/research/fps-feel-goldsrc.md §2.3), standing.
struct KickBack {
	float upBase, sideBase, upGrowth, sideGrowth, upMax, sideMax;
	int flipOdds;
};
constexpr KickBack kRifleKick{0.65f, 0.35f, 0.25f, 0.015f, 3.5f, 2.25f, 7};   // the M4A1's
constexpr KickBack kSmgKick{0.5f, 0.25f, 0.15f, 0.01f, 3.0f, 1.5f, 7};
constexpr KickBack kHeavyKick{1.0f, 0.375f, 0.175f, 0.0375f, 5.75f, 1.75f, 8}; // the AK47's
constexpr float kPistolKick = 2.0f, kShotgunKick = 3.5f, kSingleMax = 10.0f;

constexpr float kAccuracyBase = 0.35f, kAccuracyMax = 1.25f;
constexpr float kRecover = 0.4f, kPistolRecover = 0.3f; // s without a shot: the burst's over
constexpr float kRunning = 1.8f, kAir = 4.0f, kCrouched = 0.7f, kAiming = 0.6f;

float Scale(Kind kind)
{
	switch (kind) {
	case Kind::kPistol: return 0.025f;
	case Kind::kShotgun: return 0.02f;
	case Kind::kSmg: return 0.04f;
	case Kind::kRifle: return 0.03f;
	case Kind::kHeavy: return 0.05f;
	}
	return 0.03f;
}

} // namespace

Kind KindOf(int type)
{
	switch (type) {
	case 17: case 18: return Kind::kPistol;          // Colt 45, Python
	case 19: case 20: case 21: return Kind::kShotgun; // shotgun, SPAS 12, stubby
	case 22: case 23: case 24: case 25: return Kind::kSmg; // Tec-9, Uzi, Ingram, MP5
	case 26: case 27: case 28: case 29: return Kind::kRifle; // M4, Ruger, sniper rifles
	default: return Kind::kHeavy;                    // rocket launcher, flamethrower, M60, minigun
	}
}

bool Automatic(Kind kind) { return kind != Kind::kPistol && kind != Kind::kShotgun; }

int Trigger::Update(bool held, bool ready, float interval, bool automatic, float dt)
{
	cooldown_ -= dt;
	if (!held)
		armed_ = true;
	if (!bursting_ && cooldown_ < 0.0f)
		cooldown_ = 0.0f; // a fresh pull: the time it was idle isn't owed in shots
	int shots = 0;
	if (held && ready && (automatic || armed_)) {
		while (cooldown_ <= kDue && shots < kMostShotsAFrame) {
			shots++;
			cooldown_ += std::max(interval, kLeastInterval);
			if (!automatic) {
				armed_ = false;
				break;
			}
		}
	}
	bursting_ = held && ready && automatic; // only a held automatic carries the cadence's remainder
	return shots;
}

void Trigger::Reset(bool held)
{
	cooldown_ = 0.0f;
	bursting_ = false;
	armed_ = !held;
}

void Jolt::Kick(Kind kind, float random01)
{
	// Degrees at its height (pistol, shotgun, SMG, rifle, heavy).
	constexpr float kSizes[] = {0.45f, 1.1f, 0.3f, 0.45f, 0.35f};
	size_ = kSizes[int(kind)];
	t_ = 0.0f;
	yawSign_ = random01 < 0.5f ? -1.0f : 1.0f;
	rollSign_ = (random01 * 7.0f - std::floor(random01 * 7.0f)) < 0.5f ? -1.0f : 1.0f;
	Decay(0.0f);
}

void Jolt::Decay(float dt)
{
	constexpr float kHz = 9.0f, kFade = 0.045f; // a quick shake; its fade (s)
	t_ += dt;
	const float k = size_ * std::exp(-t_ / kFade) * std::sin(6.2831853f * kHz * t_ + 0.6f);
	yaw = 0.5f * yawSign_ * k;
	pitch = 0.4f * std::fabs(k);
	roll = rollSign_ * k;
}

void Punch::Decay(float dt)
{
	const float length = std::hypot(pitch, yaw);
	if (length <= 0.0f)
		return;
	const float left = std::max(0.0f, length - (10.0f + 0.5f * length) * dt);
	pitch *= left / length;
	yaw *= left / length;
}

void Punch::Kick(Kind kind, int shots, float random01)
{
	if (kind == Kind::kPistol || kind == Kind::kShotgun) {
		pitch = std::min(pitch + (kind == Kind::kPistol ? kPistolKick : kShotgunKick), kSingleMax);
		return;
	}
	const KickBack &k = kind == Kind::kSmg ? kSmgKick : kind == Kind::kHeavy ? kHeavyKick : kRifleKick;
	const float n = float(std::max(shots, 1));
	const float up = shots <= 1 ? k.upBase : n * k.upGrowth + k.upBase;
	const float side = shots <= 1 ? k.sideBase : n * k.sideGrowth + k.sideBase;
	pitch = std::min(pitch + up, k.upMax);
	yaw = std::clamp(yaw + (right ? side : -side), -k.sideMax, k.sideMax);
	if (random01 < 1.0f / float(k.flipOdds + 1))
		right = !right;
}

void Spread::Shot()
{
	shots_++;
	since_ = 0.0f;
}

void Spread::Update(float dt)
{
	since_ += dt;
	if (since_ > kRecover)
		shots_ = 0;
}

float Spread::Angle(Kind kind, Moving moving, bool crouched, bool aiming) const
{
	const bool fast = moving == Moving::kRunning || moving == Moving::kAir;
	if (kind == Kind::kPistol && since_ > kPistolRecover && !fast)
		return 0.0f; // a pistol recovers between pulls
	float accuracy = shots_ == 0 ? (fast ? kAccuracyBase : 0.0f)
	                             : std::min(kAccuracyMax, float(shots_ * shots_ * shots_) / 200.0f + kAccuracyBase);
	accuracy *= moving == Moving::kRunning ? kRunning : moving == Moving::kAir ? kAir : 1.0f;
	if (crouched)
		accuracy *= kCrouched;
	if (aiming)
		accuracy *= kAiming;
	return Scale(kind) * accuracy;
}

} // namespace fpfire

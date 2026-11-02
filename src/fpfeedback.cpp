#include "fpfeedback.h"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace fpfeedback
{
float Breath::Update(bool submerged, float dt, float maxHealth)
{
	if (!std::isfinite(dt) || dt <= 0)
		return 0;
	if (!submerged) {
		seconds_ = std::min(20.0, seconds_ + double(dt) * 4.0);
		return 0;
	}
	const float exhausted = std::max(0.0, double(dt) - seconds_);
	seconds_ = std::max(0.0, seconds_ - double(dt));
	return exhausted * std::max(1.0f, maxHealth) * 0.5f; // full health in two exhausted seconds
}
float DrowningWeaponDamage(float healthLoss)
{
	// macVC 1.0.5217: CPed::InflictDamage 0x1001c7a24 loads 0.33f;
	// 0x1001c7a78 multiplies every player hit. Weapon43 bypasses armour at
	// 0x1001c81b8, so only this scalar needs undoing for health/second units.
	return std::max(0.0f, healthLoss) / 0.33f;
}
float Pitch(float pitch)
{
	constexpr float limit = 89.0f * 3.14159265f / 180.0f;
	return std::clamp(pitch, -limit, limit);
}
Basis View(float heading, float pitch)
{
	pitch = Pitch(pitch);
	const float c = std::cos(pitch), s = std::sin(pitch), x = -std::sin(heading), y = std::cos(heading);
	return {{x * c, y * c, s}, {-x * s, -y * s, c}};
}
bool LookGate::Update(bool open)
{
	const bool consume = open || wasOpen_;
	wasOpen_ = open;
	return consume;
}
void Impact::Hit(float before, float after, float side, float ahead)
{
	const float lost = before - after;
	if (!std::isfinite(lost) || lost <= 0)
		return;
	opacity_ = std::min(0.55f, std::max(opacity_, 0.28f + lost * 0.012f));
	const float length = std::hypot(side, ahead);
	side_ = length > 1e-6f ? side / length : 0;
	ahead_ = length > 1e-6f ? ahead / length : 0;
}
void Impact::Tick(float dt)
{
	if (std::isfinite(dt) && dt > 0)
		opacity_ = std::max(0.0f, opacity_ - dt * 0.8f);
}
void Impact::Reset()
{
	opacity_ = side_ = ahead_ = 0;
}
bool CanPose(float health, int state, bool enabled)
{
	return enabled && health > 0 && state != 0x36 && state != 0x37;
}
DamageFacts ObserveDamage(bool nativeResult, float healthBefore, float healthAfter, float armourBefore, float armourAfter)
{
	const float health = healthBefore - healthAfter, armour = armourBefore - armourAfter;
	const float healthLost = std::isfinite(health) ? std::max(0.0f, health) : 0.0f;
	const float armourLost = std::isfinite(armour) ? std::max(0.0f, armour) : 0.0f;
	return {nativeResult, healthLost > 0.0f || armourLost > 0.0f, healthLost, armourLost};
}

bool AimPose::Known(int weapon) const
{
	return weapon > 0 && weapon < 40 && known_[weapon];
}
void AimPose::Captured(int weapon)
{
	if (weapon > 0 && weapon < 40)
		known_[weapon] = true;
}
AimPose::Output AimPose::Update(int weapon, uintptr_t model, uint32_t now, bool aimed, bool standing, bool ownsPose,
				float dt)
{
	if (model != model_) {
		*this = {};
		model_ = model;
	}
	if (!ownsPose) {
		standing_ = false;
		weight_ = 0;
		return {};
	}
	if (standing && (!standing_ || now < standingAt_ || weapon != standingWeapon_))
		standingAt_ = now;
	standing_ = standing;
	standingWeapon_ = weapon;
	const bool capture = weapon > 0 && weapon < 40 && aimed && standing && !Known(weapon) && now - standingAt_ > 300;
	const bool held = aimed && Known(weapon);
	if (held && weapon_ != weapon) {
		weapon_ = weapon;
		weight_ = 0;
	}
	const float t = std::isfinite(dt) ? std::max(0.0f, dt) : 0;
	weight_ += ((held ? 1.0f : 0.0f) - weight_) * (1 - std::exp(-8 * t));
	if (weight_ < .01f)
		weight_ = 0;
	return {weapon_, weight_, capture};
}
void MatrixRestore::Save(uintptr_t owner, const float *matrix)
{
	owner_ = owner;
	std::copy(matrix, matrix + 16, matrix_);
	saved_ = true;
}
bool MatrixRestore::Restore(uintptr_t owner, float *matrix)
{
	if (!saved_ || owner != owner_)
		return false;
	std::copy(std::begin(matrix_), std::end(matrix_), matrix);
	saved_ = false;
	return true;
}
} // namespace fpfeedback

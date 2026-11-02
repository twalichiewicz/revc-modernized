#include "falldamage.h"

#include <algorithm>
#include <cmath>

namespace falldamage {
namespace {

// The game's moving him (CPhysical): a frame at a time, 50 a second.
constexpr float kFrame = 50.0f;
constexpr float kGravity = 0.008f;            // a frame, a frame
constexpr float kDrag = 0.5f * (0.4f / 70.0f); // ApplyAirResistance's, a ped's

} // namespace

float Games(float down, float along, bool soft)
{
	// 100 a unit a frame over 0.25 along the ground, 150 down.
	float damage = 100.0f * std::max(along / kFrame - 0.25f, 0.0f);
	if (down > kHarmless)
		damage += 150.0f * (down / kFrame - 0.25f);
	return soft ? damage * 0.5f : damage;
}

float Damage(float down, float along, bool soft)
{
	const float hits = soft ? down * kSoft : down;
	if (hits >= kDeadly)
		return kDead;
	float damage = hits > kHarmless ? 100.0f * std::pow((hits - kHarmless) / (kFull - kHarmless), kCurve) : 0.0f;
	// Along the ground (thrown by a car, an explosion): the game's.
	damage += Games(0.0f, along, soft);
	return std::max(damage, Games(down, along, soft));
}

float Drop(float down)
{
	float z = 0.0f, v = 0.0f;
	while (v * kFrame < down && z < 1000.0f) {
		v += kGravity;
		v /= 1.0f + kDrag * v * v;
		z += v;
	}
	return z;
}

} // namespace falldamage

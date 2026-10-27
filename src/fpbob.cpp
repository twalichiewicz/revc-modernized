#include "fpbob.h"

#include <algorithm>
#include <cmath>

namespace fpbob {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kBob = 0.01f, kCycle = 0.8f, kUp = 0.5f; // cl_bob, cl_bobcycle, cl_bobup
constexpr float kLowest = -0.178f, kHighest = 0.102f;   // m: CS's -7 and 4 units
constexpr float kStepSettle = 0.06f, kStepLag = 0.45f;  // s; m (GoldSrc: 18 units)
constexpr float kRun = 4.5f, kMost = 2.2f;              // m/s: the speed the sway's sized for; its most, sprinting
constexpr float kDip = 0.18f, kRoll = 0.17f, kYaw = 0.12f; // degrees, running (a third of the first try: too much)
constexpr float kDownFrom = 0.342f;                     // sin 20°
constexpr float kDownAhead = 0.16f, kDownLower = 0.05f; // m, looking straight down
constexpr float kRollAngle = 2.0f / 3.0f, kRollSpeed = 5.08f; // Half-Life's cl_rollangle, a third (with the rest); cl_rollspeed (200 units, m/s)

} // namespace

float Bob(State &s, float speed, float dt, bool onGround)
{
	if (!onGround)
		return s.last;
	s.time += dt;
	float cycle = std::fmod(s.time, kCycle) / kCycle;
	cycle = cycle < kUp ? kPi * cycle / kUp : kPi + kPi * (cycle - kUp) / (1.0f - kUp);
	const float bob = speed * kBob;
	s.last = std::clamp(bob * 0.3f + bob * 0.7f * std::sin(cycle), kLowest, kHighest);
	return s.last;
}

Turn Sway(float phase, float speed, float side)
{
	const float k = speed / kRun, size = std::min(k * k, kMost); // grows faster than his speed: running, not walking
	const float stride = std::sin(kPi * phase), step = std::cos(2.0f * kPi * phase);
	const float strafe = kRollAngle * std::clamp(side / kRollSpeed, -1.0f, 1.0f);
	return {-kDip * size * step, kRoll * size * stride + strafe, kYaw * size * stride};
}

Push LookDown(float frontZ)
{
	float t = std::clamp((-frontZ - kDownFrom) / (1.0f - kDownFrom), 0.0f, 1.0f);
	t = t * t * (3.0f - 2.0f * t);
	return {kDownAhead * t, kDownLower * t};
}

bool StridePhase(const Stride *anims, int count, float &phase)
{
	const Stride *most = nullptr;
	for (int i = 0; i < count; i++)
		if (anims[i].blend > 0.1f && anims[i].length > 0.0f && (!most || anims[i].blend > most->blend))
			most = &anims[i];
	if (!most)
		return false;
	phase = 2.0f * std::fmod(std::max(most->time, 0.0f), most->length) / most->length;
	return true;
}

float Steps::Height(float z, float dt, bool onGround)
{
	if (!ready_ || !onGround) {
		ready_ = true;
		z_ = z;
		return z_;
	}
	z_ += (z - z_) * (1.0f - std::exp(-dt / kStepSettle));
	z_ = std::clamp(z_, z - kStepLag, z + kStepLag);
	return z_;
}

} // namespace fpbob

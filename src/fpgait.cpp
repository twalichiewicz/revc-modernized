#include "fpgait.h"

#include <cmath>
#include <cstdlib>

namespace fpgait {
namespace {

// ePedState: his own (idle, shooting, aiming, on the phone), and his falls.
constexpr int32_t kIdle = 1, kSniperMode = 0xc, kAttack = 0x10, kAimGun = 0x16, kAnswerMobile = 0x24;
constexpr int32_t kJump = 0x29, kFall = 0x2a, kGetUp = 0x2b, kDie = 0x36, kDead = 0x37;
constexpr int32_t kKillOnFoot = 8, kKillAnyMeans = 9; // eObjective
constexpr float kRun = 6.4f, kRunArmed = 5.0f, kSprint = 9.8f, kCrouched = 1.6f, kAimed = 0.52f;

// The game's pick between an axis and its D-pad pair: whichever is further.
int16_t Pick(int16_t axis, int16_t dpad) { return std::abs(axis) > std::abs(dpad) ? axis : dpad; }

} // namespace

Vec2 Push(int16_t stickX, int16_t stickY, int16_t up, int16_t down, int16_t left, int16_t right)
{
	const float x = Pick(stickX, int16_t((right - left) / 2)) / 128.0f;
	const float y = -Pick(stickY, int16_t((down - up) / 2)) / 128.0f; // up is ahead
	const float length = std::hypot(x, y);
	return length > 1.0f ? Vec2{x / length, y / length} : Vec2{x, y};
}

Mode MoveMode(const Situation &s)
{
	if (s.controlsDisabled || s.objective != 0)
		return Mode::kNone;
	if (s.state == kFall || s.state == kGetUp || s.state == kDie || s.state == kDead)
		return Mode::kNone;
	if (s.state == kJump || s.inTheAir || !s.standing)
		return Mode::kAir;
	const bool his = s.state == kIdle || s.state == kAttack || s.state == kAimGun || s.state == kAnswerMobile ||
	                 (s.scoped && s.state == kSniperMode);
	return his && s.inControl ? Mode::kGround : Mode::kNone;
}

float TopSpeed(const Gait &g)
{
	if (g.ducking)
		return kCrouched;
	const float run = g.twoHanded ? kRunArmed : kRun;
	if (g.aiming)
		return run * kAimed;
	return g.sprinting ? kSprint : run;
}

bool Enemy(int32_t objective, bool objectiveIsHim, bool gunOnHim, bool cop, bool wanted)
{
	return ((objective == kKillOnFoot || objective == kKillAnyMeans) && objectiveIsHim) || gunOnHim || (cop && wanted);
}

} // namespace fpgait

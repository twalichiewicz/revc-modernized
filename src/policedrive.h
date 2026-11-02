#pragma once
// Police cars arriving where Tommy is on foot (#21). The game resets a chase
// car's cruise speed by star level every frame and only slows it at 20 m, so a
// car pulled up at police.cpp's line arrives at full chase speed. This limits
// its approach to a braking curve that reaches the line at a gentle speed.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace policedrive {

constexpr float kArriveAt = 28.0f;   // m: PullUp's pull-up line, in plain view
constexpr float kArriveSpeed = 8.0f; // m/s there
constexpr float kBrake = 5.0f;       // m/s^2: a firm, controlled stop
constexpr float kCruisePerMetre = 1.2f; // CAutoPilot::m_nCruiseSpeed per m/s (game speed x 60 vs x 50)

// The fastest a car may go `gap` metres from him and still reach the line at kArriveSpeed.
inline float ArrivalSpeed(float gap)
{
	return std::sqrt(kArriveSpeed * kArriveSpeed + 2.0f * kBrake * std::max(0.0f, gap - kArriveAt));
}
inline uint8_t CruiseFor(float metresPerSecond)
{
	return uint8_t(std::clamp(std::lround(metresPerSecond * kCruisePerMetre), 0L, 255L));
}
// The game's cruise speed, lowered (never raised) to the arrival curve.
inline uint8_t Limit(uint8_t cruise, float gap) { return std::min(cruise, CruiseFor(ArrivalSpeed(gap))); }

} // namespace policedrive

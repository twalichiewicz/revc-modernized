#pragma once
// GTA IV-style rain (#32), without the game: how wet everything is, how full
// the puddles are and how many streaks fall, from the game's own weather
// (CWeather::Rain, CWeather::WetRoads).

#include "types.h"

#include <functional>
#include <vector>

namespace rainmodel {

constexpr int kMaxStreaks = 6000;
constexpr float kWetEase = 1.0f;       // s: the ground's wetness follows the game's this smoothly
constexpr float kPuddleFill = 180.0f;  // s of steady heavy rain to fill the puddles
constexpr float kPuddleDrain = 300.0f; // s dry to drain them
constexpr float kRaining = 0.05f;      // less is no rain

struct Weather {
	float rain = 0, wetness = 0, puddles = 0; // each 0..1
};

class Model {
public:
	Weather Update(float gameRain, float gameWetRoads, float dt);

private:
	Weather w_;
};

// Streaks falling now: none in a drizzle this light, kMaxStreaks in a downpour,
// none when he's `sheltered` (indoors, or in one of the game's no-rain zones).
int StreakCount(float rain, bool sheltered = false);

// The streaks: a box of them around the camera, fixed in the world (they
// wrap round as he moves, not with him), falling and blown by the wind.
constexpr float kStreakBox = 40.0f;    // m across
constexpr float kStreakHeight = 25.0f; // m tall...
constexpr float kStreakBelow = 8.0f;   // ...from this far under the camera
constexpr float kFallSpeed = 9.0f;     // m/s
constexpr float kStreakLength = 0.8f;  // m
struct Streak {
	Vec3 top, bottom;
};
// `count` streaks at game time `seconds`, leaving out those `covered` says a
// roof or a bridge keeps off.
std::vector<Streak> Streaks(Vec3 camera, Vec2 wind, int count, float seconds, const std::function<bool(Vec3)> &covered);

// A streak's light: the sky's, and each lamp's within its reach.
struct Lamp {
	Vec3 pos, colour;
	float radius;
};
Vec3 Lit(Vec3 p, Vec3 sky, const std::vector<Lamp> &lamps);

} // namespace rainmodel

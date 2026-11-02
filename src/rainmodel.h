#pragma once
// GTA IV-style rain (#32), without the game: how wet everything is, how full
// the puddles are and how many streaks fall, from the game's own weather
// (CWeather::Rain, CWeather::WetRoads).

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

// Streaks falling now: none in a drizzle this light, kMaxStreaks in a downpour.
int StreakCount(float rain);

} // namespace rainmodel

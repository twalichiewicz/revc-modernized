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
constexpr float kPuddleFill = 120.0f;  // s of steady rain to fill the puddles...
constexpr float kFullRain = 0.5f;      // ...this heavy or more (the game's rainy weather); lighter fills them partway
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
// wrap round as he moves, not with him), falling, carried by the steady wind
// and slanted by `wind` (the gust's).
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

// Heavy rain closes the view in: the game's far clip and fog start (the
// time cycle's) brought nearer with it, never further than the game had them.
void Haze(float rain, float farClip, float fogStart, float &far, float &fog);
// And the picture darker and greyer.
struct Gloom {
	float exposure, saturation; // scales, 1 dry
};
Gloom GloomFor(float rain);

// A box in its own axes (unit vectors), `half` its half sizes along them: the
// cabin of the car he's in, where no rain falls.
struct Box {
	Vec3 centre, right, forward, up, half;
};
bool Inside(const Box &box, Vec3 p);

// The wind, in gusts: the volumetric clouds' steady wind (clouds.cpp), now and
// then stronger (to 1.6 times) and swung round a little; `strength` 0..1 is how
// much of a gust is blowing.
constexpr Vec2 kBaseWind{7, 3};
struct Gust {
	Vec2 wind;
	float strength;
};
Gust GustAt(float seconds);

// Sheets of rain blown along (as GTA IV's): tall veils of mist round the
// camera, carried downwind, each turned to face him, thicker in a gust.
constexpr float kCurtainBox = 160.0f; // m across
constexpr int kCurtains = 16;
struct Curtain {
	Vec3 centre;
	Vec2 facing; // towards him
	float width, height, density; // m, m, 0..1
};
std::vector<Curtain> Curtains(Vec3 camera, float rain, float seconds, float gust);

// A streak's light: the sky's, and each lamp's within its reach.
struct Lamp {
	Vec3 pos, colour;
	float radius;
};
Vec3 Lit(Vec3 p, Vec3 sky, const std::vector<Lamp> &lamps);

} // namespace rainmodel

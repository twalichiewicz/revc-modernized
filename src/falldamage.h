#pragma once
// How much a landing hurts, by how fast he hits the ground. The game's
// (CPed::ProcessEntityCollision) is nothing under 12.5 m/s and 3 a m/s over
// it: in its gravity (20 m/s/s, twice the real) a four-storey drop took a
// third of a full health, and a SWAT (50 armour, which takes a fall's damage
// first) walked away from a helicopter. Here it's nothing under a storey or
// so, a fifth of his health off two storeys, nearly all of it off a fourth
// floor window, all of it off the roof, and five storeys up nobody lives.
// Speeds in m/s; health as the game's (100 a full one). No game in it;
// unit-tested (tests/test_falldamage.cpp).

namespace falldamage {

constexpr float kHarmless = 12.5f; // m/s down: under this nothing (the game's own line: a 4 m drop)
constexpr float kFull = 23.0f;     // m/s: a full health's worth (a 13.5 m drop: a four-storey roof)
constexpr float kCurve = 1.3f;     // between them, rising ever steeper
constexpr float kDeadly = 24.0f;   // m/s: from here dead whatever his health (a 15 m drop)
constexpr float kSoft = 0.84f;     // sand, grass, a bush (CSurfaceTable::IsSoftLanding): as from 70% the height
constexpr float kDead = 1000.0f;

// Landing `down` m/s (falling) and `along` m/s (over the ground), on soft ground or not.
float Damage(float down, float along, bool soft);
// The game's own, for comparison.
float Games(float down, float along, bool soft);
// The drop (m) a ped falls in the game to land at `down` m/s (for the log).
float Drop(float down);

} // namespace falldamage

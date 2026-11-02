#pragma once
// A cop (or anyone) after Tommy at his car's door, as GTA IV has it. The game
// let anyone open the door of a car going up to 10 m/s (36 km/h:
// CVehicle::CanPedEnterCar) and then held them to it (CPed::LineUpPedWithCar)
// however fast it went, so cops pulled him out of a car he was driving off
// in. Now only a car barely rolling can be opened. Faster, one who reaches
// the door catches hold of the handle and hangs on, dragged along; his grip
// goes the faster the car goes and the harder it turns, and he lets go (and
// falls). If the car slows to a crawl while he hangs on, he opens it. Too
// fast and he can't catch hold at all. No game in it; unit-tested
// (tests/test_grip.cpp).
namespace grip {

constexpr float kOpenBelow = 1.2f;   // m/s: slower than this the door can be opened (a crawl)
constexpr float kGrabBelow = 11.0f;  // ... slower than this he can catch hold of it (40 km/h)
constexpr float kOpensAfter = 0.35f; // s at a crawl, hanging on, and he opens it

enum class AtDoor { kOpen, kGrab, kMiss };
// One at the door of a car going this fast (m/s).
AtDoor Reach(float speed);

enum class Now { kHolding, kLetsGo, kOpens };
class Hold {
public:
	// This frame: the car's speed (m/s) and how fast it turns (rad/s), for dt s.
	Now Update(float speed, float turn, float dt);
	float Strain() const { return strain_; } // 0..1: at 1 he lets go

private:
	float strain_ = 0.0f, slow_ = 0.0f;
};

} // namespace grip

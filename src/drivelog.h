#pragma once
// The drive log (#37), as numbers: what his car does over 30 s, for tuning the
// handling from play: its top speed, its quickest 0-100 km/h from rest, and the
// most its body rolled and pitched against the road with all four wheels down.

namespace drivelog {

struct Sample {
	float t;            // s
	float speed;        // m/s along the car's forward
	bool allWheelsDown;
	float roll, pitch;  // rad, the body against the road: roll > 0 its right side down, pitch > 0 its nose up
};

struct Window {
	float top = 0;         // m/s
	float zeroTo100 = -1;  // s, the quickest from rest; -1: none
	float rollMost = 0, pitchMost = 0; // rad, |angle|
};

struct State {
	Window window;
	float startedAt = -1; // s, when this window began
	float runFrom = -1;   // s, when the run from rest began; -1: none under way
	bool armed = false;   // at rest since the last run
	float lastT = 0, lastSpeed = 0;
	bool any = false;
};

void Add(State &, const Sample &);
// 30 s since the window began.
bool Due(const State &, float t);
// The window so far; the next starts at `t`.
Window Take(State &, float t);

// The body's roll and pitch against the road under its wheels: `right`,
// `forward` and `up` are the car's axes, `contact` its wheels' contact points
// (0 front-left, 1 rear-left, 2 front-right, 3 rear-right), all in the world.
void BodyAngles(const float right[3], const float forward[3], const float up[3], const float contact[4][3], float &roll,
                float &pitch);

} // namespace drivelog

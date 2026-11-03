#pragma once
// Tommy balancing his head in a car's first-person seat view (#37), as numbers.
// The view rolls and pitches with the car (its springs lean the body), but he's
// not a robot: his head comes back a little towards level against the view's
// roll, and against quick pitching (a nose dip under braking, not a hill),
// after a short lag. It eases in and out, never snapping.

namespace headbalance {

struct State {
	float roll = 0, pitch = 0; // rad, the turn now
	float slowPitch = 0;       // rad, the view's pitch over the last seconds
	bool primed = false;
};

// For camera::TurnCamera(0, pitch, roll): positive roll brings a view whose
// right side is down back up; positive pitch raises the view.
struct Turn {
	float pitch, roll;
};

// viewRoll: the view's roll against the horizon, positive with the right of the
// screen down (atan2(-s.z, up.z) for s = front x up); viewPitch: asin(front.z).
// Inactive (a bike, an aircraft, a drive-by, a car on its side), it eases to 0.
Turn Step(State &, float viewRoll, float viewPitch, bool active, float dt);

} // namespace headbalance

#pragma once
// Tommy balancing his head in a car's first-person seat view (#37), as numbers.
// The view rolls and pitches with the car (its springs lean the body), but he's
// not a robot: his head comes back a little towards level against the view's
// roll, and against quick pitching (a nose dip under braking, not a hill),
// after a short lag. It eases in and out, never snapping.

namespace headbalance {

struct State {
	float roll = 0, pitch = 0; // rad, the turn now
	float slowPitch = 0;       // rad, the body's pitch over the last seconds
	bool primed = false;
};

// For camera::TurnCamera(0, pitch, roll) (its Turn is right-handed about the
// view's forward): a view whose right side is down gets a negative roll, back
// towards level; a view that dipped gets a positive pitch, raising it.
struct Turn {
	float pitch, roll;
};

// viewRoll: the view's roll against the horizon, positive with the right of the
// screen down (atan2(-s.z, up.z) for s = front x up). bodyPitch: the car body's
// pitch (asin of its forward's z), which his looking about doesn't change.
// facing: the cosine of where he looks about the car's up (1 ahead, -1 behind,
// 0 to a side), so the body's quick pitching is levelled as the view sees it.
enum class Mode : unsigned char {
	Balance, // in a car's seat view
	EaseOut, // a bike, an aircraft, a car on its side: back to nothing, eased
	Drop,    // a drive-by aims the view exactly: nothing, at once
};
Turn Step(State &, float viewRoll, float bodyPitch, float facing, Mode, float dt);

} // namespace headbalance

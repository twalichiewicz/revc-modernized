#pragma once
// Who a ped looks at (#56), as numbers. The game's own head look (CPedIK via
// CPed::SetLookFlag) turns the head and torso within its limits and eases back;
// this decides when, at whom and for how long, the way people do:
//  - someone on foot with nothing else to do glances at the people near it,
//    more often the closer they are, and stares at someone holding a weapon;
//  - Tommy, on foot in third person, looks where the camera looks, and at the
//    person the camera is on when someone is close along that line.
// Headings are the game's: 0 faces +y, positive turns left (forward is
// (-sin h, cos h)).
#include <cstdint>

namespace gazemodel {

struct Point {
	float x = 0, y = 0, z = 0;
};

// Someone the looker can see: where their head is, and what makes them worth a look.
struct Seen {
	int id = -1;
	Point head;
	bool player = false; // Tommy
	bool armed = false;  // a weapon in hand
	bool fast = false;   // sprinting past
};

// One looker's state between frames (ms on the game clock).
struct Memory {
	uint32_t until = 0;   // looking at `target` till then
	uint32_t next = 0;    // no new glance before then
	uint32_t started = 0; // when this look began
	int target = -1;
};
// A look at Tommy is kept up while he stays in view, at most this long (ms).
constexpr uint32_t kTrackCalm = 6000, kTrackArmed = 10000;

struct Glance {
	int target = -1;
	uint32_t ms = 0;     // how long to keep looking (from now)
	bool extend = false; // keeps the current look going: move its end, don't start another
	explicit operator bool() const { return target >= 0; }
};

constexpr float kNotice = 12;     // m: no one further away is looked at
// rad (80 degrees) either side of the body's heading: within the head's own
// reach (CPedIK turns it 90 degrees; women's walks and ducking turn the head
// only, and the game drops a look it can't reach at once).
constexpr float kField = 1.40f;
constexpr float kOthers = 6;      // m: other bystanders are only glanced at this close

float Wrap(float angle);                     // to [-pi, pi)
float HeadingTo(Point from, Point to);       // the heading facing `to` from `from`
// How often (per second) a looker glances at `seen`, from its head `eye` with
// its body facing `heading`; 0 when it wouldn't look at all.
float Rate(Point eye, float heading, const Seen &seen);
// A bystander on foot with nothing else to do. `roll` and `roll2` are uniform
// in [0, 1), fresh each frame; dt in seconds. Returns a new glance, an extension
// of a look at Tommy while he stays in view (`extend`), or nothing.
Glance Choose(Point eye, float heading, const Seen *seen, int count, Memory &memory, uint32_t now, float dt, float roll,
	      float roll2);

// Tommy on foot, third person, not aiming.
struct Follow {
	bool look = false;
	int target = -1;   // looking at this person, or
	float heading = 0; // along this heading (the camera's)
};
constexpr float kCameraWindow = 1.75f; // rad (100 degrees): his head follows the camera this far round
constexpr float kOnLine = .31f;        // rad (18 degrees): someone this close to the camera's line is looked at
constexpr float kKeepLine = .49f;      // rad (28 degrees): and kept while they stay within this
constexpr float kFollowRange = 7, kKeepRange = 9; // m
// `current` is the person he's looking at now (-1 for none).
Follow Track(Point eye, float body, float camera, const Seen *seen, int count, int current);

} // namespace gazemodel

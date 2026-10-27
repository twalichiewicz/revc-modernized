#pragma once
// The pause map's cursor with the controller, drawn to the icons: left alone
// near one it eases onto it, and over one the stick moves it slower, so a
// small icon is easy to land on and pushing on still carries it off. Screen
// pixels throughout.
#include "types.h"

#include <vector>

namespace magnet {

struct Tuning {
	float reach;    // a cursor left this near an icon's centre is pulled onto it
	float grip;     // this near its centre, the cursor is over the icon
	float friction; // the stick's speed over an icon, as a fraction of its own
	float rate;     // the pull, per second: the gap shrinks by e^(-rate * dt)
};

// The icon nearest to `p` within `within` of it, or -1.
int Nearest(const std::vector<Vec2> &icons, Vec2 p, float within);
// The cursor after a frame of `dt` seconds: `move` is what the stick and the
// D-pad move it by, `mapMoving` that the map pans or zooms under it (no pull
// then: the icons are sliding past).
Vec2 Step(Vec2 cursor, Vec2 move, bool mapMoving, const std::vector<Vec2> &icons, const Tuning &tuning, float dt);

} // namespace magnet

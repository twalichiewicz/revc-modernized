#pragma once
// Tommy's feet on uneven ground (#40), as numbers. The game animates him as if
// the ground were flat at the height it stands him on (`floor`, under his
// middle). On a kerb edge, a slope or stairs one foot hangs in the air or sinks
// into the step. Here each foot's ground is compared with that floor:
//  - his hips come down by the deepest a foot has to reach (stepping off a
//    kerb, the lower foot reaches the road);
//  - each foot then comes up from there to its own ground (the upper foot
//    stays on the kerb);
//  - ground beyond reach (a drop, a wall) is ignored, and every change eases in.
// The glue (footplant.cpp) bends the legs to match; heights in metres, world z.

namespace footplantmodel {

struct Foot {
	float animated = 0; // the animation's ankle height
	float ground = 0;   // the ground under it
	bool hit = false;   // ground found within reach
};

struct State {
	float drop = 0;          // hips, now (<= 0)
	float raise[2] = {0, 0}; // each foot from the lowered body, now (>= 0)
	float weight = 0;
};

struct Output {
	float drop = 0, raise[2] = {0, 0};
};

constexpr float kReach = .6f;    // ground further than this from the floor is ignored
constexpr float kMaxDrop = .35f; // the hips come down at most this far
constexpr float kMaxRaise = .45f;
constexpr float kEase = .08f;    // s: time constant of the hips and feet following the ground
constexpr float kFade = .15f;    // s: of the whole fading in or out

// `active`: on foot, standing or moving, nothing else posing his legs.
Output Step(State &state, const Foot feet[2], float floor, bool active, float dt);

// Two-bone leg: the knee and foot that put the foot at `target` (or as near as
// the leg reaches), keeping the thigh and shin lengths and the knee bent the
// way the animation bends it. Any frame; false (unchanged) on bad input.
struct V3 {
	float x = 0, y = 0, z = 0;
};
bool Leg(V3 hip, V3 knee, V3 foot, V3 target, V3 &newKnee, V3 &newFoot);

} // namespace footplantmodel

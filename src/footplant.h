#pragma once
// Tommy's feet planted on kerbs, slopes and stairs (#40). footplantmodel.h
// decides how far his hips come down and each foot comes up from the ground
// under each foot; this reads his animated legs, probes that ground, and bends
// the legs to match over the animation (body::ApplyOver: the root and legs only,
// everything else as the game animates it).
#include <cstdint>

namespace body {
struct Pose;
}

namespace footplant {
// From CPed::PreRender for the player, when nothing else poses his legs
// (swimming, climbing, diving, crouching): fills `pose` and returns the weight
// to apply it with (0: nothing to do).
float Pose(uint8_t *ped, body::Pose &pose);
} // namespace footplant

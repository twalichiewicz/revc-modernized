#pragma once
#include "../../src/reactphysics.h"
#include <array>

namespace probe {
struct JointInfo { const char *name; int tag; reactphysics::Joint joint; };
// Same native tags and ordering as reactions.cpp::kPhysicalTags.
constexpr std::array<JointInfo, reactphysics::Count> kJoints = {{
    {"pelvis",1,reactphysics::Pelvis}, {"chest",3,reactphysics::Chest}, {"head",5,reactphysics::Head},
    {"shoulder_l",32,reactphysics::ShoulderL}, {"elbow_l",33,reactphysics::ElbowL}, {"hand_l",34,reactphysics::HandL},
    {"shoulder_r",22,reactphysics::ShoulderR}, {"elbow_r",23,reactphysics::ElbowR}, {"hand_r",24,reactphysics::HandR},
    {"hip_l",41,reactphysics::HipL}, {"knee_l",42,reactphysics::KneeL}, {"foot_l",43,reactphysics::FootL},
    {"hip_r",51,reactphysics::HipR}, {"knee_r",52,reactphysics::KneeR}, {"foot_r",53,reactphysics::FootR},
}};
} // namespace probe

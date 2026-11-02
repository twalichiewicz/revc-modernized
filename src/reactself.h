#pragma once
#include "reactphysics.h"

namespace reactself {
struct HeadCenters {Vec3 current{},previous{},rest{};};
// One bounded position-constraint pass. Alternate with the body's length
// constraints, then project against the world before deriving velocity.
// Supplying the pre-integration pose also rejects crossings between frames.
void Project(reactphysics::Pose &position, const reactphysics::Pose &rest, const float *inverseMass,
			 const reactphysics::Pose *previous = nullptr, reactphysics::Sweep sweep = nullptr, void *context = nullptr,
			 reactphysics::Contact *upperContact = nullptr, bool rigidCore = false, const HeadCenters *head = nullptr);
} // namespace reactself

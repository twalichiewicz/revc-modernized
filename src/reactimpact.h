#pragma once
#include "reactphysics.h"

namespace reactimpact {
struct Selection {
	reactphysics::Joint a = reactphysics::Pelvis, b = reactphysics::Chest;
	float along = 0;
	bool valid = false;
};
// Position selects the local segment/interpolation, including a distal foot or
// hand. The coarse native body piece breaks geometric ties only.
Selection Resolve(const reactphysics::Pose &pose, Vec3 position, int nativePiece);
} // namespace reactimpact

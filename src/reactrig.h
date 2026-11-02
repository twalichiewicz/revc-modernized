#pragma once
#include "reactphysics.h"
#include <array>
namespace reactrig {
constexpr int Bones = 22;
using Matrix = std::array<float, 16>;
using Matrices = std::array<Matrix, Bones>;
reactphysics::Pose Capture(const Matrices &animated);
// Preserves each NPC's own rig offsets/scales. Unsimulated bones follow
// their physical segment, rather than being moved to guessed midpoints.
// Measured physical neck/axis keep skull collision and final skin orientation
// consistent, including partial fades, without restoring removed-head scales.
Matrices Apply(const Matrices &animated, const reactphysics::Pose &physical, float weight, const Vec3 *neck = nullptr, const Vec3 *headAxis = nullptr);
} // namespace reactrig

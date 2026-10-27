#pragma once
// Posing the viewmodel's arm: the hand where the gun puts it, the forearm
// carrying on from the hand nearly straight (angled down and out a little),
// the upper arm back past the camera, and each finger's two joints solved to
// put its tip on the gun's anchor for it. Set directly, not by IK from the
// shoulder: a viewmodel's arm only has to look right from one eye.
// Unit-tested (tests/test_vmpose.cpp).
#include "vmarms.h"
#include "vmgun.h"

namespace vm {

struct ArmAngles {
	float down = 28.0f, out = 12.0f; // degrees: the forearm off the hand's axis
	float upperOut = 0.35f, upperUp = 0.55f; // the upper arm from the elbow: back, and this much out and up
	Vec3 forearm{0, 0, 0}; // nonzero: the way the forearm runs back from the wrist (world), instead of the angles
	Vec3 upper{0, 0, 0};   // nonzero: the way the upper arm runs back from the elbow (world)
};

// The skinning matrices (W_k · Bind_k⁻¹) for every bone of `arm`, its hand at
// `handWorld`. Digits whose `reach` is set put their tips on `anchors`; the
// rest stay as the hand carries them. `left`: the left arm (out is the other
// way). `bends` (optional, a direction each, zero for none): the side a
// digit's middle joint goes out to, as an anchor may say.
void Pose(const Arm &arm, const Frame &handWorld, const Vec3 anchors[kDigits], const bool reach[kDigits],
          const ArmAngles &angles, bool left, Frame out[kArmBones], const Vec3 *bends = nullptr);

// The ways an arm's forearm and upper arm run back from its hand at `hand`
// (as Pose works them out without overrides): for an arm that should keep to
// the gun as framed while its hand follows the gun's animation.
void ArmDirections(const Frame &hand, const ArmAngles &angles, bool left, Vec3 &forearm, Vec3 &upper);

// Where his hand is for the gun to be at `gunWorld`.
inline Frame HandFor(const Gun &gun, const Frame &gunWorld) { return Compose(gunWorld, Inverse(gun.hold)); }

} // namespace vm

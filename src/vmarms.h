#pragma once
// Tommy's arms for the viewmodel, cut from his own skinned mesh (whichever
// outfit's model he's wearing) with his own texture: from the shoulder out,
// his finger slab (one hooked piece on one bone a hand) cut across into four
// fingers, the cuts capped with their own skin and the fingers parted a little,
// and new bones under his hand: a base and a tip for each finger, bending
// where the finger does, and two for his thumb. Nothing else of his mesh
// changes. All of it is worked out in his hand bone's own frame, which every
// outfit shares (x along the hand to the knuckles at 0.088, +y the palm's
// side, z across, the thumb at +z on the right hand and -z on the left),
// though their bind spaces differ. Unit-tested (tests/test_vmarms.cpp).
#include "vmskin.h"

#include <string>
#include <vector>

namespace vm {

// A skinned model as the game has it: vertices with up to four (bone,
// weight) pairs, the bones by index into `tags` (their HAnim ids) and `bind`
// (each one's frame in skin space: the inverse of its skin-to-bone matrix).
struct SkinnedMesh {
	std::vector<SkinVertex> vertices;
	std::vector<Triangle> triangles;
	std::vector<int> tags;
	std::vector<Frame> bind;
};

enum ArmBone {
	kUpper,
	kFore,
	kHand,
	kIndex,
	kIndexTip,
	kMiddle,
	kMiddleTip,
	kRing,
	kRingTip,
	kPinky,
	kPinkyTip,
	kThumb,
	kThumbTip,
	kArmBones
};
enum Digit { kDigitIndex, kDigitMiddle, kDigitRing, kDigitPinky, kDigitThumb, kDigits };
// A digit's two bones: base, then tip (the tip is base + 1).
inline int DigitBase(int digit) { return kIndex + 2 * digit; }

// One arm: its vertices' bones are ArmBones; everything in skin space.
struct Arm {
	std::vector<SkinVertex> vertices;
	std::vector<Triangle> triangles;
	Frame bind[kArmBones];
	Vec3 tip[kDigits];  // each digit's fingertip: the end of its tip bone
	Vec3 pole[kDigits]; // the side its middle joint stands out to (a direction)
};

struct Arms {
	bool ok = false;
	std::string why; // when not
	Arm right, left;
};

Arms Articulate(const SkinnedMesh &mesh);

} // namespace vm

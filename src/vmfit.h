#pragma once
// Fitting his fingers to a gun: each outfit's hand is its own size and shape
// (some hook their fingers under, some hold them out), so a gun's anchors
// can't suit every one as they stand. For each digit, the fit tries the
// anchor along its slack and the finger's middle joint out at a fan of
// angles, and keeps what puts least of the finger inside the gun (nearest
// the anchor and the plain bend when it's a tie). Done once per outfit and
// gun. Unit-tested (tests/test_vmfit.cpp).
#include "vmpose.h"

namespace vm {

// How many times the gun's surface winds round `p` (its generalized winding
// number), in the gun's frame: over 0.5 inside any of its closed parts, even
// where they overlap.
float Winding(const Gun &gun, Vec3 p);

// How many of `digit`'s vertices are inside the gun, the arm posed in the
// gun's frame (its hand at `hand`) reaching `anchors`.
int Inside(const Arm &arm, const Gun &gun, const Frame &hand, const Vec3 anchors[kDigits], const bool reach[kDigits],
           const Vec3 bends[kDigits], int digit, bool left = false);

// Where each digit reaches on the gun (its frame) and which way its middle
// joint goes out. `names`: the anchors for index, middle, ring, pinky and
// thumb ("r_index", ...); a digit whose anchor the gun hasn't doesn't reach.
// `left`: his left hand, held where the gun's `holdLeft` puts it.
struct Fit {
	Vec3 anchor[kDigits]{}, bend[kDigits]{};
	bool reach[kDigits]{};
	int inside[kDigits]{};
	uint32_t part[kDigits]{}; // the part each anchor moves with
};
Fit FitHand(const Arm &arm, const Gun &gun, const char *const names[kDigits], bool left);

} // namespace vm

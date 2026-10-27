#pragma once
// First person's crosshair, as Counter-Strike 1.6's: four short lines about
// the centre, their gap the spread the next shot would have (so it opens
// with each shot and as he moves, and closes as it recovers). Sizes in
// pixels for a screen this high (2 px thick, 9 long and at least 4 apart at
// 1080). Unit-tested (tests/test_fpcross.cpp).
namespace fpcross {

float MinGap(float screenHeight);
float Length(float screenHeight);
float Thickness(float screenHeight);
// The dark edge round a line (whole pixels, at least one): every crosshair the mod draws.
float Edge(float screenHeight);
// The gap from the centre to each line's inner end: the spread's edge on
// screen (`tanHalfV`: tan of half the view's vertical field of view), past the least.
float Gap(float spreadAngle, float tanHalfV, float screenHeight);

// The four lines (up, down, left, right) about the centre `cx, cy`, `gap`
// from it, as rectangles on whole pixels: crisp, and evenly about it (an odd
// thickness's centre is mid-pixel: the gap rounds out on each side alike).
struct Rect {
	float x, y, w, h;
};
void Lines(float cx, float cy, float gap, float screenHeight, Rect out[4]);

} // namespace fpcross

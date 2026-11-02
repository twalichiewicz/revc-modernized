#pragma once
// What the port's ray-traced shadows are given to trace each frame (its
// dynamic triangles: peds, vehicles, objects; 9 floats a triangle), checked:
// Metal's acceleration structures are undefined for non-finite or absurd
// vertices, and the pass waits on the GPU without a timeout (a freeze).
#include <cstdint>
#include <vector>

namespace rtgeometry {

constexpr float kWorld = 20000;      // m from the origin: VC is about +-2.5 km, its flights 3.5 km up
constexpr float kLongestEdge = 400;  // m: no moving thing is that big (a jumbo is about 70 m)

struct Report {
	int triangles = 0, dropped = 0; // dropped: unfit to trace
	int collapsed = 0, atOrigin = 0; // all three corners one point (at 0,0,0: a matrix of zeros); left out too
	float point[3] = {};    // the first that collapsed away from the origin
	int firstBad = -1;      // its index in what was given
	float bad[9] = {};      // that triangle, as given
	float lo[3] = {}, hi[3] = {}; // the bounds of what's kept
	float longestEdge = 0;  // of what's kept
};

// `count` triangles at `tris`: true when some must be left out, and then `out`
// holds the rest; false, and `out` untouched, when all are fit to trace.
bool Clean(const float *tris, int count, std::vector<float> &out, Report &report);

// The trace's view: `head` its 22 floats (the camera, the far plane's four
// corners, the sun, four parameters), `lights` its lights' records, 16 floats
// each. The first that isn't finite, counting from `head` through the lights,
// or -1.
int BadViewField(const float *head, const float *lights, int count);

// The shadow mask a trace gives back (one byte a pixel: 0 lit, 255 fully
// shadowed): the share of pixels in any shadow, their mean darkness (0..1),
// and the share at least half dark.
struct MaskStats {
	float shadowed = 0, darkness = 0, strong = 0;
};
MaskStats Stats(const uint8_t *mask, int pixels);

} // namespace rtgeometry

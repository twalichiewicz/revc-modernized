#pragma once
// Flat shapes for the HUD with smooth edges. The game's 2D drawing has no
// anti-aliasing, so every edge gets a feather: a strip a pixel wide fading
// from the shape's colour to nothing (per-vertex alpha). Borders are the
// shape pushed out by a set width (mitred), so they're as thick all round.
#include "canvas.h"
#include "types.h"

#include <vector>

namespace shapes {

constexpr float kFeather = 1.0f; // px

// A simple polygon (either winding; concave is fine), filled.
void Fill(Canvas &c, const std::vector<Vec2> &points, CRGBA col, float feather = kFeather);
// The polygon pushed out by `by` px, its corners mitred (and capped, at sharp points).
std::vector<Vec2> Grow(const std::vector<Vec2> &points, float by);
// A five-pointed star about `at`, `r` to its points (pointing up), its inner corners `inner` of that in.
std::vector<Vec2> Star(Vec2 at, float r, float inner);
void Disc(Canvas &c, Vec2 at, float r, CRGBA col, float feather = kFeather);
// A ring `width` wide at radius `r`, `share` of the way round clockwise from the top.
void Ring(Canvas &c, Vec2 at, float r, float width, float share, CRGBA col, float feather = kFeather);
void Line(Canvas &c, Vec2 a, Vec2 b, float width, CRGBA col, float feather = kFeather);

} // namespace shapes

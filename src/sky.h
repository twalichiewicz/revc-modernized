#pragma once
// Volumetric clouds (clouds.cpp) in the game's sky, drawn where CClouds::Render
// draws its flat cloud sprites, before the world: the noise made on a thread
// at start-up, then each frame a quarter of the pixels marched at half the
// frame's resolution, the rest reprojected from the last frame, and the result
// laid over the sky. Lit by the sun (or the moon) in the time of day's
// colours, more and greyer with the weather; the sun's glow goes behind thick
// cloud. At night the game's moon and stars are drawn first.
#include "game.h"

namespace sky {

bool Install();
// The clouds as they were drawn this frame, on the CPU: how much of what's
// at `to` shows through them from `from`, and how much sunlight reaches `at`
// (0..1); false (and 1) with no clouds drawn (indoors, Clouds = 0, the noise
// still being made).
bool Through(const CVector &from, const CVector &to, float &shows);
bool SunAt(const CVector &at, float &sunlight);

} // namespace sky

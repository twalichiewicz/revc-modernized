#pragma once
// A modern lighting look over the 3D frame, after the game's colour filter and
// before the HUD (CCamera::RenderMotionBlur): bloom around bright light (the
// sun, neon, headlights), shafts of light from the sun when it's in view, a
// filmic roll-off of highlights, a touch more contrast and colour, and a
// vignette. Vice City's lighting itself is baked into its models; this is the
// camera's look, not new light or shadows.
namespace grade {

bool Install();
// How much of bullet time's look (0..1: less colour, a deeper vignette).
void SlowMotion(float amount);

} // namespace grade

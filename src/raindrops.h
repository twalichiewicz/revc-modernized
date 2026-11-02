#pragma once
// Drops on the view in the rain (#32): on foot in first person, and on bikes
// and boats, on the "lens"; in a car's or an aircraft's seat view, on its
// windscreen only (masked to the car's own glass, so the dash, pillars and roof
// stay dry). Never in third person. Each drop shows the frame through it,
// upside down, as a real drop does. With RainDrops=0, none.
namespace raindrops {

bool Install();
// After the lighting pass, before the HUD (grade.cpp's CCamera::RenderMotionBlur hook).
void Draw();

} // namespace raindrops

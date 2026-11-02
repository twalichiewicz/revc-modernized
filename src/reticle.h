#pragma once
#include "reticlemodel.h"
namespace reticle {
// Framebuffer pixels; spread is the next shot's angular radius in radians.
void Draw(float x, float y, float spreadAngle = 0.0f, float opacity = 1.0f);
// Same renderer and settings as gameplay; preview supplies its own spread and screen scale.
void Preview(float x, float y, float screenHeight, float spreadAngle);
reticlemodel::Settings Settings();
void Change(reticlemodel::Row row, int direction);
} // namespace reticle

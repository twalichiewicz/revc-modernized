#pragma once
#include "fpcross.h"
#include "types.h"
#include <array>
#include <string>

// Settings and pixel geometry shared by every ordinary aiming reticle and its preview.
namespace reticlemodel {
struct Settings {
	int style = 0, color = 0;
	float size = 1, thickness = 1;
	bool dot = false, dynamic = true;
};
constexpr int kStyles = 3, kColors = 6;
enum class Row { kStyle, kSize, kThickness, kColor, kDot, kDynamic, kDefaults, kCount };
struct Shape {
	fpcross::Rect rects[5]{};
	int count = 0;
	float edge = 1;
};
struct Value {
	const char *key;
	std::string value;
};
Settings Sanitize(Settings settings);
Shape Build(Settings settings, float x, float y, float screenHeight, float spreadAngle, float tanHalfV);
void Change(Settings &settings, Row row, int direction);
std::array<Value, 6> Values(Settings settings);
CRGBA Color(int index);
const char *StyleName(int index);
const char *ColorName(int index);
std::string Label(Settings settings, Row row);
} // namespace reticlemodel

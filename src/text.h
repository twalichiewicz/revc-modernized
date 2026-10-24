#pragma once
// Left-aligned text through the game's CFont. PrintString only queues text: it
// is drawn at the next CFont::DrawFonts (later in the HUD frame, or Flush()).
#include "types.h"

namespace text {

struct Style {
	int16_t font;         // CFont style: 0 standard, 1 bank, 2 heading
	float scaleX, scaleY; // CFont::SetScale
	CRGBA colour;
	bool proportional = true;
};

float Width(const char *ascii, const Style &style);
// Top and bottom of the line CFont draws for Draw(x, y, ...) in this style.
void LineExtent(const Style &style, float y, float &top, float &bottom);
void Draw(float x, float y, const char *ascii, const Style &style);
void Flush();

} // namespace text

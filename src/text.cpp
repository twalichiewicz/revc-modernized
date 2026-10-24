#include "text.h"
#include "game.h"

#include <algorithm>
#include <cstring>

using game::api;

namespace text {
namespace {

constexpr size_t kMaxChars = 63;

struct Unicode {
	uint16_t chars[kMaxChars + 1];
	explicit Unicode(const char *ascii)
	{
		char buf[kMaxChars + 1];
		strncpy(buf, ascii, kMaxChars);
		buf[kMaxChars] = '\0';
		api.AsciiToUnicode(buf, chars);
	}
};

void Apply(const Style &style)
{
	api.FontSetBackgroundOff();
	api.FontSetBackGroundOnlyTextOff();
	if (style.proportional)
		api.FontSetPropOn();
	else
		api.FontSetPropOff();
	api.FontSetFontStyle(style.font);
	api.FontSetJustifyOff();
	api.FontSetCentreOff();
	api.FontSetDropShadowPosition(0);
	api.FontSetScale(style.scaleX, style.scaleY);
	api.FontSetColor(style.colour);
}

} // namespace

float Width(const uint16_t *text, const Style &style)
{
	Apply(style);
	return api.FontGetStringWidth(const_cast<uint16_t *>(text), true);
}

float Width(const char *ascii, const Style &style) { return Width(Unicode(ascii).chars, style); }

void LineExtent(const Style &style, float y, float &top, float &bottom)
{
	Unicode u("0");
	Apply(style);
	float rect[4]; // CRect; only its y extent is meaningful for one line
	api.FontGetTextRect(rect, 0.0f, y, u.chars);
	top = std::min(rect[1], rect[3]);
	bottom = std::max(rect[1], rect[3]);
}

void Draw(float x, float y, const uint16_t *text, const Style &style)
{
	Apply(style);
	api.FontPrintString(x, y, const_cast<uint16_t *>(text));
}

void Draw(float x, float y, const char *ascii, const Style &style) { Draw(x, y, Unicode(ascii).chars, style); }

void Flush() { api.FontDrawFonts(); }

} // namespace text

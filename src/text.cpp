#include "text.h"
#include "fontbuf.h"
#include "game.h"
#include "log.h"

#include <algorithm>
#include <cstdio>
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
	// As Draw: the font's measuring runs on from an open token too (fontbuf.h).
	uint16_t closed[256];
	size_t n = 0;
	for (; n + 1 < sizeof closed / sizeof closed[0] && text[n]; n++)
		closed[n] = text[n];
	closed[n] = 0;
	fontbuf::CloseTokens(closed);
	Apply(style);
	return api.FontGetStringWidth(closed, true);
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

// The game's font never checks a piece fits its buffer before queuing it
// (fontbuf.h): if this text mightn't, what's queued is drawn first.
void MakeRoom(const uint16_t *text)
{
	if (!api.fontBuffer || !api.fontBufferEnd || !*api.fontBufferEnd)
		return;
	size_t units = 0, words = 1;
	for (const uint16_t *p = text; *p; p++, units++)
		words += *p == ' ';
	const ptrdiff_t used = *api.fontBufferEnd - api.fontBuffer;
	if (used > 0 && !fontbuf::Fits(size_t(used), units, words)) {
		static int logged;
		if (logged++ < 3)
			Log("text: the font's buffer was nearly full (%d of %d bytes); drew it before queuing more", int(used),
			    int(fontbuf::kBytes));
		api.FontDrawFonts();
	}
}

void Draw(float x, float y, const uint16_t *text, const Style &style)
{
	// Never a token left open (fontbuf.h): the font's parser would copy on to the next '~' in memory.
	uint16_t closed[256];
	size_t n = 0;
	for (; n + 1 < sizeof closed / sizeof closed[0] && text[n]; n++)
		closed[n] = text[n];
	closed[n] = 0;
	fontbuf::CloseTokens(closed);
	fontbuf::EndAfterToken(closed, sizeof closed / sizeof closed[0]);
	Guard();
	Apply(style);
	MakeRoom(closed);
	api.FontPrintString(x, y, closed);
}

void Draw(float x, float y, const char *ascii, const Style &style) { Draw(x, y, Unicode(ascii).chars, style); }

void Flush()
{
	Guard();
	api.FontDrawFonts();
}

void Guard()
{
	if (!api.fontBuffer || !api.fontBufferEnd || !*api.fontBufferEnd)
		return;
	const ptrdiff_t used = *api.fontBufferEnd - api.fontBuffer;
	if (used <= 0 || size_t(used) > fontbuf::kBytes + 0x200)
		return; // not a buffer we can read as one
	const size_t good = fontbuf::GoodUpTo(api.fontBuffer, size_t(used));
	if (good == size_t(used))
		return;
	static int logged;
	if (logged++ < 5) {
		char hex[200] = "";
		for (size_t i = good + fontbuf::kHeader, k = 0; i + 1 < size_t(used) && k < 24; i += 2, k++)
			snprintf(hex + strlen(hex), sizeof hex - strlen(hex), " %04x", unsigned(api.fontBuffer[i] | api.fontBuffer[i + 1] << 8));
		Log("text: the font's buffer went bad at byte %zu of %d (it would have crashed the game); dropped from there. "
		    "Its text:%s",
		    good, int(used), hex);
	}
	*api.fontBufferEnd = api.fontBuffer + good;
}

} // namespace text

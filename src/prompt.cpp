#include "prompt.h"
#include "hints.h"
#include "hinttext.h"
#include "text.h"

#include <string>

namespace prompt {
namespace {

constexpr float kIconUnits = 17.0f; // CFont::DrawButton's icon: 17 of the font's scale across

text::Style Style(float screenH, float alpha)
{
	return {1, screenH * 0.3f / 480, screenH * 0.42f / 448, {255, 255, 255, uint8_t(235 * alpha)}};
}

std::string Ascii(const std::u16string &s)
{
	std::string out;
	for (char16_t c : s)
		out += c < 128 ? char(c) : '?';
	return out;
}

} // namespace

float Height(float screenH)
{
	float top, bottom;
	text::LineExtent(Style(screenH, 1.0f), 0.0f, top, bottom);
	return (bottom - top) * 1.9f;
}

float Draw(Canvas &canvas, float right, float cy, float screenH, bindings::Action action, const char *label,
           float alpha)
{
	const text::Style style = Style(screenH, alpha);
	float top, bottom;
	text::LineExtent(style, 0.0f, top, bottom);
	const float lineH = bottom - top, pillH = lineH * 1.9f, pad = pillH * 0.38f, textY = cy - (top + bottom) / 2;
	// The button as hints name it: on a pad a token the font draws as its icon, on the keyboard the key's name.
	const std::string button = Ascii(hinttext::Printable(hints::Glyph(action))); // never ending in a token
	const bool key = hints::KeyboardNow();
	const float capPad = lineH * 0.35f;
	const float buttonW = button.empty() ? 0.0f : key ? text::Width(button.c_str(), style) + 2 * capPad : kIconUnits * style.scaleX;
	const float w = pad + buttonW + pad * 0.7f + text::Width(label, style) + pad * 1.2f;
	const float x = right - w, y = cy - pillH / 2;
	canvas.Pill(x, y, w, pillH, {0, 0, 0, uint8_t(175 * alpha)});
	if (key && !button.empty()) {
		const float capH = lineH + capPad;
		canvas.RoundRectOutline(x + pad, cy - capH / 2, buttonW, capH, capH * 0.2f, screenH * 1.5f / 1080.0f,
		                        {255, 255, 255, uint8_t(200 * alpha)});
	}
	canvas.Flush();
	if (!button.empty())
		text::Draw(x + pad + (key ? capPad : 0.0f), textY, button.c_str(), style);
	text::Draw(x + pad + buttonW + pad * 0.7f, textY, label, style);
	return w;
}

} // namespace prompt

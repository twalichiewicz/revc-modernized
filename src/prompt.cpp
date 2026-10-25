#include "prompt.h"
#include "controls.h"
#include "text.h"

#include <cmath>

namespace prompt {
namespace {

text::Style Style(float screenH, float alpha)
{
	return {1, screenH * 0.3f / 480, screenH * 0.42f / 448, {255, 255, 255, uint8_t(235 * alpha)}};
}

CRGBA Faded(CRGBA c, float alpha) { return {c.r, c.g, c.b, uint8_t(c.a * alpha)}; }

void Disc(Canvas &canvas, float cx, float cy, float r, CRGBA col) { canvas.Pill(cx - r, cy - r, 2 * r, 2 * r, col); }

void Ring(Canvas &canvas, float cx, float cy, float r, float width, CRGBA col)
{
	constexpr int kSeg = 24;
	const float in = r - width;
	for (int i = 0; i < kSeg; i++) {
		const float a0 = 6.2831853f * i / kSeg, a1 = 6.2831853f * (i + 1) / kSeg;
		canvas.Quad({cx + in * cosf(a0), cy + in * sinf(a0)}, {cx + r * cosf(a0), cy + r * sinf(a0)},
		            {cx + r * cosf(a1), cy + r * sinf(a1)}, {cx + in * cosf(a1), cy + in * sinf(a1)}, col);
	}
}

void Frame(Canvas &canvas, float x0, float y0, float x1, float y1, float width, CRGBA col)
{
	canvas.Quad({x0, y0}, {x1, y0}, {x1, y0 + width}, {x0, y0 + width}, col);
	canvas.Quad({x0, y1 - width}, {x1, y1 - width}, {x1, y1}, {x0, y1}, col);
	canvas.Quad({x0, y0 + width}, {x0 + width, y0 + width}, {x0 + width, y1 - width}, {x0, y1 - width}, col);
	canvas.Quad({x1 - width, y0 + width}, {x1, y0 + width}, {x1, y1 - width}, {x1 - width, y1 - width}, col);
}

// The D-pad: a plus, the pressed arm lit.
void Dpad(Canvas &canvas, float cx, float cy, float icon, Button arm, float alpha)
{
	const float a = icon * 0.36f / 2, h = icon / 2;
	const CRGBA dim{255, 255, 255, uint8_t(70 * alpha)}, lit{255, 255, 255, uint8_t(255 * alpha)};
	canvas.Quad({cx - a, cy - h}, {cx + a, cy - h}, {cx + a, cy + h}, {cx - a, cy + h}, dim);
	canvas.Quad({cx - h, cy - a}, {cx + h, cy - a}, {cx + h, cy + a}, {cx - h, cy + a}, dim);
	switch (arm) {
	case Button::kUp: canvas.Quad({cx - a, cy - h}, {cx + a, cy - h}, {cx + a, cy - a}, {cx - a, cy - a}, lit); break;
	case Button::kDown: canvas.Quad({cx - a, cy + a}, {cx + a, cy + a}, {cx + a, cy + h}, {cx - a, cy + h}, lit); break;
	case Button::kLeft: canvas.Quad({cx - h, cy - a}, {cx - a, cy - a}, {cx - a, cy + a}, {cx - h, cy + a}, lit); break;
	default: canvas.Quad({cx + a, cy - a}, {cx + h, cy - a}, {cx + h, cy + a}, {cx + a, cy + a}, lit); break;
	}
}

// A light button cap with the PlayStation symbol (Square, Circle) in its colour.
void PlayStationButton(Canvas &canvas, float cx, float cy, float icon, bool west, float alpha)
{
	const float r = icon / 2, stroke = icon * 0.11f;
	Disc(canvas, cx, cy, r, Faded({236, 236, 240, 255}, alpha));
	if (west) {
		const float s = r * 0.48f;
		Frame(canvas, cx - s, cy - s, cx + s, cy + s, stroke, Faded({213, 98, 176, 255}, alpha));
	} else {
		Ring(canvas, cx, cy, r * 0.56f, stroke, Faded({226, 62, 62, 255}, alpha));
	}
}

// A disc in the Xbox button's colour (X blue, B red); its letter is drawn as text.
void XboxButton(Canvas &canvas, float cx, float cy, float icon, bool west, float alpha)
{
	Disc(canvas, cx, cy, icon / 2, Faded(west ? CRGBA{46, 112, 214, 255} : CRGBA{210, 52, 46, 255}, alpha));
}

} // namespace

float Height(float screenH)
{
	float top, bottom;
	text::LineExtent(Style(screenH, 1.0f), 0.0f, top, bottom);
	return (bottom - top) * 1.9f;
}

float Draw(Canvas &canvas, float right, float cy, float screenH, Button button, const char *label, float alpha)
{
	const text::Style style = Style(screenH, alpha);
	float top, bottom;
	text::LineExtent(style, 0.0f, top, bottom);
	const float pillH = (bottom - top) * 1.9f, pad = pillH * 0.38f;
	const bool face = button == Button::kFaceWest || button == Button::kFaceEast;
	const bool west = button == Button::kFaceWest, playStation = face && controls::PlayStationPad();
	const float icon = pillH * (face ? 0.66f : 0.56f);
	const float w = pad + icon + pad * 0.7f + text::Width(label, style) + pad * 1.2f;
	const float x = right - w, y = cy - pillH / 2;
	canvas.Pill(x, y, w, pillH, {0, 0, 0, uint8_t(175 * alpha)});
	const float cx = x + pad + icon / 2;
	if (!face)
		Dpad(canvas, cx, cy, icon, button, alpha);
	else if (playStation)
		PlayStationButton(canvas, cx, cy, icon, west, alpha);
	else
		XboxButton(canvas, cx, cy, icon, west, alpha);
	canvas.Flush();
	if (face && !playStation) {
		text::Style letter = style;
		letter.scaleX *= 0.9f;
		letter.scaleY *= 0.9f;
		letter.colour = {255, 255, 255, uint8_t(255 * alpha)};
		const char *name = west ? "X" : "B";
		float t, b;
		text::LineExtent(letter, 0.0f, t, b);
		text::Draw(cx - text::Width(name, letter) / 2, cy - (t + b) / 2, name, letter);
	}
	text::Draw(x + pad + icon + pad * 0.7f, cy - (top + bottom) / 2, label, style);
	return w;
}

} // namespace prompt

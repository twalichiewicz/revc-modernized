#include "brickphone.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr float kBodyH = kBrickPhoneHeight, kAntennaH = kBrickPhoneAntenna;
constexpr float kMarqueeHold = 1.0f, kMarqueeSpeed = 18.0f; // seconds, dots per second

// The LCD: two lines of 5x7 dots (margins 2.5 dots, lines 7, gap 3).
constexpr float kLcdX0 = 0.13f, kLcdY0 = 0.37f, kLcdX1 = 0.87f, kLcdY1 = 0.87f;
constexpr int kLcdRows = 22;
constexpr float kLabelPitch = 0.0105f; // dot size of the key labels

constexpr CRGBA kBodyTop{54, 57, 63, 255}, kBodyBottom{35, 37, 41, 255}, kEdge{10, 11, 13, 230}, kRim{98, 104, 114, 255};
constexpr CRGBA kAntenna{20, 21, 24, 255}, kSlot{18, 19, 22, 255}, kBezel{14, 15, 17, 255};
constexpr CRGBA kLcd{30, 15, 6, 255}, kDotOff{50, 27, 12, 255}, kDotOn{255, 158, 64, 255};
constexpr CRGBA kKey{70, 75, 82, 255}, kKeySide{24, 26, 29, 255}, kLabel{196, 200, 207, 255};
constexpr CRGBA kSend{56, 142, 78, 255}, kEnd{166, 58, 52, 255};

// 5x7 glyphs, rows top down, 0x10 the leftmost dot.
struct Glyph {
	char c;
	uint8_t rows[7];
};
constexpr Glyph kGlyphs[] = {
    {'A', {0x0e, 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11}}, {'B', {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}},
    {'C', {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e}}, {'D', {0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c}},
    {'E', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}}, {'F', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}},
    {'G', {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f}}, {'H', {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
    {'I', {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}}, {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}, {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}},
    {'M', {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}}, {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}}, {'P', {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}},
    {'Q', {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d}}, {'R', {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}},
    {'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}}, {'T', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}}, {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a}}, {'X', {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04}}, {'Z', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f}},
    {'0', {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}}, {'1', {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'2', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}}, {'3', {0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e}},
    {'4', {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}}, {'5', {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e}},
    {'6', {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e}}, {'7', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}}, {'9', {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c}},
    {':', {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00}}, {'*', {0x00, 0x04, 0x15, 0x0e, 0x15, 0x04, 0x00}},
    {'#', {0x0a, 0x0a, 0x1f, 0x0a, 0x1f, 0x0a, 0x0a}},
};

// The phone's units on screen, and where to draw.
struct Frame {
	Canvas &canvas;
	float x, y, u;      // the body's top-left on screen, and the unit in pixels
	float c = 1, s = 0; // rotation about the bottom centre
	Vec2 At(float lx, float ly) const
	{
		const float dx = (lx - 0.5f) * u, dy = (ly - kBodyH) * u;
		return {x + 0.5f * u + c * dx - s * dy, y + kBodyH * u + s * dx + c * dy};
	}
};

Frame FrameOf(Canvas &canvas, const BrickPhone &phone)
{
	return {canvas, phone.x, phone.y, phone.unit, cosf(phone.tilt), sinf(phone.tilt)};
}

CRGBA Mix(CRGBA a, CRGBA b, float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	auto m = [t](uint8_t p, uint8_t q) { return uint8_t(p + (q - p) * t); };
	return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}
CRGBA Shade(CRGBA c, float k)
{
	auto m = [k](uint8_t v) { return uint8_t(std::min(255.0f, v * k)); };
	return {m(c.r), m(c.g), m(c.b), c.a};
}
CRGBA Faded(CRGBA c, float k) { return {c.r, c.g, c.b, uint8_t(c.a * k)}; }

void Rect(const Frame &f, float x0, float y0, float x1, float y1, CRGBA col)
{
	f.canvas.Quad(f.At(x0, y0), f.At(x1, y0), f.At(x1, y1), f.At(x0, y1), col);
}

// A rounded rectangle, shaded from `top` to `bottom`.
void RoundRect(const Frame &f, float x0, float y0, float x1, float y1, float r, CRGBA top, CRGBA bottom)
{
	constexpr int kArc = 5;
	r = std::min({r, (x1 - x0) / 2, (y1 - y0) / 2});
	const float cx[4] = {x1 - r, x1 - r, x0 + r, x0 + r}, cy[4] = {y0 + r, y1 - r, y1 - r, y0 + r};
	Vec2 edge[4 * (kArc + 1)];
	int n = 0;
	for (int k = 0; k < 4; k++) // the corners clockwise from the top right (y down)
		for (int i = 0; i <= kArc; i++) {
			const float a = float(M_PI) * 0.5f * (k - 1 + float(i) / kArc);
			edge[n++] = {cx[k] + r * cosf(a), cy[k] + r * sinf(a)};
		}
	auto vtx = [&f](Vec2 l) {
		const Vec2 p = f.At(l.x, l.y);
		return Vtx{p.x, p.y, 0, 0};
	};
	auto col = [&](float y) { return Mix(top, bottom, (y - y0) / (y1 - y0)); };
	const Vec2 mid{(x0 + x1) / 2, (y0 + y1) / 2};
	for (int i = 0; i < n; i++) {
		const Vec2 a = edge[i], b = edge[(i + 1) % n];
		f.canvas.Tri(vtx(mid), vtx(a), vtx(b), col(mid.y), col(a.y), col(b.y));
	}
}

const uint8_t *GlyphRows(char c)
{
	for (const Glyph &g : kGlyphs)
		if (g.c == c)
			return g.rows;
	return nullptr;
}

// Lights the dots of `str` from grid column `first` on (6 columns a character),
// within columns [0, cols).
template <typename Lit>
void DotText(const char *str, int first, int cols, Lit lit)
{
	for (int i = 0; str[i]; i++)
		if (const uint8_t *rows = GlyphRows(str[i]))
			for (int r = 0; r < 7; r++)
				for (int c = 0; c < 5; c++) {
					const int col = first + i * 6 + c;
					if ((rows[r] & (0x10 >> c)) && col >= 0 && col < cols)
						lit(col, r);
				}
}

int DotWidth(const char *str) { return int(strlen(str)) * 6 - 1; }

// One line of the LCD at grid row `row`: centred if it fits, else scrolling past.
void LcdLine(const Frame &f, float x0, float y0, float pitch, int cols, int row, const char *str, float t)
{
	const float y = y0 + row * pitch, d = pitch * 0.8f, inset = (pitch - d) / 2, glow = pitch * 0.35f;
	for (int r = 0; r < 7; r++)
		for (int c = 0; c < cols; c++) {
			const float dx = x0 + c * pitch + inset, dy = y + r * pitch + inset;
			Rect(f, dx, dy, dx + d, dy + d, kDotOff);
		}
	auto lit = [&](int c, int r) {
		const float dx = x0 + c * pitch + inset, dy = y + r * pitch + inset;
		Rect(f, dx - glow, dy - glow, dx + d + glow, dy + d + glow, Faded(kDotOn, 0.2f));
		Rect(f, dx, dy, dx + d, dy + d, kDotOn);
	};
	const int width = DotWidth(str);
	if (width <= cols) {
		DotText(str, (cols - width) / 2, cols, lit);
		return;
	}
	const int loop = width + 1 + 3 * 6, shift = int(std::max(0.0f, t - kMarqueeHold) * kMarqueeSpeed) % loop;
	DotText(str, 1 - shift, cols, lit);
	DotText(str, 1 - shift + loop, cols, lit);
}

// A key: its cap, the side under it, and a label in small dots when they're big enough to read.
void Key(const Frame &f, float x0, float y0, float x1, float y1, CRGBA cap, const char *label)
{
	RoundRect(f, x0, y0 + 0.025f, x1, y1 + 0.025f, 0.04f, kKeySide, kKeySide);
	RoundRect(f, x0, y0, x1, y1, 0.04f, Shade(cap, 1.2f), Shade(cap, 0.88f));
	if (!label || kLabelPitch * f.u < 0.9f)
		return;
	const float p = kLabelPitch, lx = (x0 + x1) / 2 - DotWidth(label) * p / 2, ly = (y0 + y1) / 2 - 3.5f * p;
	DotText(label, 0, 64, [&](int c, int r) {
		Rect(f, lx + c * p, ly + r * p, lx + (c + 0.85f) * p, ly + (r + 0.85f) * p, kLabel);
	});
}

void Draw(const Frame &f, const char *line1, const char *line2, float t, float sendGlow)
{
	// Antenna, behind the top of the body.
	Rect(f, 0.66f, -0.07f, 0.88f, 0.05f, kAntenna);
	RoundRect(f, 0.698f, -kAntennaH - 0.012f, 0.842f, 0.0f, 0.072f, Shade(kRim, 0.8f), Shade(kRim, 0.5f));
	RoundRect(f, 0.71f, -kAntennaH, 0.83f, 0.0f, 0.06f, Shade(kAntenna, 1.6f), kAntenna);
	// Body: a dark edge and a light rim (it shows on both light and dark scenes),
	// and a highlight down its left side.
	RoundRect(f, -0.035f, -0.035f, 1.035f, kBodyH + 0.035f, 0.19f, kEdge, kEdge);
	RoundRect(f, -0.012f, -0.012f, 1.012f, kBodyH + 0.012f, 0.17f, kRim, Shade(kRim, 0.6f));
	RoundRect(f, 0.0f, 0.0f, 1.0f, kBodyH, 0.16f, kBodyTop, kBodyBottom);
	Rect(f, 0.035f, 0.22f, 0.06f, kBodyH - 0.22f, {255, 255, 255, 16});
	for (int i = 0; i < 3; i++) // earpiece
		RoundRect(f, 0.33f, 0.13f + i * 0.055f, 0.67f, 0.155f + i * 0.055f, 0.0125f, kSlot, kSlot);
	// Display.
	RoundRect(f, 0.08f, 0.32f, 0.92f, 0.92f, 0.07f, kBezel, kBezel);
	RoundRect(f, kLcdX0, kLcdY0, kLcdX1, kLcdY1, 0.03f, kLcd, kLcd);
	const float pitch = (kLcdY1 - kLcdY0) / kLcdRows;
	const int cols = int((kLcdX1 - kLcdX0) / pitch);
	const float x0 = kLcdX0 + ((kLcdX1 - kLcdX0) - cols * pitch) / 2;
	LcdLine(f, x0, kLcdY0, pitch, cols, 2, line1, t);
	LcdLine(f, x0, kLcdY0, pitch, cols, 12, line2, t);
	// Keypad: send / clear / end, then the digits.
	constexpr float kCols[3][2] = {{0.10f, 0.34f}, {0.38f, 0.62f}, {0.66f, 0.90f}};
	Key(f, kCols[0][0], 1.04f, kCols[0][1], 1.18f, Mix(kSend, {120, 255, 150, 255}, sendGlow), "SND");
	Key(f, kCols[1][0], 1.04f, kCols[1][1], 1.18f, kKey, "CLR");
	Key(f, kCols[2][0], 1.04f, kCols[2][1], 1.18f, kEnd, "END");
	constexpr const char *kDigits[4][3] = {{"1", "2", "3"}, {"4", "5", "6"}, {"7", "8", "9"}, {"*", "0", "#"}};
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 3; c++) {
			const float y = 1.30f + r * 0.235f;
			Key(f, kCols[c][0], y, kCols[c][1], y + 0.17f, kKey, kDigits[r][c]);
		}
	RoundRect(f, 0.40f, 2.52f, 0.60f, 2.55f, 0.015f, kSlot, kSlot); // mouthpiece
}

} // namespace

void DrawBrickPhone(Canvas &canvas, const BrickPhone &phone)
{
	Draw(FrameOf(canvas, phone), phone.line1, phone.line2, phone.scroll, phone.sendGlow);
}

Vec2 BrickPhonePoint(const BrickPhone &phone, float x, float y)
{
	struct NoCanvas : Canvas {
		void SetTexture(void *) override {}
		void SetDepthMode(DepthMode) override {}
		void Tri(const Vtx &, const Vtx &, const Vtx &, CRGBA, CRGBA, CRGBA) override {}
		void Flush() override {}
	} none;
	return FrameOf(none, phone).At(x, y);
}

float BrickPhoneLcdCentre(const BrickPhone &phone) { return BrickPhonePoint(phone, 0.0f, (kLcdY0 + kLcdY1) / 2).y; }

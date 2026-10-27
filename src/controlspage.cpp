#include "controlspage.h"
#include "actions.h"
#include "config.h"
#include "controls.h"
#include "controlsmodel.h"
#include "draw.h"
#include "game.h"
#include "hints.h"
#include "hooks.h"
#include "keys.h"
#include "log.h"
#include "text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace game;
using bindings::Action;
using bindings::Pad;
namespace cm = controlsmodel;

namespace controlspage {
namespace {

constexpr int32_t kActionGoBack = 35;   // a page's Back entry
constexpr size_t kControlMethod = 0x5b; // FrontEndMenuManager: 0 Standard, 1 Classic
constexpr size_t kVibration = 9;        // FrontEndMenuManager: the pad rumbles
constexpr float kMouseMin = 1.0f / 3200, kMouseMax = 1.0f / 200; // CMenuManager::CheckSliderMovement's range
constexpr float kLookMin = 0.25f, kLookMax = 2.0f;               // the mod's LookSensitivity on the page
constexpr int kBars = 16;
constexpr int kSettings = 3; // VIBRATION, INVERT LOOK, LOOK SENSITIVITY
constexpr int kLookSlider = 2;
constexpr int kMouseRows = 3; // SENSITIVITY, INVERT LOOK, MOUSE STEERING
constexpr float kHeadingPx = 40; // the page's headings: as tall as the frontend's own rows (scale 1 of 448)
constexpr float kStripY = 717;   // the pad's settings, clear of Back (at 396 of 448: 796 of the stage)

constexpr CRGBA kPink{255, 150, 225, 255}, kGreen{124, 204, 116, 255}, kAmber{232, 176, 74, 255};
constexpr CRGBA kWhite{255, 255, 255, 255}, kDim{255, 255, 255, 153}, kBox{0, 0, 0, 158}, kInk{17, 17, 17, 255};

constexpr uintptr_t kJoystickName = 0x7a14f4; // the 128-byte buffer macVC.ini's JoystickName is read into

bool gAvailable;
cm::Focus gFocus;
int gScroll[2]; // each key box's first row shown
Im2DCanvas gCanvas;

// What a click lands on, as this frame's drawing laid it out (screen pixels).
enum class Hit : uint8_t { kTab, kSituation, kSetting, kKey, kMouseRow, kUseThis };
struct Target {
	float x0, y0, x1, y1;
	Hit hit;
	int a, b;
};
std::vector<Target> gTargets;
bool gMouseWas[8];  // the mouse's buttons, while a key is wanted
bool gUseThisWas;   // the pad's Y, last frame
cm::MouseGate gMouse;
bool gFrontendRelease; // this frame's left release is the frontend's (a click on its BACK)
int gUseThis = -1;  // the status's USE THIS ONE, this frame

// The approved mockup's 1600x900 stage on the screen.
struct Stage {
	float s, ox;
	Vec2 P(float x, float y) const { return {ox + x * s, y * s}; }
	float L(float v) const { return v * s; }
};
Stage Now()
{
	const float W = float(ScreenWidth()), H = float(ScreenHeight()), s = H / 900.0f;
	return {s, (W - 1600.0f * s) / 2};
}

// The game's font, its line `px` stage pixels tall (font 1 the wide capitals, 2 the heading).
text::Style Font(int16_t font, float px, CRGBA col)
{
	static float unit[3];
	if (!unit[font]) {
		float t, b;
		text::LineExtent({font, 1.0f, 1.0f, col}, 0.0f, t, b);
		unit[font] = b - t > 0 ? b - t : 1.0f;
	}
	const cm::TextScale scale = cm::ScaleFor(Now().L(px), unit[font]);
	return {font, scale.x, scale.y, col};
}

// Text vertically centred on `cy` (screen pixels), from `x`.
void TextAt(float x, float cy, const std::string &s, const text::Style &style)
{
	float t, b;
	text::LineExtent(style, 0.0f, t, b);
	text::Draw(x, cy - (t + b) / 2, s.c_str(), style);
}

float Width(const std::string &s, const text::Style &style) { return text::Width(s.c_str(), style); }

// A heading word with the frontend's dark drop shadow.
void Heading(float x, float cy, const std::string &s, float px, CRGBA col)
{
	const float shadow = Now().L(px * 0.05f);
	TextAt(x + shadow, cy + shadow, s, Font(2, px, {20, 20, 20, uint8_t(230 * col.a / 255)}));
	TextAt(x, cy, s, Font(2, px, col));
}

void Line(Vec2 a, Vec2 b, float width, CRGBA col)
{
	const float len = std::hypot(b.x - a.x, b.y - a.y);
	if (len <= 0)
		return;
	const float nx = -(b.y - a.y) / len * width / 2, ny = (b.x - a.x) / len * width / 2;
	gCanvas.Quad({a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}, col);
}

void Disc(Vec2 c, float r, CRGBA col) { gCanvas.Pill(c.x - r, c.y - r, 2 * r, 2 * r, col); }

void Image(void *raster, float x, float y, float w, float h, CRGBA col = kWhite)
{
	if (!raster)
		return;
	gCanvas.SetTexture(raster);
	const Vtx tl{x, y, 0, 0}, tr{x + w, y, 1, 0}, br{x + w, y + h, 1, 1}, bl{x, y + h, 0, 1};
	gCanvas.Tri(tl, tr, br, col);
	gCanvas.Tri(tl, br, bl, col);
	gCanvas.SetTexture(nullptr);
}

void Icon(Pad pad, float x, float y, float size)
{
	if (RwTexture *t = hints::Icon(pad))
		Image(api.RwTextureGetRaster(t), x, y, size, size);
}

// The pad pictures macVC's frontend ships (it never shows them), and where their buttons are, in 256ths.
struct Picture {
	const char *file, *slot;
	Vec2 at[size_t(Pad::kCount)]; // by Pad: kNone, A B X Y LB RB LT RT L3 R3 Back Start Up Down Left Right LeftRight sticks
};
const Picture kPictures[] = {
    {"models/frontend_xone.txd", "ctlxone",
     {{0, 0}, {198, 128}, {215, 113}, {180, 113}, {198, 97}, {62, 68}, {195, 68}, {58, 47}, {200, 47}, {60, 117},
      {162, 150}, {109, 114}, {148, 114}, {96, 137}, {96, 157}, {85, 147}, {107, 147}, {96, 147}, {60, 117}, {162, 150}}},
    {"models/frontend_ds4.txd", "ctlds4",
     {{0, 0}, {201, 119}, {218, 104}, {184, 104}, {201, 90}, {60, 52}, {196, 52}, {57, 40}, {199, 40}, {92, 138},
      {163, 138}, {86, 68}, {172, 68}, {52, 92}, {52, 114}, {41, 103}, {63, 103}, {52, 103}, {92, 138}, {163, 138}}},
    {"models/frontend_nsw.txd", "ctlnsw",
     {{0, 0}, {204, 128}, {224, 114}, {183, 114}, {204, 98}, {48, 57}, {207, 57}, {57, 44}, {199, 44}, {51, 115},
      {163, 147}, {91, 95}, {166, 95}, {90, 133}, {90, 152}, {76, 143}, {103, 143}, {90, 143}, {51, 115}, {163, 147}}},
};
static_assert(sizeof kPictures / sizeof kPictures[0] == 3, "one picture per bindings::PadFamily");

void *PictureRaster(int family)
{
	static void *raster[3];
	static bool tried[3];
	if (!tried[family]) {
		tried[family] = true;
		const Picture &p = kPictures[family];
		FILE *f = fopen(p.file, "rb"); // cwd: GameData. CTxdStore::LoadTxd waits forever for a file it can't open.
		if (!f) {
			Log("controls page: %s missing", p.file);
			return nullptr;
		}
		fclose(f);
		int slot = api.TxdFindSlot(p.slot);
		if (slot < 0)
			slot = api.TxdAddSlot(p.slot);
		if (slot >= 0 && api.TxdLoad(slot, p.file)) {
			api.TxdAddRef(slot);
			if (RwTexDictionary *dict = TxdDictionary(slot))
				if (RwTexture *tex = api.TexDictionaryFindNamedTexture(dict, "fe_controller"))
					raster[family] = api.RwTextureGetRaster(tex);
		}
		Log("controls page: %s %s", p.file, raster[family] ? "loaded" : "missing");
	}
	return raster[family];
}

// The left half of the pad (its row goes in the left column).
bool LeftSide(Pad p)
{
	switch (p) {
	case Pad::kLT: case Pad::kLB: case Pad::kUp: case Pad::kDown: case Pad::kLeft: case Pad::kRight:
	case Pad::kLeftRight: case Pad::kBack: case Pad::kL3: case Pad::kLeftStick:
		return true;
	default:
		return false;
	}
}

// Every listed action's keys now (the game's three settings, the mod's two).
cm::KeyTable Table()
{
	cm::KeyTable t;
	for (const cm::KeySection &section : cm::KeySections())
		for (Action a : section.actions) {
			if (a < Action::kGameCount) {
				t[a] = actions::GameSlots(a);
			} else {
				const keys::Binding b = actions::Keys(a);
				t[a] = {b.key[0], b.key[1], 0};
			}
		}
	return t;
}

int KeyRows()
{
	int n = 0;
	for (const cm::KeySection &section : cm::KeySections())
		n += int(section.actions.size());
	return n;
}

cm::Sizes Sizes() { return {kSettings, KeyRows() + kMouseRows, kLookSlider}; }

int Bars(float value, float lo, float hi) { return std::clamp(int(std::lround((value - lo) / (hi - lo) * (kBars - 1))) + 1, 1, kBars); }

// The frontend's stepped-bar slider, `on` of its bars lit.
void Slider(float x, float cy, int on, const Stage &st)
{
	for (int i = 0; i < kBars; i++) {
		const float h = st.L(8 + i * 1.4f), w = st.L(9);
		gCanvas.Quad({x + i * st.L(13), cy + st.L(15) - h}, {x + i * st.L(13) + w, cy + st.L(15) - h},
		             {x + i * st.L(13) + w, cy + st.L(15)}, {x + i * st.L(13), cy + st.L(15)},
		             i < on ? kPink : CRGBA{255, 255, 255, 72});
	}
}

// A white pill row of choices, the chosen one lit (the pause tabs' look), centred at stage y `top`.
void Pills(const std::vector<std::string> &items, int chosen, float top, float px, float h, bool focused, const Stage &st)
{
	const text::Style style = Font(1, px, kWhite);
	const float pad = st.L(20), gap = st.L(10), edge = st.L(7);
	std::vector<float> widths;
	float total = 0;
	for (const std::string &s : items) {
		widths.push_back(Width(s, style) + 2 * pad);
		total += widths.back();
	}
	total += gap * float(items.size() - 1);
	float x = st.ox + st.L(800) - total / 2;
	const float y = st.L(top), ph = st.L(h);
	gCanvas.Pill(x - edge, y - edge, total + 2 * edge, ph + 2 * edge, {0, 0, 0, 171});
	if (focused)
		gCanvas.RoundRectOutline(x - edge, y - edge, total + 2 * edge, ph + 2 * edge, (ph + 2 * edge) / 2, st.L(2), kPink);
	for (size_t i = 0; i < items.size(); i++) {
		const bool on = int(i) == chosen;
		gTargets.push_back({x, y, x + widths[i], y + ph, Hit::kTab, int(i), 0});
		if (on)
			gCanvas.Pill(x, y, widths[i], ph, kWhite);
		TextAt(x + pad, y + ph / 2, items[i], Font(1, px, on ? CRGBA{0, 0, 0, 255} : CRGBA{255, 255, 255, 171}));
		x += widths[i] + gap;
	}
}

// The status: a dot (green, amber, a grey ring), the name, the reason, and its fix.
void StatusLine(const cm::Status &status, const Stage &st)
{
	const text::Style name = Font(1, 26, status.dot == cm::Dot::kMissing ? kDim : kWhite);
	const text::Style why = Font(1, 26, kAmber), dash = Font(1, 26, kDim), fix = Font(1, 22, {0, 0, 0, 255});
	const float dot = st.L(13), gap = st.L(14), pad = st.L(24), h = st.L(38);
	float w = dot + gap + Width(status.name, name);
	if (!status.why.empty())
		w += gap + Width("-", dash) + gap + Width(status.why, why);
	const bool useThis = status.useThis >= 0 && gFocus.tab == cm::Tab::kController;
	const float fixW = useThis ? st.L(34) + Width("USE THIS ONE", fix) + st.L(14) : 0;
	if (useThis)
		w += gap + fixW;
	float x = st.ox + st.L(800) - (w + 2 * pad) / 2;
	const float cy = st.L(184) + h / 2;
	gCanvas.Pill(x, cy - h / 2, w + 2 * pad, h, {0, 0, 0, 158});
	x += pad;
	const Vec2 c{x + dot / 2, cy};
	if (status.dot == cm::Dot::kMissing)
		gCanvas.RoundRectOutline(c.x - dot / 2 + st.L(1), c.y - dot / 2 + st.L(1), dot - st.L(2), dot - st.L(2), dot / 2, st.L(2), {255, 255, 255, 115});
	else
		Disc(c, dot / 2, status.dot == cm::Dot::kReady ? kGreen : kAmber);
	x += dot + gap;
	TextAt(x, cy, status.name, name);
	x += Width(status.name, name);
	if (!status.why.empty()) {
		TextAt(x + gap, cy, "-", dash);
		x += gap + Width("-", dash) + gap;
		TextAt(x, cy, status.why, why);
		x += Width(status.why, why);
	}
	if (useThis) {
		x += gap;
		gTargets.push_back({x, cy - st.L(15), x + fixW, cy + st.L(15), Hit::kUseThis, status.useThis, 0});
		gCanvas.Pill(x, cy - st.L(15), fixW, st.L(30), kWhite);
		Icon(Pad::kY, x + st.L(4), cy - st.L(13), st.L(26));
		TextAt(x + st.L(34), cy, "USE THIS ONE", fix);
	}
}

void ControllerTab(const Stage &st)
{
	// The situations
	static const char *kSituations[] = {"on foot", "driving", "flying", "pause map"};
	const float headPx = kHeadingPx, gap = st.L(40);
	float total = 0;
	for (const char *s : kSituations)
		total += Width(s, Font(2, headPx, kWhite));
	total += gap * 3;
	float x = st.ox + st.L(800) - total / 2;
	const float cy = st.L(258);
	for (int i = 0; i < 4; i++) {
		const bool on = int(gFocus.situation) == i;
		const float w = Width(kSituations[i], Font(2, headPx, kWhite));
		gTargets.push_back({x, cy - st.L(20), x + w, cy + st.L(20), Hit::kSituation, i, 0});
		Heading(x, cy, kSituations[i], headPx, on ? kPink : CRGBA{255, 255, 255, 153});
		if (on && gFocus.area == cm::Area::kSituations)
			gCanvas.Quad({x, cy + st.L(17)}, {x + w, cy + st.L(17)}, {x + w, cy + st.L(20)}, {x, cy + st.L(20)}, kPink);
		x += w + gap;
	}

	// The pad, its buttons' rows down both sides, a line from each to its button
	const int family = int(controls::Family());
	const Picture &pic = kPictures[family];
	const float picX = 590, picY = 250, picW = 420;
	const float leftDot = picX - 16, rightDot = picX + picW + 16, leftIcon = picX - 64, rightIcon = picX + picW + 22;
	auto at = [&](Pad p) { return st.P(picX + pic.at[size_t(p)].x * picW / 256, picY + pic.at[size_t(p)].y * picW / 256); };
	const auto rows = cm::Legend(hints::Switches(), gFocus.situation);
	int left = 0, right = 0;
	struct Placed {
		const cm::LegendRow *row;
		bool left;
		float y;
	};
	std::vector<Placed> placed;
	for (const cm::LegendRow &row : rows) {
		const bool l = LeftSide(row.pad);
		placed.push_back({&row, l, l ? 336.0f + 46.0f * float(left++) : 336.0f + 44.0f * float(right++)});
	}
	for (const Placed &p : placed) { // the lines, and a held button's glow
		const Vec2 to = at(p.row->pad), from = st.P(p.left ? leftDot : rightDot, p.y);
		const Vec2 knee{to.x + (p.left ? -st.L(20) : st.L(20)), from.y};
		const bool held = gFocus.situation != cm::PageSituation::kPauseMap && controls::Down(p.row->pad);
		if (held)
			Disc(to, st.L(18), {255, 150, 225, 140});
		Disc(from, st.L(4.5f), kInk);
		Line(from, knee, st.L(3), kInk);
		Line(knee, to, st.L(3), kInk);
	}
	gCanvas.Flush();
	Image(PictureRaster(family), st.ox + st.L(picX), st.L(picY), st.L(picW), st.L(picW));
	for (const Placed &p : placed) {
		const bool held = gFocus.situation != cm::PageSituation::kPauseMap && controls::Down(p.row->pad);
		const text::Style label = Font(1, 26, held ? kPink : kWhite);
		const float cy = st.L(p.y), icon = st.L(40);
		if (p.left) {
			const float iconX = st.ox + st.L(leftIcon);
			Icon(p.row->pad, iconX, cy - icon / 2, icon);
			float textRight = iconX - st.L(10);
			if (p.row->pad2 != Pad::kNone) {
				Icon(p.row->pad2, iconX - icon - st.L(4), cy - icon / 2, icon);
				textRight -= icon + st.L(4);
			}
			TextAt(textRight - Width(p.row->label, label), cy, p.row->label, label);
		} else {
			const float iconX = st.ox + st.L(rightIcon);
			Icon(p.row->pad, iconX, cy - icon / 2, icon);
			float textX = iconX + icon + st.L(10);
			if (p.row->pad2 != Pad::kNone) {
				Icon(p.row->pad2, textX - st.L(6), cy - icon / 2, icon);
				textX += icon + st.L(4);
			}
			TextAt(textX, p.row->more.empty() ? cy : cy - st.L(10), p.row->label, label);
			if (!p.row->more.empty())
				TextAt(textX, cy + st.L(16), p.row->more, Font(1, 22, kPink));
		}
	}

	// The pad's settings
	gCanvas.Quad(st.P(450, kStripY - 23), st.P(1460, kStripY - 23), st.P(1460, kStripY + 23), st.P(450, kStripY + 23), kBox);
	const bool vibration = api.frontEndMenuManager[kVibration] != 0, invert = *api.invertLookForPad != 0;
	const char *names[kSettings] = {"VIBRATION", "INVERT LOOK", "LOOK SENSITIVITY"};
	const float slots[kSettings] = {500, 800, 1060};
	for (int i = 0; i < kSettings; i++) {
		const bool focused = gFocus.area == cm::Area::kSettings && gFocus.item == i;
		const text::Style label = Font(1, 24, focused ? kPink : kDim);
		const Vec2 p = st.P(slots[i], kStripY);
		gTargets.push_back({p.x, p.y - st.L(23), p.x + st.L(i < 2 ? 260 : 400), p.y + st.L(23), Hit::kSetting, i, 0});
		TextAt(p.x, p.y, names[i], label);
		const float vx = p.x + Width(names[i], label) + st.L(14);
		if (i < 2)
			TextAt(vx, p.y, (i == 0 ? vibration : invert) ? "ON" : "OFF", Font(1, 24, kWhite));
		else
			Slider(vx, p.y, Bars(gConfig.lookSensitivity, kLookMin, kLookMax), st);
	}
}

// One key cap: the key's name in a light outline, pink when it's focused.
float Cap(float x, float cy, const std::string &name, bool focused, const Stage &st, int item, int slot)
{
	const text::Style style = Font(1, 22, focused ? kPink : kWhite);
	const float w = std::max(Width(name, style), st.L(12)) + st.L(18), h = st.L(30);
	gTargets.push_back({x, cy - h / 2, x + w, cy + h / 2, Hit::kKey, item, slot});
	gCanvas.Quad({x, cy - h / 2}, {x + w, cy - h / 2}, {x + w, cy + h / 2}, {x, cy + h / 2},
	             focused ? CRGBA{255, 150, 225, 64} : CRGBA{0, 0, 0, 115});
	gCanvas.RoundRectOutline(x, cy - h / 2, w, h, 0.0f, st.L(2), focused ? kPink : CRGBA{255, 255, 255, 140});
	TextAt(x + st.L(9), cy, name, style);
	return w + st.L(10);
}

void KeyboardTab(const Stage &st)
{
	const cm::KeyTable table = Table();
	const auto &sections = cm::KeySections();
	const int rowsInBox = 10;
	int first = 0; // the first row of this box in the focus's numbering
	for (int box = 0; box < 2; box++) {
		const float bx = box == 0 ? 130 : 820, by = 250, bw = 650, bh = 488;
		gCanvas.Quad(st.P(bx, by), st.P(bx + bw, by), st.P(bx + bw, by + bh), st.P(bx, by + bh), kBox);
		Heading(st.ox + st.L(bx + 28), st.L(by + 34), sections[size_t(box)].title, kHeadingPx, kPink);
		// Its rows: the section's actions, and in the right box the mouse's heading and settings after them.
		const int actionRows = int(sections[size_t(box)].actions.size());
		const int mouseHead = box == 1 ? actionRows : -1; // the line the mouse's heading takes
		const int count = actionRows + (box == 1 ? kMouseRows : 0);
		auto line = [&](int r) { return mouseHead >= 0 && r >= actionRows ? r + 1 : r; }; // a row's line in the box
		const int lines = count + (mouseHead >= 0 ? 1 : 0);
		const bool focusHere = gFocus.tab == cm::Tab::kKeyboard && gFocus.area == cm::Area::kKeys &&
		                       gFocus.item >= first && gFocus.item < first + count;
		if (focusHere) { // keep the focused row in view (with the mouse's heading over its first)
			const int at = line(gFocus.item - first);
			gScroll[box] = std::clamp(gScroll[box], at - rowsInBox + 1, at == mouseHead + 1 ? at - 1 : at);
		}
		gScroll[box] = std::clamp(gScroll[box], 0, std::max(0, lines - rowsInBox));
		for (int l = gScroll[box]; l < std::min(lines, gScroll[box] + rowsInBox); l++) {
			const float cy = st.L(by + 6 + 56 + 42 * float(l - gScroll[box]) + 21);
			const float lx = st.ox + st.L(bx + 28), vx = st.ox + st.L(bx + 340);
			if (l == mouseHead) {
				Heading(lx, cy, "mouse", kHeadingPx, kPink);
				continue;
			}
			const int r = mouseHead >= 0 && l > mouseHead ? l - 1 : l;
			const bool focused = focusHere && gFocus.item - first == r;
			if (focused)
				gCanvas.Quad({st.ox + st.L(bx), cy - st.L(21)}, {st.ox + st.L(bx + bw), cy - st.L(21)},
				             {st.ox + st.L(bx + bw), cy + st.L(21)}, {st.ox + st.L(bx), cy + st.L(21)}, {255, 150, 225, 46});
			const text::Style label = Font(1, 26, focused ? kPink : kWhite);
			if (r < actionRows) {
				const Action a = sections[size_t(box)].actions[size_t(r)];
				TextAt(lx, cy, cm::Label(a), label);
				const auto &keys = table.at(a);
				const bool none = !keys[0] && !keys[1] && !keys[2];
				const int slots = a < Action::kGameCount ? 3 : 2;
				float x = vx;
				for (int s = 0; s < slots; s++) {
					const bool here = focused && gFocus.slot == s;
					if (here && gFocus.capturing)
						x += Cap(x, cy, "PRESS A KEY...", true, st, first + r, s);
					else if (keys[size_t(s)])
						x += Cap(x, cy, keys::DisplayName(keys[size_t(s)]), here, st, first + r, s);
					else if (here || (none && s == 0)) // an empty slot: where a key would go
						x += Cap(x, cy, "", here, st, first + r, s);
				}
			} else {
				const int m = r - actionRows;
				gTargets.push_back({st.ox + st.L(bx), cy - st.L(21), st.ox + st.L(bx + bw), cy + st.L(21), Hit::kMouseRow,
				                    first + r, 0});
				static const char *kMouse[kMouseRows] = {"SENSITIVITY", "INVERT LOOK", "MOUSE STEERING"};
				TextAt(lx, cy, kMouse[m], label);
				if (m == 0)
					Slider(vx, cy, Bars(*api.mouseAccel, kMouseMin, kMouseMax), st);
				else
					TextAt(vx, cy, (m == 1 ? api.mousePointerStateHelper[1] != 0 : !*api.disableMouseSteering) ? "ON" : "OFF",
					       Font(1, 26, kWhite));
			}
		}
		first += actionRows;
	}
}

// The page's own list (Standard Controls, Redefine Controls, Detect Joystick,
// Mouse Settings, Restore Defaults, and what macVC adds) goes, but Back.
void TrimList()
{
	auto action = [](int entry) { return *reinterpret_cast<const int32_t *>(MenuPageEntry(kPage, entry)); };
	uint8_t back[kMenuEntrySize] = {};
	bool found = false;
	for (int i = 0; i < kMenuEntries && action(i) != 0; i++)
		if (action(i) == kActionGoBack) {
			memcpy(back, MenuPageEntry(kPage, i), kMenuEntrySize);
			found = true;
		}
	for (int i = 0; i < kMenuEntries; i++)
		memset(MenuPageEntry(kPage, i), 0, kMenuEntrySize);
	if (!found)
		return;
	// Alone it would take the list's first line; it goes where Audio Setup's is, under its last row.
	uint8_t *entry = MenuPageEntry(kPage, 0);
	memcpy(entry, back, kMenuEntrySize);
	*reinterpret_cast<uint16_t *>(entry + 32) = 320;
	*reinterpret_cast<uint16_t *>(entry + 34) = 367 + 29;
	entry[36] = 3;
}

const uint16_t *Wide(const char *ascii, uint16_t (&out)[32])
{
	size_t i = 0;
	for (; ascii[i] && i + 1 < 32; i++)
		out[i] = uint8_t(ascii[i]);
	out[i] = 0;
	return out;
}

// The action of a key row, by the focus's numbering (KeySections' rows in order).
Action RowAction(int item)
{
	for (const cm::KeySection &section : cm::KeySections()) {
		if (item < int(section.actions.size()))
			return section.actions[size_t(item)];
		item -= int(section.actions.size());
	}
	return Action::kModEnd;
}

float FromBars(int bars, float lo, float hi) { return lo + float(bars - 1) / float(kBars - 1) * (hi - lo); }

// A slider stepped by `step` bars; select steps on and wraps round.
int Step(int bars, int step, bool wrap) { return wrap ? bars % kBars + 1 : std::clamp(bars + step, 1, kBars); }

void SaveGame()
{
	if (api.SaveINISettings)
		api.SaveINISettings();
	if (api.SaveINIControllerSettings)
		api.SaveINIControllerSettings();
}

// The Controller tab's settings: VIBRATION, INVERT LOOK, LOOK SENSITIVITY.
void ChangeSetting(int i, int step)
{
	if (i == 0)
		api.frontEndMenuManager[kVibration] ^= 1;
	else if (i == 1)
		*api.invertLookForPad ^= 1;
	else {
		const int bars = Step(Bars(gConfig.lookSensitivity, kLookMin, kLookMax), step, step == 0);
		gConfig.lookSensitivity = FromBars(bars, kLookMin, kLookMax);
		char value[16];
		snprintf(value, sizeof value, "%.2f", gConfig.lookSensitivity);
		SaveValue("Minimap", "LookSensitivity", value);
		return;
	}
	SaveGame();
}

// The mouse's settings: SENSITIVITY, INVERT LOOK, MOUSE STEERING.
void ChangeMouse(int m, int step)
{
	if (m == 0)
		*api.mouseAccel = FromBars(Step(Bars(*api.mouseAccel, kMouseMin, kMouseMax), step, step == 0), kMouseMin, kMouseMax);
	else if (m == 1)
		api.mousePointerStateHelper[1] ^= 1;
	else
		*api.disableMouseSteering = !*api.disableMouseSteering;
	SaveGame();
}

void StartCapture()
{
	gFocus.capturing = true;
	for (int b = 1; b < 8; b++) // a button held as it starts isn't the new key
		gMouseWas[b] = api.GetIsMouseButtonDown(api.controlsManager, b);
}

// What the frontend's input does on the page; false: it's the frontend's (Back).
bool Act(cm::Input in)
{
	const bool keysTab = gFocus.tab == cm::Tab::kKeyboard && gFocus.area == cm::Area::kKeys;
	const int keyRows = KeyRows();
	const int step = in == cm::Input::kLeft ? -1 : in == cm::Input::kRight ? 1 : 0;
	if (keysTab && gFocus.item >= keyRows && (step || in == cm::Input::kSelect)) { // the mouse's rows
		ChangeMouse(gFocus.item - keyRows, step);
		return true;
	}
	if (const int bars = cm::SliderStep(gFocus, in, Sizes())) {
		ChangeSetting(gFocus.item, bars);
		return true;
	}
	if (in == cm::Input::kSelect && gFocus.tab == cm::Tab::kController && gFocus.area == cm::Area::kSettings) {
		ChangeSetting(gFocus.item, 0);
		return true;
	}
	if (in == cm::Input::kSelect && keysTab) {
		if (RowAction(gFocus.item) >= Action::kGameCount)
			gFocus.slot = std::min(gFocus.slot, 1); // the mod's actions have two
		StartCapture();
		return true;
	}
	return cm::Apply(gFocus, in, Sizes());
}

// One action's keys, from `was` to `now`, into the game's settings or the mod's.
void Store(Action a, const std::array<uint16_t, 3> &was, const std::array<uint16_t, 3> &now)
{
	if (a >= Action::kGameCount) {
		keys::Binding b;
		b.key[0] = now[0];
		b.key[1] = now[1];
		actions::SetKeys(a, b);
		return;
	}
	for (int type = 0; type < 3; type++) {
		if (was[size_t(type)] == now[size_t(type)])
			continue;
		api.ClearSettingsAssociatedWithAction(api.controlsManager, int(a), type);
		if (now[size_t(type)] == 0)
			continue;
		if (type < 2)
			api.SetControllerKeyAssociatedWithAction(api.controlsManager, int(a), now[size_t(type)], type);
		else
			api.SetMouseButtonAssociatedWithAction(api.controlsManager, int(a), now[size_t(type)] - keys::kMouseBase);
	}
	api.ResetSettingOrder(api.controlsManager, int(a));
}

// The new key for the focused cap: taken from any other action it overlaps, then saved.
void BindKey(uint16_t key)
{
	const Action a = RowAction(gFocus.item);
	const bool mouse = key > keys::kMouseBase;
	int slot = gFocus.slot;
	if (a < Action::kGameCount) // the game's: a mouse button only in its mouse setting, keys only in the others
		slot = mouse ? 2 : (slot == 2 ? 0 : slot);
	cm::KeyTable table = Table();
	const cm::KeyTable was = table;
	std::vector<Action> changed = cm::Rebind(table, a, slot, key);
	changed.push_back(a);
	for (Action c : changed)
		Store(c, was.at(c), table.at(c));
	if (api.SaveINIControllerSettings)
		api.SaveINIControllerSettings();
	actions::GameKeysChanged();
	gFocus.capturing = false;
	Log("controls page: %s %s", cm::Label(a), keys::DisplayName(key).c_str());
}

// A key is wanted: Escape cancels; else the first key or mouse button that goes down.
void Capture()
{
	if (!api.GetIsKeyboardKeyJustDown || !api.ClearSettingsAssociatedWithAction) {
		gFocus.capturing = false;
		return;
	}
	if (api.GetIsKeyboardKeyJustDown(api.controlsManager, keys::kSpecialBase)) { // Escape
		gFocus.capturing = false;
		return;
	}
	for (int key = 32; key < 127; key++)
		if ((key < 'a' || key > 'z') && api.GetIsKeyboardKeyJustDown(api.controlsManager, key))
			return BindKey(uint16_t(key));
	for (int key = keys::kSpecialBase + 1; key < keys::kSpecialBase + 56; key++)
		if (api.GetIsKeyboardKeyJustDown(api.controlsManager, key))
			return BindKey(uint16_t(key));
	for (int b = 2; b < 8; b++) { // the left button: a click on the cap (UserInput)
		const bool down = api.GetIsMouseButtonDown(api.controlsManager, b);
		const bool pressed = down && !gMouseWas[b];
		gMouseWas[b] = down;
		if (pressed)
			return BindKey(uint16_t(keys::kMouseBase + b));
	}
}

// The game reads this pad from now on, and remembers it (macVC.ini's JoystickName).
void UseThisOne(int id)
{
	controls::UseJoystick(id);
	for (const cm::Joystick &j : controls::Pads())
		if (j.id == id) {
			char *name = reinterpret_cast<char *>(const_cast<void *>(hooks::ImageBase())) + kJoystickName;
			snprintf(name, 128, "%s", j.name.c_str());
		}
	if (api.SaveINIControllerSettings)
		api.SaveINIControllerSettings();
	Log("controls page: the game reads joystick %d", id);
}

// The page's target under the pointer, if any (as this frame's drawing laid them out).
const Target *TargetAt(uint8_t *menu)
{
	const float mx = float(MenuMouseX(menu)), my = float(MenuMouseY(menu));
	for (const Target &t : gTargets)
		if (mx >= t.x0 && mx <= t.x1 && my >= t.y0 && my <= t.y1)
			return &t;
	return nullptr;
}

void Click(const Target &t)
{
	switch (t.hit) {
	case Hit::kTab:
		gFocus = {};
		gFocus.tab = cm::Tab(t.a);
		break;
	case Hit::kSituation:
		gFocus.area = cm::Area::kSituations;
		gFocus.situation = cm::PageSituation(t.a);
		break;
	case Hit::kSetting:
		gFocus.area = cm::Area::kSettings;
		gFocus.item = t.a;
		ChangeSetting(t.a, 0);
		break;
	case Hit::kKey:
		gFocus.area = cm::Area::kKeys;
		gFocus.item = t.a;
		gFocus.slot = t.b;
		StartCapture();
		break;
	case Hit::kMouseRow:
		gFocus.area = cm::Area::kKeys;
		gFocus.item = t.a;
		ChangeMouse(t.a - KeyRows(), 0);
		break;
	case Hit::kUseThis:
		UseThisOne(t.a);
		break;
	}
}

} // namespace

void SetAvailable(bool available) { gAvailable = available; }

void Enter(uint8_t *menu)
{
	if (!gAvailable)
		return;
	TrimList();
	MenuOption(menu) = 0;
	MenuOptionCount(menu) = 1;
	api.frontEndMenuManager[kControlMethod] = 0; // the mod's sticks need Standard
	actions::LoadKeys(); // the menu can open before the first gameplay frame reads them
	gFocus = {};
	gMouse = {};
	gScroll[0] = gScroll[1] = 0;
}

void Draw(uint8_t *)
{
	if (!gAvailable)
		return;
	const Stage st = Now();
	gTargets.clear();
	SetHudRenderStates();
	gCanvas.SetTexture(nullptr);
	// The frontend wraps text at x 630 of a centred 4:3 screen (1381 of the stage): words past it would go on the next line.
	auto &wrapX = *reinterpret_cast<float *>(api.fontDetails + 0x2c);
	const float wrap = wrapX;
	api.FontSetWrapx(float(ScreenWidth()));
	Pills({"CONTROLLER", "KEYBOARD & MOUSE"}, int(gFocus.tab), 128, 26, 36, gFocus.area == cm::Area::kTabs, st);
	if (gFocus.tab == cm::Tab::kController) {
		const cm::Status status = cm::PadStatus(controls::Pads(), controls::GameJoystickId());
		gUseThis = status.useThis;
		StatusLine(status, st);
		ControllerTab(st);
	} else {
		StatusLine(cm::KeyStatus(cm::Unbound(Table())), st);
		KeyboardTab(st);
	}
	const char *help = gFocus.tab == cm::Tab::kController ? "PRESS ANY BUTTON TO TEST IT"
	                   : gFocus.capturing                  ? "PRESS A KEY - ESC CANCELS"
	                                                       : "SELECT AN ACTION, THEN PRESS ITS NEW KEY";
	const text::Style style = Font(1, 28, kWhite);
	TextAt(st.ox + st.L(1576) - Width(help, style), st.L(876), help, style);
	gCanvas.Flush();
	text::Flush(); // the frontend has drawn its text for this frame
	api.FontSetWrapx(wrap);
}

bool UserInput(uint8_t *menu)
{
	if (!gAvailable)
		return false;
	// The frontend selects on the left button's release: a click the page took keeps it.
	gFrontendRelease = false;
	if (!api.newMouseState[0] && api.oldMouseState[0]) {
		gFrontendRelease = gMouse.Release();
		if (!gFrontendRelease)
			api.oldMouseState[0] = 0;
	}
	const bool click = LeftClickJustDown();
	const Target *hit = click ? TargetAt(menu) : nullptr;
	if (gFocus.capturing) {
		if (click) {
			gMouse.Press(true);
			api.oldMouseState[0] = 1;
			const bool onCap = hit && hit->hit == Hit::kKey && hit->a == gFocus.item && hit->b == gFocus.slot;
			switch (cm::OnClickWhileCapturing(onCap, hit != nullptr)) {
			case cm::CaptureClick::kBind: BindKey(uint16_t(keys::kMouseBase + 1)); break;
			case cm::CaptureClick::kRedirect: gFocus.capturing = false; Click(*hit); break;
			case cm::CaptureClick::kCancel: gFocus.capturing = false; break;
			}
		} else {
			Capture();
		}
		return true;
	}
	// USE THIS ONE: Y on the pad it names, read from that pad (the game may not read
	// it, and the frontend may swap Y with B)
	const bool y = gUseThis >= 0 && controls::Down(gUseThis, Pad::kY);
	if (y && !gUseThisWas && gFocus.tab == cm::Tab::kController)
		UseThisOne(gUseThis);
	gUseThisWas = y;
	if (click) {
		gMouse.Press(hit != nullptr);
		if (hit) {
			api.oldMouseState[0] = 1; // the page's click: not the frontend's too
			Click(*hit);
		}
	}
	return false;
}

void ProcessInput(uint8_t *, uint8_t &down, uint8_t &up, uint8_t &select, uint8_t &back, int8_t &change)
{
	if (!gAvailable || (select && gFrontendRelease)) // a click on the frontend's BACK is its own
		return;
	cm::Input in;
	if (up)
		in = cm::Input::kUp;
	else if (down)
		in = cm::Input::kDown;
	else if (change < 0)
		in = cm::Input::kLeft;
	else if (change > 0)
		in = cm::Input::kRight;
	else if (select)
		in = cm::Input::kSelect;
	else if (back)
		in = cm::Input::kBack;
	else
		return;
	if (Act(in))
		down = up = select = back = 0, change = 0;
}

const uint16_t *Text(const char *key)
{
	static uint16_t entry[32], title[32];
	if (!gAvailable || !key)
		return nullptr;
	if (!strcmp(key, "FEO_CON"))
		return Wide("Controls", entry);
	if (!strcmp(key, "FET_CTL"))
		return Wide("CONTROLS", title);
	return nullptr;
}

} // namespace controlspage

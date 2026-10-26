#include "menu.h"
#include "config.h"
#include "controls.h"
#include "draw.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "text.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <utility>

using namespace game;

namespace menu {
namespace {

struct Tab {
	const char *label;
	int32_t page;
};
// GTA IV's order: the map first, the game (load/quit) before the settings.
constexpr Tab kTabs[] = {
    {"MAP", kPageMap}, {"BRIEF", kPageBriefs}, {"STATS", kPageStats}, {"GAME", kPagePause}, {"OPTIONS", kPageOptions},
};
constexpr int kNumTabs = sizeof kTabs / sizeof kTabs[0];
constexpr int kTabGame = 3;
constexpr int kMenuPages = 45; // aScreens
constexpr int32_t kActionChangeMenu = 4, kActionGoBack = 35;

// Return addresses of frontend calls (macVC 1.0.5217, relative to the image base).
constexpr uint32_t kCursorSites[] = {0xee3b8, 0xee3d0}; // DrawBackground: the cursor's shadow, the cursor
constexpr uint32_t kLegendTitleScaleSite = 0xf82c0;     // PrintMap: CFont::SetScale for "FE_MLG"
constexpr uint32_t kLegendBoxSite = 0xf83ec;            // PrintMap: CSprite2d::DrawRect behind the legend
constexpr uint32_t kLegendTitleSite = 0xf8498;          // PrintMap: CFont::PrintString of "FE_MLG"
constexpr uint32_t kLegendEntryScaleSite = 0xf8510;     // PrintMap: CFont::SetScale for the entries

constexpr int kStickInUse = 32;        // of 128: a stick pushed this far is the controller in use

// The pause map (CMenuManager, MAP_ENHANCEMENTS): its half size and middle,
// in pixels; zoomed out to 162 and in to 1000 (448ths of the screen height).
constexpr size_t kMapSize = 0x40, kMapCentreX = 0x44, kMapCentreY = 0x48, kShowMouse = 0x1b4;
constexpr float kMapMinSize = 162.0f, kMapMaxSize = 1000.0f;
// Its cursor with the controller, in screen heights per second: the left
// stick at full push, the D-pad, the right stick moving the map; zooming
// (per second, a trigger fully in: e to that power); the cursor stops this
// far from the screen's edge and moves the map instead.
constexpr float kMapCursorSpeed = 0.9f, kMapNudgeSpeed = 0.1f, kMapPanSpeed = 1.1f, kMapZoomRate = 1.8f, kMapEdge = 0.05f;
constexpr float kStickDead = 0.12f, kStickCurve = 2.2f; // of the stick's travel; its speed's power

constexpr float kLegendScale = 0.55f;  // at most
constexpr float kLegendMaxHeight = 0.6f; // of the screen: a long legend shrinks to fit
constexpr CRGBA kWhite{255, 255, 255, 255};

struct Box {
	float x0, y0, x1, y1;
	bool Contains(float x, float y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
};

void (*orig_DrawBackground)(uint8_t *, bool);
void (*orig_UserInput)(uint8_t *);
void (*orig_ProcessUserInput)(uint8_t *, uint8_t, uint8_t, uint8_t, uint8_t, int8_t);
void (*orig_DrawRect)(const CRect &, const CRGBA &);
void (*orig_Draw2DPolygon)(float, float, float, float, float, float, float, float, const CRGBA &);
void (*orig_DrawLegend)(int, int, int);
uint16_t *(*orig_TextGet)(uint8_t *, const char *);
SpriteDrawFn gSpriteDraw;
CVector2D *gMapCrosshair; // PrintMap's crosshair, in screen pixels

const uint8_t *gText; // image base, for call sites
bool gTabsOn, gLegendOn, gCircleBack;
using Clock = std::chrono::steady_clock;
Clock::time_point gMapLastInput;
uint8_t gMapInputSaved[kPadOldState]; // the pad's state, kept from the game while it reads the map's input
bool gMapInputHeld;
bool gFreshOpen;      // gameplay was on screen since the menu was last drawn
bool gPrevLB, gPrevRB;
Box gTabBoxes[kNumTabs];
bool gHaveTabBoxes;
Im2DCanvas gCanvas;

// The cursor is drawn last by DrawBackground; with tabs it is held back and
// drawn after them.
struct DeferredSprite {
	const CSprite2d *sprite;
	float rect[4];
	CRGBA col;
};
bool gDeferCursor;
DeferredSprite gCursor[2];
int gNumCursor;

// Legend: everything PrintMap draws for it is moved from its vanilla box
// (top-left gLegendFrom) to the corner (gLegendTo) and scaled.
bool gLegendBoxKnown; // this frame's legend background has been seen
bool gInLegend;       // inside CRadar::DrawLegend
Vec2 gLegendFrom, gLegendTo;
float gLegendScale = kLegendScale; // this frame's (the title is scaled before the box is seen: last frame's)

uint32_t Offset(const void *ret) { return uint32_t(static_cast<const uint8_t *>(ret) - gText); }

bool TabsActive(const uint8_t *menu) { return gTabsOn && MenuActive(menu) && !MenuGameNotLoaded(menu); }

int RootTab(int32_t page)
{
	for (int i = 0; i < kNumTabs; i++)
		if (kTabs[i].page == page)
			return i;
	return -1;
}

// The tab a page belongs to: its own, or the one its "back" chain leads to.
int TabOf(int32_t page)
{
	for (int depth = 0; depth < 8 && page >= 0 && page < kMenuPages; depth++) {
		const int tab = RootTab(page);
		if (tab >= 0)
			return tab;
		if (page == kPageNone) // back to the pause menu
			return kTabGame;
		page = MenuPreviousPage(page);
	}
	return -1;
}

int32_t EntryAction(int32_t page, int entry) { return *reinterpret_cast<const int32_t *>(MenuPageEntry(page, entry)); }
int32_t &EntryTarget(int32_t page, int entry) { return *reinterpret_cast<int32_t *>(MenuPageEntry(page, entry) + 28); }

// The pause list becomes the GAME tab: it keeps what has no tab of its own
// (Resume, Start Game, Quit). aScreens is filled by the game's static
// initialisers, after our constructor, so this runs on the first frontend frame.
void TrimPauseList()
{
	uint8_t kept[kMenuEntries][kMenuEntrySize];
	int n = 0;
	for (int i = 0; i < kMenuEntries && EntryAction(kPagePause, i) != 0; i++) {
		const int32_t target = EntryTarget(kPagePause, i);
		if (EntryAction(kPagePause, i) == kActionChangeMenu && target != kPagePause && RootTab(target) >= 0)
			continue;
		memcpy(kept[n++], MenuPageEntry(kPagePause, i), kMenuEntrySize);
	}
	for (int i = 0; i < kMenuEntries; i++) {
		if (i < n)
			memcpy(MenuPageEntry(kPagePause, i), kept[i], kMenuEntrySize);
		else
			memset(MenuPageEntry(kPagePause, i), 0, kMenuEntrySize);
	}
	Log("menu: pause list trimmed to %d entries", n);
}

void GoToPage(uint8_t *menu, int32_t page)
{
	if (page == 0) {
		// SwitchToNewScreen(0) means "the highlighted entry's target", so page 0
		// is reached through that entry, its target swapped for the call.
		const int32_t from = MenuScreen(menu);
		const int option = std::clamp(MenuOption(menu), 0, kMenuEntries - 1);
		MenuOption(menu) = option;
		int32_t &target = EntryTarget(from, option);
		const int32_t saved = target;
		target = page;
		api.SwitchToNewScreen(menu, 0);
		target = saved;
	} else {
		api.SwitchToNewScreen(menu, int8_t(page));
	}
	api.PlayFrontEndSound(api.dmAudio, kSoundMenuHighlight, 0);
}

// The frontend goes back on Triangle (Y on Xbox pads), as on the PS2, and
// ignores Circle (B). Swapping the two while it reads its input makes Circle
// the back button; gameplay never sees the swap.
void SwapTriangleCircle(uint8_t *pad)
{
	std::swap(PadNew(pad, kPadTriangle), PadNew(pad, kPadCircle));
	std::swap(PadOld(pad, kPadTriangle), PadOld(pad, kPadCircle));
}

void TabInput(uint8_t *menu)
{
	uint8_t *pad = api.GetPad(0);
	const bool lb = PadNew(pad, kPadLB) != 0, rb = PadNew(pad, kPadRB) != 0;
	const bool lbDown = lb && !gPrevLB, rbDown = rb && !gPrevRB;
	gPrevLB = lb;
	gPrevRB = rb;
	const int32_t page = MenuScreen(menu);
	if (page == kPageMap) { // RB zooms the map in; it switches tabs now, so LT takes that over
		PadNew(pad, kPadRB) = PadNew(pad, kPadLT);
		// X (Square) shows or hides the legend, as L does (CMenuManager::m_PrefsShowLegends).
		if (PadNew(pad, kPadSquare) && !PadOld(pad, kPadSquare))
			menu[0x21] = !menu[0x21];
	}
	const int tab = RootTab(page);
	if (tab < 0)
		return;
	int next = -1;
	if (lbDown || CharJustDown('Q'))
		next = (tab + kNumTabs - 1) % kNumTabs;
	else if (rbDown || CharJustDown('E'))
		next = (tab + 1) % kNumTabs;
	else if (gHaveTabBoxes && LeftClickJustDown())
		for (int i = 0; i < kNumTabs; i++)
			if (gTabBoxes[i].Contains(float(MenuMouseX(menu)), float(MenuMouseY(menu)))) {
				next = i;
				api.oldMouseState[0] = 1; // the click is ours, not the page's
			}
	if (next >= 0 && next != tab)
		GoToPage(menu, kTabs[next].page);
}

// The controller (not the keyboard or mouse, which reach the pad state too) is in use.
bool ControllerInUse(uint8_t *pad)
{
	for (PadButton axis : {kPadLeftStickX, kPadLeftStickY, kPadRightStickX, kPadRightStickY})
		if (std::abs(PadJoy(pad, axis)) > kStickInUse)
			return true;
	for (PadButton b : {kPadLB, kPadRB, kPadDPadUp, kPadDPadDown, kPadDPadLeft, kPadDPadRight, kPadSquare, kPadTriangle,
	                    kPadCross, kPadCircle})
		if (PadJoy(pad, b))
			return true;
	return false;
}

float &MenuFloat(uint8_t *menu, size_t offset) { return *reinterpret_cast<float *>(menu + offset); }

// The map's bounds, as AdditionalOptionInput keeps them: zoomed out as far as
// it goes the map sits in the middle; zoomed in it moves no further than
// keeps an edge on screen.
void ClampMap(uint8_t *menu)
{
	const float W = float(ScreenWidth()), H = float(ScreenHeight());
	const float size = MenuFloat(menu, kMapSize);
	const float rx = std::max(size - kMapMinSize * H / 480.0f, 0.0f), ry = std::max(size - kMapMinSize * H / 448.0f, 0.0f);
	MenuFloat(menu, kMapCentreX) = std::clamp(MenuFloat(menu, kMapCentreX), W / 2 - rx, W / 2 + rx);
	MenuFloat(menu, kMapCentreY) = std::clamp(MenuFloat(menu, kMapCentreY), H / 2 - ry, H / 2 + ry);
}

// A stick's push as a speed: nothing in the dead zone, then slow for a small
// push and fast at the end of its travel (for fine placing).
Vec2 StickSpeed(int16_t x, int16_t y, float fullSpeed)
{
	const float sx = x / 128.0f, sy = y / 128.0f, m = std::min(std::hypot(sx, sy), 1.0f);
	if (m < kStickDead)
		return {0, 0};
	const float s = std::pow((m - kStickDead) / (1.0f - kStickDead), kStickCurve) * fullSpeed / std::hypot(sx, sy);
	return {sx * s, sy * s};
}

// The pause map with the controller, as GTA V has it: the left stick moves a
// cursor over the map (the crosshair, which the waypoint goes by), pushed
// against the screen's edge it moves the map along; the right stick moves
// the map under it, the D-pad nudges it, and the triggers zoom about it (LT
// in, RT out, as far as they're pulled). The game's own handling of these
// for the map is kept out: it moved the map under a crosshair fixed in the
// middle, in big steps whatever the push, and not at all until zoomed in.
// The mouse keeps the game's (its pointer is the crosshair).
void MapControl(uint8_t *menu, uint8_t *pad)
{
	gMapInputHeld = false;
	if (!gMapCrosshair || MenuScreen(menu) != kPageMap)
		return;
	// A trigger pulled is the controller too (else the game's own zoom, about
	// the screen's middle, had it while its pointer was up).
	float in, out;
	controls::Triggers(in, out);
	if (ControllerInUse(pad) || in > 0.0f || out > 0.0f)
		menu[kShowMouse] = 0;
	const Clock::time_point now = Clock::now();
	const float dt = std::clamp(std::chrono::duration<float>(now - gMapLastInput).count(), 0.0f, 0.05f);
	gMapLastInput = now;
	if (menu[kShowMouse])
		return;
	const float W = float(ScreenWidth()), H = float(ScreenHeight());
	float &size = MenuFloat(menu, kMapSize), &cx = MenuFloat(menu, kMapCentreX), &cy = MenuFloat(menu, kMapCentreY);
	CVector2D &cursor = *gMapCrosshair;
	if (cursor.x < cx - size || cursor.x > cx + size || cursor.y < cy - size || cursor.y > cy + size)
		cursor = {W / 2, H / 2};

	// Zoom, about the cursor.
	const float lo = kMapMinSize * H / 448.0f, hi = kMapMaxSize * H / 448.0f;
	const float zoom = std::clamp(size * std::exp((in - out) * kMapZoomRate * dt), lo, hi) / size;
	cx += (cursor.x - cx) * (1.0f - zoom);
	cy += (cursor.y - cy) * (1.0f - zoom);
	size *= zoom;
	// The right stick moves the map under the cursor.
	const Vec2 pan = StickSpeed(PadNew(pad, kPadRightStickX), PadNew(pad, kPadRightStickY), kMapPanSpeed * H);
	cx -= pan.x * dt;
	cy -= pan.y * dt;
	// The left stick and the D-pad move the cursor; at the screen's edge the map moves instead.
	Vec2 move = StickSpeed(PadNew(pad, kPadLeftStickX), PadNew(pad, kPadLeftStickY), kMapCursorSpeed * H);
	move.x += float((PadNew(pad, kPadDPadRight) != 0) - (PadNew(pad, kPadDPadLeft) != 0)) * kMapNudgeSpeed * H;
	move.y += float((PadNew(pad, kPadDPadDown) != 0) - (PadNew(pad, kPadDPadUp) != 0)) * kMapNudgeSpeed * H;
	cursor.x += move.x * dt;
	cursor.y += move.y * dt;
	const float edge = kMapEdge * H;
	if (cursor.x < edge)
		cx += edge - cursor.x, cursor.x = edge;
	if (cursor.x > W - edge)
		cx -= cursor.x - (W - edge), cursor.x = W - edge;
	if (cursor.y < edge)
		cy += edge - cursor.y, cursor.y = edge;
	if (cursor.y > H - edge)
		cy -= cursor.y - (H - edge), cursor.y = H - edge;
	ClampMap(menu);
	cursor.x = std::clamp(cursor.x, std::max(cx - size, edge), std::min(cx + size, W - edge));
	cursor.y = std::clamp(cursor.y, std::max(cy - size, edge), std::min(cy + size, H - edge));

	// Out of the game's sight while it reads the input; put back after.
	std::memcpy(gMapInputSaved, pad, sizeof gMapInputSaved);
	gMapInputHeld = true;
	for (PadButton b : {kPadLeftStickX, kPadLeftStickY, kPadRightStickX, kPadRightStickY, kPadDPadUp, kPadDPadDown,
	                    kPadDPadLeft, kPadDPadRight, kPadLT, kPadRB, kPadRT})
		PadNew(pad, b) = 0;
}

void MapInputBack(uint8_t *pad)
{
	if (gMapInputHeld)
		std::memcpy(pad, gMapInputSaved, sizeof gMapInputSaved);
	gMapInputHeld = false;
}

void DrawTabs(const uint8_t *menu, int current)
{
	const float W = float(ScreenWidth()), H = float(ScreenHeight());
	// The frontend's text scale: 640x448 units, with a 4:3 width.
	const text::Style label{1, H * 0.42f / 480.0f, H * 0.6f / 448.0f, kWhite};
	float t, b;
	text::LineExtent(label, 0.0f, t, b);
	const float tabH = (b - t) * 1.5f, padX = tabH * 0.6f, gap = tabH * 0.3f;
	const float rowY = H * 0.03f;
	float widths[kNumTabs], total = gap * (kNumTabs - 1);
	for (int i = 0; i < kNumTabs; i++)
		total += widths[i] = text::Width(kTabs[i].label, label) + 2 * padX;

	const bool mouse = menu[kShowMouse] != 0; // the cursor is shown
	const float mx = float(MenuMouseX(menu)), my = float(MenuMouseY(menu));
	int hover = -1;
	SetHudRenderStates();
	gCanvas.SetTexture(nullptr);
	// A backing behind the tabs only, so the page's own title and the legend stay clear.
	const float back = tabH * 0.25f;
	gCanvas.Pill((W - total) * 0.5f - back, rowY - back, total + 2 * back, tabH + 2 * back, {0, 0, 0, 170});
	float x = (W - total) * 0.5f;
	for (int i = 0; i < kNumTabs; i++) {
		gTabBoxes[i] = {x, rowY, x + widths[i], rowY + tabH};
		if (mouse && gTabBoxes[i].Contains(mx, my))
			hover = i;
		if (i == current)
			gCanvas.Pill(x, rowY, widths[i], tabH, kWhite);
		else if (i == hover)
			gCanvas.Pill(x, rowY, widths[i], tabH, {255, 255, 255, 45});
		x += widths[i] + gap;
	}
	gHaveTabBoxes = true;
	gCanvas.Flush();

	const float textY = rowY + tabH * 0.5f - (t + b) * 0.5f;
	for (int i = 0; i < kNumTabs; i++) {
		text::Style s = label;
		s.colour = i == current ? CRGBA{0, 0, 0, 255} : CRGBA{255, 255, 255, uint8_t(i == hover ? 255 : 170)};
		text::Draw(gTabBoxes[i].x0 + padX, textY, kTabs[i].label, s);
	}
	text::Flush(); // the frontend has already drawn its text for this frame
}

Vec2 QuadCentre(const Vec2 (&quad)[4])
{
	return {(quad[0].x + quad[1].x + quad[2].x + quad[3].x) / 4, (quad[0].y + quad[1].y + quad[2].y + quad[3].y) / 4};
}

// True if the box lies inside the convex quad, at least `inset` from its edges.
bool BoxInQuad(const Vec2 (&quad)[4], Vec2 boxMin, Vec2 boxMax, float inset)
{
	const Vec2 centre = QuadCentre(quad);
	const Vec2 box[4] = {boxMin, {boxMax.x, boxMin.y}, boxMax, {boxMin.x, boxMax.y}};
	for (int e = 0; e < 4; e++) {
		const Vec2 a = quad[e], b = quad[(e + 1) % 4];
		const float ex = b.x - a.x, ey = b.y - a.y, len = std::hypot(ex, ey);
		if (len < 1.0f)
			continue;
		const float inward = ex * (centre.y - a.y) - ey * (centre.x - a.x) > 0 ? 1.0f : -1.0f;
		for (const Vec2 &p : box)
			if (inward * (ex * (p.y - a.y) - ey * (p.x - a.x)) / len < inset)
				return false;
	}
	return true;
}

// The legend sits in the screen's bottom-right corner, shrunk if it's long.
// The pause screens show through a slanted frame (animated between pages), and
// DrawBackground paints everything outside it black right after PrintMap, so
// if the frame doesn't reach that corner the legend moves up, left or towards
// the frame's centre until it's inside. (Starting from the frame's own corner
// put it off screen where the frame runs past the screen's edge.)
void LegendPlace(const float *rect)
{
	const float x0 = std::min(rect[0], rect[2]), x1 = std::max(rect[0], rect[2]);
	const float y0 = std::min(rect[1], rect[3]), y1 = std::max(rect[1], rect[3]);
	const float W = float(ScreenWidth()), H = float(ScreenHeight());
	gLegendScale = std::min(kLegendScale, kLegendMaxHeight * H / std::max(y1 - y0, 1.0f));
	const Vec2 size{(x1 - x0) * gLegendScale, (y1 - y0) * gLegendScale};
	const Vec2 corner{W - H * 0.04f, H * 0.96f};
	gLegendFrom = {x0, y0};
	gLegendTo = {corner.x - size.x, corner.y - size.y};
	gLegendBoxKnown = true;

	// menuBg keeps its corners top-left, top-right, bottom-left, bottom-right;
	// round the frame they go top-left, top-right, bottom-right, bottom-left.
	// (Taken as they're kept, two "edges" ran corner to corner, nothing passed,
	// and the legend stayed at the screen's corner, half outside the frame.)
	constexpr int kRound[4] = {0, 1, 3, 2};
	Vec2 quad[4];
	for (int i = 0; i < 4; i++)
		quad[i] = {api.menuFrame[2 * kRound[i]] * W / 640.0f, api.menuFrame[2 * kRound[i] + 1] * H / 448.0f};
	const float inset = H * 0.02f, step = H * 0.004f;
	if (BoxInQuad(quad, gLegendTo, corner, inset))
		return;
	const Vec2 centre = QuadCentre(quad);
	const float toCentre = std::max(std::hypot(centre.x - corner.x, centre.y - corner.y), 1.0f);
	const Vec2 dirs[3] = {{0, -1}, {-1, 0}, {(centre.x - corner.x) / toCentre, (centre.y - corner.y) / toCentre}};
	for (int i = 1; i < 500; i++)
		for (const Vec2 &d : dirs) {
			const Vec2 max{corner.x + d.x * step * i, corner.y + d.y * step * i};
			const Vec2 min{max.x - size.x, max.y - size.y};
			if (min.x >= 0 && min.y >= 0 && BoxInQuad(quad, min, max, inset)) {
				gLegendTo = min;
				return;
			}
		}
}

Vec2 LegendMap(float x, float y)
{
	return {gLegendTo.x + (x - gLegendFrom.x) * gLegendScale, gLegendTo.y + (y - gLegendFrom.y) * gLegendScale};
}

// CRect: x at 0 and 2, y at 1 and 3.
void LegendMapRect(const void *rect, float *out)
{
	const auto *r = static_cast<const float *>(rect);
	const Vec2 a = LegendMap(r[0], r[1]), b = LegendMap(r[2], r[3]);
	out[0] = a.x, out[1] = a.y, out[2] = b.x, out[3] = b.y;
}

const CRect &AsRect(const float *r) { return *reinterpret_cast<const CRect *>(r); }

// --- hooks ------------------------------------------------------------------

void Hook_DrawBackground(uint8_t *menu, bool transition)
{
	static bool trimmed;
	if (gTabsOn && !trimmed) {
		TrimPauseList();
		trimmed = true;
	}
	if (gFreshOpen) {
		gFreshOpen = false;
		// DrawFrontEnd has just picked the pause list for a new session: open on the map.
		if (TabsActive(menu) && MenuScreen(menu) == kPagePause) {
			MenuScreen(menu) = kPageMap;
			MenuOption(menu) = 0;
			MenuOptionCount(menu) = api.GetOptionCount(kPageMap);
		}
	}
	gLegendBoxKnown = false;
	const int tab = TabsActive(menu) ? TabOf(MenuScreen(menu)) : -1;
	gDeferCursor = tab >= 0;
	gNumCursor = 0;
	orig_DrawBackground(menu, transition);
	gDeferCursor = false;
	if (tab >= 0)
		DrawTabs(menu, tab);
	for (int i = 0; i < gNumCursor; i++)
		gSpriteDraw(gCursor[i].sprite, AsRect(gCursor[i].rect), gCursor[i].col);
	gNumCursor = 0;
}

void Hook_UserInput(uint8_t *menu)
{
	uint8_t *pad = api.GetPad(0);
	if (gCircleBack)
		SwapTriangleCircle(pad);
	if (TabsActive(menu))
		TabInput(menu);
	MapControl(menu, pad);
	orig_UserInput(menu);
	MapInputBack(pad);
	if (gCircleBack)
		SwapTriangleCircle(pad);
}

void Hook_ProcessUserInput(uint8_t *menu, uint8_t down, uint8_t up, uint8_t select, uint8_t back, int8_t change)
{
	const int32_t page = MenuScreen(menu);
	// On the pause map Cross (A on Xbox pads) already toggles the waypoint in
	// AdditionalOptionInput; it must not also select the page's only item, "Back".
	if (select && page == kPageMap && CrossJustDown(api.GetPad(0)))
		select = 0;
	if (TabsActive(menu) && RootTab(page) >= 0) {
		// Back, or a "Back" item, on a tab resumes the game (instead of opening the pause list).
		const int option = std::clamp(MenuOption(menu), 0, kMenuEntries - 1);
		if (back || (select && EntryAction(page, option) == kActionGoBack)) {
			api.RequestFrontEndShutDown(menu);
			back = select = 0;
		}
	}
	orig_ProcessUserInput(menu, down, up, select, back, change);
}

void Hook_DrawRect(const CRect &rect, const CRGBA &col)
{
	if (gLegendOn && Offset(__builtin_return_address(0)) == kLegendBoxSite)
		LegendPlace(reinterpret_cast<const float *>(&rect));
	else if (!gInLegend) {
		orig_DrawRect(rect, col);
		return;
	}
	float moved[4];
	LegendMapRect(&rect, moved);
	orig_DrawRect(AsRect(moved), col);
}

void Hook_Draw2DPolygon(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4,
                        const CRGBA &col)
{
	if (gInLegend) {
		const Vec2 a = LegendMap(x1, y1), b = LegendMap(x2, y2), c = LegendMap(x3, y3), d = LegendMap(x4, y4);
		orig_Draw2DPolygon(a.x, a.y, b.x, b.y, c.x, c.y, d.x, d.y, col);
		return;
	}
	orig_Draw2DPolygon(x1, y1, x2, y2, x3, y3, x4, y4, col);
}

void Hook_DrawLegend(int x, int y, int id)
{
	gInLegend = gLegendOn && gLegendBoxKnown;
	if (!gInLegend) {
		orig_DrawLegend(x, y, id);
		return;
	}
	// The entries wrap at a fixed x that the moved text could run past.
	auto &wrapX = *reinterpret_cast<float *>(api.fontDetails + 0x2c);
	const float saved = wrapX;
	api.FontSetWrapx(float(ScreenWidth()));
	orig_DrawLegend(x, y, id);
	api.FontSetWrapx(saved);
	gInLegend = false;
}

// Text reVC added for its own menus and messages, which the port's text files
// (the original game's) don't have: the game showed "FED_VPL missing" and the
// like. Used only when the game's text has none (CKeyArray::Search returns
// its "... missing" in WideErrorString), so a reVC text file still wins.
struct ExtraText {
	const char *key, *text;
};
constexpr ExtraText kExtraText[] = {
    {"FET_GFX", "Graphics Setup"},    // Options: the graphics page
    {"FED_FLS", "FULLSCREEN"},        // screen mode
    {"FED_WND", "WINDOWED"},          //
    {"FED_VPL", "VEHICLE PIPELINE"},  // graphics options
    {"FED_MFX", "MATFX"},             //
    {"FED_NEO", "NEO"},               //
    {"FED_PRM", "PED RIM LIGHT"},     //
    {"FED_WLM", "WORLD LIGHTMAPS"},   //
    {"FED_RGL", "ROAD GLOSS"},        //
    {"FEM_NON", "NONE"},              // colour filter, island loading
    {"FEM_SIM", "SIMPLE"},            //
    {"FEM_NRM", "NORMAL"},            //
    {"FEM_MOB", "MOBILE"},            //
    {"FEM_LOW", "LOW"},               //
    {"FEM_MED", "MEDIUM"},            //
    {"FEM_HIG", "HIGH"},              //
    {"FEM_AUT", "AUTO"},              // aspect ratio
    {"FEL_RUS", "RUSSIAN"},           // language
    {"FEC_JOD", "DETECT JOYSTICK"},   // controls
    {"FEC_GSL", "SHOW HEAD BOB"},     //
    {"FESZ_RM", "RETRY MISSION?"},    // after failing a mission
    {"FEC_SLC", "SLOT ~1~ IS CORRUPTED"},
    {"FED_LFL", "Loading the saved game failed. The game will restart now."},
    {"FED_LWR", "SAVE YOUR GAME?"},
    {"FET_ADV", "ADVANCED"},                // graphics and display pages
    {"FED_AAS", "ANTI ALIASING"},           //
    {"FED_CLF", "COLOUR FILTER"},           //
    {"FED_MBL", "MOTION BLUR"},             //
    {"FEM_2PR", "PS2 ALPHA TEST"},          //
    {"FEM_SCF", "SCREEN FORMAT"},           //
    {"FEM_ISL", "MAP MEMORY USAGE"},        //
    {"FEM_CSB", "CUTSCENE BORDERS"},        //
    {"FEM_PED", "PED DENSITY"},             //
    {"FEM_CAR", "CAR DENSITY"},             //
    {"FED_UPS", "UPSCALING"},               // the port's own: MetalFX,
    {"FED_RTR", "RAY TRACING"},             // ray-traced shadows,
    {"FED_HDR", "HDR"},                     // EDR output,
    {"FED_FPS", "FRAME RATE"},              // its frame rate / interpolation
    {"FEC_FRC", "FREE CAM"},                // controls
    {"FEC_JDE", "DETECTED JOYSTICK"},       //
    {"FEC_JPR", "PRESS ANY BUTTON ON THE CONTROLLER YOU WANT TO USE"},
};
const uint16_t *gWideError; // WideErrorString

uint16_t *Hook_TextGet(uint8_t *text, const char *key)
{
	uint16_t *found = orig_TextGet(text, key);
	if (found != gWideError || !key)
		return found;
	// The lookup is case-sensitive and the keys are capitals, but default.ide
	// names the police car POLIcar (the vehicle name read "missing").
	char upper[16];
	size_t n = 0;
	bool lower = false;
	for (; key[n] && n + 1 < sizeof upper; n++) {
		lower |= key[n] >= 'a' && key[n] <= 'z';
		upper[n] = char(key[n] >= 'a' && key[n] <= 'z' ? key[n] - 'a' + 'A' : key[n]);
	}
	upper[n] = 0;
	if (lower && !key[n])
		if (uint16_t *again = orig_TextGet(text, upper); again != gWideError)
			return again;
	static uint16_t wide[sizeof kExtraText / sizeof kExtraText[0]][64];
	for (size_t i = 0; i < sizeof kExtraText / sizeof kExtraText[0]; i++)
		if (!strcmp(key, kExtraText[i].key)) {
			if (!wide[i][0])
				for (size_t c = 0; c < 63 && kExtraText[i].text[c]; c++)
					wide[i][c] = uint8_t(kExtraText[i].text[c]);
			return wide[i];
		}
	// Any other, once each, for the next round of this list.
	static char logged[32][8];
	static int numLogged;
	for (int i = 0; i < numLogged; i++)
		if (!strncmp(logged[i], key, 7))
			return found;
	if (numLogged < 32) {
		strncpy(logged[numLogged++], key, 7);
		Log("menu: the game's text has nothing for %s", key);
	}
	return found;
}

} // namespace

bool Install(SpriteDrawFn spriteDraw, bool fontHooks)
{
	gWideError = static_cast<const uint16_t *>(dlsym(RTLD_MAIN_ONLY, "WideErrorString"));
	if (!gWideError || !hooks::Install("_ZN5CText3GetEPKc", Hook_TextGet, orig_TextGet))
		Log("menu: no CText::Get hook (re-run install.sh); reVC's own menu text shows as \"... missing\"");
	gSpriteDraw = spriteDraw;
	gText = static_cast<const uint8_t *>(hooks::ImageBase());
	// ProcessUserInput also carries the pause-map Cross fix, so it is always in.
	if (!hooks::Install("_ZN12CMenuManager16ProcessUserInputEhhhha", Hook_ProcessUserInput, orig_ProcessUserInput))
		return false;

	const char *spriteDrawName = "_ZN9CSprite2d4DrawERK5CRectRK5CRGBA";
	bool tabs = gConfig.pauseTabs && hooks::IsCallTo(kCursorSites[0], spriteDrawName) &&
	            hooks::IsCallTo(kCursorSites[1], spriteDrawName);
	bool legend = gConfig.compactLegend && fontHooks &&
	              hooks::IsCallTo(kLegendTitleScaleSite, "_ZN5CFont8SetScaleEff") &&
	              hooks::IsCallTo(kLegendBoxSite, "_ZN9CSprite2d8DrawRectERK5CRectRK5CRGBA") &&
	              hooks::IsCallTo(kLegendTitleSite, "_ZN5CFont11PrintStringEffPt") &&
	              hooks::IsCallTo(kLegendEntryScaleSite, "_ZN5CFont8SetScaleEff");
	// Both draw from DrawBackground (the legend resets there every frame).
	if ((tabs || legend) &&
	    !hooks::Install("_ZN12CMenuManager14DrawBackgroundEb", Hook_DrawBackground, orig_DrawBackground))
		tabs = legend = false;
	gMapCrosshair = static_cast<CVector2D *>(dlsym(RTLD_MAIN_ONLY, "mapCrosshair"));
	const bool input = (tabs || gConfig.circleBack || gMapCrosshair) &&
	                   hooks::Install("_ZN12CMenuManager9UserInputEv", Hook_UserInput, orig_UserInput);
	gTabsOn = tabs && input;
	gCircleBack = gConfig.circleBack && input;
	gLegendOn = legend && hooks::Install("_ZN9CSprite2d8DrawRectERK5CRectRK5CRGBA", Hook_DrawRect, orig_DrawRect) &&
	            hooks::Install("_ZN9CSprite2d13Draw2DPolygonEffffffffRK5CRGBA", Hook_Draw2DPolygon,
	                           orig_Draw2DPolygon) &&
	            hooks::Install("_ZN6CRadar10DrawLegendEiii", Hook_DrawLegend, orig_DrawLegend);
	if (!input)
		gMapCrosshair = nullptr;
	Log("menu: tabs %s, compact legend %s, back on %s, map crosshair %s", gTabsOn ? "on" : "off",
	    gLegendOn ? "on" : "off", gCircleBack ? "Circle" : "Triangle", gMapCrosshair ? "a cursor on the sticks" : "vanilla");
	return true;
}

void OnGameplayFrame()
{
	if (!MenuActive(api.frontEndMenuManager))
		gFreshOpen = true;
}

bool SpriteDraw(const CSprite2d *sprite, const CRect &rect, const CRGBA &col, const void *ret)
{
	if (gInLegend) {
		float moved[4];
		LegendMapRect(&rect, moved);
		gSpriteDraw(sprite, AsRect(moved), col);
		return true;
	}
	if (gDeferCursor && gNumCursor < 2) {
		const uint32_t off = Offset(ret);
		if (off == kCursorSites[0] || off == kCursorSites[1]) {
			DeferredSprite &d = gCursor[gNumCursor++];
			d.sprite = sprite;
			memcpy(d.rect, &rect, sizeof d.rect);
			d.col = col;
			return true;
		}
	}
	return false;
}

void AdjustText(const void *ret, float &x, float &y)
{
	if (gInLegend || (gLegendBoxKnown && gLegendOn && Offset(ret) == kLegendTitleSite)) {
		const Vec2 p = LegendMap(x, y);
		x = p.x;
		y = p.y;
	}
}

void AdjustScale(const void *ret, float &sx, float &sy)
{
	if (!gLegendOn)
		return;
	const uint32_t off = Offset(ret);
	if (off == kLegendTitleScaleSite || off == kLegendEntryScaleSite) {
		sx *= gLegendScale;
		sy *= gLegendScale;
	}
}

} // namespace menu

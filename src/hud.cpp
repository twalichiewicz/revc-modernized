#include "hud.h"
#include "aimcam.h"
#include "config.h"
#include "credit.h"
#include "draw.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "menu.h"
#include "text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>

using namespace game;

namespace hud {
namespace {

enum Site { kNone, kMoney, kClock, kWeapon, kAmmo, kStars };

// Return addresses of the calls CHud::Draw makes for each element
// (macVC 1.0.5217, relative to the __TEXT base 0x100000000).
struct CallSite {
	uint32_t ret;
	Site site;
};
constexpr CallSite kPrintSites[] = {{0x1f8284, kMoney}, {0x1fa5b0, kClock}, {0x1f8b00, kAmmo}, {0x1f9674, kStars}};
constexpr CallSite kScaleSites[] = {{0x1f89dc, kAmmo}, {0x1fa4b8, kClock}};
constexpr CallSite kDrawSites[] = {{0x1f8800, kWeapon}, {0x1f8968, kWeapon}};
// The third-person (free-aim camera) crosshair: the M4, Ruger and M60's, and every other gun's.
constexpr uint32_t kCrosshairSites[] = {0x1f7be8, 0x1fc714};

constexpr float kWeaponScale = 0.5f;
// Our stars, in the clock's font scale (CHud::Draw gives both 0.7 x 1.25 units).
constexpr float kStarScale = 0.5f;
constexpr int kStarSlots = 6;
constexpr float kStarSpacing = 23.0f / 0.7f; // the vanilla row: 23 units apart at a 0.7 scale
constexpr uint32_t kStarFlashMs = 2000;      // a new star blinks (CHud::Draw)
constexpr float kStarsIn = 0.25f, kStarsOut = 0.4f; // seconds
constexpr float kClockScale = 0.3f; // relative to the vanilla clock
constexpr CRGBA kClockText{240, 242, 245, 255};
constexpr CRGBA kClockBox{0, 0, 0, 190};
constexpr float kPillTextScale = 0.36f; // relative to the vanilla clock
constexpr CRGBA kPillFill{0, 0, 0, 205};
constexpr CRGBA kGained{120, 214, 112, 255}, kSpent{236, 86, 78, 255}, kOwedText{246, 182, 84, 255};
constexpr uint32_t kMoneyNewsMs = 4000; // the money shows this long after it changes
constexpr float kShopRadius = 9.0f;     // around Ammu-Nation counter pickups
constexpr float kPropertyRadius = 3.0f; // around a "for sale" pickup

void (*orig_PrintString)(float, float, uint16_t *);
void (*orig_SetScale)(float, float);
void (*orig_DrawUV)(const CSprite2d *, const float *, const CRGBA *, float, float, float, float, float, float, float,
                    float);

const uint8_t *gText;  // __TEXT base of the running game
bool gCrosshairOnlyWhenAiming;
Im2DCanvas gDotCanvas;
bool gActive;          // HUD changes on this frame
bool gHideClock;       // our clock replaces the vanilla one this frame
bool gWeaponAnchorThisFrame;
Vec2 gWeaponAnchor;    // top-right of the vanilla weapon icon
bool gHaveWeaponAnchor;
bool gHudDrawn;        // CHud::Draw drew the weapon or clock this frame
Vec2 gClockScaleXY;
bool gHaveClockScale;
const CRGBA *gWantedColour, *gNotWantedColour; // the game's star colours
float gStars;          // the stars' fade, 0..1
int gStarsLevel;       // the level they show (kept while they fade out)
float gWeaponDrop;     // how far the stars push the weapon down
uint32_t gLastStarsMs;
float gMoneyAlpha, gDeltaAlpha, gOwedAlpha; // pill fades, 0..1
bool gTestMoney;       // VCHUD_TEST_MONEY: always show the money pill
uint32_t gLastPillMs;
bool gHaveMoney;       // gLastMoney is this game's
int32_t gLastMoney, gMoneyDelta; // what changed since the money showed up
bool gMoneyNews;
uint32_t gMoneyNewsAt;

template <size_t N>
Site Find(const CallSite (&sites)[N], const void *ret)
{
	const intptr_t off = static_cast<const uint8_t *>(ret) - gText;
	for (const CallSite &s : sites)
		if (off == s.ret)
			return s.site;
	return kNone;
}

// Each site must be a `bl` to the function we hooked, or the table is stale.
template <size_t N>
bool Verify(const CallSite (&sites)[N], const char *symbol)
{
	for (const CallSite &s : sites)
		if (!hooks::IsCallTo(s.ret, symbol)) {
			Log("hud: call site 0x%x is not a call to %s", s.ret, symbol);
			return false;
		}
	return true;
}

float Margin() { return std::max(8.0f, ScreenHeight() * gConfig.margin); }

Vec2 Remap(Vec2 p, Vec2 from, Vec2 to, float scale) { return {to.x + (p.x - from.x) * scale, to.y + (p.y - from.y) * scale}; }

Vec2 WeaponTarget() { return {ScreenWidth() - Margin(), Margin() + gWeaponDrop}; }

float Fade(float v, bool in, float dt, float inFor, float outFor)
{
	return in ? std::min(1.0f, v + dt / inFor) : std::max(0.0f, v - dt / outFor);
}

float Smooth(float t) { return t * t * (3 - 2 * t); }

// CPed::m_pWanted: the level at +0x20, when it last changed at +0xc.
const uint8_t *PlayerWanted()
{
	const uint8_t *ped = api.FindPlayerPed();
	return ped ? *reinterpret_cast<uint8_t *const *>(ped + 0x778) : nullptr;
}
int WantedLevel(const uint8_t *wanted) { return wanted ? *reinterpret_cast<const int32_t *>(wanted + 0x20) : 0; }
uint32_t WantedChangedAt(const uint8_t *wanted) { return *reinterpret_cast<const uint32_t *>(wanted + 0xc); }

text::Style HudText(float scale, CRGBA colour, bool proportional);

// The vanilla star: ">" in font 1.
text::Style StarStyle(CRGBA colour)
{
	text::Style s = HudText(kStarScale, colour, true);
	s.font = 1;
	return s;
}

float StarRowHeight()
{
	float top, bottom;
	text::LineExtent(StarStyle({255, 255, 255, 255}), 0.0f, top, bottom);
	return bottom - top;
}

int32_t Money(const uint8_t *info) { return *reinterpret_cast<const int32_t *>(info + 0xc8); }

// Standing at an Ammu-Nation counter or a property for sale.
bool MoneyUsableHere()
{
	const uint8_t *ped = api.FindPlayerPed();
	if (!ped || PedInVehicle(ped))
		return false;
	const CVector &me = EntityPos(ped);
	for (int i = 0; i < kNumPickups; i++) {
		const uint8_t *p = Pickup(i);
		const uint8_t type = p[0x36];
		const bool shop = type == kPickupInShop || type == kPickupInShopOutOfStock;
		if ((!shop && type != kPickupPropertyForSale) || p[0x37] /* removed */)
			continue;
		const CVector &at = PickupPos(p);
		const float radius = shop ? kShopRadius : kPropertyRadius;
		const float dx = at.x - me.x, dy = at.y - me.y;
		if (dx * dx + dy * dy < radius * radius && fabsf(at.z - me.z) < 3.0f)
			return true;
	}
	return false;
}

} // namespace

void FormatMoney(int32_t money, char *out, size_t size)
{
	char digits[16];
	snprintf(digits, sizeof digits, "%lld", std::llabs(static_cast<long long>(money)));
	const size_t n = strlen(digits);
	size_t o = 0;
	if (money < 0 && o + 1 < size)
		out[o++] = '-';
	if (o + 1 < size)
		out[o++] = '$';
	for (size_t i = 0; i < n && o + 1 < size; i++) {
		if (i > 0 && (n - i) % 3 == 0 && o + 2 < size)
			out[o++] = ',';
		out[o++] = digits[i];
	}
	out[o] = '\0';
}

namespace {

text::Style HudText(float scale, CRGBA colour, bool proportional)
{
	return {2, gClockScaleXY.x * scale, gClockScaleXY.y * scale, colour, proportional};
}

__attribute__((noinline)) void Hook_PrintString(float x, float y, uint16_t *str)
{
	const void *ret = __builtin_return_address(0);
	menu::AdjustText(ret, x, y);
	switch (gActive ? Find(kPrintSites, ret) : kNone) {
	case kMoney:
		return;
	case kClock:
		if (gHideClock)
			return;
		break;
	case kAmmo:
		if (gHaveWeaponAnchor) {
			const Vec2 p = Remap({x, y}, gWeaponAnchor, WeaponTarget(), kWeaponScale);
			x = p.x;
			y = p.y;
		}
		break;
	case kStars: // DrawWanted draws our own
		if (gWantedColour)
			return;
		break;
	default:
		break;
	}
	orig_PrintString(x, y, str);
}

__attribute__((noinline)) void Hook_SetScale(float sx, float sy)
{
	const void *ret = __builtin_return_address(0);
	menu::AdjustScale(ret, sx, sy);
	switch (gActive ? Find(kScaleSites, ret) : kNone) {
	case kAmmo:
		sx *= kWeaponScale;
		sy *= kWeaponScale;
		break;
	case kClock:
		gClockScaleXY = {sx, sy};
		gHaveClockScale = gHudDrawn = true;
		break;
	default:
		break;
	}
	orig_SetScale(sx, sy);
}

__attribute__((noinline)) void Hook_DrawUV(const CSprite2d *sprite, const float *rect, const CRGBA *col, float u0,
                                           float v0, float u1, float v1, float u3, float v3, float u2, float v2)
{
	// The full crosshair only while aiming; otherwise a small dot where it points.
	const intptr_t site = static_cast<const uint8_t *>(__builtin_return_address(0)) - gText;
	if (gCrosshairOnlyWhenAiming && (site == kCrosshairSites[0] || site == kCrosshairSites[1]) &&
	    !aimcam::Aiming()) {
		const float cx = (rect[0] + rect[2]) / 2, cy = (rect[1] + rect[3]) / 2;
		const float r = std::max(1.5f, ScreenHeight() * 0.0022f);
		SetHudRenderStates();
		gDotCanvas.SetTexture(nullptr);
		gDotCanvas.Pill(cx - r - 1, cy - r - 1, 2 * r + 2, 2 * r + 2, {0, 0, 0, 140});
		gDotCanvas.Pill(cx - r, cy - r, 2 * r, 2 * r, {255, 255, 255, 230});
		gDotCanvas.Flush();
		return;
	}
	if (gActive && Find(kDrawSites, __builtin_return_address(0)) == kWeapon) {
		// CRect: x at 0 and 8, y at 4 and 12 (whichever order the fields are in).
		if (!gWeaponAnchorThisFrame) {
			gWeaponAnchor = {std::max(rect[0], rect[2]), std::min(rect[1], rect[3])};
			gWeaponAnchorThisFrame = gHaveWeaponAnchor = gHudDrawn = true;
		}
		const Vec2 to = WeaponTarget();
		float moved[4];
		for (int i = 0; i < 4; i++)
			moved[i] = i % 2 == 0 ? to.x + (rect[i] - gWeaponAnchor.x) * kWeaponScale
			                      : to.y + (rect[i] - gWeaponAnchor.y) * kWeaponScale;
		orig_DrawUV(sprite, moved, col, u0, v0, u1, v1, u3, v3, u2, v2);
		return;
	}
	orig_DrawUV(sprite, rect, col, u0, v0, u1, v1, u3, v3, u2, v2);
}

} // namespace

bool Install()
{
	gText = reinterpret_cast<const uint8_t *>(hooks::ImageBase());
	gTestMoney = getenv("VCHUD_TEST_MONEY") != nullptr;
	if (!gText || !Verify(kPrintSites, "_ZN5CFont11PrintStringEffPt") || !Verify(kScaleSites, "_ZN5CFont8SetScaleEff") ||
	    !Verify(kDrawSites, "_ZN9CSprite2d4DrawERK5CRectRK5CRGBAffffffff"))
		return false;
	gWantedColour = static_cast<const CRGBA *>(dlsym(RTLD_MAIN_ONLY, "WANTED_COLOR"));
	gNotWantedColour = static_cast<const CRGBA *>(dlsym(RTLD_MAIN_ONLY, "NOTWANTED_COLOR"));
	if (!gNotWantedColour)
		gWantedColour = nullptr; // the vanilla stars stay
	gCrosshairOnlyWhenAiming = gConfig.aimCamera;
	for (uint32_t site : kCrosshairSites)
		gCrosshairOnlyWhenAiming =
		    gCrosshairOnlyWhenAiming && hooks::IsCallTo(site, "_ZN9CSprite2d4DrawERK5CRectRK5CRGBAffffffff");
	return hooks::Install("_ZN5CFont11PrintStringEffPt", Hook_PrintString, orig_PrintString) &&
	       hooks::Install("_ZN5CFont8SetScaleEff", Hook_SetScale, orig_SetScale) &&
	       hooks::Install("_ZN9CSprite2d4DrawERK5CRectRK5CRGBAffffffff", Hook_DrawUV, orig_DrawUV);
}

void BeginFrame(bool minimapShown)
{
	gActive = orig_PrintString && gConfig.enabled && gConfig.hud;
	gHideClock = gActive && minimapShown;
	gWeaponAnchorThisFrame = gHudDrawn = false;

	// The stars fade in at the top right while he's wanted, pushing the weapon down.
	const uint32_t now = *api.timeMs;
	const float dt = gLastStarsMs && now >= gLastStarsMs ? std::min(now - gLastStarsMs, 100u) / 1000.0f : 0.0f;
	gLastStarsMs = now;
	const int level = gActive && gWantedColour ? WantedLevel(PlayerWanted()) : 0;
	if (level > 0)
		gStarsLevel = std::min(level, kStarSlots);
	gStars = Fade(gStars, level > 0, dt, kStarsIn, kStarsOut);
	gWeaponDrop = gHaveClockScale && gStars > 0.0f ? Smooth(gStars) * StarRowHeight() * 1.35f : 0.0f;
}

void DrawWanted()
{
	if (!gActive || !gWantedColour || !gHudDrawn || !gHaveClockScale || gStars <= 0.0f)
		return;
	const uint8_t *wanted = PlayerWanted();
	const uint32_t now = *api.timeMs;
	// For two seconds after it goes up, the stars he has blink (to the unlit colour here).
	const bool blinkOff = WantedLevel(wanted) > 0 && now <= WantedChangedAt(wanted) + kStarFlashMs && (now / 130) % 2;
	const float a = Smooth(gStars), H = float(ScreenHeight());
	const text::Style base = StarStyle({255, 255, 255, 255});
	float top, bottom;
	text::LineExtent(base, 0.0f, top, bottom);
	const float glyph = text::Width(">", base), edge = std::max(1.0f, H / 540.0f);
	const float spacing = std::max(base.scaleX * kStarSpacing, glyph + 3 * edge);
	const float right = ScreenWidth() - Margin(), y = Margin() - top;
	// Right to left, as the game fills them; every outline first so none covers a star.
	const text::Style outline = StarStyle({0, 0, 0, uint8_t(215 * a)});
	for (int i = 0; i < kStarSlots; i++) {
		const float x = right - glyph - i * spacing;
		for (int dy = -1; dy <= 1; dy++)
			for (int dx = -1; dx <= 1; dx++)
				if (dx || dy)
					text::Draw(x + dx * edge, y + dy * edge, ">", outline);
	}
	for (int i = 0; i < kStarSlots; i++) {
		const CRGBA c = i < gStarsLevel && !blinkOff ? *gWantedColour : *gNotWantedColour;
		text::Draw(right - glyph - i * spacing, y, ">", StarStyle({c.r, c.g, c.b, uint8_t(255 * a)}));
	}
	text::Flush();
}

void DrawClock(Canvas &canvas, float mapX, float mapY, float mapW, float mapH)
{
	if (!gActive || !gHaveClockScale)
		return;
	char label[8];
	snprintf(label, sizeof label, "%02d:%02d", *api.clockHours, *api.clockMinutes);
	const text::Style style = HudText(kClockScale, kClockText, false);

	// A box hugging the text, in the map's top-right corner.
	const float inset = std::max(3.0f, mapH * 0.03f);
	const float right = mapX + mapW - inset * 2.0f, top = mapY + inset * 2.0f;
	const float left = right - text::Width(label, style);
	float y0, y1;
	text::LineExtent(style, top, y0, y1);
	const float padX = std::max(2.0f, (y1 - y0) * 0.2f);
	canvas.SetTexture(nullptr);
	canvas.Quad({left - padX, y0}, {right + padX, y0}, {right + padX, y1}, {left - padX, y1}, kClockBox);
	canvas.Flush();
	// Queued for CFont::DrawFonts later this frame, so it lands on the box.
	text::Draw(left, top, label, style);
}

namespace {

// A pill with its bottom at `bottom`; returns its height.
float Pill(Canvas &canvas, float x, float bottom, const char *label, CRGBA colour, float alpha)
{
	const text::Style style = HudText(kPillTextScale, {colour.r, colour.g, colour.b, uint8_t(colour.a * alpha)}, true);
	float t, b;
	text::LineExtent(style, 0.0f, t, b);
	const float h = (b - t) * 1.25f;
	const float w = text::Width(label, style) + h; // half a height of padding per rounded end
	const float y = bottom - h;
	canvas.SetTexture(nullptr);
	canvas.Pill(x, y, w, h, {kPillFill.r, kPillFill.g, kPillFill.b, uint8_t(kPillFill.a * alpha)});
	canvas.Flush();
	text::Draw(x + h * 0.5f, y + h * 0.5f - (t + b) * 0.5f, label, style);
	return h;
}

} // namespace

void DrawPills(Canvas &canvas, float mapX, float mapY, float mapW, float mapH, bool expanded)
{
	const uint32_t now = *api.timeMs;
	if (gLastPillMs && now < gLastPillMs) // a save loaded: its money isn't news
		gHaveMoney = false;
	const float dt = gLastPillMs && now >= gLastPillMs ? std::min(now - gLastPillMs, 100u) / 1000.0f : 0.0f;
	gLastPillMs = now;

	// What he gains or spends shows for a while: the money, counting up or
	// down as the game does, and the change above it.
	const uint8_t *info = PlayerInfo();
	const int32_t money = Money(info);
	if (!gHaveMoney) {
		gHaveMoney = true;
		gLastMoney = money;
		gMoneyDelta = 0;
		gMoneyNews = false;
	} else if (money != gLastMoney) {
		gMoneyDelta += money - gLastMoney;
		gLastMoney = money;
		gMoneyNews = true;
		gMoneyNewsAt = now;
	}
	if (gMoneyNews && now - gMoneyNewsAt >= kMoneyNewsMs)
		gMoneyNews = false;
	if (!gMoneyNews && gDeltaAlpha <= 0.0f)
		gMoneyDelta = 0;

	// D-pad down (the expanded map) shows them all, what he owes on credit too.
	const bool on = gActive && gConfig.moneyPill, counting = VisibleMoney(info) != money;
	const int32_t owed = credit::Owed();
	gMoneyAlpha = Fade(gMoneyAlpha, on && (gTestMoney || expanded || gMoneyNews || counting || MoneyUsableHere()), dt,
	                   0.2f, 0.35f);
	gDeltaAlpha = Fade(gDeltaAlpha, on && gMoneyNews && gMoneyDelta != 0, dt, 0.2f, 0.35f);
	gOwedAlpha = Fade(gOwedAlpha, on && expanded && owed > 0, dt, 0.2f, 0.35f);
	if (!gActive || !gHaveClockScale || gMoneyAlpha <= 0.0f)
		return;

	// They stack upwards from the map's bottom edge, just right of it.
	const float x = mapX + mapW + std::max(4.0f, mapH * 0.035f), gap = std::max(2.0f, mapH * 0.02f);
	float bottom = mapY + mapH;
	char label[32];
	FormatMoney(VisibleMoney(info), label, sizeof label);
	bottom -= Pill(canvas, x, bottom, label, {255, 255, 255, 255}, gMoneyAlpha) + gap;
	if (gDeltaAlpha > 0.0f && gMoneyDelta != 0) {
		char amount[24];
		FormatMoney(gMoneyDelta, amount, sizeof amount);
		snprintf(label, sizeof label, "%s%s", gMoneyDelta > 0 ? "+" : "", amount);
		bottom -= (Pill(canvas, x, bottom, label, gMoneyDelta > 0 ? kGained : kSpent, gDeltaAlpha) + gap) *
		          Smooth(gDeltaAlpha);
	}
	if (gOwedAlpha > 0.0f && owed > 0) {
		char amount[24];
		FormatMoney(owed, amount, sizeof amount);
		snprintf(label, sizeof label, "OWED %s", amount);
		Pill(canvas, x, bottom, label, kOwedText, gOwedAlpha);
	}
}

} // namespace hud

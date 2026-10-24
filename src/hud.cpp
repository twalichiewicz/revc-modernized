#include "hud.h"
#include "config.h"
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
constexpr CallSite kScaleSites[] = {{0x1f89dc, kAmmo}, {0x1f94f0, kStars}, {0x1fa4b8, kClock}};
constexpr CallSite kDrawSites[] = {{0x1f8800, kWeapon}, {0x1f8968, kWeapon}};

constexpr float kWeaponScale = 0.5f;
constexpr float kStarScale = 0.5f;
constexpr float kClockScale = 0.3f; // relative to the vanilla clock
constexpr CRGBA kClockText{240, 242, 245, 255};
constexpr CRGBA kClockBox{0, 0, 0, 190};
constexpr float kPillTextScale = 0.36f; // relative to the vanilla clock
constexpr CRGBA kPillFill{0, 0, 0, 205};
constexpr float kShopRadius = 9.0f;     // around Ammu-Nation counter pickups
constexpr float kPropertyRadius = 3.0f; // around a "for sale" pickup

void (*orig_PrintString)(float, float, uint16_t *);
void (*orig_SetScale)(float, float);
void (*orig_DrawUV)(const CSprite2d *, const float *, const CRGBA *, float, float, float, float, float, float, float,
                    float);

const uint8_t *gText;  // __TEXT base of the running game
bool gActive;          // HUD changes on this frame
bool gHideClock;       // our clock replaces the vanilla one this frame
bool gWeaponAnchorThisFrame;
Vec2 gWeaponAnchor;    // top-right of the vanilla weapon icon
bool gHaveWeaponAnchor;
float gStarMinX = 1e9f, gStarMaxX = -1e9f, gStarY; // vanilla star anchors (right-justified)
int gStarScreenW;
Vec2 gClockScaleXY;
bool gHaveClockScale;
float gMoneyAlpha;     // money pill fade, 0..1
bool gTestMoney;       // VCHUD_TEST_MONEY: always show the money pill
uint32_t gLastPillMs;

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

Vec2 WeaponTarget() { return {ScreenWidth() - Margin(), Margin()}; }

int PlayerWantedLevel()
{
	const uint8_t *ped = api.FindPlayerPed();
	const uint8_t *wanted = ped ? *reinterpret_cast<uint8_t *const *>(ped + 0x778) : nullptr;
	return wanted ? *reinterpret_cast<const int32_t *>(wanted + 0x20) : 0;
}

// Vanilla stars are right-justified at 6 evenly spaced anchors; the row's
// centre is half a glyph left of the anchors' midpoint.
bool StarSource(Vec2 &src)
{
	if (gStarMaxX <= gStarMinX)
		return false;
	const float spacing = (gStarMaxX - gStarMinX) / 5.0f;
	src = {(gStarMinX + gStarMaxX - spacing) * 0.5f, gStarY};
	return true;
}

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

// "$1,234,567"
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
	case kStars: {
		if (gStarScreenW != ScreenWidth()) { // resolution changed: relearn the row
			gStarScreenW = ScreenWidth();
			gStarMinX = 1e9f;
			gStarMaxX = -1e9f;
		}
		gStarMinX = std::min(gStarMinX, x);
		gStarMaxX = std::max(gStarMaxX, x);
		gStarY = y;
		Vec2 src;
		if (PlayerWantedLevel() <= 0 || !StarSource(src))
			return;
		const Vec2 p = Remap({x, y}, src, {ScreenWidth() * 0.5f, Margin()}, kStarScale);
		x = p.x;
		y = p.y;
		break;
	}
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
	case kStars:
		sx *= kStarScale;
		sy *= kStarScale;
		break;
	case kClock:
		gClockScaleXY = {sx, sy};
		gHaveClockScale = true;
		break;
	default:
		break;
	}
	orig_SetScale(sx, sy);
}

__attribute__((noinline)) void Hook_DrawUV(const CSprite2d *sprite, const float *rect, const CRGBA *col, float u0,
                                           float v0, float u1, float v1, float u3, float v3, float u2, float v2)
{
	if (gActive && Find(kDrawSites, __builtin_return_address(0)) == kWeapon) {
		// CRect: x at 0 and 8, y at 4 and 12 (whichever order the fields are in).
		if (!gWeaponAnchorThisFrame) {
			gWeaponAnchor = {std::max(rect[0], rect[2]), std::min(rect[1], rect[3])};
			gWeaponAnchorThisFrame = gHaveWeaponAnchor = true;
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
	return hooks::Install("_ZN5CFont11PrintStringEffPt", Hook_PrintString, orig_PrintString) &&
	       hooks::Install("_ZN5CFont8SetScaleEff", Hook_SetScale, orig_SetScale) &&
	       hooks::Install("_ZN9CSprite2d4DrawERK5CRectRK5CRGBAffffffff", Hook_DrawUV, orig_DrawUV);
}

void BeginFrame(bool minimapShown)
{
	gActive = orig_PrintString && gConfig.enabled && gConfig.hud;
	gHideClock = gActive && minimapShown;
	gWeaponAnchorThisFrame = false;
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

void DrawPills(Canvas &canvas, float mapX, float mapY, float mapW, float mapH)
{
	const uint32_t now = *api.timeMs;
	const float dt = gLastPillMs && now >= gLastPillMs ? std::min(now - gLastPillMs, 100u) / 1000.0f : 0.0f;
	gLastPillMs = now;
	const bool want = gActive && gConfig.moneyPill && (gTestMoney || MoneyUsableHere());
	gMoneyAlpha = want ? std::min(1.0f, gMoneyAlpha + dt / 0.2f) : std::max(0.0f, gMoneyAlpha - dt / 0.35f);
	if (!gActive || !gHaveClockScale || gMoneyAlpha <= 0.0f)
		return;

	char label[24];
	FormatMoney(VisibleMoney(PlayerInfo()), label, sizeof label);
	const auto fade = [](uint8_t a) { return uint8_t(a * gMoneyAlpha); };
	const text::Style style = HudText(kPillTextScale, {255, 255, 255, fade(255)}, true);
	float t, b;
	text::LineExtent(style, 0.0f, t, b);
	// Pills stack upwards from the map's bottom edge, just right of it.
	const float h = (b - t) * 1.25f;
	const float w = text::Width(label, style) + h; // half a height of padding per rounded end
	const float x = mapX + mapW + std::max(4.0f, mapH * 0.035f), y = mapY + mapH - h;
	canvas.SetTexture(nullptr);
	canvas.Pill(x, y, w, h, {kPillFill.r, kPillFill.g, kPillFill.b, fade(kPillFill.a)});
	canvas.Flush();
	text::Draw(x + h * 0.5f, y + h * 0.5f - (t + b) * 0.5f, label, style);
}

} // namespace hud

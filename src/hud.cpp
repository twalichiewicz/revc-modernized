#include "hud.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"

#include <algorithm>
#include <cstdio>
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
constexpr CallSite kScaleSites[] = {{0x1f89dc, kAmmo}, {0x1f94f0, kStars}, {0x1fa4b8, kClock}};
constexpr CallSite kDrawSites[] = {{0x1f8800, kWeapon}, {0x1f8968, kWeapon}};

constexpr float kWeaponScale = 0.5f;
constexpr float kStarScale = 0.5f;
constexpr float kClockScale = 0.3f; // relative to the vanilla clock
constexpr CRGBA kClockText{240, 242, 245, 255};
constexpr CRGBA kClockBox{0, 0, 0, 190};

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
	const auto *fn = static_cast<const uint8_t *>(dlsym(RTLD_MAIN_ONLY, symbol));
	for (const CallSite &s : sites) {
		uint32_t insn;
		memcpy(&insn, gText + s.ret - 4, 4);
		const int32_t imm = int32_t(insn << 6) >> 6;
		if ((insn & 0xFC000000) != 0x94000000 || gText + s.ret - 4 + intptr_t(imm) * 4 != fn) {
			Log("hud: call site 0x%x is not a call to %s", s.ret, symbol);
			return false;
		}
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

__attribute__((noinline)) void Hook_PrintString(float x, float y, uint16_t *str)
{
	switch (gActive ? Find(kPrintSites, __builtin_return_address(0)) : kNone) {
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
	switch (gActive ? Find(kScaleSites, __builtin_return_address(0)) : kNone) {
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
	char ascii[8];
	snprintf(ascii, sizeof ascii, "%02d:%02d", *api.clockHours, *api.clockMinutes);
	uint16_t text[8];
	api.AsciiToUnicode(ascii, text);

	api.FontSetBackgroundOff();
	api.FontSetBackGroundOnlyTextOff();
	api.FontSetPropOff();
	api.FontSetFontStyle(2);
	api.FontSetJustifyOff();
	api.FontSetCentreOff();
	api.FontSetRightJustifyOn();
	api.FontSetRightJustifyWrap(0.0f);
	api.FontSetDropShadowPosition(0);
	api.FontSetScale(gClockScaleXY.x * kClockScale, gClockScaleXY.y * kClockScale);
	api.FontSetColor(kClockText);

	// Box hugging the text: width from the font, height from its text rect
	// (whose horizontal extent runs to the wrap edge, so it isn't used).
	const float inset = std::max(3.0f, mapH * 0.03f);
	const float right = mapX + mapW - inset * 2.0f, top = mapY + inset * 2.0f;
	const float width = api.FontGetStringWidth(text, true);
	float rect[4];
	api.FontGetTextRect(rect, right, top, text);
	const float padX = std::max(2.0f, (std::max(rect[1], rect[3]) - std::min(rect[1], rect[3])) * 0.2f);
	const float y0 = std::min(rect[1], rect[3]), y1 = std::max(rect[1], rect[3]);
	canvas.SetTexture(nullptr);
	canvas.Quad({right - width - padX, y0}, {right + padX, y0}, {right + padX, y1}, {right - width - padX, y1},
	            kClockBox);
	canvas.Flush();
	// Queued for CFont::DrawFonts later this frame, so it lands on the box.
	api.FontPrintString(right, top, text);
}

} // namespace hud

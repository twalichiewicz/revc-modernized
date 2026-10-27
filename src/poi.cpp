#include "poi.h"
#include "config.h"
#include "draw.h"
#include "hud.h"
#include "magnet.h"
#include "places.h"
#include "story.h"
#include "text.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace game;

namespace poi {
namespace {

constexpr float kIconUnits = 8.0f;        // CRadar::DrawRadarSprite's half size, in 448ths of the screen height
constexpr float kSame = 0.5f;             // world units: the waypoint is on an icon this near it
constexpr uint16_t kSoundWaypoint = 0xc4; // AdditionalOptionInput's, placing the waypoint
constexpr CRGBA kBorder{164, 76, 242, 255}; // the GPS route's waypoint purple

// The legend's text for each icon (CRadar::DrawLegend's table), from Avery
// Carrington's (4) to the last radio station's (39).
constexpr uint16_t kFirstSprite = 4;
constexpr const char *kLegendKeys[] = {
    "LG_02", "LG_03", "LG_04", "LG_05", "LG_06", "LG_07", "LG_08", "LG_03", "LG_09", "LG_10", "LG_11", "LG_12",
    "LG_13", "LG_14", "LG_15", "LG_16", "LG_37", "LG_17", "LG_18", "LG_19", "LG_20", "LG_21", "LG_36", "LG_22",
    "LG_23", "LG_24", "LG_25", "LG_26", "LG_27", "LG_28", "LG_29", "LG_30", "LG_31", "LG_32", "LG_33", "LG_34",
};
static_assert(kFirstSprite + sizeof kLegendKeys / sizeof kLegendKeys[0] == kSpriteWaypoint, "one key per icon");

struct Icon {
	Vec2 world, screen;
	const uint16_t *wide; // the game's name for it (its legend text), or
	const char *ascii;    // ours
	int32_t price;        // for sale at this price
};
std::vector<Icon> gIcons; // on the pause map, where it last drew them
std::vector<Vec2> gScreens;
bool gDrawn; // Collect ran since the last tooltip
bool gLive;  // the pause map was drawn last frame: gIcons are where they show
Im2DCanvas gCanvas;

float IconRadius() { return kIconUnits * float(ScreenHeight()) / 448.0f; }

// The cursor is pulled from about an icon's width away, and is over one inside its edge.
magnet::Tuning Tuning()
{
	const float r = IconRadius();
	return {r * 2.2f, r * 1.15f, 0.4f, 14.0f};
}

// An icon of a place: not the player, the north, the waypoint, a plain
// coloured blip or anything that moves; not a for-sale house places.h shows as its business.
bool Pointable(const Blip &b)
{
	return b.inUse && (b.type == kBlipCoord || b.type == kBlipContact) && (b.display & ~1u) == 2 &&
	       b.sprite >= kFirstSprite && b.sprite < kSpriteWaypoint && !places::Replaces(b);
}

// The icon under the cursor; the mouse's pointer has to be on it.
int Hovered(Vec2 at, bool mouse)
{
	if (!gLive || !gConfig.mapIcons)
		return -1;
	return magnet::Nearest(gScreens, at, mouse ? IconRadius() : Tuning().grip);
}

} // namespace

void Collect(const std::function<bool(float x, float y, Vec2 &out)> &toScreen)
{
	gIcons.clear();
	gScreens.clear();
	if (!gConfig.mapIcons)
		return;
	gDrawn = true;
	Vec2 p;
	for (int i = 0; i < kNumBlips; i++) {
		const Blip &b = api.blips[i];
		if (!Pointable(b) || !toScreen(b.pos2D.x, b.pos2D.y, p))
			continue;
		Icon icon{{b.pos2D.x, b.pos2D.y}, p, nullptr, nullptr, 0};
		for (const story::Business &s : story::kBusinesses)
			if (story::OwnedBlip(s) == &b)
				icon.ascii = s.title;
		if (!icon.ascii)
			icon.wide = api.TextGet(api.theText, kLegendKeys[b.sprite - kFirstSprite]);
		gIcons.push_back(icon);
	}
	static std::vector<places::Spot> spots;
	places::List(spots);
	for (const places::Spot &s : spots)
		if (toScreen(s.x, s.y, p))
			gIcons.push_back({{s.x, s.y}, p, nullptr, s.name, s.price});
	for (const Icon &icon : gIcons)
		gScreens.push_back(icon.screen);
}

Vec2 Steer(Vec2 cursor, Vec2 move, bool mapMoving, float dt)
{
	if (!gLive || !gConfig.mapIcons)
		return {cursor.x + move.x, cursor.y + move.y};
	return magnet::Step(cursor, move, mapMoving, gScreens, Tuning(), dt);
}

bool Press(Vec2 at, bool mouse)
{
	const int i = Hovered(at, mouse);
	if (i < 0)
		return false;
	const Vec2 to = gIcons[i].world;
	const bool here = IsDestination(to.x, to.y);
	if (*api.targetMarkerId != -1)
		api.ToggleTargetMarker(0.0f, 0.0f); // toggling the waypoint removes it
	if (!here)
		api.ToggleTargetMarker(to.x, to.y);
	api.PlayFrontEndSound(api.dmAudio, kSoundWaypoint, 0);
	return true;
}

// The icon's name, and its price if it's for sale.
void DrawTooltip(Vec2 at, bool mouse, bool onMap)
{
	gLive = gDrawn && onMap;
	gDrawn = false;
	const int i = Hovered(at, mouse);
	if (i < 0)
		return;
	const Icon &icon = gIcons[i];
	const float W = float(ScreenWidth()), H = float(ScreenHeight()), r = IconRadius();
	const text::Style name{1, H * 0.34f / 480.0f, H * 0.5f / 448.0f, {255, 255, 255, 255}};
	const text::Style money{1, name.scaleX, name.scaleY, {124, 204, 116, 255}};
	char price[24] = "";
	if (icon.price > 0)
		hud::FormatMoney(icon.price, price, sizeof price);

	float top, bottom;
	text::LineExtent(name, 0.0f, top, bottom);
	const float nameH = bottom - top, pad = nameH * 0.45f, gap = nameH * 0.3f;
	const float nameW = icon.wide ? text::Width(icon.wide, name) : text::Width(icon.ascii, name);
	const float priceW = price[0] ? 2 * gap + text::Width(price, money) : 0.0f;
	const float w = nameW + priceW + 2 * pad, h = nameH + 2 * pad;
	// Beside the icon, on its left if the right is out of room.
	const float edge = H * 0.02f;
	float x = at.x + r * 1.6f;
	if (x + w > W - edge)
		x = at.x - r * 1.6f - w;
	const float y = std::clamp(at.y - h / 2, edge, H - edge - h);

	SetHudRenderStates();
	gCanvas.SetTexture(nullptr);
	gCanvas.RoundRect(x, y, w, h, pad * 0.6f, {16, 16, 18, 225});
	gCanvas.Flush();
	if (icon.wide)
		text::Draw(x + pad, y + pad - top, icon.wide, name);
	else
		text::Draw(x + pad, y + pad - top, icon.ascii, name);
	if (price[0])
		text::Draw(x + pad + nameW + 2 * gap, y + pad - top, price, money);
	text::Flush(); // the frontend has already drawn its text for this frame
}

void Forget() { gLive = gDrawn = false; }

int WaypointBlip() { return *api.targetMarkerId == -1 ? -1 : (*api.targetMarkerId & 0xffff); }

bool IsDestination(float x, float y)
{
	return gConfig.mapIcons && *api.targetMarkerId != -1 && std::fabs(api.targetMarkerPos->x - x) < kSame &&
	       std::fabs(api.targetMarkerPos->y - y) < kSame;
}

bool IsDestination(const Blip &blip) { return Pointable(blip) && IsDestination(blip.pos2D.x, blip.pos2D.y); }

bool DestinationIcon(const Blip *&blip)
{
	if (!gConfig.mapIcons || *api.targetMarkerId == -1)
		return false;
	for (int i = 0; i < kNumBlips; i++)
		if (IsDestination(api.blips[i])) {
			blip = &api.blips[i];
			return true;
		}
	static std::vector<places::Spot> spots;
	places::List(spots);
	for (const places::Spot &s : spots)
		if (IsDestination(s.x, s.y)) {
			blip = nullptr;
			return true;
		}
	return false;
}

// On the icon's edge: the game's are square badges with slightly rounded
// corners that fill their sprite; places.h's are discs.
void Border(Canvas &canvas, Vec2 at, bool round)
{
	const float r = IconRadius(), half = round ? r * places::kBadgeRadius : r;
	canvas.RoundRectOutline(at.x - half, at.y - half, 2 * half, 2 * half, round ? half : r * 0.16f, r * 0.3f, kBorder);
}

} // namespace poi

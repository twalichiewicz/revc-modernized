// GTA VI-style rectangular, tilted minimap with 3D buildings and GPS for macVC.
//
// CRadar keeps doing the bookkeeping (rotation, range, blips); we replace how
// its ground plane reaches the screen:
//   DrawRadarMap                      -> our scene: base map, vector roads,
//                                        3D buildings, route (scene.cpp)
//   DrawBlips                         -> runs with a widened range so blips in the
//                                        whole (larger) visible area are not clamped,
//                                        then our north marker goes on top
//   TransformRadarPointToScreenSpace  -> perspective projection, edge pinning
//   DrawCoordBlip / DrawEntityBlip    -> tell the projection which blip it is
//   CSprite2d::Draw                   -> skips the round radar ring
//   CHud::Draw                        -> hides the top-right health/armour (skip
//                                        patches) while our bars are shown
// Everything falls through to the original whenever the pause-menu map is
// open, the radar mode is not "map & blips", or the minimap is disabled.
// The rest of the HUD is in hud.cpp, the pause menu in menu.cpp.
#include "config.h"
#include "draw.h"
#include "game.h"
#include "gps.h"
#include "hooks.h"
#include "hud.h"
#include "log.h"
#include "mapdata.h"
#include "menu.h"
#include "projection.h"
#include "scene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>

using namespace game;

namespace {

constexpr int kCellsPerTile = 8; // fallback tiles: subdivision keeps texture warp invisible
constexpr float kOffscreen = -10000.0f;
constexpr CRGBA kWhite{255, 255, 255, 255};
constexpr CRGBA kFallbackWater{38, 62, 86, 235};

void (*orig_DrawRadarMap)();
void (*orig_DrawBlips)();
void (*orig_DrawCoordBlip)(int);
void (*orig_DrawEntityBlip)(int);
void (*orig_TransformRadarPointToScreenSpace)(CVector2D &, const CVector2D &);
void (*orig_SpriteDraw)(const CSprite2d *, const CRect &, const CRGBA &);
void (*orig_HudDraw)();
volatile uint64_t *gHideHealth, *gHideArmour; // skip patches in CHud::Draw

mapdata::Map gMap;      // baked by tools/bake at install time
RwRaster *gBaseRaster;  // gMap's classes coloured, with the art's roads painted out
bool gBaseFailed;

Projector gProj;
SceneInput gScene;
Im2DCanvas gCanvas;
bool gMapDrawn;   // our DrawRadarMap ran; consumed by DrawBlips
bool gInBlips;    // inside CRadar::DrawBlips
float gBlipScale; // radar range multiplier while blips draw
int gCurBlip = -1; // blip being drawn by DrawCoordBlip/DrawEntityBlip
float gIconInset; // keeps pinned icons fully inside the rect
uint32_t gLastMapMs; // game time our minimap was last drawn

struct BarsRect {
	float x, y, w, h;
} gBars;

Vec2 WorldToGround(float wx, float wy)
{
	const float dx = wx - gScene.origin.x, dy = wy - gScene.origin.y;
	return {(gScene.cos * dx + gScene.sin * dy) / gScene.range, (gScene.cos * dy - gScene.sin * dx) / gScene.range};
}

// Sized from the vanilla radar (so it follows the game's HUD scaling), placed
// in the bottom-left corner with the status bars underneath, GTA V/VI style.
void Layout()
{
	CVector2D tl, br;
	orig_TransformRadarPointToScreenSpace(tl, {-1.0f, 1.0f});
	orig_TransformRadarPointToScreenSpace(br, {1.0f, -1.0f});
	const float h = (br.y - tl.y) * gConfig.size;
	const float w = h * gConfig.aspect;
	const float margin = std::max(8.0f, ScreenHeight() * gConfig.margin);
	const float barH = gConfig.bars ? std::max(4.0f, h * 0.045f) : 0.0f;
	const float gap = gConfig.bars ? std::max(3.0f, h * 0.025f) : 0.0f;
	const float bottom = ScreenHeight() - margin;
	gBars = {margin, bottom - barH, w, barH};
	gProj.Setup(gConfig.view, margin, bottom - barH - gap - h, w, h);
	gIconInset = h * 0.07f;
}

CRGBA Dim(CRGBA c, float k, uint8_t alpha) { return {uint8_t(c.r * k), uint8_t(c.g * k), uint8_t(c.b * k), alpha}; }

void DrawBar(float x, float y, float w, float h, float fill, CRGBA col)
{
	gCanvas.Quad({x - 1, y - 1}, {x + w + 1, y - 1}, {x + w + 1, y + h + 1}, {x - 1, y + h + 1}, {0, 0, 0, 170});
	gCanvas.Quad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, Dim(col, 0.3f, 225));
	const float fw = w * std::clamp(fill, 0.0f, 1.0f);
	if (fw > 0.5f)
		gCanvas.Quad({x, y}, {x + fw, y}, {x + fw, y + h}, {x, y + h}, col);
}

// Health, armour and stamina, 3:1:1 across the width of the map.
void DrawStatusBars()
{
	const uint8_t *ped = api.FindPlayerPed();
	if (!gConfig.bars || !ped)
		return;
	const uint8_t *info = PlayerInfo();
	const float health = Health(ped) / std::max(1, MaxHealth(info) ? MaxHealth(info) : 100);
	const float armour = Armour(ped) / std::max(1, MaxArmour(info) ? MaxArmour(info) : 100);
	const float stamina = InfiniteSprint(info) ? 1.0f : Stamina(ped) / std::max(1.0f, MaxStamina(ped));

	const float gap = std::max(2.0f, gBars.w * 0.008f);
	const float unit = (gBars.w - 2 * gap) / 5.0f;
	const CRGBA healthCol = health < 0.25f ? CRGBA{206, 58, 52, 255} : CRGBA{88, 174, 84, 255};
	gCanvas.SetTexture(nullptr);
	DrawBar(gBars.x, gBars.y, 3 * unit, gBars.h, health, healthCol);
	DrawBar(gBars.x + 3 * unit + gap, gBars.y, unit, gBars.h, armour, {66, 144, 220, 255});
	DrawBar(gBars.x + 4 * unit + 2 * gap, gBars.y, unit, gBars.h, stamina, {230, 190, 70, 255});
	gCanvas.Flush();
}

// Loaded on the first minimap frame rather than in Init(): globals holding
// std::vector are dynamically initialised and could wipe anything a dylib
// constructor put into them.
void LoadMapData()
{
	static bool attempted = false;
	if (attempted)
		return;
	attempted = true;
	const std::string path = SupportDir() + "/vcminimap/map.bin";
	if (gMap.Load(path))
		Log("map data: %d px base, %zu buildings, %zu road segments", gMap.size, gMap.buildings.size(),
		    gMap.roads.size());
	else
		Log("map data missing or from an older version (%s); using the radar tiles (re-run install.sh)",
		    path.c_str());
}

void BuildBase()
{
	if (gBaseRaster || gBaseFailed || gMap.size == 0)
		return;
	gBaseRaster = CreateRaster(gMap.size, gMap.size, BuildBaseImage(gMap));
	gBaseFailed = !gBaseRaster;
	Log(gBaseRaster ? "base map texture created (%dx%d)" : "base map texture creation failed (%dx%d)", gMap.size,
	    gMap.size);
}

// --- fallback when map.bin is missing: the vanilla radar tiles ------------------

int TileX(float wx) { return int(floorf((wx - kWorldMin) / kRadarTileSize)); }
int TileY(float wy) { return int(floorf((-kWorldMin - wy) / kRadarTileSize)); }

void DrawFallbackTiles()
{
	// Tiles the view needs (vanilla streams only the 3x3 around the player).
	float minX = gScene.origin.x, maxX = minX, minY = gScene.origin.y, maxY = minY;
	for (int i = 0; i < 4; i++) {
		const Vec2 g = gProj.Corners()[i];
		const float rx = g.x * gScene.range, ry = g.y * gScene.range;
		const float wx = gScene.origin.x + gScene.cos * rx - gScene.sin * ry;
		const float wy = gScene.origin.y + gScene.sin * rx + gScene.cos * ry;
		minX = std::min(minX, wx), maxX = std::max(maxX, wx), minY = std::min(minY, wy), maxY = std::max(maxY, wy);
	}
	auto clampTile = [](int t) { return std::clamp(t, 0, kRadarTiles - 1); };
	const int tx0 = clampTile(TileX(minX)), tx1 = clampTile(TileX(maxX));
	const int ty0 = clampTile(TileY(maxY)), ty1 = clampTile(TileY(minY));
	for (int y = 0; y < kRadarTiles; y++)
		for (int x = 0; x < kRadarTiles; x++) {
			const int txd = api.radarTxdIds[x + kRadarTiles * y];
			if (txd < 0)
				continue;
			if (x >= tx0 && x <= tx1 && y >= ty0 && y <= ty1)
				api.RequestModel(txd + kStreamTxdOffset, 5);
			else if (x < tx0 - 1 || x > tx1 + 1 || y < ty0 - 1 || y > ty1 + 1)
				api.RemoveModel(txd + kStreamTxdOffset);
		}

	const float x = gProj.X(), y = gProj.Y(), w = gProj.W(), h = gProj.H();
	gCanvas.SetTexture(nullptr);
	gCanvas.Quad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, kFallbackWater);
	constexpr float cell = kRadarTileSize / kCellsPerTile;
	for (int ty = ty0; ty <= ty1; ty++)
		for (int tx = tx0; tx <= tx1; tx++) {
			RwTexture *tex = RadarTileTexture(tx, ty);
			if (!tex)
				continue;
			gCanvas.SetTexture(api.RwTextureGetRaster(tex));
			const float west = kWorldMin + tx * kRadarTileSize, north = -kWorldMin - ty * kRadarTileSize;
			for (int j = 0; j < kCellsPerTile; j++)
				for (int i = 0; i < kCellsPerTile; i++) {
					const float x0 = west + i * cell, x1 = x0 + cell, y1 = north - j * cell, y0 = y1 - cell;
					const ClipVert quad[4] = {{WorldToGround(x0, y0), {x0, y0}}, {WorldToGround(x1, y0), {x1, y0}},
					                          {WorldToGround(x1, y1), {x1, y1}}, {WorldToGround(x0, y1), {x0, y1}}};
					ClipVert clipped[8];
					const int n = gProj.ClipPolygon(quad, 4, clipped);
					Vtx v[8];
					int m = 0;
					for (int k = 0; k < n; k++) {
						Vec2 px;
						if (!gProj.GroundToScreen(clipped[k].g, px))
							break;
						v[m++] = {px.x, px.y, (clipped[k].world.x - west) / kRadarTileSize,
						          (north - clipped[k].world.y) / kRadarTileSize};
					}
					if (m == n && m >= 3)
						gCanvas.Fan(v, m, kWhite);
				}
		}
	gCanvas.Flush();
}

bool MinimapActive() { return gConfig.enabled && !MenuMapActive() && RadarMode() == 0; }

// First HUD frame: log the layout once. Developer switches:
//   VCMINIMAP_TEST_WAYPOINT=dx,dy  waypoint relative to the player
//   VCMINIMAP_TEST_WANTED=n        wanted level
//   VCMINIMAP_TEST_MONEY=1         money pill always shown (hud.cpp)
//   VCMINIMAP_DUMP_ROADS=path      write the car path network (for tools/preview)
void FirstFrame()
{
	Log("first frame: rect %.0f,%.0f %.0fx%.0f, range %.0f, view %.2f, map %s", gProj.X(), gProj.Y(), gProj.W(),
	    gProj.H(), gScene.range, gProj.MaxVisibleDistance(), gBaseRaster ? "baked" : "fallback tiles");
	if (const char *path = getenv("VCMINIMAP_DUMP_ROADS")) {
		if (FILE *f = fopen(path, "w")) {
			const auto *links = reinterpret_cast<const uint16_t *>(api.thePaths + 0x3bd08);
			for (int i = 0; i < NumCarPathNodes(); i++) {
				const uint8_t *node = api.thePaths + i * 0x14;
				const CVector p = NodePos(reinterpret_cast<const CPathNode *>(node));
				const uint32_t flags = *reinterpret_cast<const uint32_t *>(node + 0x10);
				const int16_t first = *reinterpret_cast<const int16_t *>(node + 0xc);
				fprintf(f, "n,%d,%.2f,%.2f,%.2f,%d,%u", i, p.x, p.y, p.z, node[0xe], flags);
				for (uint32_t l = 0; l < (flags & 0xf); l++)
					fprintf(f, ",%d", links[first + l] & 0x3fff);
				fputc('\n', f);
			}
			fclose(f);
			Log("dumped %d car path nodes to %s", NumCarPathNodes(), path);
		}
	}
	if (const char *wanted = getenv("VCMINIMAP_TEST_WANTED")) {
		using SetWanted = void (*)(uint8_t *, int);
		auto set = reinterpret_cast<SetWanted>(dlsym(RTLD_MAIN_ONLY, "_ZN10CPlayerPed14SetWantedLevelEi"));
		if (set && api.FindPlayerPed()) {
			set(api.FindPlayerPed(), atoi(wanted));
			Log("test wanted level %d", atoi(wanted));
		}
	}
	float dx, dy;
	const char *test = getenv("VCMINIMAP_TEST_WAYPOINT");
	if (test && sscanf(test, "%f,%f", &dx, &dy) == 2 && *api.targetMarkerId == -1) {
		api.ToggleTargetMarker(gScene.origin.x + dx, gScene.origin.y + dy);
		Log("test waypoint at %.0f,%.0f", gScene.origin.x + dx, gScene.origin.y + dy);
	}
}

// --- hooks ------------------------------------------------------------------

void Hook_DrawRadarMap()
{
	if (!MinimapActive()) {
		orig_DrawRadarMap();
		return;
	}
	gScene.cos = *api.cachedCos;
	gScene.sin = *api.cachedSin;
	gScene.range = *api.radarRange;
	gScene.origin = {api.radarOrigin->x, api.radarOrigin->y};
	Layout();
	LoadMapData();
	BuildBase();
	static bool firstFrame = true;
	if (firstFrame) {
		firstFrame = false;
		FirstFrame();
	}
	const Route *route = nullptr;
	if (gConfig.gps) {
		gps::Update(*api.FindPlayerCentreOfWorld_NoSniperShift());
		route = gps::Current();
	}

	SetHudRenderStates();
	const bool baked = gBaseRaster != nullptr;
	if (!baked)
		DrawFallbackTiles();
	gScene.background = baked;
	gScene.baseTexture = gBaseRaster;
	gScene.roads = baked ? &gMap.roads : nullptr; // the tiles already show roads
	gScene.buildings = baked && gConfig.buildings ? &gMap.buildings : nullptr;
	gScene.buildingScale = gConfig.buildingScale;
	gScene.route = route;
	DrawScene(gCanvas, gProj, gScene);
	DrawStatusBars();
	hud::DrawClock(gCanvas, gProj.X(), gProj.Y(), gProj.W(), gProj.H());
	hud::DrawPills(gCanvas, gProj.X(), gProj.Y(), gProj.W(), gProj.H());
	gMapDrawn = true;
	gLastMapMs = std::max(*api.timeMs, 1u);
}

void Hook_HudDraw()
{
	// The top-right health/armour/clock are drawn before the radar, so decide from
	// whether the minimap has been showing (it skips frames while flashing).
	const uint32_t now = *api.timeMs;
	const bool shown = gConfig.enabled && RadarMode() == 0 && gLastMapMs != 0 && now >= gLastMapMs &&
	                   now - gLastMapMs < 1000;
	if (gHideHealth)
		*gHideHealth = shown && gConfig.bars;
	if (gHideArmour)
		*gHideArmour = shown && gConfig.bars;
	hud::BeginFrame(shown);
	menu::OnGameplayFrame();
	orig_HudDraw();
}

void Hook_DrawBlips()
{
	if (!gMapDrawn || MenuMapActive()) {
		orig_DrawBlips();
		return;
	}
	gMapDrawn = false;
	const float range = *api.radarRange;
	gBlipScale = gProj.MaxVisibleDistance() * 1.05f;
	*api.radarRange = range * gBlipScale;
	gInBlips = true;
	orig_DrawBlips();
	gInBlips = false;
	*api.radarRange = range;
	SetHudRenderStates();
	DrawNorthMarker(gCanvas, gProj, gScene);
}

void Hook_DrawCoordBlip(int blip)
{
	gCurBlip = blip;
	orig_DrawCoordBlip(blip);
	gCurBlip = -1;
}

void Hook_DrawEntityBlip(int blip)
{
	gCurBlip = blip;
	orig_DrawEntityBlip(blip);
	gCurBlip = -1;
}

void Hook_TransformRadarPointToScreenSpace(CVector2D &out, const CVector2D &in)
{
	if (!gInBlips || MenuMapActive()) {
		orig_TransformRadarPointToScreenSpace(out, in);
		return;
	}
	// Outside a blip, DrawBlips only asks for the centre (player arrow) and the
	// north sprite; we draw our own north marker instead.
	if (gCurBlip < 0 && (in.x != 0.0f || in.y != 0.0f)) {
		out = {kOffscreen, kOffscreen};
		return;
	}
	Vec2 g{in.x * gBlipScale, in.y * gBlipScale};
	if (!gProj.Inside(g)) {
		if (gCurBlip >= 0 && api.blips[gCurBlip].shortRange) { // shops etc.: only when on the map
			out = {kOffscreen, kOffscreen};
			return;
		}
		g = gProj.ClampToEdge(g);
	}
	Vec2 px;
	if (!gProj.GroundToScreen(g, px)) {
		out = {kOffscreen, kOffscreen};
		return;
	}
	out.x = std::clamp(px.x, gProj.X() + gIconInset, gProj.X() + gProj.W() - gIconInset);
	out.y = std::clamp(px.y, gProj.Y() + gIconInset, gProj.Y() + gProj.H() - gIconInset);
}

void Hook_SpriteDraw(const CSprite2d *sprite, const CRect &rect, const CRGBA &col)
{
	if (gMapDrawn && sprite == RadarDiscSprite())
		return; // the round ring around the vanilla radar
	if (menu::SpriteDraw(sprite, rect, col, __builtin_return_address(0)))
		return;
	orig_SpriteDraw(sprite, rect, col);
}

__attribute__((constructor)) void Init()
{
	LogOpen((SupportDir() + "/vcminimap.log").c_str());
	Log("VC Minimap loaded");
	LoadConfig(); // gConfig is constant-initialised, so this survives static init
	if (!hooks::Init(kBuildUuid) || !Resolve()) {
		Log("staying vanilla");
		return;
	}
	const bool ok =
	    hooks::Install("_ZN6CRadar12DrawRadarMapEv", Hook_DrawRadarMap, orig_DrawRadarMap) &&
	    hooks::Install("_ZN6CRadar9DrawBlipsEv", Hook_DrawBlips, orig_DrawBlips) &&
	    hooks::Install("_ZN6CRadar13DrawCoordBlipEi", Hook_DrawCoordBlip, orig_DrawCoordBlip) &&
	    hooks::Install("_ZN6CRadar14DrawEntityBlipEi", Hook_DrawEntityBlip, orig_DrawEntityBlip) &&
	    hooks::Install("_ZN6CRadar32TransformRadarPointToScreenSpaceER9CVector2DRKS0_",
	                   Hook_TransformRadarPointToScreenSpace, orig_TransformRadarPointToScreenSpace) &&
	    hooks::Install("_ZN9CSprite2d4DrawERK5CRectRK5CRGBA", Hook_SpriteDraw, orig_SpriteDraw) &&
	    hooks::Install("_ZN4CHud4DrawEv", Hook_HudDraw, orig_HudDraw);
	gHideHealth = hooks::Flag("skip:hud_health");
	gHideArmour = hooks::Flag("skip:hud_armour");
	if (!ok || !gHideHealth || !gHideArmour) {
		hooks::UninstallAll();
		Log("a hook failed to install; staying vanilla (re-run install.sh)");
		return;
	}
	const bool hudOk = hud::Install();
	Log(hudOk ? "minimap and HUD hooks installed" : "minimap hooks installed; HUD layout left as is");
	if (!menu::Install(orig_SpriteDraw, hudOk))
		Log("pause menu hooks missing; menu left as is (re-run install.sh)");
}

} // namespace

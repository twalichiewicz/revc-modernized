#include "streethud.h"
#include "config.h"
#include "game.h"
#include "hud.h"
#include "log.h"
#include "minimap.h"
#include "streetmodel.h"
#include "text.h"
#include <cmath>
#include <dlfcn.h>
#include <memory>

using namespace game;

namespace streethud {
namespace {

const uint16_t *const *gZoneName; // CHud::m_pZoneName: the area's name, as the game shows it
const bool *gWantsHud;            // CHud::m_Wants_To_Draw_Hud: a script's HUD switch
constexpr size_t kWideScreen = 0x75; // CCamera::m_WideScreenOn: a cutscene
constexpr float kJump = 50.0f;       // m in one frame: a respawn, a load or a teleport, not travel
std::unique_ptr<streetmodel::Index> gIndex;
const mapdata::Map *gIndexed;
streetmodel::Tracker gTracker;
streetmodel::Banner gBanner;
uint32_t gLastMs;
bool gWasInVehicle;
Vec3 gLastPos;

int AreaKey(const uint16_t *name)
{
	uint32_t hash = 2166136261u;
	for (; name && *name; ++name)
		hash = (hash ^ *name) * 16777619u;
	return int(hash);
}

} // namespace

bool Install()
{
	if (!gZoneName)
		gZoneName = static_cast<const uint16_t *const *>(dlsym(RTLD_MAIN_ONLY, "_ZN4CHud11m_pZoneNameE"));
	if (!gWantsHud)
		gWantsHud = static_cast<const bool *>(dlsym(RTLD_MAIN_ONLY, "_ZN4CHud19m_Wants_To_Draw_HudE"));
	if (!gZoneName)
		Log("streethud: CHud::m_pZoneName missing; no street names");
	return gZoneName != nullptr;
}

void Draw()
{
	const mapdata::Map *map = gConfig.streetNames && gZoneName ? minimap::BakedMap() : nullptr;
	uint8_t *player = map && api.FindPlayerPed ? api.FindPlayerPed() : nullptr;
	hud::SuppressAreaName(map != nullptr);
	if (!map || !player || !api.timeMs)
		return;
	// Not over a cutscene, nor when a script has the HUD off (the game's own area name isn't shown then either).
	if ((api.theCamera && api.theCamera[kWideScreen]) || (gWantsHud && !*gWantsHud))
		return;
	if (map != gIndexed) {
		gIndex = std::make_unique<streetmodel::Index>(*map);
		gIndexed = map;
	}
	const uint32_t now = *api.timeMs;
	const float dt = gLastMs && now >= gLastMs ? std::min(0.25f, (now - gLastMs) / 1000.0f) : 0.0f;
	const bool inVehicle = PedInVehicle(player) && PedVehicle(player);
	const uint8_t *body = inVehicle ? PedVehicle(player) : player;
	const CVector pos = EntityPos(body), forward = EntityForward(body);
	// Put somewhere else (a respawn, a load, a teleport): where he is now, at once.
	if (now < gLastMs || std::hypot(pos.x - gLastPos.x, pos.y - gLastPos.y) > kJump)
		gTracker = {};
	gLastMs = now;
	gLastPos = {pos.x, pos.y, pos.z};
	const int seen = streetmodel::StreetAt(*gIndex, *map, {pos.x, pos.y, pos.z}, {forward.x, forward.y}, inVehicle,
	                                       gTracker.Current());
	const int street = gTracker.Update(seen, {pos.x, pos.y, pos.z}, dt);
	const uint16_t *area = *gZoneName;
	gBanner.Update(street, AreaKey(area), inVehicle && !gWasInVehicle, minimap::Expanded(), dt);
	gWasInVehicle = inVehicle;
	const float alpha = gBanner.Alpha();
	if (alpha <= 0)
		return;
	// Bottom right, right-justified, where the game puts its area name: the
	// street in the wide capitals, the area under it a little smaller.
	const float W = float(ScreenWidth()), H = float(ScreenHeight()), m = gConfig.margin * H;
	const uint8_t a = uint8_t(235 * alpha);
	const text::Style big{1, H * 0.36f / 480, H * 0.5f / 448, {255, 255, 255, a}};
	const text::Style small{1, H * 0.29f / 480, H * 0.4f / 448, {200, 210, 220, a}};
	float top, bottom;
	text::LineExtent(small, 0, top, bottom);
	float y = H - m - bottom;
	if (area && *area) {
		text::Draw(W - m - text::Width(area, small), y, area, small);
		y -= (bottom - top) * 1.25f;
	}
	if (street >= 0 && size_t(street) < map->streets.size()) {
		const char *name = map->streets[size_t(street)].c_str();
		float t2, b2;
		text::LineExtent(big, 0, t2, b2);
		text::Draw(W - m - text::Width(name, big), y - (b2 - bottom), name, big);
	}
	text::Flush();
}

} // namespace streethud

#include "streethud.h"
#include "config.h"
#include "game.h"
#include "hud.h"
#include "log.h"
#include "minimap.h"
#include "streetmodel.h"
#include "text.h"
#include <algorithm>
#include <cmath>
#include <dlfcn.h>
#include <memory>

using namespace game;

namespace streethud {
namespace {

const uint16_t *const *gZoneName; // CHud::m_pZoneName: the area's name, as the game shows it
std::unique_ptr<streetmodel::Index> gIndex;
const mapdata::Map *gIndexed;
streetmodel::Tracker gTracker;
streetmodel::Banner gBanner;
uint32_t gLastMs;
bool gWasInVehicle;

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
	if (map != gIndexed) {
		gIndex = std::make_unique<streetmodel::Index>(*map);
		gIndexed = map;
	}
	const uint32_t now = *api.timeMs;
	const float dt = gLastMs && now >= gLastMs ? std::min(0.25f, (now - gLastMs) / 1000.0f) : 0.0f;
	gLastMs = now;
	const bool inVehicle = PedInVehicle(player) && PedVehicle(player);
	const uint8_t *body = inVehicle ? PedVehicle(player) : player;
	const CVector pos = EntityPos(body), forward = EntityForward(body);
	const int seen = streetmodel::StreetAt(*gIndex, *map, {pos.x, pos.y, pos.z}, {forward.x, forward.y}, inVehicle);
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
	// The area in the game's own heading font (as its area name was), at the
	// height the smaller line had, with the game's dark shadow under it.
	const text::Style sized{1, H * 0.29f / 480, H * 0.4f / 448, {}};
	float top, bottom, unitTop, unitBottom;
	text::LineExtent(sized, 0, top, bottom);
	text::LineExtent({2, 1, 1, {}}, 0, unitTop, unitBottom);
	// (as controlsmodel::ScaleFor: CFont's cell is 32 wide and 20 tall, drawn 0.8 as wide as tall)
	const float sy = (bottom - top) / std::max(1.0f, unitBottom - unitTop), sx = sy * 0.8f * 20 / 32;
	const text::Style heading{2, sx, sy, {255, 255, 255, a}};
	const text::Style shadow{2, sx, sy, {20, 20, 20, uint8_t(230 * alpha)}};
	text::LineExtent(heading, 0, top, bottom);
	float y = H - m - bottom;
	if (area && *area) {
		const float x = W - m - text::Width(area, heading), offset = (bottom - top) * 0.05f + 1;
		text::Draw(x + offset, y + offset, area, shadow);
		text::Draw(x, y, area, heading);
		y -= (bottom - top) * 1.15f;
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

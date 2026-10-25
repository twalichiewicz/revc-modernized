#include "radiohud.h"
#include "config.h"
#include "draw.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "text.h"
#include "wheel.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

using namespace game;

namespace radiohud {
namespace {

using Clock = std::chrono::steady_clock;

constexpr float kHold = 1.8f, kFade = 0.6f; // seconds up, then fading
constexpr float kLogo = 0.085f;             // logo size, in screen heights

void (*orig_DisplayRadioStationName)(uint8_t *music);
bool gInstalled;
const uint8_t *gVehicle; // the car whose station was last seen
int gStation = -1;
bool gShowing;
Clock::time_point gShownAt;
Im2DCanvas gCanvas;

void Hook_DisplayRadioStationName(uint8_t *) {} // Draw shows it instead

const uint16_t *StationName(int channel)
{
	char key[8];
	if (channel < kRadioMP3)
		snprintf(key, sizeof key, "FEA_FM%d", channel);
	else
		snprintf(key, sizeof key, "%s", channel == kRadioMP3 ? "FEA_MP3" : "FEA_NON");
	return api.TextGet(api.theText, key);
}

} // namespace

void Draw()
{
	if (!gInstalled)
		return;
	const Clock::time_point now = Clock::now();
	uint8_t *ped = api.FindPlayerPed();
	uint8_t *vehicle = ped && PedInVehicle(ped) ? PedVehicle(ped) : nullptr;
	if (vehicle && !RadioUsable(vehicle))
		vehicle = nullptr;
	const int station = vehicle ? RadioTuning(vehicle) : -1;
	if (vehicle && (vehicle != gVehicle || station != gStation)) { // got in, or retuned
		gShowing = true;
		gShownAt = now;
	}
	gVehicle = vehicle;
	gStation = station;
	if (!gShowing || !vehicle)
		return;
	const float t = std::chrono::duration<float>(now - gShownAt).count();
	const float alpha = t < kHold ? 1.0f : 1.0f - (t - kHold) / kFade;
	if (alpha <= 0.0f) {
		gShowing = false;
		return;
	}

	const float W = float(ScreenWidth()), H = float(ScreenHeight()), m = gConfig.margin * H;
	const text::Style style{1, H * 0.3f / 480, H * 0.42f / 448, {255, 255, 255, uint8_t(230 * alpha)}};
	float top, bottom;
	text::LineExtent(style, 0.0f, top, bottom);
	const uint16_t *name = StationName(station);
	const float textY = H - m - (bottom - top) * 1.2f;
	if (void *logo = wheel::StationLogo(station)) {
		const float size = kLogo * H, x = (W - size) / 2, y = textY - size * 1.05f;
		SetHudRenderStates();
		gCanvas.SetTexture(logo);
		const CRGBA col{255, 255, 255, uint8_t(255 * alpha)};
		const Vtx tl{x, y, 0, 0}, tr{x + size, y, 1, 0}, br{x + size, y + size, 1, 1}, bl{x, y + size, 0, 1};
		gCanvas.Tri(tl, tr, br, col);
		gCanvas.Tri(tl, br, bl, col);
		gCanvas.Flush();
		gCanvas.SetTexture(nullptr);
	}
	if (name) {
		text::Draw((W - text::Width(name, style)) / 2, textY - top, name, style);
		text::Flush();
	}
}

bool Install()
{
	if (!gConfig.radioLogo)
		return true;
	gInstalled =
	    hooks::Install("_ZN13cMusicManager23DisplayRadioStationNameEv", Hook_DisplayRadioStationName,
	                   orig_DisplayRadioStationName);
	if (!gInstalled)
		Log("radiohud: hook missing (re-run install.sh); the game's station name stays");
	return true;
}

} // namespace radiohud

#include "sky.h"
#include "clouds.h"
#include "config.h"
#include "draw.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "shaders.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <thread>

using namespace game;

namespace sky {
namespace {

constexpr float kNightBelow = 0.25f; // daylight under which the game's own sky (moon, stars) is kept
constexpr float kScale = 0.5f;       // the clouds' resolution, of the frame's each way
constexpr float kKeep = 0.5f;        // how much of its past a marched pixel keeps
constexpr float kSunHiddenBelow = 0.35f; // sunlight through the clouds under which the sun's glow goes
constexpr float kMoonLight = 0.15f;
constexpr CVector kMoonColour{0.55f, 0.62f, 0.8f};
// Texture units of ours (librw binds 0-3).
constexpr int kShapeUnit = 4, kDetailUnit = 5, kWeatherUnit = 6, kHistoryUnit = 7, kMarchUnit = 8;

void (*orig_Render)();
bool (*gCanSeeOutside)(); // CGame::CanSeeOutSideFromCurrArea: not in an interior
bool *gSunBlocked;        // CCoronas::SunBlockedByClouds: the sun's glow fades

// The noise, made on a thread of its own at start-up.
clouds::Noise gNoise;
std::atomic<bool> gNoiseMade{false};

enum class Gpu { kWaiting, kReady, kFailed };
Gpu gGpu = Gpu::kWaiting;
bool gTried;
void *gMarch, *gResolve, *gComposite;
int uView, uWind, uSun, uSunCol, uSky, uLow, uJitter, uPick, uPrev;
unsigned gShape, gDetail, gWeather;
shaders::Target gHistory[2], gMarchTarget;
int gCurrent, gFrame;
bool gHavePast;
float gPastVP[16];
CVector gPastPosition, gPastForward;
Im2DCanvas gCanvas;

bool Prepare()
{
	if (gTried)
		return gMarch && gResolve && gComposite;
	gTried = true;
	if (!shaders::LoadGL())
		return false;
	uView = shaders::Uniform("u_cloudView");
	uWind = shaders::Uniform("u_cloudWind");
	uSun = shaders::Uniform("u_cloudSun");
	uSunCol = shaders::Uniform("u_cloudSunCol");
	uSky = shaders::Uniform("u_cloudSky");
	uLow = shaders::Uniform("u_cloudLow");
	uJitter = shaders::Uniform("u_cloudJitter");
	uPick = shaders::Uniform("u_cloudPick");
	uPrev = shaders::UniformMatrix("u_cloudPrev");
	gMarch = shaders::Create("clouds", clouds::kVertex, clouds::MarchFragment().c_str());
	gResolve = shaders::Create("clouds (resolve)", clouds::kVertex, clouds::ResolveFragment().c_str());
	gComposite = shaders::Create("clouds (over the sky)", clouds::kVertex, clouds::kCompositeFragment);
	if (!gMarch || !gResolve || !gComposite)
		return false;
	shaders::SetSampler(gMarch, "u_shape", kShapeUnit);
	shaders::SetSampler(gMarch, "u_detail", kDetailUnit);
	shaders::SetSampler(gMarch, "u_weather", kWeatherUnit);
	shaders::SetSampler(gResolve, "u_history", kHistoryUnit);
	shaders::SetSampler(gResolve, "u_march", kMarchUnit);
	shaders::SetSampler(gComposite, "u_clouds", kHistoryUnit);
	return true;
}

// The noise on the GPU, once it's made.
bool Upload()
{
	if (gGpu != Gpu::kWaiting)
		return gGpu == Gpu::kReady;
	if (!gNoiseMade.load(std::memory_order_acquire))
		return false;
	gShape = shaders::Texture3D(kShapeUnit, clouds::kShapeSize, gNoise.shape.data());
	gDetail = shaders::Texture3D(kDetailUnit, clouds::kDetailSize, gNoise.detail.data());
	gWeather = shaders::Texture2D(kWeatherUnit, clouds::kWeatherSize, clouds::kWeatherSize, gNoise.weather.data());
	gGpu = gShape && gDetail && gWeather ? Gpu::kReady : Gpu::kFailed;
	Log(gGpu == Gpu::kReady ? "sky: clouds ready" : "sky: the clouds' noise didn't upload; the clouds are the game's own");
	return gGpu == Gpu::kReady;
}

void Pass(void *shader)
{
	shaders::OverrideIm2D(shader);
	const float W = float(ScreenWidth()), H = float(ScreenHeight());
	gCanvas.SetTexture(nullptr);
	gCanvas.Quad({0, 0}, {W, 0}, {W, H}, {0, H}, {255, 255, 255, 255});
	gCanvas.Flush();
}

float Dot(const CVector &a, const CVector &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

void Hook_Render()
{
	if (!gConfig.clouds || !gCanSeeOutside() || !Prepare() || !Upload()) {
		orig_Render();
		return;
	}
	const shaders::Sky sky = shaders::CurrentSky();
	if (sky.daylight < kNightBelow)
		orig_Render(); // the moon and the stars
	float w, h;
	shaders::FrameSize(w, h);
	const int cw = std::max(2, int(w * kScale) / 2 * 2), ch = std::max(2, int(h * kScale) / 2 * 2);
	const bool resized = gHistory[0].width != cw || gHistory[0].height != ch;
	float vp[16];
	if (!shaders::Resize(gHistory[0], cw, ch) || !shaders::Resize(gHistory[1], cw, ch) ||
	    !shaders::Resize(gMarchTarget, cw / 2, ch / 2) || !shaders::ViewProjection(vp)) {
		if (sky.daylight >= kNightBelow)
			orig_Render();
		return;
	}
	// Nothing to reproject after a cut (or a resize, or the first frame).
	const CVector position = shaders::CameraPosition(), forward = shaders::CameraForward();
	const CVector moved{position.x - gPastPosition.x, position.y - gPastPosition.y, position.z - gPastPosition.z};
	const bool cut = !gHavePast || resized || Dot(forward, gPastForward) < 0.9f || Dot(moved, moved) > 30.0f * 30.0f;

	const clouds::Weather weather{shaders::Seconds(),
	                              std::clamp(0.42f + 0.45f * sky.cloudCover + 0.2f * sky.fog, 0.0f, 0.95f),
	                              std::clamp(sky.rain + 0.3f * sky.fog, 0.0f, 1.0f)};
	// Lit by the sun, or by the moon (across the sky from it) at night.
	CVector light = sky.sun, colour = sky.sunColour;
	float strength = std::clamp((sky.sun.z + 0.05f) / 0.13f, 0.0f, 1.0f);
	if (sky.sun.z < -0.05f) {
		light = {-sky.sun.x, -sky.sun.y, -sky.sun.z};
		colour = kMoonColour;
		strength = kMoonLight;
	}
	float windX, windY;
	clouds::Wind(weather.time, windX, windY);
	int pickX, pickY;
	clouds::Pick(gFrame, pickX, pickY);
	shaders::Set(uView, weather.time, weather.cover, weather.rain, float(gFrame));
	shaders::Set(uWind, windX, windY, 0.0f, float(ch));
	shaders::Set(uSun, light.x, light.y, light.z, strength);
	shaders::Set(uSunCol, colour.x, colour.y, colour.z);
	shaders::Set(uSky, sky.top.x, sky.top.y, sky.top.z);
	shaders::Set(uLow, sky.bottom.x, sky.bottom.y, sky.bottom.z);
	shaders::Set(uPick, float(pickX), float(pickY), cut ? 0.0f : kKeep);
	shaders::SetMatrix(uPrev, cut ? vp : gPastVP);
	{
		const shaders::SavedStates saved;
		SetHudRenderStates();
		api.RwRenderStateSet(kStateVertexAlpha, nullptr); // the first two passes write as they are
		shaders::BindTexture(kShapeUnit, true, gShape);
		shaders::BindTexture(kDetailUnit, true, gDetail);
		shaders::BindTexture(kWeatherUnit, false, gWeather);
		// 1. A quarter of the pixels, the rays moved onto them.
		shaders::Set(uJitter, (2 * pickX - 1) / float(cw), (2 * pickY - 1) / float(ch), 0.0f);
		{
			const shaders::DrawInto into(gMarchTarget);
			Pass(gMarch);
		}
		// 2. All of them, the rest from the last frame.
		shaders::Set(uJitter, 0.0f, 0.0f, 0.0f);
		shaders::BindTexture(kMarchUnit, false, gMarchTarget.texture);
		shaders::BindTexture(kHistoryUnit, false, gHistory[gCurrent ^ 1].texture);
		{
			const shaders::DrawInto into(gHistory[gCurrent]);
			Pass(gResolve);
		}
		// 3. Over the sky (premultiplied).
		shaders::BindTexture(kHistoryUnit, false, gHistory[gCurrent].texture);
		api.RwRenderStateSet(kStateVertexAlpha, reinterpret_cast<void *>(1));
		api.RwRenderStateSet(kStateSrcBlend, reinterpret_cast<void *>(kBlendOne));
		api.RwRenderStateSet(kStateDestBlend, reinterpret_cast<void *>(kBlendInvSrcAlpha));
		Pass(gComposite);
		shaders::OverrideIm2D(nullptr);
	}
	memcpy(gPastVP, vp, sizeof vp);
	gPastPosition = position;
	gPastForward = forward;
	gHavePast = true;
	gCurrent ^= 1;
	gFrame++;

	// The sun's glow goes behind thick cloud (the game's own clouds did this).
	if (sky.sun.z > 0.0f) {
		const float from[3] = {position.x, position.y, position.z}, dir[3] = {sky.sun.x, sky.sun.y, sky.sun.z};
		*gSunBlocked = clouds::Transmittance(gNoise, weather, from, dir) < kSunHiddenBelow;
	}
}

} // namespace

bool Install()
{
	if (!gConfig.clouds)
		return true;
	gCanSeeOutside = reinterpret_cast<bool (*)()>(dlsym(RTLD_MAIN_ONLY, "_ZN5CGame25CanSeeOutSideFromCurrAreaEv"));
	gSunBlocked = static_cast<bool *>(dlsym(RTLD_MAIN_ONLY, "_ZN8CCoronas18SunBlockedByCloudsE"));
	if (!shaders::Ready() || !gCanSeeOutside || !gSunBlocked ||
	    !hooks::Install("_ZN7CClouds6RenderEv", Hook_Render, orig_Render))
		return false;
	std::thread([] {
		const auto start = std::chrono::steady_clock::now();
		gNoise = clouds::MakeNoise();
		gNoiseMade.store(true, std::memory_order_release);
		Log("sky: the clouds' noise made in %.2f s",
		    std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
	}).detach();
	return true;
}

} // namespace sky

#include "rainstreaks.h"
#include "camera.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "minimap.h"
#include "rainmodel.h"
#include "shaders.h"
#include "weather.h"
#include "worlddraw.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <dlfcn.h>
#include <unordered_map>

using namespace game;
namespace rainstreaks {
namespace {

constexpr Vec2 kWind{7, 3};           // m/s: the volumetric clouds' (clouds.cpp), so the two agree
constexpr uint8_t kNoRainZone = 0x08; // CCullZones' no-rain attribute (as CWeather::AddRain tests it)
constexpr int kLamps = 8;             // the nearest lights that catch the rain

void (*orig_RenderRainStreaks)();
const int32_t *gArea;
const uint8_t *gCameraZone, *gPlayerZone;
bool gTried, gReady;
void *gShader;
int uEye;

const char *const kVertex = R"(
uniform vec4 u_rainEye;
VSIN(ATTRIB_POS) vec3 in_pos;
VSOUT vec4 v_colour;
VSOUT vec2 v_uv;
VSOUT float v_fog;
void main(void)
{
	vec4 Vertex = u_world * vec4(in_pos, 1.0);
	v_colour = in_color;
	v_uv = in_tex0;
	gl_Position = u_proj * u_view * Vertex;
	v_fog = DoFog(gl_Position.w);
}
)";

// Blended ONE, ONE: soft across, rounded at the ends, as bright as its light.
const char *const kFragment = R"(
FSIN vec4 v_colour;
FSIN vec2 v_uv;
FSIN float v_fog;
void main(void)
{
	float across = 1.0 - abs(v_uv.x);
	float along = sin(3.14159 * clamp(v_uv.y, 0.0, 1.0));
	FRAGCOLOR(vec4(v_colour.rgb * (across * across * along * v_fog), 1.0));
}
)";

bool Prepare()
{
	if (gTried)
		return gReady;
	gTried = true;
	if (!shaders::Ready())
		return false;
	uEye = shaders::Uniform("u_rainEye");
	gShader = shaders::Create("rain streaks", kVertex, kFragment);
	gReady = gShader != nullptr;
	return gReady;
}

// The cabin of the car he's in (the box round its glass, in its own axes,
// found once per model): no rain falls in it, though the cars aren't in the
// map's cover. None for an open car.
std::unordered_map<int, rainmodel::Box> gCabins; // centre relative to the car, axes unused
bool Cabin(const uint8_t *car, rainmodel::Box &out)
{
	if (!car || worlddraw::OpenTop(car))
		return false;
	const int model = *reinterpret_cast<const int16_t *>(car + 0x6a); // CEntity::m_modelIndex
	const worlddraw::Axes ax = worlddraw::VehicleAxes(car);
	auto it = gCabins.find(model);
	if (it == gCabins.end()) {
		Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
		for (const worlddraw::Face &f : worlddraw::VehicleFaces(car)) {
			if (!f.glass)
				continue;
			for (const Vec3 &v : {f.a, f.b, f.c}) {
				const float dx = v.x - ax.pos.x, dy = v.y - ax.pos.y, dz = v.z - ax.pos.z;
				const Vec3 c{dx * ax.right.x + dy * ax.right.y + dz * ax.right.z, dx * ax.forward.x + dy * ax.forward.y + dz * ax.forward.z,
				             dx * ax.up.x + dy * ax.up.y + dz * ax.up.z};
				lo = {std::min(lo.x, c.x), std::min(lo.y, c.y), std::min(lo.z, c.z)};
				hi = {std::max(hi.x, c.x), std::max(hi.y, c.y), std::max(hi.z, c.z)};
			}
		}
		rainmodel::Box local{};
		if (hi.x > lo.x)
			local = {{(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2}, {}, {}, {}, {(hi.x - lo.x) / 2, (hi.y - lo.y) / 2, (hi.z - lo.z) / 2}};
		it = gCabins.emplace(model, local).first;
	}
	const rainmodel::Box &l = it->second;
	if (!(l.half.x > 0))
		return false;
	out = {{ax.pos.x + ax.right.x * l.centre.x + ax.forward.x * l.centre.y + ax.up.x * l.centre.z,
	        ax.pos.y + ax.right.y * l.centre.x + ax.forward.y * l.centre.y + ax.up.y * l.centre.z,
	        ax.pos.z + ax.right.z * l.centre.x + ax.forward.z * l.centre.y + ax.up.z * l.centre.z},
	       ax.right, ax.forward, ax.up, l.half};
	return true;
}

bool Sheltered()
{
	return (gArea && *gArea != 0) || (gCameraZone && (*gCameraZone & kNoRainZone)) ||
	       (gPlayerZone && (*gPlayerZone & kNoRainZone));
}

struct Stats {
	double ms = 0;
	size_t streaks = 0, frames = 0;
	std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
} gStats;

void Draw()
{
	const rainmodel::Weather &w = weather::Now();
	const int count = rainmodel::StreakCount(w.rain, Sheltered());
	if (count == 0)
		return;
	const auto start = std::chrono::steady_clock::now();
	const CVector eye = shaders::CameraPosition();
	const shaders::Sky sky = shaders::CurrentSky();
	const mapdata::Map *map = minimap::RainMap();
	rainmodel::Box cabin;
	const bool inCar = camera::InSeat() && Cabin(camera::SeatVehicle(), cabin);
	const auto covered = [&](Vec3 p) { return (map && mapdata::CoveredAt(*map, p)) || (inCar && rainmodel::Inside(cabin, p)); };
	const std::vector<rainmodel::Streak> streaks =
	    rainmodel::Streaks({eye.x, eye.y, eye.z}, kWind, count, shaders::Seconds(), covered);

	// The nearest lights catch it; the sky lights the rest (dim at night).
	std::vector<worlddraw::Light> lights = worlddraw::PointLights();
	const auto away = [&](const worlddraw::Light &l) {
		return (l.pos.x - eye.x) * (l.pos.x - eye.x) + (l.pos.y - eye.y) * (l.pos.y - eye.y) + (l.pos.z - eye.z) * (l.pos.z - eye.z);
	};
	std::sort(lights.begin(), lights.end(), [&](const auto &a, const auto &b) { return away(a) < away(b); });
	std::vector<rainmodel::Lamp> lamps;
	for (size_t i = 0; i < lights.size() && int(i) < kLamps; i++)
		lamps.push_back({lights[i].pos, lights[i].colour, lights[i].radius * 1.5f});
	const Vec3 skyLight{(sky.top.x + sky.bottom.x) * 0.4f, (sky.top.y + sky.bottom.y) * 0.4f, (sky.top.z + sky.bottom.z) * 0.4f};
	const float strength = 0.25f + 0.35f * w.rain;

	shaders::Set(uEye, eye.x, eye.y, eye.z);
	const shaders::SavedStates saved;
	api.RwRenderStateSet(kStateTextureRaster, nullptr);
	api.RwRenderStateSet(kStateZTest, reinterpret_cast<void *>(1));
	api.RwRenderStateSet(kStateZWrite, nullptr);
	api.RwRenderStateSet(kStateVertexAlpha, reinterpret_cast<void *>(1));
	api.RwRenderStateSet(kStateFog, reinterpret_cast<void *>(1));
	api.RwRenderStateSet(kStateSrcBlend, reinterpret_cast<void *>(kBlendOne));
	api.RwRenderStateSet(kStateDestBlend, reinterpret_cast<void *>(kBlendOne));
	{
		worlddraw::Batch batch(gShader);
		for (const rainmodel::Streak &s : streaks) {
			const Vec3 mid{(s.top.x + s.bottom.x) / 2, (s.top.y + s.bottom.y) / 2, (s.top.z + s.bottom.z) / 2};
			const float tx = eye.x - mid.x, ty = eye.y - mid.y, tz = eye.z - mid.z;
			const float d = std::sqrt(tx * tx + ty * ty + tz * tz), flat = std::hypot(tx, ty);
			// Not right in his face; gone before the edge of the box.
			const float fade = std::clamp((d - 0.6f) / 1.8f, 0.0f, 1.0f) *
			                   std::clamp((rainmodel::kStreakBox / 2 - flat) / 6.0f, 0.0f, 1.0f);
			if (fade <= 0 || d <= 0)
				continue;
			// A ribbon facing him: about a pixel wide however far (thinner looks dimmer).
			const float width = std::max(0.008f, 0.0016f * d);
			const float ax = s.bottom.x - s.top.x, ay = s.bottom.y - s.top.y, az = s.bottom.z - s.top.z;
			float sx = ay * tz - az * ty, sy = az * tx - ax * tz, sz = ax * ty - ay * tx;
			const float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
			if (sl <= 1e-6f)
				continue;
			sx *= width / 2 / sl;
			sy *= width / 2 / sl;
			sz *= width / 2 / sl;
			const Vec3 c = rainmodel::Lit(mid, skyLight, lamps);
			const float k = strength * fade * std::min(1.0f, 0.012f / width * 1.5f + 0.25f);
			const uint8_t r = worlddraw::Byte(c.x * k), g = worlddraw::Byte(c.y * k), b = worlddraw::Byte(c.z * k);
			const Vec3 q[4] = {{s.top.x - sx, s.top.y - sy, s.top.z - sz}, {s.top.x + sx, s.top.y + sy, s.top.z + sz},
			                   {s.bottom.x + sx, s.bottom.y + sy, s.bottom.z + sz},
			                   {s.bottom.x - sx, s.bottom.y - sy, s.bottom.z - sz}};
			const float u[4] = {-1, 1, 1, -1}, v[4] = {0, 0, 1, 1};
			for (int i : {0, 1, 2, 0, 2, 3})
				batch.Put(q[i], r, g, b, u[i], v[i]);
		}
	}
	gStats.ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	gStats.streaks += streaks.size();
	if (++gStats.frames && std::chrono::steady_clock::now() - gStats.since > std::chrono::seconds(20)) {
		Log("rain streaks: rain %.2f, %zu a frame, %.3f ms a frame to send", w.rain, gStats.streaks / gStats.frames,
		    gStats.ms / double(gStats.frames));
		gStats = {};
	}
}

void Hook_RenderRainStreaks()
{
	if (gConfig.rainStreaks && Prepare())
		Draw(); // ours instead of the game's
	else
		orig_RenderRainStreaks();
}

} // namespace

bool Install()
{
	if (!gConfig.rainStreaks)
		return true;
	gArea = static_cast<const int32_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN5CGame8currAreaE"));
	gCameraZone = static_cast<const uint8_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN10CCullZones19CurrentFlags_CameraE"));
	gPlayerZone = static_cast<const uint8_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN10CCullZones19CurrentFlags_PlayerE"));
	const bool installed = weather::Install() && worlddraw::Install() &&
	                       hooks::Install("_ZN8CWeather17RenderRainStreaksEv", Hook_RenderRainStreaks, orig_RenderRainStreaks);
	Log("rain streaks: %s", installed ? "on" : "hook missing (re-run install.sh); the game's own rain");
	return installed;
}

} // namespace rainstreaks

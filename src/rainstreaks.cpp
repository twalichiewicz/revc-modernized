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

constexpr uint8_t kNoRainZone = 0x08; // CCullZones' no-rain attribute (as CWeather::AddRain tests it)
constexpr int kLamps = 8;             // the nearest lights that catch the rain

void (*orig_RenderRainStreaks)();
const int32_t *gArea;
const uint8_t *gCameraZone, *gPlayerZone;
bool gTried, gReady;
void *gShader, *gCurtainShader;
int uSheet = -1;

const char *const kVertex = R"(
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

// Blended as alpha: a veil of mist, billowing as it blows across, its rain's
// grain falling through it, soft at every edge (its wind across it in the
// vertex red, its strength in the alpha).
const char *const kCurtainFragment = R"(
uniform vec4 u_rainSheet; // seconds, unused, the sheet's width and height (m)
FSIN vec4 v_colour;
FSIN vec2 v_uv;
FSIN float v_fog;
// (no sin(): it loses its precision on large arguments on the GPU, and the noise frays)
float Hash(vec2 p)
{
	vec3 q = fract(vec3(p.xyx) * 0.1031);
	q += dot(q, q.yzx + 33.33);
	return fract((q.x + q.y) * q.z);
}
float Noise(vec2 p)
{
	vec2 i = floor(p), f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(Hash(i), Hash(i + vec2(1.0, 0.0)), f.x), mix(Hash(i + vec2(0.0, 1.0)), Hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
float Fbm(vec2 p)
{
	float s = 0.0, a = 0.5;
	for (int i = 0; i < 4; i++) {
		s += a * Noise(p);
		p = p * 2.03 + vec2(1.7, 9.2);
		a *= 0.5;
	}
	return s;
}
void main(void)
{
	float t = u_rainSheet.x, across = (v_colour.r - 0.5) * 40.0;
	vec2 m = v_uv * u_rainSheet.zw;
	float billow = Fbm(vec2(m.x * 0.06 - t * across * 0.06, m.y * 0.04 + t * 0.15));
	float grain = Noise(vec2(m.x * 1.5, m.y * 0.12 + t * 2.5));
	float k = smoothstep(0.32, 0.8, billow) * (0.7 + 0.3 * grain);
	float edges = smoothstep(0.0, 0.25, v_uv.x) * (1.0 - smoothstep(0.75, 1.0, v_uv.x)) *
	              smoothstep(0.0, 0.3, v_uv.y) * (1.0 - smoothstep(0.6, 1.0, v_uv.y));
	vec3 mist = mix(u_fogColor.rgb, vec3(0.75, 0.78, 0.82), 0.2);
	FRAGCOLOR(vec4(mist, k * edges * v_colour.a));
}
)";

bool Prepare()
{
	if (gTried)
		return gReady;
	gTried = true;
	if (!shaders::Ready())
		return false;
	gShader = shaders::Create("rain streaks", kVertex, kFragment);
	uSheet = shaders::Uniform("u_rainSheet");
	gCurtainShader = shaders::Create("rain curtains", kVertex, kCurtainFragment);
	gReady = gShader != nullptr; // (the curtains are drawn if theirs built)
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
	const float seconds = shaders::Seconds();
	const rainmodel::Gust gust = rainmodel::GustAt(seconds);
	const std::vector<rainmodel::Streak> streaks =
	    rainmodel::Streaks({eye.x, eye.y, eye.z}, gust.wind, count, seconds, covered);

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
	// Sheets of rain blown past, thicker in a gust (depth-tested: the buildings in front hide them).
	if (gCurtainShader) {
		const std::vector<rainmodel::Curtain> curtains = rainmodel::Curtains({eye.x, eye.y, eye.z}, w.rain, seconds, gust.strength);
		if (!curtains.empty()) {
			shaders::Set(uSheet, std::fmod(seconds, 600.0f), 0, curtains[0].width, curtains[0].height); // (small: precise)
			api.RwRenderStateSet(kStateSrcBlend, reinterpret_cast<void *>(kBlendSrcAlpha));
			api.RwRenderStateSet(kStateDestBlend, reinterpret_cast<void *>(kBlendInvSrcAlpha));
			worlddraw::Batch batch(gCurtainShader);
			for (const rainmodel::Curtain &c : curtains) {
				const float d = std::hypot(c.centre.x - eye.x, c.centre.y - eye.y);
				const float near = std::clamp((d - 10.0f) / 14.0f, 0.0f, 1.0f); // never a flat card in his face
				const float rx = -c.facing.y, ry = c.facing.x, across = gust.wind.x * rx + gust.wind.y * ry;
				const uint8_t r = worlddraw::Byte(0.5f + across / 40.0f), a = worlddraw::Byte(0.45f * c.density * near);
				if (a == 0)
					continue;
				const float hw = c.width / 2, lo = c.centre.z - c.height / 2, hi = c.centre.z + c.height / 2;
				const Vec3 q[4] = {{c.centre.x - rx * hw, c.centre.y - ry * hw, lo}, {c.centre.x + rx * hw, c.centre.y + ry * hw, lo},
				                   {c.centre.x + rx * hw, c.centre.y + ry * hw, hi}, {c.centre.x - rx * hw, c.centre.y - ry * hw, hi}};
				const float u[4] = {0, 1, 1, 0}, v[4] = {0, 0, 1, 1};
				for (int i : {0, 1, 2, 0, 2, 3})
					batch.Put(q[i], r, 0, 0, u[i], v[i], a);
			}
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
	if (!weather::InstallHaze())
		Log("rain haze: hook missing (re-run install.sh); the view as far in the rain as the game has it");
	return installed;
}

} // namespace rainstreaks

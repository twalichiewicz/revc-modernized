#include "wetroads.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "camera.h"
#include "minimap.h"
#include "rainmodel.h"
#include "shaders.h"
#include "weather.h"
#include "wetmodel.h"
#include "worlddraw.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <dlfcn.h>
#include <unordered_map>
#include <vector>

using namespace game;
namespace wetroads {
namespace {

constexpr uint32_t kFromRenderScene = 0x1418c4; // RenderScene's call (not the cars' environment maps)
constexpr float kRadius = 150.0f;                // m of wet ground drawn around the camera
constexpr float kDarken = 0.35f;                 // the albedo, darker by this much when soaked

void (*orig_RenderEverythingBarRoads)();
const int32_t *gArea;

bool gTried, gReady;
void *gWet, *gStreak, *gSplash;
constexpr int kDiscSides = 14;
constexpr int kRoofs = 8; // the nearest cars the rain splashes on
std::unordered_map<int, std::vector<wetmodel::Puddle>> gPuddles; // by cell: where they stand when full
std::unordered_map<int, float> gRoofs;                           // by model: the roof over its middle, NAN for none
int uEye, uState, uSkyTop, uSkyLow, uSun, uSunColour;

// Shared by both: placed on the world just over the ground (the collision the
// ground comes from and the road's model differ by centimetres).
const char *const kVertex = R"(
uniform vec4 u_wetEye;
VSIN(ATTRIB_POS) vec3 in_pos;
VSOUT vec3 v_world;
VSOUT vec4 v_colour;
VSOUT vec2 v_uv;
VSOUT float v_fog;
void main(void)
{
	vec4 Vertex = u_world * vec4(in_pos, 1.0);
	Vertex.z += 0.03 + 0.0015 * length(u_wetEye.xyz - Vertex.xyz);
	v_world = Vertex.xyz;
	v_colour = in_color;
	v_uv = in_tex0;
	gl_Position = u_proj * u_view * Vertex;
	v_fog = DoFog(gl_Position.w);
}
)";

// Blended ONE, INVSRCALPHA: the ground darkened by the alpha, the sky's
// reflection (Fresnel: strong only at grazing angles) and the sun's glint added.
const char *const kWetFragment = R"(
uniform vec4 u_wetEye;     // the camera, the drawn radius
uniform vec4 u_wetState;   // wetness, darkening, puddles, seconds
uniform vec4 u_wetSkyTop;  // the sky overhead, daylight
uniform vec4 u_wetSkyLow;  // the sky at the horizon
uniform vec4 u_wetSun;     // towards the sun, its strength
uniform vec4 u_wetSunColour;
FSIN vec3 v_world;
FSIN vec4 v_colour;
FSIN vec2 v_uv;
FSIN float v_fog;
void main(void)
{
	vec3 toEye = u_wetEye.xyz - v_world;
	float dist = length(toEye);
	vec3 V = toEye / max(dist, 0.001);
	float facing = clamp(V.z, 0.0, 1.0);
	float fresnel = 0.02 + 0.98 * pow(1.0 - facing, 5.0);
	vec3 R = vec3(-V.xy, V.z);
	vec3 sky = mix(u_wetSkyLow.rgb, u_wetSkyTop.rgb, clamp(R.z * 1.5, 0.0, 1.0));
	float glint = pow(max(dot(R, u_wetSun.xyz), 0.0), 180.0) * u_wetSun.w;
	float wet = u_wetState.x;
	float fade = v_fog * (1.0 - smoothstep(u_wetEye.w * 0.7, u_wetEye.w, dist));
	if (v_colour.g < 0.5) {
		// A puddle (over the wet ground already drawn): a mirror, soft at its edge, rippled by the rain.
		float edge = 1.0 - smoothstep(0.65, 1.0, v_uv.x);
		float ripple = 1.0 + 0.1 * sin(v_uv.x * 47.0 - u_wetState.w * 9.0);
		float mirror = fresnel * 0.9 * edge * max(wet, u_wetState.z); // they outlast the wet road
		vec3 seen = (sky * ripple + u_wetSunColour.rgb * pow(max(dot(R, u_wetSun.xyz), 0.0), 600.0) * u_wetSun.w * 2.0);
		FRAGCOLOR(vec4(seen * mirror, mirror) * fade);
		return;
	}
	float gloss = wet * fresnel * 0.55;
	vec3 reflected = sky * gloss + u_wetSunColour.rgb * glint * wet;
	float alpha = 1.0 - (1.0 - u_wetState.y * wet) * (1.0 - gloss);
	FRAGCOLOR(vec4(reflected, alpha) * fade);
}
)";

// Blended ONE, ONE: a light's streak, soft across, brightest near its foot,
// rippled along its length by the rain.
const char *const kStreakFragment = R"(
uniform vec4 u_wetState;
FSIN vec3 v_world;
FSIN vec4 v_colour;
FSIN vec2 v_uv;
FSIN float v_fog;
void main(void)
{
	float across = 1.0 - abs(v_uv.x);
	across *= across;
	float along = smoothstep(0.0, 0.12, v_uv.y) * pow(max(1.0 - v_uv.y, 0.0), 1.5); // (never NaN: the frame is FP16)
	float ripple = 0.75 + 0.25 * sin(v_uv.y * 37.0 + u_wetState.w * 5.0 + v_uv.x * 2.0);
	FRAGCOLOR(vec4(v_colour.rgb * (across * along * ripple * u_wetState.x * v_fog), 1.0));
}
)";

// Blended ONE, ONE: a drop's crown, spreading and fading through its moment
// (its age in the vertex alpha).
const char *const kSplashFragment = R"(
FSIN vec3 v_world;
FSIN vec4 v_colour;
FSIN vec2 v_uv;
FSIN float v_fog;
void main(void)
{
	float age = v_colour.a;
	vec2 p = vec2(v_uv.x, v_uv.y * 1.6);
	float d = length(p) - (0.25 + 0.7 * age);
	float crown = exp(-d * d * 90.0) * (1.0 - smoothstep(0.5, 1.0, v_uv.y));
	float k = crown * (1.0 - age) * (1.0 - age) * v_fog;
	FRAGCOLOR(vec4(v_colour.rgb * k, 1.0));
}
)";

bool Prepare()
{
	if (gTried)
		return gReady;
	gTried = true;
	if (!shaders::Ready())
		return false;
	uEye = shaders::Uniform("u_wetEye");
	uState = shaders::Uniform("u_wetState");
	uSkyTop = shaders::Uniform("u_wetSkyTop");
	uSkyLow = shaders::Uniform("u_wetSkyLow");
	uSun = shaders::Uniform("u_wetSun");
	uSunColour = shaders::Uniform("u_wetSunColour");
	gWet = shaders::Create("wet ground", kVertex, kWetFragment);
	gStreak = shaders::Create("wet ground (light streaks)", kVertex, kStreakFragment);
	gSplash = shaders::Create("rain splashes", kVertex, kSplashFragment);
	gReady = gWet && gStreak && gSplash;
	return gReady;
}

// The roof's height over a car's middle, along its up (found once per model from its own triangles).
float RoofHeight(const uint8_t *car)
{
	const int model = *reinterpret_cast<const int16_t *>(car + 0x6a); // CEntity::m_modelIndex
	auto it = gRoofs.find(model);
	if (it != gRoofs.end())
		return it->second;
	const worlddraw::Axes ax = worlddraw::VehicleAxes(car);
	float top = NAN;
	for (const worlddraw::Face &f : worlddraw::VehicleFaces(car))
		for (const Vec3 &v : {f.a, f.b, f.c}) {
			const float dx = v.x - ax.pos.x, dy = v.y - ax.pos.y, dz = v.z - ax.pos.z;
			const float x = dx * ax.right.x + dy * ax.right.y + dz * ax.right.z;
			const float y = dx * ax.forward.x + dy * ax.forward.y + dz * ax.forward.z;
			const float z = dx * ax.up.x + dy * ax.up.y + dz * ax.up.z;
			if (!f.glass && std::fabs(x) < 0.45f && std::fabs(y) < 0.7f && !(z <= top))
				top = z;
		}
	return gRoofs[model] = top;
}

// The nearest cars' roofs (not his, from inside; not an open car's).
std::vector<wetmodel::Roof> Roofs(Vec3 eye)
{
	std::vector<std::pair<float, uint8_t *>> near;
	for (uint8_t *v : worlddraw::Vehicles()) {
		if (*reinterpret_cast<const int32_t *>(v + 0x348) != 0 || v == camera::SeatVehicle() || worlddraw::OpenTop(v))
			continue; // cars only (eVehicleType)
		const Vec3 p = worlddraw::VehicleAxes(v).pos;
		const float d = std::hypot(p.x - eye.x, p.y - eye.y);
		if (d < wetmodel::kSplashReach)
			near.push_back({d, v});
	}
	std::sort(near.begin(), near.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	std::vector<wetmodel::Roof> out;
	for (size_t i = 0; i < near.size() && int(i) < kRoofs; i++) {
		const float h = RoofHeight(near[i].second);
		if (std::isnan(h))
			continue;
		const worlddraw::Axes ax = worlddraw::VehicleAxes(near[i].second);
		out.push_back({{ax.pos.x + ax.up.x * h, ax.pos.y + ax.up.y * h, ax.pos.z + ax.up.z * h}, ax.right, ax.forward});
	}
	return out;
}

void DrawSplashes(const mapdata::Map &map, Vec3 eye, const rainmodel::Weather &w, const shaders::Sky &sky,
                  const std::vector<worlddraw::Light> &lights)
{
	const wetmodel::GroundIndex index(map, eye, wetmodel::kSplashReach);
	const wetmodel::GroundAt ground = [&](Vec3 p, float &z) { return index.At(p, z); };
	const auto covered = [&](Vec3 p) { return mapdata::CoveredAt(map, p); };
	const std::vector<wetmodel::Splash> splashes =
	    wetmodel::Splashes(eye, w.rain, shaders::Seconds(), ground, Roofs(eye), covered);
	std::vector<rainmodel::Lamp> lamps;
	for (const worlddraw::Light &l : lights)
		if (std::hypot(l.pos.x - eye.x, l.pos.y - eye.y) < wetmodel::kSplashReach + l.radius && lamps.size() < 8)
			lamps.push_back({l.pos, l.colour, l.radius * 1.5f});
	const Vec3 skyLight{(sky.top.x + sky.bottom.x) * 0.4f, (sky.top.y + sky.bottom.y) * 0.4f, (sky.top.z + sky.bottom.z) * 0.4f};
	worlddraw::Batch batch(gSplash);
	for (const wetmodel::Splash &s : splashes) {
		const float tx = eye.x - s.pos.x, ty = eye.y - s.pos.y, flat = std::hypot(tx, ty);
		if (flat < 0.5f)
			continue;
		const float rx = -ty / flat * s.size, ry = tx / flat * s.size, up = s.size * 0.8f; // facing him, standing up
		const Vec3 c = rainmodel::Lit(s.pos, skyLight, lamps);
		const float k = 0.55f;
		const uint8_t r = worlddraw::Byte(c.x * k), g = worlddraw::Byte(c.y * k), b = worlddraw::Byte(c.z * k);
		const uint8_t age = worlddraw::Byte(s.age);
		const Vec3 q[4] = {{s.pos.x - rx, s.pos.y - ry, s.pos.z}, {s.pos.x + rx, s.pos.y + ry, s.pos.z},
		                   {s.pos.x + rx, s.pos.y + ry, s.pos.z + up}, {s.pos.x - rx, s.pos.y - ry, s.pos.z + up}};
		const float u[4] = {-1, 1, 1, -1}, v[4] = {0, 0, 1, 1};
		for (int i : {0, 1, 2, 0, 2, 3})
			batch.Put(q[i], r, g, b, u[i], v[i], age);
	}
}

struct Stats {
	double ms = 0;
	size_t triangles = 0, streaks = 0, frames = 0;
	std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
} gStats;

void Draw()
{
	const rainmodel::Weather &w = weather::Now();
	if (!gConfig.wetGround || !wetmodel::Shows(w.wetness, w.puddles) || (gArea && *gArea != 0))
		return;
	const mapdata::Map *map = minimap::RainMap();
	static bool told;
	if (!map && !told) {
		told = true;
		Log("wet roads: map.bin has no wet ground (baked before version 6; re-run install.sh)");
	}
	if (!map || !Prepare())
		return;
	const auto start = std::chrono::steady_clock::now();
	const CVector eye = shaders::CameraPosition();
	const shaders::Sky sky = shaders::CurrentSky();
	shaders::Set(uEye, eye.x, eye.y, eye.z, kRadius);
	shaders::Set(uState, w.wetness, kDarken, w.puddles, shaders::Seconds());
	shaders::Set(uSkyTop, sky.top.x, sky.top.y, sky.top.z, sky.daylight);
	shaders::Set(uSkyLow, sky.bottom.x, sky.bottom.y, sky.bottom.z);
	shaders::Set(uSun, sky.sun.x, sky.sun.y, sky.sun.z, sky.daylight * (1.0f - 0.7f * sky.cloudCover));
	shaders::Set(uSunColour, sky.sunColour.x, sky.sunColour.y, sky.sunColour.z);

	const shaders::SavedStates saved;
	api.RwRenderStateSet(kStateTextureRaster, nullptr);
	api.RwRenderStateSet(kStateZTest, reinterpret_cast<void *>(1));
	api.RwRenderStateSet(kStateZWrite, nullptr);
	api.RwRenderStateSet(kStateVertexAlpha, reinterpret_cast<void *>(1));
	api.RwRenderStateSet(kStateFog, reinterpret_cast<void *>(1)); // so the fog's uniforms are the world's
	api.RwRenderStateSet(kStateSrcBlend, reinterpret_cast<void *>(kBlendOne));
	api.RwRenderStateSet(kStateDestBlend, reinterpret_cast<void *>(kBlendInvSrcAlpha));
	size_t triangles = 0;
	const std::vector<int> cells = wetmodel::CellsNear(*map, {eye.x, eye.y, eye.z}, kRadius);
	{
		worlddraw::Batch ground(gWet);
		for (int c : cells) {
			const std::vector<Vec3> &tris = map->wet[size_t(c)].tris;
			for (const Vec3 &p : tris)
				ground.Put(p, 255, 255, 255, 0, 0);
			triangles += tris.size() / 3;
		}
		ground.Flush();
		// The puddles over it: discs, their radius growing with how long it's rained.
		if (wetmodel::Shows(0, w.puddles))
			for (int c : cells) {
				auto it = gPuddles.find(c);
				if (it == gPuddles.end())
					it = gPuddles.emplace(c, wetmodel::Puddles(map->wet[size_t(c)], 1)).first;
				for (const wetmodel::Puddle &p : it->second) {
					const float r = p.radius * std::min(w.puddles, 1.0f);
					for (int k = 0; k < kDiscSides; k++) {
						const float a0 = 6.2831853f * k / kDiscSides, a1 = 6.2831853f * (k + 1) / kDiscSides;
						ground.Put(p.centre, 255, 0, 255, 0, 0);
						ground.Put({p.centre.x + std::cos(a0) * r, p.centre.y + std::sin(a0) * r, p.centre.z}, 255, 0, 255, 1, 0);
						ground.Put({p.centre.x + std::cos(a1) * r, p.centre.y + std::sin(a1) * r, p.centre.z}, 255, 0, 255, 1, 0);
					}
				}
			}
	}

	const wetmodel::GroundAt ground = [&](Vec3 p, float &z) { return wetmodel::WetGroundAt(*map, p, z); };
	const std::vector<worlddraw::Light> pointLights = worlddraw::PointLights();
	std::vector<wetmodel::Light> lights;
	for (const worlddraw::Light &l : pointLights)
		lights.push_back({l.pos, l.colour, l.radius});
	const std::vector<wetmodel::Reflection> streaks = wetmodel::Reflections(lights, {eye.x, eye.y, eye.z}, ground);
	api.RwRenderStateSet(kStateDestBlend, reinterpret_cast<void *>(kBlendOne));
	{
		worlddraw::Batch batch(gStreak);
		for (const wetmodel::Reflection &s : streaks) {
			const uint8_t r = worlddraw::Byte(s.colour.x * 0.8f), g = worlddraw::Byte(s.colour.y * 0.8f),
			              b = worlddraw::Byte(s.colour.z * 0.8f);
			const float u[4] = {-1, 1, 1, -1}, v[4] = {0, 0, 1, 1};
			for (int k : {0, 1, 2, 0, 2, 3})
				batch.Put(s.quad[k], r, g, b, u[k], v[k]);
		}
	}
	if (gConfig.rainSplashes && w.rain >= rainmodel::kRaining)
		DrawSplashes(*map, {eye.x, eye.y, eye.z}, w, sky, pointLights);

	gStats.ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	gStats.triangles += triangles;
	gStats.streaks += streaks.size();
	if (++gStats.frames && std::chrono::steady_clock::now() - gStats.since > std::chrono::seconds(20)) {
		Log("wet roads: wetness %.2f, %zu triangles and %zu light streaks a frame, %.3f ms a frame to send", w.wetness,
		    gStats.triangles / gStats.frames, gStats.streaks / gStats.frames, gStats.ms / double(gStats.frames));
		gStats = {};
	}
}

void Hook_RenderEverythingBarRoads()
{
	orig_RenderEverythingBarRoads();
	const uint32_t site = uint32_t(reinterpret_cast<uintptr_t>(__builtin_return_address(0)) -
	                               reinterpret_cast<uintptr_t>(hooks::ImageBase()));
	if (site == kFromRenderScene)
		Draw();
}

} // namespace

bool Install()
{
	if (!gConfig.wetGround)
		return true;
	gArea = static_cast<const int32_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN5CGame8currAreaE"));
	const bool ready = weather::Install() && worlddraw::Install() &&
	                   hooks::IsCallTo(kFromRenderScene, "_ZN9CRenderer24RenderEverythingBarRoadsEv");
	const bool installed =
	    ready && hooks::Install("_ZN9CRenderer24RenderEverythingBarRoadsEv", Hook_RenderEverythingBarRoads,
	                            orig_RenderEverythingBarRoads);
	Log("wet roads: %s", installed ? "on" : "hook missing (re-run install.sh)");
	return installed;
}

} // namespace wetroads

#include "wetroads.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "minimap.h"
#include "shaders.h"
#include "weather.h"
#include "wetmodel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <dlfcn.h>
#include <vector>

using namespace game;
namespace wetroads {
namespace {

constexpr uint32_t kFromRenderScene = 0x1418c4; // RenderScene's call (not the cars' environment maps)
constexpr float kRadius = 150.0f;                // m of wet ground drawn around the camera
constexpr float kDarken = 0.35f;                 // the albedo, darker by this much when soaked
constexpr int kBatch = 3 * 4000;                 // vertices per draw (16-bit indices)

struct Vertex {
	float x, y, z, nx, ny, nz;
	uint8_t r, g, b, a;
	float u, v;
};
// CPointLights::aLights' records (as estatelighting reads them).
struct PointLight {
	Vec3 position, direction;
	float radius;
	Vec3 colour;
	uint8_t type, fog, extraShadows, pad;
};
static_assert(sizeof(PointLight) == 0x2c);

void (*orig_RenderEverythingBarRoads)();
void *(*gTransform)(Vertex *, uint32_t, float *, uint32_t);
int (*gRender)(int, uint16_t *, int);
int (*gEnd)();
void (*gUse)(void *shader);
const int16_t *gPointCount;
const PointLight *gPointLights;
const int32_t *gArea;

bool gTried, gReady;
void *gWet, *gStreak;
int uEye, uState, uSkyTop, uSkyLow, uSun, uSunColour;
std::vector<Vertex> gVertices;
std::vector<uint16_t> gIndices;

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
	float along = smoothstep(0.0, 0.12, v_uv.y) * pow(1.0 - v_uv.y, 1.5);
	float ripple = 0.75 + 0.25 * sin(v_uv.y * 37.0 + u_wetState.w * 5.0 + v_uv.x * 2.0);
	FRAGCOLOR(vec4(v_colour.rgb * (across * along * ripple * u_wetState.x * v_fog), 1.0));
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
	gReady = gWet && gStreak;
	return gReady;
}

void Flush(void *shader)
{
	if (gVertices.empty())
		return;
	gIndices.resize(gVertices.size());
	for (size_t i = 0; i < gIndices.size(); i++)
		gIndices[i] = uint16_t(i);
	if (gTransform(gVertices.data(), uint32_t(gVertices.size()), nullptr, 1)) {
		gUse(shader); // after the transform picked the game's, before the draw sends the camera to it
		gRender(3, gIndices.data(), int(gIndices.size()));
		gEnd();
	}
	gVertices.clear();
}

void Put(void *shader, Vec3 p, uint8_t r, uint8_t g, uint8_t b, float u, float v)
{
	gVertices.push_back({p.x, p.y, p.z, 0, 0, 1, r, g, b, 255, u, v});
	if (gVertices.size() >= size_t(kBatch))
		Flush(shader);
}

uint8_t Byte(float x) { return uint8_t(std::clamp(x * 255.0f, 0.0f, 255.0f)); }

std::vector<wetmodel::Light> Lights()
{
	std::vector<wetmodel::Light> out;
	if (!gPointCount || !gPointLights)
		return out;
	const int count = std::clamp(int(*gPointCount), 0, 64);
	for (int i = 0; i < count; i++) {
		const PointLight &l = gPointLights[i];
		if (l.type > 1 || !std::isfinite(l.position.x) || !std::isfinite(l.position.y) || !std::isfinite(l.position.z) ||
		    !std::isfinite(l.radius) || !std::isfinite(l.colour.x) || !std::isfinite(l.colour.y) ||
		    !std::isfinite(l.colour.z))
			continue; // points and headlights; not the darkening or fog-only kinds
		out.push_back({l.position, l.colour, l.radius});
	}
	return out;
}

struct Stats {
	double ms = 0;
	size_t triangles = 0, streaks = 0, frames = 0;
	std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
} gStats;

void Draw()
{
	const rainmodel::Weather &w = weather::Now();
	if (!gConfig.wetGround || w.wetness < 0.02f || (gArea && *gArea != 0))
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
	for (int c : wetmodel::CellsNear(*map, {eye.x, eye.y, eye.z}, kRadius)) {
		const std::vector<Vec3> &tris = map->wet[size_t(c)].tris;
		for (const Vec3 &p : tris)
			Put(gWet, p, 255, 255, 255, 0, 0);
		triangles += tris.size() / 3;
	}
	Flush(gWet);

	const wetmodel::GroundAt ground = [&](Vec3 p, float &z) { return wetmodel::WetGroundAt(*map, p, z); };
	const std::vector<wetmodel::Reflection> streaks = wetmodel::Reflections(Lights(), {eye.x, eye.y, eye.z}, ground);
	api.RwRenderStateSet(kStateDestBlend, reinterpret_cast<void *>(kBlendOne));
	for (const wetmodel::Reflection &s : streaks) {
		const uint8_t r = Byte(s.colour.x * 0.8f), g = Byte(s.colour.y * 0.8f), b = Byte(s.colour.z * 0.8f);
		const float u[4] = {-1, 1, 1, -1}, v[4] = {0, 0, 1, 1};
		for (int k : {0, 1, 2, 0, 2, 3})
			Put(gStreak, s.quad[k], r, g, b, u[k], v[k]);
	}
	Flush(gStreak);

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

template <typename T> bool Bind(T &out, const char *symbol)
{
	out = reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY, symbol));
	return out != nullptr;
}

} // namespace

bool Install()
{
	if (!gConfig.wetGround)
		return true;
	gPointCount = static_cast<const int16_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN12CPointLights9NumLightsE"));
	gPointLights = static_cast<const PointLight *>(dlsym(RTLD_MAIN_ONLY, "_ZN12CPointLights7aLightsE"));
	gArea = static_cast<const int32_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN5CGame8currAreaE"));
	const bool ready = weather::Install() && hooks::IsCallTo(kFromRenderScene, "_ZN9CRenderer24RenderEverythingBarRoadsEv") &&
	                   Bind(gTransform, "_Z15RwIm3DTransformPN2rw3gl310Im3DVertexEjPNS_6MatrixEj") &&
	                   Bind(gRender, "_Z28RwIm3DRenderIndexedPrimitive15RwPrimitiveTypePti") &&
	                   Bind(gEnd, "_Z9RwIm3DEndv") && Bind(gUse, "_ZN2rw3gl36Shader3useEv");
	const bool installed =
	    ready && hooks::Install("_ZN9CRenderer24RenderEverythingBarRoadsEv", Hook_RenderEverythingBarRoads,
	                            orig_RenderEverythingBarRoads);
	Log("wet roads: %s", installed ? "on" : "hook missing (re-run install.sh)");
	return installed;
}

} // namespace wetroads

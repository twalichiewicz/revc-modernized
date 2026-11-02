#include "checkpoint.h"
#include "checkpointmodel.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "shaders.h"
#include <cmath>
#include <cstring>
#include <dlfcn.h>

using namespace game;
namespace checkpoint {
namespace {
// Verified from the player's unmodified main.scm1.0.5217, not guessed from
// marker proximity. RACE uses opcode04D5 at file120dc7/120de9. Both RC race
// scripts use the same current/next pair pattern. Unordered collection rings
// (Dirt Ring, PCJ/4x4) have no single following checkpoint and are omitted.
struct Race {
	const char *name;
	uint16_t current, next, currentHandle, nextHandle;
	float radius;
};
constexpr Race kRaces[] = {{"RACE", 0x74f4, 0x7500, 0x7424, 0x7428, 5.5f},
                           {"RCRACE1", 0x7ff4, 0x8000, 0x8048, 0x804c, 3.8f},
                           {"RCPLNE1", 0x83c4, 0x83d0, 0x841c, 0x8420, 7.0f}};
constexpr size_t kScriptStride = 0x90, kScripts = 128;
const uint8_t *gScriptPool;
void (*orig_Render)();
struct Vertex {
	float x, y, z, nx, ny, nz;
	uint8_t r, g, b, a;
	float u, v;
};
void *(*gTransform)(Vertex *, uint32_t, float *, uint32_t);
int (*gRender)(int, uint16_t *, int);
int (*gEnd)();

template <typename T> T Global(uint16_t offset)
{
	T result{};
	std::memcpy(&result, api.scriptSpace + offset, sizeof result);
	return result;
}
bool ScriptValid(const uint8_t *script)
{
	const uintptr_t p = reinterpret_cast<uintptr_t>(script), base = reinterpret_cast<uintptr_t>(gScriptPool);
	return p >= base && p < base + kScriptStride * kScripts && (p - base) % kScriptStride == 0;
}
const Race *ActiveRace()
{
	int remaining = int(kScripts);
	for (uint8_t *script = *api.activeScripts; script && remaining-- && ScriptValid(script);
	     script = ScriptNext(script)) {
		for (const Race &race : kRaces)
			if (strncasecmp(ScriptName(script), race.name, 8) == 0)
				return &race;
	}
	return nullptr;
}
void Draw()
{
	if (!gConfig.raceArrows)
		return;
	const Race *race = ActiveRace();
	if (!race)
		return;
	checkpointmodel::Input input{true,
	                             Global<int32_t>(race->currentHandle),
	                             Global<int32_t>(race->nextHandle),
	                             Global<Vec3>(race->current),
	                             Global<Vec3>(race->next),
	                             race->radius};
	checkpointmodel::Blip blips[kNumBlips];
	for (int i = 0; i < kNumBlips; i++) {
		const Blip &b = api.blips[i];
		blips[i] = {b.inUse && b.type == kBlipCoord, b.uniqueIndex, {b.pos.x, b.pos.y, b.pos.z}};
	}
	const auto arrow = checkpointmodel::Resolve(input, blips, kNumBlips);
	if (!arrow.valid)
		return;
	const CVector eye = shaders::CameraPosition();
	if (std::hypot(input.current.x - eye.x, input.current.y - eye.y) > 220)
		return;
	Vertex vertices[7]{};
	for (int i = 0; i < 7; i++) {
		vertices[i].x = arrow.vertices[i].x;
		vertices[i].y = arrow.vertices[i].y;
		vertices[i].z = arrow.vertices[i].z;
		vertices[i].nz = 1;
		vertices[i].r = 255;
		vertices[i].g = 245;
		vertices[i].b = 190;
		vertices[i].a = 235;
	}
	// Both faces are visible without changing the engine's shared cull mode.
	uint16_t indices[] = {0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 5, 0, 5, 6, 0, 2, 1, 0, 3, 2, 0, 4, 3, 0, 5, 4, 0, 6, 5};
	const shaders::SavedStates saved;
	api.RwRenderStateSet(kStateTextureRaster, nullptr);
	api.RwRenderStateSet(kStateZTest, reinterpret_cast<void *>(1));
	api.RwRenderStateSet(kStateZWrite, nullptr);
	api.RwRenderStateSet(kStateVertexAlpha, reinterpret_cast<void *>(1));
	api.RwRenderStateSet(kStateSrcBlend, reinterpret_cast<void *>(kBlendSrcAlpha));
	api.RwRenderStateSet(kStateDestBlend, reinterpret_cast<void *>(kBlendInvSrcAlpha));
	api.RwRenderStateSet(kStateFog, nullptr);
	if (gTransform(vertices, 7, nullptr, 0)) {
		gRender(3, indices, 30);
		gEnd();
	}
}
void Hook_Render()
{
	orig_Render();
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
	if (!gConfig.raceArrows)
		return true;
	const bool ready =
	    api.scriptSpace && api.activeScripts && api.blips && Bind(gScriptPool, "_ZN11CTheScripts12ScriptsArrayE") &&
	    Bind(gTransform, "_Z15RwIm3DTransformPN2rw3gl310Im3DVertexEjPNS_6MatrixEj") &&
	    Bind(gRender, "_Z28RwIm3DRenderIndexedPrimitive15RwPrimitiveTypePti") && Bind(gEnd, "_Z9RwIm3DEndv");
	const bool installed = ready && hooks::Install("_ZN10C3dMarkers6RenderEv", Hook_Render, orig_Render);
	Log("checkpoint: racing next-direction arrows %s (street, RC car, RC plane)", installed ? "on" : "unavailable");
	return installed;
}
} // namespace checkpoint

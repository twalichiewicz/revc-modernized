#include "planes.h"
#include "config.h"
#include "flightpath.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "shaders.h"
#include "sky.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <dlfcn.h>
#include <vector>

using namespace game;

namespace planes {
namespace {

// CPlane, CVehicleModelInfo (macVC 1.0.5217); CPlaneNode: its point, how far
// along the path it is over the ground, on the runway or not (20 bytes).
constexpr size_t kRwObject = 0x58, kNodeSize = 0x14;
constexpr size_t kWingLight = 0x6c, kTailLight = 0x78; // m_positions[PLANE_POS_LIGHT_RIGHT], [... TAIL]
constexpr int kAtomic = 1, kClump = 2;                 // RwObject types
constexpr uint32_t kModulate = 0x40;                   // rpGEOMETRYMODULATEMATERIALCOLOR

// Lit by the sky: the sun's light (the game's directional for the hour) on
// it while the sun's up, a quarter of it in a cloud's shadow; the sky's
// colours (its top and its horizon) all round it.
constexpr float kSkyAmbient = 0.7f, kShadowLight = 0.25f;
// Faded in cloud: drawn as it is above this much showing, not at all below this.
constexpr float kShowsAll = 0.97f, kShowsNone = 0.03f;

// Its lights (their places in its frame where the model has none: the
// beacons on top of the fuselage and under it), how they flash (s), their
// size (m, and at least as big as they'd look this far off), how far off
// they're seen.
constexpr CVector kBeaconTop{0.0f, 0.0f, 2.45f}, kBeaconUnder{0.0f, 0.0f, -2.3f};
constexpr float kBeaconEvery = 1.3f, kBeaconOn = 0.4f, kStrobeEvery = 1.5f, kStrobeOn = 0.3f;
constexpr float kBeaconSize = 1.4f, kStrobeSize = 1.8f, kNavSize = 1.0f, kSizeFrom = 400.0f;
constexpr float kLightsReach = 4000.0f, kDayLights = 0.2f;
constexpr uint32_t kFirstLight = 100; // our coronas' ids, after the plane's (the game's are + 10 to 12)
constexpr int16_t kJumbo = 180;       // MI_AIRTRAIN: the lights are where its model has them
// Buildings in the way hide its lights: one look along the line to it, now
// and then, rather than the game's for each corona (LOScheck). (The game's
// lines of sight read past the world's grid for a point off it, and the
// jumbos fly off it: gridfix.h.)
constexpr uint32_t kLookEvery = 150; // ms

uint8_t **gPathNodes; // pPathNodes (flight.dat's: the jumbos')
int32_t *gNumPathNodes;
const uint8_t *gRaisedNodes; // what was raised, and its highest node then (a fresh load where it was comes back lower)
int gPeakNode = -1;
float gPeakZ;

void **gAmbient, **gDirect; // pAmbient, pDirect: RpLight
const float *gDirectionalColour; // DirectionalLightColourForFrame (RGBA, 0..1)
void (*gSetLightColour)(void *light, const float *rgba);
void (*gCarsAmbient)(); // SetAmbientColoursForPedsCarsAndObjects

int (*gObjectType)(const void *object);
void *(*gForAllAtomics)(void *clump, void *(*cb)(void *atomic, void *data), void *data);
void *(*gAtomicGeometry)(const void *atomic);
uint32_t (*gGeometryFlags)(const void *geometry);
void (*gSetGeometryFlags)(void *geometry, uint32_t flags);
void *(*gMaterial)(const void *geometry, int index);
const uint8_t *(*gMaterialColour)(const void *material);
void (*gSetMaterialColour)(void *material, const uint8_t *rgba);
int (*gNumTriangles)(const void *geometry);
const uint16_t *(*gTriangles)(const void *geometry); // v[3], material

void (*gRegisterCorona)(uint32_t, uint8_t, uint8_t, uint8_t, uint8_t, const CVector &, float, float, uint8_t, int8_t,
                        uint8_t, uint8_t, uint8_t, float, bool, float);
void (*orig_Render)(uint8_t *plane);
void (*orig_PreRender)(uint8_t *plane);

// --- Up into the clouds --------------------------------------------------------------------

void RaisePath()
{
	uint8_t *nodes = gPathNodes ? *gPathNodes : nullptr;
	const int n = gNumPathNodes ? *gNumPathNodes : 0;
	if (!nodes || n < 3)
		return;
	if (nodes == gRaisedNodes && gPeakNode >= 0 && gPeakNode < n &&
	    *reinterpret_cast<const float *>(nodes + size_t(gPeakNode) * kNodeSize + 8) == gPeakZ)
		return;
	std::vector<Vec3> path(static_cast<size_t>(n));
	for (int i = 0; i < n; i++) {
		const float *p = reinterpret_cast<const float *>(nodes + size_t(i) * kNodeSize);
		path[size_t(i)] = {p[0], p[1], p[2]};
	}
	float before = 0.0f;
	for (const Vec3 &p : path)
		before = std::max(before, p.z);
	flightpath::Raise(path);
	gPeakNode = 0;
	for (int i = 0; i < n; i++) {
		reinterpret_cast<float *>(nodes + size_t(i) * kNodeSize)[2] = path[size_t(i)].z;
		if (path[size_t(i)].z > path[size_t(gPeakNode)].z)
			gPeakNode = i;
	}
	gRaisedNodes = nodes;
	gPeakZ = path[size_t(gPeakNode)].z;
	Log("planes: the jumbos' flight path raised (%d nodes): up to %.0f m, was %.0f m", n, gPeakZ, before);
}

// --- In the clouds --------------------------------------------------------------------------

// How much of each plane shows through the clouds this frame (worked out as it's pre-rendered).
struct Seen {
	const uint8_t *plane;
	float shows;
	bool clear = true;      // no building between the camera and it
	uint32_t lookedAt = 0;  // when that was looked at
};
std::vector<Seen> gSeen;
float Shows(const uint8_t *plane)
{
	for (const Seen &s : gSeen)
		if (s.plane == plane)
			return s.shows;
	return 1.0f;
}

// Material pointers are shared across a clump's geometries (and its cloned
// actors). Snapshot/apply each once for this render scope. Local ownership
// also lets a nested render restore its caller's temporary state exactly.
struct Faded {
	std::vector<std::pair<void *, uint32_t>> geometries;
	std::vector<std::pair<void *, std::array<uint8_t, 4>>> colours;
	uint8_t alpha = 255;
	~Faded()
	{
		for (const auto &[material, colour] : colours)
			gSetMaterialColour(material, colour.data());
		for (const auto &[geometry, flags] : geometries)
			gSetGeometryFlags(geometry, flags);
	}
};

int Materials(const void *geometry)
{
	int most = -1;
	const uint16_t *t = gTriangles(geometry);
	for (int i = 0, n = gNumTriangles(geometry); t && i < n; i++)
		most = std::max(most, int(t[i * 4 + 3]));
	return most + 1;
}

void *FadeAtomic(void *atomic, void *context)
{
	auto &f = *static_cast<Faded *>(context);
	void *geometry = gAtomicGeometry(atomic);
	if (!geometry || std::any_of(f.geometries.begin(), f.geometries.end(),
	                             [&](const auto &entry) { return entry.first == geometry; }))
		return atomic;
	const uint32_t flags = gGeometryFlags(geometry);
	f.geometries.push_back({geometry, flags});
	gSetGeometryFlags(geometry, flags | kModulate);
	for (int i = 0, n = Materials(geometry); i < n; i++) {
		void *m = gMaterial(geometry, i);
		if (!m || std::any_of(f.colours.begin(), f.colours.end(),
		                      [&](const auto &entry) { return entry.first == m; }))
			continue;
		std::array<uint8_t, 4> c;
		std::copy(gMaterialColour(m), gMaterialColour(m) + 4, c.begin());
		f.colours.push_back({m, c});
		c[3] = uint8_t(c[3] * f.alpha / 255);
		gSetMaterialColour(m, c.data());
	}
	return atomic;
}

void Fade(void *object, float alpha, Faded &f)
{
	f.alpha = uint8_t(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
	const int type = gObjectType(object);
	if (type == kClump)
		gForAllAtomics(object, FadeAtomic, &f);
	else if (type == kAtomic)
		FadeAtomic(object, &f);
}

// --- Its lights ------------------------------------------------------------------------------

// A point of its, in the world (its CMatrix: right, forward, up, position, 16 bytes each).
CVector At(const uint8_t *plane, const CVector &p)
{
	auto row = [&](int i) { return *reinterpret_cast<const CVector *>(plane + 8 + 16 * i); };
	const CVector r = row(0), f = row(1), u = row(2), o = row(3);
	return {o.x + r.x * p.x + f.x * p.y + u.x * p.z, o.y + r.y * p.x + f.y * p.y + u.y * p.z, o.z + r.z * p.x + f.z * p.y + u.z * p.z};
}

void Light(const uint8_t *plane, uint32_t which, const CVector &local, uint8_t r, uint8_t g, uint8_t b, float size,
           float strength)
{
	if (strength <= 0.0f)
		return;
	const CVector at = At(plane, local), cam = shaders::CameraPosition();
	const float far = std::sqrt((at.x - cam.x) * (at.x - cam.x) + (at.y - cam.y) * (at.y - cam.y) + (at.z - cam.z) * (at.z - cam.z));
	const uint8_t a = uint8_t(std::clamp(strength, 0.0f, 1.0f) * 255.0f);
	// (No LOScheck: Hook_PreRender looks for buildings in the way.)
	gRegisterCorona(uint32_t(uintptr_t(plane)) + kFirstLight + which, r, g, b, a, at, size * std::max(1.0f, far / kSizeFrom),
	                kLightsReach, 1, 0, 0, 0, 0, 0.0f, false, 1.5f);
}

// A pulse `on` s long every `every` s, each plane its own time (its address).
bool Pulse(const uint8_t *plane, float every, float on, float offset = 0.0f)
{
	const float t = float(*api.timeMs) / 1000.0f + float((uintptr_t(plane) >> 4) % 97) * 0.13f + offset;
	return std::fmod(t, every) < on;
}

void Lights(const uint8_t *plane, float shows)
{
	if (!gRegisterCorona)
		return;
	const int16_t model = EntityModel(plane);
	const uint8_t *mi = model == kJumbo ? api.modelInfoPtrs[model] : nullptr;
	if (!mi)
		return;
	const shaders::Sky sky = shaders::CurrentSky();
	const float strength = (1.0f - (1.0f - kDayLights) * sky.daylight) * shows;
	const CVector wing = *reinterpret_cast<const CVector *>(mi + kWingLight), tail = *reinterpret_cast<const CVector *>(mi + kTailLight);
	const CVector right{std::fabs(wing.x), wing.y, wing.z}, left{-std::fabs(wing.x), wing.y, wing.z};
	// Navigation lights: red on the left wingtip, green on the right, white on the tail.
	Light(plane, 0, left, 255, 40, 30, kNavSize, strength * 0.8f);
	Light(plane, 1, right, 40, 255, 90, kNavSize, strength * 0.8f);
	Light(plane, 2, tail, 255, 255, 255, kNavSize, strength * 0.6f);
	// Red beacons on top and underneath, pulsing; white strobes on the wingtips, flashing.
	const bool beacon = Pulse(plane, kBeaconEvery, kBeaconOn), strobe = Pulse(plane, kStrobeEvery, kStrobeOn, 0.5f);
	Light(plane, 3, kBeaconTop, 255, 30, 20, kBeaconSize, beacon ? strength : 0.0f);
	Light(plane, 4, kBeaconUnder, 255, 30, 20, kBeaconSize, beacon ? strength : 0.0f);
	Light(plane, 5, {left.x - 0.3f, left.y, left.z}, 255, 255, 255, kStrobeSize, strobe ? strength : 0.0f);
	Light(plane, 6, {right.x + 0.3f, right.y, right.z}, 255, 255, 255, kStrobeSize, strobe ? strength : 0.0f);
}

// --- The hooks -----------------------------------------------------------------------------

// Before it's drawn: the path raised (once), how much of it the clouds let
// through from the camera, and its lights.
void Hook_PreRender(uint8_t *plane)
{
	orig_PreRender(plane);
	RaisePath();
	float shows = 1.0f;
	sky::Through(shaders::CameraPosition(), EntityPos(plane), shows);
	auto it = std::find_if(gSeen.begin(), gSeen.end(), [&](const Seen &s) { return s.plane == plane; });
	if (it == gSeen.end()) {
		if (gSeen.size() > 16)
			gSeen.erase(gSeen.begin());
		gSeen.push_back({plane, shows});
		it = gSeen.end() - 1;
		it->lookedAt = *api.timeMs - kLookEvery;
	}
	it->shows = shows;
	const uint32_t now = *api.timeMs;
	if (now - it->lookedAt >= kLookEvery) {
		it->lookedAt = now;
		it->clear = api.GetIsLineOfSightClear(shaders::CameraPosition(), EntityPos(plane), true, false, false, false, false,
		                                      false, false);
	}
	Lights(plane, it->clear ? shows : 0.0f);
}

// Drawn lit by the sky it's in, faded in cloud (or not at all, inside it).
void Hook_Render(uint8_t *plane)
{
	void *object = *reinterpret_cast<void **>(plane + kRwObject);
	const float shows = Shows(plane);
	if (!object || shows < kShowsNone)
		return;
	const shaders::Sky sky = shaders::CurrentSky();
	float sun = 1.0f;
	sky::SunAt(EntityPos(plane), sun);
	const float up = std::clamp((sky.sun.z + 0.05f) / 0.2f, 0.0f, 1.0f), lit = up * (kShadowLight + (1.0f - kShadowLight) * sun);
	const float direct[4] = {gDirectionalColour[0] * lit, gDirectionalColour[1] * lit, gDirectionalColour[2] * lit, 1.0f};
	const float ambient[4] = {(sky.top.x + sky.bottom.x) * 0.5f * kSkyAmbient, (sky.top.y + sky.bottom.y) * 0.5f * kSkyAmbient,
	                          (sky.top.z + sky.bottom.z) * 0.5f * kSkyAmbient, 1.0f};
	// Restore the existing native light state even if drawing is interrupted.
	struct LightingScope {
		~LightingScope()
		{
			gCarsAmbient();
			gSetLightColour(*gDirect, gDirectionalColour);
		}
	} lighting;
	gSetLightColour(*gAmbient, ambient);
	gSetLightColour(*gDirect, direct);
	{
		Faded faded;
		if (shows < kShowsAll)
			Fade(object, shows, faded);
		orig_Render(plane);
	}
}

template <typename T>
bool Bind(T &out, const char *name)
{
	out = reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY, name));
	if (!out)
		Log("planes: missing %s", name);
	return out != nullptr;
}

} // namespace

bool Install()
{
	if (!gConfig.planes)
		return true;
	const bool bound =
	    Bind(gPathNodes, "pPathNodes") && Bind(gNumPathNodes, "NumPathNodes") && Bind(gAmbient, "pAmbient") &&
	    Bind(gDirect, "pDirect") && Bind(gDirectionalColour, "DirectionalLightColourForFrame") &&
	    Bind(gSetLightColour, "_Z15RpLightSetColorPN2rw5LightEPKNS_5RGBAfE") &&
	    Bind(gCarsAmbient, "_Z38SetAmbientColoursForPedsCarsAndObjectsv") &&
	    Bind(gObjectType, "_Z15RwObjectGetTypePKN2rw6ObjectE") &&
	    Bind(gForAllAtomics, "_Z20RpClumpForAllAtomicsPN2rw5ClumpEPFPNS_6AtomicES3_PvES4_") &&
	    Bind(gAtomicGeometry, "_Z19RpAtomicGetGeometryPKN2rw6AtomicE") &&
	    Bind(gGeometryFlags, "_Z18RpGeometryGetFlagsPKN2rw8GeometryE") &&
	    Bind(gSetGeometryFlags, "_Z18RpGeometrySetFlagsPN2rw8GeometryEj") &&
	    Bind(gMaterial, "_Z21RpGeometryGetMaterialPKN2rw8GeometryEi") &&
	    Bind(gMaterialColour, "_Z18RpMaterialGetColorPKN2rw8MaterialE") &&
	    Bind(gSetMaterialColour, "_Z18RpMaterialSetColorPN2rw8MaterialEPKNS_4RGBAE") &&
	    Bind(gNumTriangles, "_Z25RpGeometryGetNumTrianglesPKN2rw8GeometryE") &&
	    Bind(gTriangles, "_Z22RpGeometryGetTrianglesPKN2rw8GeometryE") &&
	    Bind(gRegisterCorona, "_ZN8CCoronas14RegisterCoronaEjhhhhRK7CVectorffhahhhfbf") && api.GetIsLineOfSightClear;
	if (!bound || !hooks::Install("_ZN6CPlane6RenderEv", Hook_Render, orig_Render) ||
	    !hooks::Install("_ZN6CPlane9PreRenderEv", Hook_PreRender, orig_PreRender)) {
		Log("planes: functions or hooks missing (re-run install.sh); the airliners are the game's own");
		return false;
	}
	return true;
}

} // namespace planes

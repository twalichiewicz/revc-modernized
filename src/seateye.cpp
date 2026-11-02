#include "seateye.h"
#include "config.h"
#include "game.h"
#include "log.h"
#include "seatview.h"

#include <algorithm>
#include <cmath>
#include <dlfcn.h>
#include <strings.h>
#include <unordered_map>
#include <vector>

using namespace game;

namespace seateye {
namespace {

// CVehicle, CBaseModelInfo (macVC 1.0.5217).
constexpr size_t kRwObject = 0x58, kModelIndex = 0x6a, kVehicleType = 0x348;
constexpr size_t kModelName = 0x8; // CBaseModelInfo::m_name
constexpr int32_t kCar = 0;        // eVehicleType
// The body's pieces that can be in the way ahead (their undamaged ones; the
// windscreen's glass isn't, nor any piece's glass).
const char *const kBody[] = {"chassis_hi",    "bonnet_hi_ok",     "wing_lf_hi_ok", "wing_rf_hi_ok",
                             "bump_front_hi_ok", "door_lf_hi_ok", "door_rf_hi_ok"};
constexpr float kSettle = 0.5f; // s in the seat before his eye's where he sits (the get-in's blend over)
constexpr float kEaseUp = 4.0f; // a second

struct V3 {
	float x, y, z;
};
struct Triangle {
	uint16_t v[3], material;
};
struct RW {
	void *(*ForAllAtomics)(void *clump, void *(*cb)(void *atomic, void *data), void *data);
	void *(*AtomicFrame)(const void *atomic);
	void *(*AtomicGeometry)(const void *atomic);
	float *(*FrameLTM)(void *frame);
	const char *(*FrameName)(void *frame);
	int (*NumTriangles)(const void *geometry);
	const Triangle *(*Triangles)(const void *geometry);
	void *(*MorphTarget)(const void *geometry, int index);
	const V3 *(*Vertices)(const void *morphTarget);
	const void *(*Material)(const void *geometry, int index);
	const uint8_t *(*Colour)(const void *material); // RwRGBA
} rw;
bool gReady;

V3 Apply(const float *m, V3 p) // rw::Matrix: right, up, at and position, each with a pad
{
	return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
	        m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
}
// The car's CMatrix: right, forward, up and position (each 16 bytes from +8).
const V3 &Axis(const uint8_t *car, size_t row) { return *reinterpret_cast<const V3 *>(car + 8 + row); }
Vec3 InCar(const uint8_t *car, V3 world)
{
	const V3 &o = Axis(car, 0x30);
	const V3 d{world.x - o.x, world.y - o.y, world.z - o.z};
	auto dot = [&](const V3 &a) { return d.x * a.x + d.y * a.y + d.z * a.z; };
	return {dot(Axis(car, 0)), dot(Axis(car, 0x10)), dot(Axis(car, 0x20))};
}

struct Body {
	const uint8_t *car;
	std::vector<seatview::Triangle> *out;
};
void *Collect(void *atomic, void *data)
{
	const Body &b = *static_cast<Body *>(data);
	void *frame = rw.AtomicFrame(atomic);
	const char *name = frame ? rw.FrameName(frame) : nullptr;
	if (!name || std::none_of(std::begin(kBody), std::end(kBody), [&](const char *n) { return !strcasecmp(name, n); }))
		return atomic;
	const void *geometry = rw.AtomicGeometry(atomic);
	void *morph = geometry ? rw.MorphTarget(geometry, 0) : nullptr;
	const V3 *verts = morph ? rw.Vertices(morph) : nullptr;
	const Triangle *tris = geometry ? rw.Triangles(geometry) : nullptr;
	const float *ltm = rw.FrameLTM(frame);
	if (!verts || !tris || !ltm)
		return atomic;
	for (int i = 0, n = rw.NumTriangles(geometry); i < n; i++) {
		const void *material = rw.Material(geometry, tris[i].material);
		const uint8_t *colour = material ? rw.Colour(material) : nullptr;
		if (colour && colour[3] < 255) // glass
			continue;
		const Vec3 a = InCar(b.car, Apply(ltm, verts[tris[i].v[0]])), c = InCar(b.car, Apply(ltm, verts[tris[i].v[1]])),
		           d = InCar(b.car, Apply(ltm, verts[tris[i].v[2]]));
		b.out->push_back({a, c, d});
	}
	return atomic;
}

std::unordered_map<int, float> gLifts; // by model
const uint8_t *gCar;
float gSeated, gNow;

float Work(const uint8_t *car, int model, Vec3 eye)
{
	std::vector<seatview::Triangle> body;
	Body b{car, &body};
	if (void *clump = *reinterpret_cast<void *const *>(car + kRwObject))
		rw.ForAllAtomics(clump, Collect, &b);
	const float lift = seatview::Lift(body, eye);
	const seatview::Window was = seatview::Ahead(body, eye, 0.0f), now = seatview::Ahead(body, eye, lift);
	const uint8_t *mi = api.modelInfoPtrs[model];
	Log("seateye: %s: his eye %.2f m over the car's middle, up %.2f m (degrees from level: the hood %+.1f, now %+.1f; "
	    "the roof's edge %+.1f, now %+.1f)",
	    mi ? reinterpret_cast<const char *>(mi + kModelName) : "?", eye.z, lift, was.low, now.low, was.high, now.high);
	return lift;
}

} // namespace

float Lift(const uint8_t *vehicle, Vec3 eye, bool driving, float dt)
{
	if (!gReady || !vehicle || *reinterpret_cast<const int32_t *>(vehicle + kVehicleType) != kCar) {
		gCar = nullptr;
		return 0.0f;
	}
	if (vehicle != gCar) {
		gCar = vehicle;
		gSeated = gNow = 0.0f;
	}
	gSeated = driving ? gSeated + dt : 0.0f;
	const int model = *reinterpret_cast<const int16_t *>(vehicle + kModelIndex);
	auto it = gLifts.find(model);
	if (it == gLifts.end() && gSeated >= kSettle && model >= 0)
		it = gLifts.emplace(model, Work(vehicle, model, eye)).first;
	const float want = it != gLifts.end() ? it->second : 0.0f;
	gNow += (want - gNow) * std::min(1.0f, dt * kEaseUp);
	return gNow;
}

template <typename T>
bool Bind(T &out, const char *name)
{
	out = reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY, name));
	return out != nullptr;
}

bool Install()
{
	if (!gConfig.seatView)
		return true;
	gReady = Bind(rw.ForAllAtomics, "_Z20RpClumpForAllAtomicsPN2rw5ClumpEPFPNS_6AtomicES3_PvES4_") &&
	         Bind(rw.AtomicFrame, "_Z16RpAtomicGetFramePKN2rw6AtomicE") &&
	         Bind(rw.AtomicGeometry, "_Z19RpAtomicGetGeometryPKN2rw6AtomicE") &&
	         Bind(rw.FrameLTM, "_Z13RwFrameGetLTMPN2rw5FrameE") && Bind(rw.FrameName, "_Z16GetFrameNodeNamePN2rw5FrameE") &&
	         Bind(rw.NumTriangles, "_Z25RpGeometryGetNumTrianglesPKN2rw8GeometryE") &&
	         Bind(rw.Triangles, "_Z22RpGeometryGetTrianglesPKN2rw8GeometryE") &&
	         Bind(rw.MorphTarget, "_Z24RpGeometryGetMorphTargetPKN2rw8GeometryEi") &&
	         Bind(rw.Vertices, "_Z24RpMorphTargetGetVerticesPKN2rw11MorphTargetE") &&
	         Bind(rw.Material, "_Z21RpGeometryGetMaterialPKN2rw8GeometryEi") &&
	         Bind(rw.Colour, "_Z18RpMaterialGetColorPKN2rw8MaterialE") && api.modelInfoPtrs;
	if (!gReady)
		Log("seateye: functions missing; his eye stays where the game sits his head");
	return gReady;
}

} // namespace seateye

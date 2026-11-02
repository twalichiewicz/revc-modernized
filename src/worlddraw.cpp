#include "worlddraw.h"
#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <dlfcn.h>

namespace worlddraw {
namespace {

constexpr size_t kBatch = 3 * 4000; // vertices per draw

// CPointLights::aLights' records (as estatelighting reads them).
struct PointLight {
	Vec3 position, direction;
	float radius;
	Vec3 colour;
	uint8_t type, fog, extraShadows, pad;
};
static_assert(sizeof(PointLight) == 0x2c);

void *(*gTransform)(Vertex *, uint32_t, float *, uint32_t);
int (*gRender)(int, uint16_t *, int);
int (*gEnd)();
void (*gUse)(void *shader);
const int16_t *gPointCount;
const PointLight *gPointLights;
std::vector<uint16_t> gIndices;

// CVehicle (macVC 1.0.5217).
constexpr size_t kRwObject = 0x58;
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
bool gGeometry;
uint8_t **gVehiclePool;
bool (*gIsOpenTopCar)(const void *car);

V3 Apply(const float *m, V3 p) // rw::Matrix: right, up, at and position, each with a pad
{
	return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
	        m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
}
void *Collect(void *atomic, void *data)
{
	constexpr uint8_t kRenders = 0x04; // rw::Atomic's RENDER flag: the game hides the damaged pieces with it
	if (!(static_cast<const uint8_t *>(atomic)[2] & kRenders))
		return atomic;
	void *frame = rw.AtomicFrame(atomic);
	const char *name = frame ? rw.FrameName(frame) : nullptr;
	if (name && strstr(name, "_vlo"))
		return atomic; // the far version (drawn instead of the near one, never with it)
	const void *geometry = rw.AtomicGeometry(atomic);
	void *morph = geometry ? rw.MorphTarget(geometry, 0) : nullptr;
	const V3 *verts = morph ? rw.Vertices(morph) : nullptr;
	const Triangle *tris = geometry ? rw.Triangles(geometry) : nullptr;
	const float *ltm = frame ? rw.FrameLTM(frame) : nullptr;
	if (!verts || !tris || !ltm)
		return atomic;
	std::vector<Face> &out = *static_cast<std::vector<Face> *>(data);
	for (int i = 0, n = rw.NumTriangles(geometry); i < n; i++) {
		const void *material = rw.Material(geometry, tris[i].material);
		const uint8_t *colour = material ? rw.Colour(material) : nullptr;
		const V3 a = Apply(ltm, verts[tris[i].v[0]]), b = Apply(ltm, verts[tris[i].v[1]]), c = Apply(ltm, verts[tris[i].v[2]]);
		out.push_back({{a.x, a.y, a.z}, {b.x, b.y, b.z}, {c.x, c.y, c.z}, colour && colour[3] < 255});
	}
	return atomic;
}

template <typename T> bool Bind(T &out, const char *symbol)
{
	out = reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY, symbol));
	return out != nullptr;
}

bool Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

} // namespace

bool Install()
{
	gPointCount = static_cast<const int16_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN12CPointLights9NumLightsE"));
	gPointLights = static_cast<const PointLight *>(dlsym(RTLD_MAIN_ONLY, "_ZN12CPointLights7aLightsE"));
	gGeometry = Bind(rw.ForAllAtomics, "_Z20RpClumpForAllAtomicsPN2rw5ClumpEPFPNS_6AtomicES3_PvES4_") &&
	            Bind(rw.AtomicFrame, "_Z16RpAtomicGetFramePKN2rw6AtomicE") &&
	            Bind(rw.AtomicGeometry, "_Z19RpAtomicGetGeometryPKN2rw6AtomicE") &&
	            Bind(rw.FrameLTM, "_Z13RwFrameGetLTMPN2rw5FrameE") && Bind(rw.FrameName, "_Z16GetFrameNodeNamePN2rw5FrameE") &&
	            Bind(rw.NumTriangles, "_Z25RpGeometryGetNumTrianglesPKN2rw8GeometryE") &&
	            Bind(rw.Triangles, "_Z22RpGeometryGetTrianglesPKN2rw8GeometryE") &&
	            Bind(rw.MorphTarget, "_Z24RpGeometryGetMorphTargetPKN2rw8GeometryEi") &&
	            Bind(rw.Vertices, "_Z24RpMorphTargetGetVerticesPKN2rw11MorphTargetE") &&
	            Bind(rw.Material, "_Z21RpGeometryGetMaterialPKN2rw8GeometryEi") &&
	            Bind(rw.Colour, "_Z18RpMaterialGetColorPKN2rw8MaterialE");
	gVehiclePool = static_cast<uint8_t **>(dlsym(RTLD_MAIN_ONLY, "_ZN6CPools15ms_pVehiclePoolE"));
	gIsOpenTopCar = reinterpret_cast<bool (*)(const void *)>(dlsym(RTLD_MAIN_ONLY, "_ZN11CAutomobile12IsOpenTopCarEv"));
	return Bind(gTransform, "_Z15RwIm3DTransformPN2rw3gl310Im3DVertexEjPNS_6MatrixEj") &&
	       Bind(gRender, "_Z28RwIm3DRenderIndexedPrimitive15RwPrimitiveTypePti") && Bind(gEnd, "_Z9RwIm3DEndv") &&
	       Bind(gUse, "_ZN2rw3gl36Shader3useEv");
}

void Batch::Put(Vec3 p, uint8_t r, uint8_t g, uint8_t b, float u, float v, uint8_t a)
{
	vertices_.push_back({p.x, p.y, p.z, 0, 0, 1, r, g, b, a, u, v});
	if (vertices_.size() >= kBatch)
		Flush();
}

void Batch::Flush()
{
	if (vertices_.empty() || !gTransform || !shader_) {
		vertices_.clear();
		return;
	}
	if (gIndices.size() < vertices_.size())
		for (size_t i = gIndices.size(); i < vertices_.size(); i++)
			gIndices.push_back(uint16_t(i));
	if (gTransform(vertices_.data(), uint32_t(vertices_.size()), nullptr, 1)) {
		gUse(shader_);
		gRender(3, gIndices.data(), int(vertices_.size()));
		gEnd();
	}
	vertices_.clear();
}

std::vector<Light> PointLights()
{
	std::vector<Light> out;
	if (!gPointCount || !gPointLights)
		return out;
	const int count = std::clamp(int(*gPointCount), 0, 64);
	for (int i = 0; i < count; i++) {
		const PointLight &l = gPointLights[i];
		if (l.type <= 1 && Finite(l.position) && Finite(l.colour) && std::isfinite(l.radius))
			out.push_back({l.position, l.colour, l.radius});
	}
	return out;
}

uint8_t Byte(float x) { return uint8_t(std::clamp(x * 255.0f, 0.0f, 255.0f)); }

std::vector<uint8_t *> Vehicles()
{
	std::vector<uint8_t *> out;
	const uint8_t *pool = gVehiclePool ? *gVehiclePool : nullptr;
	if (!pool || !game::api.PoolsGetVehicle)
		return out;
	const uint8_t *flags = *reinterpret_cast<uint8_t *const *>(pool + 8); // 0x80: a free entry
	const int size = *reinterpret_cast<const int32_t *>(pool + 0x10);
	for (int i = 0; i < size; i++)
		if (!(flags[i] & 0x80))
			if (uint8_t *v = game::api.PoolsGetVehicle((i << 8) | flags[i]))
				out.push_back(v);
	return out;
}

Axes VehicleAxes(const uint8_t *vehicle) // its CMatrix: right, forward, up and position, 16 bytes each from +8
{
	auto row = [&](size_t at) { return *reinterpret_cast<const Vec3 *>(vehicle + 8 + at); };
	return {row(0), row(0x10), row(0x20), row(0x30)};
}

std::vector<Face> VehicleFaces(const uint8_t *vehicle)
{
	std::vector<Face> faces;
	if (void *clump = gGeometry ? *reinterpret_cast<void *const *>(vehicle + kRwObject) : nullptr)
		rw.ForAllAtomics(clump, Collect, &faces);
	return faces;
}

bool OpenTop(const uint8_t *car) { return gIsOpenTopCar && gIsOpenTopCar(car); }

} // namespace worlddraw

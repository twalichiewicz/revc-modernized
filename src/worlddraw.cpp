#include "worlddraw.h"

#include <algorithm>
#include <cmath>
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
	return Bind(gTransform, "_Z15RwIm3DTransformPN2rw3gl310Im3DVertexEjPNS_6MatrixEj") &&
	       Bind(gRender, "_Z28RwIm3DRenderIndexedPrimitive15RwPrimitiveTypePti") && Bind(gEnd, "_Z9RwIm3DEndv") &&
	       Bind(gUse, "_ZN2rw3gl36Shader3useEv");
}

void Batch::Put(Vec3 p, uint8_t r, uint8_t g, uint8_t b, float u, float v)
{
	vertices_.push_back({p.x, p.y, p.z, 0, 0, 1, r, g, b, 255, u, v});
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

} // namespace worlddraw

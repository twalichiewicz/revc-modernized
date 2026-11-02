#pragma once
#include "game.h"
#include "reactphysics.h"
#include <cstddef>
#include <cstdint>

namespace reactcontact {
// Native macVC1.0.5217 CColPoint. A one-sphere world query writes at most
// one point to gaTempSphereColPoints; a returned car can also have NO point.
struct NativePoint {
	Vec3 point{};
	uint32_t pad1 = 0;
	Vec3 normal{};
	uint32_t pad2 = 0;
	uint8_t surfaceA = 0, pieceA = 0, surfaceB = 0, pieceB = 0;
	float depth = 0;
};
static_assert(sizeof(NativePoint) == 0x28 && offsetof(NativePoint, normal) == 0x10 &&
              offsetof(NativePoint, depth) == 0x24);
using SphereQuery = uint8_t *(*)(CVector, float, uint8_t *, bool, bool, bool, bool, bool, bool);
// Native CCollision ABI. Model A has exactly one sphere and no lines, so
// the native output is bounded to one CColPoint. Optional for pure fixtures.
using ModelsQuery = int (*)(const uint8_t *, uint8_t *, const uint8_t *, uint8_t *, uint8_t *, uint8_t *, float *);
struct NativeBindings {
	ModelsQuery models = nullptr;
	uint8_t **modelInfos = nullptr; // CModelInfo::ms_modelInfoPtrs[6500]
};
struct World {
	SphereQuery sphere = nullptr;
	NativePoint *point = nullptr;
	const NativeBindings *native = nullptr;
};
struct Stats {
	unsigned queries = 0;
	bool exhausted = false, unresolved = false;
};
constexpr unsigned kMaxQueries = 192;
// This overload is the native ABI seam, also usable by offline collision fixtures.
reactphysics::Contact Sweep(const World &world, Vec3 from, Vec3 to, float radius, void *ped, Stats *stats = nullptr);
// Bind the verified native world query/contact buffer, then pass Sweep to Body.
bool Install();
reactphysics::Contact Sweep(Vec3 from, Vec3 to, float radius, void *ped);
} // namespace reactcontact

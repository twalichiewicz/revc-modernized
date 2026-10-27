#include "aimslow.h"
#include "fpgait.h"
#include "fplook.h"
#include "game.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <dlfcn.h>

using namespace game;

namespace aimslow {
namespace {

// CPools::ms_pPedPool: entries at +0, free flags at +0x8, size at +0x10.
constexpr size_t kPedSize = 0x8d0;
constexpr size_t kInObjective = 0x1a8; // CPed::m_pedInObjective
constexpr size_t kPointGunAt = 0x650;  // CPed::m_pPointGunAt
constexpr size_t kWanted = 0x778;      // CPlayerPed::m_pWanted; its level at +0x20
constexpr int32_t kCop = 6;            // ePedType
constexpr float kRange = 60.0f;        // m
// A target as a capsule about his position (his hips): feet to head, his body's width.
constexpr float kFeet = 1.0f, kHead = 0.8f, kChest = 0.4f, kRadius = 0.35f;
constexpr float kSightWithin = 3.0f * 3.14159265f / 180.0f; // looked for only this near the crosshair

uint8_t **gPedPool;

template <typename Each> void ForEachPed(Each &&each)
{
	const uint8_t *pool = gPedPool ? *gPedPool : nullptr;
	if (!pool)
		return;
	uint8_t *entries = *reinterpret_cast<uint8_t *const *>(pool);
	const uint8_t *flags = *reinterpret_cast<const uint8_t *const *>(pool + 0x8);
	const int32_t size = *reinterpret_cast<const int32_t *>(pool + 0x10);
	for (int i = 0; i < size; i++)
		if (!(flags[i] & 0x80))
			each(entries + size_t(i) * kPedSize);
}

bool Wanted(const uint8_t *player)
{
	const uint8_t *wanted = *reinterpret_cast<const uint8_t *const *>(player + kWanted);
	return wanted && *reinterpret_cast<const int32_t *>(wanted + 0x20) > 0;
}

} // namespace

bool Install()
{
	gPedPool = static_cast<uint8_t **>(dlsym(RTLD_MAIN_ONLY, "_ZN6CPools11ms_pPedPoolE"));
	if (!gPedPool)
		Log("aimslow: the ped pool is missing; no aim slowdown");
	return gPedPool != nullptr;
}

float Multiplier(bool aiming, float push)
{
	uint8_t *player = api.FindPlayerPed();
	if (!player || !api.GetIsLineOfSightClear)
		return 1.0f;
	const CVector eye = *reinterpret_cast<const CVector *>(api.theCamera + 0x30);
	const CVector dir = *reinterpret_cast<const CVector *>(api.theCamera + 0x10);
	const bool wanted = Wanted(player);
	float best = 1.0f;
	ForEachPed([&](uint8_t *ped) {
		if (ped == player || Health(ped) <= 0.0f || PedInVehicle(ped))
			return;
		const bool atHim = *reinterpret_cast<uint8_t *const *>(ped + kInObjective) == player;
		const bool gunOnHim = *reinterpret_cast<uint8_t *const *>(ped + kPointGunAt) == player;
		if (!fpgait::Enemy(PedObjective(ped), atHim, gunOnHim, PedType(ped) == kCop, wanted))
			return;
		const CVector &at = EntityPos(ped);
		const float dist = std::sqrt((at.x - eye.x) * (at.x - eye.x) + (at.y - eye.y) * (at.y - eye.y) +
		                             (at.z - eye.z) * (at.z - eye.z));
		if (dist > kRange)
			return;
		const float off = fplook::OffAngle(eye, dir, {at.x, at.y, at.z - kFeet}, {at.x, at.y, at.z + kHead}, kRadius);
		if (off >= kSightWithin)
			return;
		const CVector chest{at.x, at.y, at.z + kChest};
		if (!api.GetIsLineOfSightClear(eye, chest, true, false, false, true, false, true, false)) // walls, not glass
			return;
		best = std::min(best, fplook::Slowdown(off, dist, aiming, push));
	});
	return best;
}

} // namespace aimslow

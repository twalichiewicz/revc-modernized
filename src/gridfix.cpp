#include "gridfix.h"
#include "game.h"
#include "gridclip.h"
#include "hooks.h"
#include "log.h"
#include "reacthit.h"
#include <algorithm>
#include <cmath>

using namespace game;

namespace gridfix {
namespace {

bool (*orig_GetIsLineOfSightClear)(const CVector &a, const CVector &b, bool buildings, bool vehicles, bool peds,
                                   bool objects, bool dummies, bool ignoreSeeThrough, bool ignoreSomeObjects);
bool (*orig_ProcessLineOfSight)(const CVector &a, const CVector &b, uint8_t *colPoint, uint8_t *&entity, bool buildings,
                                bool vehicles, bool peds, bool objects, bool dummies, bool ignoreSeeThrough,
                                bool ignoreSomeObjects, bool ignoreShootThrough);
uint32_t gCut; // lines cut, for the log
uint8_t *(*orig_Rect)(uint8_t *, float, float, float, float);
uintptr_t gPedRectReturn;
bool gRectCut;
constexpr uint32_t kPedRectReturn = 0x18db50;
constexpr const char *kRectSymbol = "_ZN5CRectC1Effff";
struct QueryBounds { float left, top, right, bottom; };
static_assert(sizeof(QueryBounds) == 16);
QueryBounds (*orig_PhysicalBounds)(uint8_t *);
uintptr_t gPhysicalBoundsReturn[2];
constexpr uint32_t kPhysicalBoundsReturn[2] = {0x154d68, 0x155264}; // Add / RemoveAndAdd
constexpr uint32_t kPhysicalBoundsCtorReturn = 0x1556ac;
bool gMembershipCut;

bool BoundQuery(QueryBounds &b)
{
	const QueryBounds old = b;
	const float x0 = gridclip::kMinX + gridclip::kInside, x1 = gridclip::kMaxX - gridclip::kInside;
	const float y0 = gridclip::kMinY + gridclip::kInside, y1 = gridclip::kMaxY - gridclip::kInside;
	if (std::isfinite(b.left) && std::isfinite(b.bottom) && std::isfinite(b.right) && std::isfinite(b.top)) {
		b.left = std::clamp(b.left, x0, x1);
		b.right = std::clamp(b.right, x0, x1);
		b.bottom = std::clamp(b.bottom, y0, y1);
		b.top = std::clamp(b.top, y0, y1);
	} else {
		// An inverted, finite sector interval makes the native query empty.
		b = {x1, y0, x0, y1};
	}
	return b.left != old.left || b.bottom != old.bottom || b.right != old.right || b.top != old.top;
}

// The native producers store the sector itself in m_entryInfoList. Guard the
// bounds before they write a CPtrList or entry, so collision, simple-car and
// shift consumers all retain valid native membership and native response.
__attribute__((noinline)) QueryBounds Hook_PhysicalBounds(uint8_t *entity)
{
	const auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
	QueryBounds bounds = orig_PhysicalBounds(entity); // native HFA return s0..s3
	reacthit::ExpandBounds(entity, bounds.left, bounds.top, bounds.right, bounds.bottom);
	if ((gPhysicalBoundsReturn[0] && caller == gPhysicalBoundsReturn[0]) ||
	    (gPhysicalBoundsReturn[1] && caller == gPhysicalBoundsReturn[1])) {
		if (BoundQuery(bounds) && !gMembershipCut) {
			gMembershipCut = true;
			Log("gridfix: off-grid physical sector membership bounded; actor and collisions remain native");
		}
	}
	return bounds;
}

// BuildPedLists' GetSector only handles indices ABOVE79. Its query can cross
// below the world's south/west boundary while the real ped remains airborne.
// Keep only that local query rectangle on the grid; never move the actor.
__attribute__((noinline)) uint8_t *Hook_Rect(uint8_t *rect, float left, float bottom, float right, float top)
{
	if (gPedRectReturn && reinterpret_cast<uintptr_t>(__builtin_return_address(0)) == gPedRectReturn) {
		QueryBounds bounds{left, top, right, bottom};
		if (BoundQuery(bounds) && !gRectCut) {
			gRectCut = true;
			Log("gridfix: off-grid BuildPedLists query bounded; actor position unchanged");
		}
		left = bounds.left; top = bounds.top; right = bounds.right; bottom = bounds.bottom;
	}
	// This native constructor leaves x0=this and stores left/top/right/bottom.
	return orig_Rect(rect, left, bottom, right, top);
}

bool Cut(const CVector &a, const CVector &b, CVector &from, CVector &to)
{
	Vec3 f, t;
	if (!gridclip::Clip({a.x, a.y, a.z}, {b.x, b.y, b.z}, f, t))
		return false;
	from = {f.x, f.y, f.z};
	to = {t.x, t.y, t.z};
	if ((from.x != a.x || from.y != a.y || to.x != b.x || to.y != b.y) && gCut++ == 0)
		Log("gridfix: a line of sight off the world's grid cut at its edge (the game would read past its sectors)");
	return true;
}

// Off the grid altogether: nothing in the way.
bool Hook_GetIsLineOfSightClear(const CVector &a, const CVector &b, bool buildings, bool vehicles, bool peds,
                                bool objects, bool dummies, bool ignoreSeeThrough, bool ignoreSomeObjects)
{
	CVector from, to;
	return !Cut(a, b, from, to) ||
	       orig_GetIsLineOfSightClear(from, to, buildings, vehicles, peds, objects, dummies, ignoreSeeThrough, ignoreSomeObjects);
}

// ... and nothing hit.
bool Hook_ProcessLineOfSight(const CVector &a, const CVector &b, uint8_t *colPoint, uint8_t *&entity, bool buildings,
                             bool vehicles, bool peds, bool objects, bool dummies, bool ignoreSeeThrough,
                             bool ignoreSomeObjects, bool ignoreShootThrough)
{
	CVector from, to;
	return Cut(a, b, from, to) && orig_ProcessLineOfSight(from, to, colPoint, entity, buildings, vehicles, peds, objects,
	                                                      dummies, ignoreSeeThrough, ignoreSomeObjects, ignoreShootThrough);
}

} // namespace

bool Install()
{
	const bool lines =
	    hooks::Install("_ZN6CWorld21GetIsLineOfSightClearERK7CVectorS2_bbbbbbb", Hook_GetIsLineOfSightClear,
	                   orig_GetIsLineOfSightClear) &&
	    hooks::Install("_ZN6CWorld18ProcessLineOfSightERK7CVectorS2_R9CColPointRP7CEntitybbbbbbbb",
	                   Hook_ProcessLineOfSight, orig_ProcessLineOfSight);
	if (!lines)
		Log("gridfix: hooks missing (re-run install.sh); a line of sight off the world's grid can crash the game");
	const auto base = reinterpret_cast<uintptr_t>(hooks::ImageBase());
	const bool ped = base && hooks::IsCallTo(kPedRectReturn, kRectSymbol) &&
	                 hooks::Install(kRectSymbol, Hook_Rect, orig_Rect);
	gPedRectReturn = ped ? base + kPedRectReturn : 0;
	if (!ped)
		Log("gridfix: verified BuildPedLists rectangle hook missing; off-grid ped queries remain unsafe");
	const bool physical = base && hooks::IsCallTo(kPhysicalBoundsCtorReturn, kRectSymbol) &&
	                      hooks::Install("_ZN9CPhysical12GetBoundRectEv", Hook_PhysicalBounds, orig_PhysicalBounds);
	for (int i = 0; i < 2; ++i)
		gPhysicalBoundsReturn[i] = physical ? base + kPhysicalBoundsReturn[i] : 0;
	if (!physical)
		Log("gridfix: verified physical bounds hook missing; off-grid sector membership remains unsafe");
	return lines && ped && physical;
}

} // namespace gridfix

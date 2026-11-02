#include "shotimpact.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include <cmath>
#include <cstring>
#include <dlfcn.h>

namespace shotimpact {
namespace {
using Native = void (*)(uint8_t *, uint8_t *, uint8_t *, CVector *, CVector *, uint8_t *, CVector2D);
Native gNext;
int (*gRef)(uint8_t *);
bool gReady;
struct Context;
thread_local const Context *gContext;
bool Finite(Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
struct Context {
	const Context *parent = gContext;
	uint8_t *victim = nullptr, *clump = nullptr;
	int ref = -1, weapon = -1;
	Event event;
	bool valid = false;
	Context(uint8_t *w, uint8_t *ped, const CVector *from, const CVector *to, const uint8_t *point)
	{
		gContext = this; // An invalid nested event also hides an outer event.
		if (!gReady || !w || !ped || !from || !to || !point || !gRef || !game::api.PoolsGetPed ||
		    (game::EntityFlags(ped) & 7) != 3)
			return;
		ref = gRef(ped);
		if (ref < 0 || game::api.PoolsGetPed(ref) != ped || !(clump = game::EntityClump(ped)))
			return;
		std::memcpy(&event.position, point, sizeof event.position);
		const Vec3 a{from->x, from->y, from->z}, b{to->x, to->y, to->z};
		if (!Finite(a) || !Finite(b) || !Finite(event.position))
			return;
		const Vec3 d{b.x-a.x,b.y-a.y,b.z-a.z};
		const float length = std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
		const CVector root = game::EntityPos(ped);
		const Vec3 local{event.position.x-root.x,event.position.y-root.y,event.position.z-root.z};
		if (!std::isfinite(length) || length < 1e-5f || !Finite(local) ||
		    local.x*local.x+local.y*local.y+local.z*local.z > 16)
			return;
		event.direction = {d.x/length,d.y/length,d.z/length};
		std::memcpy(&weapon, w, sizeof weapon);
		victim = ped;
		valid = true;
	}
	~Context() { gContext = parent; }
};
void Hook_Impact(uint8_t *weapon, uint8_t *shooter, uint8_t *victim, CVector *source, CVector *target,
                 uint8_t *point, CVector2D ahead)
{
	const Context context(weapon,victim,source,target,point);
	gNext(weapon, shooter, victim, source, target, point, ahead);
}
} // namespace
bool Install()
{
	if (!gConfig.npcReactions || gReady)
		return true;
	gRef = reinterpret_cast<decltype(gRef)>(dlsym(RTLD_MAIN_ONLY, "_ZN6CPools9GetPedRefEP4CPed"));
	if (!gRef || !game::api.PoolsGetPed)
		return false;
	gNext = reinterpret_cast<Native>(hooks::Chain("_ZN7CWeapon14DoBulletImpactEP7CEntityS1_P7CVectorS3_P9CColPoint9CVector2D", reinterpret_cast<void *>(Hook_Impact)));
	gReady = gNext != nullptr;
	return gReady;
}
bool Current(uint8_t *victim, int weapon, Event &event)
{
	const Context *c = gContext;
	if (!c || !c->valid || c->victim != victim || c->weapon != weapon || !game::api.PoolsGetPed ||
	    game::api.PoolsGetPed(c->ref) != victim || gRef(victim) != c->ref || game::EntityClump(victim) != c->clump)
		return false;
	event = c->event;
	return true;
}
} // namespace shotimpact

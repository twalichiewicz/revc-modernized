#include "falls.h"
#include "config.h"
#include "falldamage.h"
#include "game.h"
#include "log.h"
#include "player.h"

#include <algorithm>
#include <cmath>
#include <dlfcn.h>

using namespace game;

namespace falls {
namespace {

// --- The game (macVC 1.0.5217) -------------------------------------------------------------

constexpr int32_t kWeaponFall = 44;     // eWeaponType WEAPONTYPE_FALL
constexpr size_t kMoveSpeed = 0x84;     // CPhysical::m_vecMoveSpeed, units a frame (50 a second)
constexpr size_t kSurface = 0x158;      // CPhysical::m_nSurfaceTouched (set as he lands, before the damage)
constexpr size_t kArmour = 0x450;       // CPed::m_fArmour
constexpr float kFrame = 50.0f;
constexpr int kLogAtMost = 12;          // landings of others logged

bool (*gIsSoftLanding)(uint8_t surface);
bool gInstalled;
int gLogged;

} // namespace

float Land(uint8_t *ped, int32_t weapon, float &damage)
{
	if (!gInstalled || weapon != kWeaponFall)
		return 0.0f;
	// (CPed::ProcessEntityCollision deals it as he lands: his speed is still the fall's.)
	const float *speed = reinterpret_cast<const float *>(ped + kMoveSpeed);
	const float down = std::max(-speed[2], 0.0f) * kFrame, along = std::hypot(speed[0], speed[1]) * kFrame;
	const bool soft = gIsSoftLanding && gIsSoftLanding(ped[kSurface]);
	const float games = damage;
	damage = std::max(damage, falldamage::Damage(down, along, soft)); // (the game's own small knocks stay)
	const bool his = ped == api.FindPlayerPed();
	if (damage >= 1.0f && (his || gLogged < kLogAtMost)) {
		gLogged += !his;
		Log("falls: %s landed at %.0f m/s (a %.0f m drop%s): %.0f, the game's %.0f", his ? "Tommy" : "a ped", down,
		    falldamage::Drop(down), soft ? ", soft ground" : "", damage, games);
	}
	float &armour = *reinterpret_cast<float *>(ped + kArmour);
	const float kept = armour;
	armour = 0.0f;
	return kept;
}

void Landed(uint8_t *ped, float armour)
{
	if (armour > 0.0f)
		*reinterpret_cast<float *>(ped + kArmour) = armour;
}

bool Install()
{
	if (!gConfig.fallDamage)
		return true;
	if (!player::DamageHooked())
		return false;
	gIsSoftLanding = reinterpret_cast<decltype(gIsSoftLanding)>(dlsym(RTLD_MAIN_ONLY, "_ZN13CSurfaceTable13IsSoftLandingEh"));
	if (!gIsSoftLanding)
		Log("falls: CSurfaceTable::IsSoftLanding is missing; every landing is hard ground");
	gInstalled = true;
	return true;
}

} // namespace falls

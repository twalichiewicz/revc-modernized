#include "player.h"
#include "camera.h"
#include "steering.h"
#include "aimcam.h"
#include "body.h"
#include "climb.h"
#include "combat.h"
#include "crouch.h"
#include "shootdodge.h"
#include "driveby.h"
#include "fpshoot.h"
#include "fpwalk.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "rumble.h"
#include "swim.h"

using namespace game;

namespace player {
namespace {

void (*orig_ProcessControl)(uint8_t *ped);
void (*orig_ProcessPlayerWeapon)(uint8_t *ped, uint8_t *pad);
void (*orig_PreRender)(uint8_t *ped);
bool (*orig_InflictDamage)(uint8_t *ped, uint8_t *by, int32_t weapon, float damage, int32_t piece, uint8_t direction);
bool gControl, gPose, gDamage;

void Hook_ProcessControl(uint8_t *ped)
{
	climb::BeforeControl(ped);
	const bool strafed = aimcam::BeforeControl();
	const bool fpStrafed = fpwalk::BeforeControl();
	orig_ProcessControl(ped);
	aimcam::AfterControl(ped, strafed);
	swim::AfterControl(ped);
	climb::AfterControl(ped);
	crouch::AfterControl(ped); // crouched, his walk is its
	fpwalk::AfterControl(ped, fpStrafed); // first person: the stick's, CS-style
	shootdodge::AfterControl(ped); // last: diving, his speed is its
}

void Hook_ProcessPlayerWeapon(uint8_t *ped, uint8_t *pad)
{
	if (swim::Swimming() || climb::Active()) {
		aimcam::Lower(ped);
		return;
	}
	const bool freeCam = fpwalk::BeforeWeapon();
	orig_ProcessPlayerWeapon(ped, pad);
	fpwalk::AfterWeapon(freeCam);
	fpshoot::AfterWeapon(ped, pad);
	aimcam::AfterPlayerWeapon(ped, pad);
}

void Hook_PreRender(uint8_t *ped)
{
	if (ped == api.FindPlayerPed()) {
		driveby::PilotHolster(ped);
		driveby::PreRender(ped);
		if ((swim::Posing() || climb::Posing()) && body::Prepare(ped)) {
			body::Pose pose;
			float weight = swim::Pose(pose);
			weight = climb::Pose(pose, weight);
			body::Apply(ped, pose, weight);
		} else if (shootdodge::Posing() && body::Prepare(ped)) {
			body::Pose pose; // laid out in the air; his arms as the game aims them
			const float weight = shootdodge::Pose(ped, pose);
			body::ApplyOver(ped, pose, weight);
		} else if (crouch::Posing() && body::Prepare(ped)) {
			body::Pose pose; // his legs only: the rest is the game's crouch
			const float weight = crouch::Pose(ped, pose);
			body::ApplyOver(ped, pose, weight);
		} else {
			aimcam::PoseArms(ped);
		}
	}
	orig_PreRender(ped);
	if (ped == api.FindPlayerPed())
		steering::PoseDriver(ped); // his hands on the wheel
	camera::AfterPreRender(ped);
}

bool Hook_InflictDamage(uint8_t *ped, uint8_t *by, int32_t weapon, float damage, int32_t piece, uint8_t direction)
{
	if (swim::RefusesDamage(ped, weapon))
		return false;
	const bool hurt = orig_InflictDamage(ped, by, weapon, damage, piece, direction);
	if (hurt) {
		rumble::Damaged(ped, by, weapon, damage);
		combat::Hurt(ped, by, Health(ped) <= 0.0f);
	}
	return hurt;
}

} // namespace

bool Install()
{
	gControl = hooks::Install("_ZN10CPlayerPed14ProcessControlEv", Hook_ProcessControl, orig_ProcessControl) &&
	           hooks::Install("_ZN10CPlayerPed19ProcessPlayerWeaponEP4CPad", Hook_ProcessPlayerWeapon,
	                          orig_ProcessPlayerWeapon);
	gPose = hooks::Install("_ZN4CPed9PreRenderEv", Hook_PreRender, orig_PreRender);
	gDamage = hooks::Install("_ZN4CPed13InflictDamageEP7CEntity11eWeaponTypef14ePedPieceTypesh", Hook_InflictDamage,
	                         orig_InflictDamage);
	return gControl && gPose && gDamage;
}

void Damage(uint8_t *ped, int32_t weapon, float amount)
{
	if (gDamage)
		orig_InflictDamage(ped, nullptr, weapon, amount, 0, 0);
}

bool ControlHooked() { return gControl; }
bool PoseHooked() { return gPose; }
bool DamageHooked() { return gDamage; }

} // namespace player

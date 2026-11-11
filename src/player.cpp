#include "achievements.h"
#include "player.h"
#include "playerreact.h"
#include "camera.h"
#include "steering.h"
#include "aimcam.h"
#include "body.h"
#include "climb.h"
#include "combat.h"
#include "crouch.h"
#include "shootdodge.h"
#include "driveby.h"
#include "drivers.h"
#include "falls.h"
#include "fpshoot.h"
#include "fpwalk.h"
#include "fpfeedback.h"
#include "game.h"
#include "hooks.h"
#include "jetboardingpose.h"
#include "log.h"
#include "rumble.h"
#include "footplant.h"
#include "gaze.h"
#include "reactions.h"
#include "seatgun.h"
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
	playerreact::BeforeControl(ped);
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
	playerreact::AfterControl(ped);
	gaze::Frame(ped); // after aiming has set its look: ambient looks never override it
	drivers::Frame();
	reactions::Frame();
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
	camera::BeforePreRender(ped);
	playerreact::BeforePreRender(ped);
	reactions::BeforePreRender(ped);
	if (ped == api.FindPlayerPed() && fpfeedback::CanPose(Health(ped), PedState(ped), true)) {
		driveby::PilotHolster(ped);
		seatgun::BeforePose(ped); // driving, the gun on the console till he aims
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
			// His feet on the ground under them: hips and legs over the animation.
			if (body::Prepare(ped)) {
				body::Pose pose;
				const float weight = footplant::Pose(ped, pose);
				if (weight > 0)
					body::ApplyOver(ped, pose, weight);
			}
		}
	}
	jetboardingpose::BeforePedRender(ped);
	orig_PreRender(ped);
	jetboardingpose::AfterPedRender(ped);
	if (ped == api.FindPlayerPed() && fpfeedback::CanPose(Health(ped), PedState(ped), true)) {
		steering::PoseDriver(ped); // his hands on the wheel
		seatgun::Pose(ped);        // ... his right hand going for the gun
	}
	playerreact::AfterPreRender(ped);
	// The ragdoll's pose before the camera's (#25): knocked down in first person,
	// his articulated body is drawn and only then is his head hidden from the eye.
	reactions::AfterPreRender(ped);
	camera::AfterPreRender(ped);
}

bool Hook_InflictDamage(uint8_t *ped, uint8_t *by, int32_t weapon, float damage, int32_t piece, uint8_t direction)
{
	if (swim::RefusesDamage(ped, weapon))
		return false;
	const float armour = falls::Land(ped, weapon, damage); // (a landing: as it would hurt, his armour aside)
	const float healthBefore = Health(ped), armourBefore = Armour(ped);
	const bool nativeResult = orig_InflictDamage(ped, by, weapon, damage, piece, direction);
	const float healthAfter = Health(ped);
	// Observe before Landed restores armour temporarily removed for a fall.
	// InflictDamage returns false after ordinary nonfatal health loss; its
	// bool must be preserved for the engine, not used as hit acceptance.
	const auto facts = fpfeedback::ObserveDamage(nativeResult, healthBefore, healthAfter, armourBefore, Armour(ped));
	falls::Landed(ped, armour);
	if (facts.accepted)
		camera::Damaged(ped, by, healthBefore, healthBefore - facts.healthLost - facts.armourLost * 0.5f);
	if (facts.accepted) {
		reactions::Hit(ped, by, weapon, piece);
		achievements::Damaged(ped, by, weapon, healthAfter <= 0.0f); // the mission kills (#24)
		rumble::Damaged(ped, by, weapon, damage);
		combat::Hurt(ped, by, healthAfter <= 0.0f);
	}
	return facts.nativeResult;
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
	if (!gDamage || !ped)
		return;
	const float healthBefore = Health(ped), armourBefore = Armour(ped);
	const bool nativeResult = orig_InflictDamage(ped, nullptr, weapon, amount, 0, 0);
	const float healthAfter = Health(ped);
	const auto facts = fpfeedback::ObserveDamage(nativeResult, healthBefore, healthAfter, armourBefore, Armour(ped));
	if (facts.accepted)
		camera::Damaged(ped, nullptr, healthBefore, healthBefore - facts.healthLost - facts.armourLost * 0.5f);
}

bool ControlHooked() { return gControl; }
bool PoseHooked() { return gPose; }
bool DamageHooked() { return gDamage; }

} // namespace player

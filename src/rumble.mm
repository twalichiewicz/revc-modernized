#include "rumble.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "player.h"

#import <CoreHaptics/CoreHaptics.h>
#import <GameController/GameController.h>

#include <algorithm>
#include <cmath>
#include <dlfcn.h>

using namespace game;

namespace rumble {
namespace {

constexpr float kShakeRange = 70.0f; // StartShake_Distance ignores anything further from the camera
constexpr float kFullStrength = 160; // StartShake's "frequency" (0-255) that we play at full intensity
constexpr CFTimeInterval kOverlap = 0.05; // seconds left on a buzz before a repeat replaces it
// The suspension's jolt (CAutomobile / CBike::ProcessControl, by return address):
// the game asks for up to 250 over a kerb, which shakes like a crash.
constexpr uint32_t kSuspensionCalls[] = {0x24d808, 0x261018};
constexpr float kSuspensionScale = 0.4f;
constexpr int kSuspensionMaxMs = 140;
// A punch or a swing that lands: strength by damage (fists do a few points, a bat more).
constexpr int kHitMs = 110;
constexpr float kHitBase = 110, kHitPerDamage = 4, kHitMax = 230;
// Driving: the car's own acceleration (m/s^2) while on the gas or the brake.
constexpr float kPullFrom = 1.5f, kPullRange = 7.0f;     // flooring it
constexpr float kBrakeFrom = 4.0f, kBrakeRange = 10.0f;  // braking hard from speed
constexpr float kCarPull = 0.55f, kBikePull = 0.7f, kBrake = 0.6f, kBurnout = 0.45f, kBikeIdle = 0.12f;
constexpr float kRise = 0.05f, kFall = 0.2f, kAccelSmoothing = 0.12f; // seconds
constexpr double kPulseEvery = 0.09, kPulseLength = 0.12;            // seconds

void (*orig_StartShake)(uint8_t *pad, int16_t ms, uint8_t strength);
void (*orig_StartShake_Distance)(uint8_t *pad, int16_t ms, uint8_t strength, float x, float y, float z);
const uint8_t *gCutscene, *gIntro; // CCutsceneMgr::ms_running, CGame::playingIntro
bool gInstalled;

GCController *gController;
CHHapticEngine *gEngine;
id<CHHapticPatternPlayer> gPlayer;
id<CHHapticPatternPlayer> gDrivePulse; // the driving buzz: short overlapping pulses at its level
CFTimeInterval gNextPulse;
float gDriveLevel, gAccel, gLastSpeed = NAN;
CFTimeInterval gLastDrive;
CFTimeInterval gEngineRetry;
CFTimeInterval gPlayingUntil; // what's playing, so a weaker, shorter request doesn't cut it off
float gPlayingStrength;
bool gLogged;

// The game's pad, or failing that whichever has haptics.
GCController *Controller()
{
	GCController *best = nil;
	for (GCController *c in GCController.controllers)
		if (c.haptics && (!best || c == GCController.current))
			best = c;
	return best;
}

CHHapticEngine *Engine()
{
	GCController *c = Controller();
	if (c != gController) {
		@try {
			[gEngine stopWithCompletionHandler:nil];
		} @catch (NSException *) {
		}
		gEngine = nil;
		gPlayer = nil;
		gDrivePulse = nil;
		gController = c;
	}
	if (!gController || gEngine || CFAbsoluteTimeGetCurrent() < gEngineRetry)
		return gEngine;
	gEngineRetry = CFAbsoluteTimeGetCurrent() + 5.0; // not every frame if it won't start
	@try {
		gEngine = [gController.haptics createEngineWithLocality:GCHapticsLocalityDefault];
		if (!gEngine)
			return nil;
		gEngine.playsHapticsOnly = YES;
		CHHapticEngine *engine = gEngine;
		gEngine.resetHandler = ^{ [engine startAndReturnError:nil]; };
		if (![gEngine startAndReturnError:nil]) {
			Log("rumble: couldn't start haptics on %s", gController.vendorName.UTF8String ?: "the controller");
			gEngine = nil;
			return nil;
		}
	} @catch (NSException *e) {
		Log("rumble: haptics refused to start (%s)", e.reason.UTF8String ?: "?");
		gEngine = nil;
		return nil;
	}
	if (!gLogged) {
		gLogged = true;
		Log("rumble: on, through %s", gController.vendorName.UTF8String ?: "the controller");
	}
	return gEngine;
}

// A continuous buzz starting now, or nil. CoreHaptics throws Objective-C
// exceptions for what it won't do (the controller's engine has no advanced
// players, for one): caught here, so the game never goes down with it.
id<CHHapticPatternPlayer> Buzz(id<CHHapticPatternPlayer> replacing, float intensity, float sharpness, double seconds)
{
	CHHapticEngine *engine = Engine();
	if (!engine)
		return nil;
	@try {
		CHHapticEvent *event = [[CHHapticEvent alloc]
		    initWithEventType:CHHapticEventTypeHapticContinuous
		           parameters:@[
			           [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity
			                                                         value:intensity],
			           [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness
			                                                         value:sharpness],
		           ]
		         relativeTime:0
		             duration:seconds];
		CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithEvents:@[event] parameters:@[] error:nil];
		id<CHHapticPatternPlayer> player = pattern ? [engine createPlayerWithPattern:pattern error:nil] : nil;
		if (!player)
			return nil;
		[replacing stopAtTime:CHHapticTimeImmediate error:nil];
		return [player startAtTime:CHHapticTimeImmediate error:nil] ? player : nil;
	} @catch (NSException *e) {
		Log("rumble: haptics refused a buzz (%s)", e.reason.UTF8String ?: "?");
		return nil;
	}
}

// A buzz of `ms` milliseconds at `strength` (0-255, as the game asks for it).
void Play(int ms, float strength)
{
	if (ms <= 0 || strength <= 0)
		return;
	const CFTimeInterval now = CFAbsoluteTimeGetCurrent(), until = now + ms / 1000.0;
	// Something at least as strong is still going (the game repeats some every
	// frame, a bumpy road for one): not a new player each time.
	if (strength <= gPlayingStrength && now < gPlayingUntil - kOverlap)
		return;
	if (id<CHHapticPatternPlayer> player = Buzz(gPlayer, std::clamp(strength / kFullStrength, 0.15f, 1.0f), 0.3f,
	                                            ms / 1000.0)) {
		gPlayer = player;
		gPlayingUntil = until;
		gPlayingStrength = strength;
	}
}

bool Muted(const uint8_t *pad) { return pad != api.GetPad(0) || *gCutscene || *gIntro; }

// The driving buzz at `level` (0-1): short pulses that overlap a little, each
// at the level of the moment (the controller's haptics can't change a playing
// buzz's strength).
void DriveLevel(float level)
{
	const CFTimeInterval now = CFAbsoluteTimeGetCurrent();
	if (level < 0.02f) {
		if (gDrivePulse)
			@try {
				[gDrivePulse stopAtTime:CHHapticTimeImmediate error:nil];
			} @catch (NSException *) {
			}
		gDrivePulse = nil;
		gNextPulse = 0;
		return;
	}
	if (now < gNextPulse)
		return;
	gDrivePulse = Buzz(gDrivePulse, level, 0.2f, kPulseLength);
	gNextPulse = now + kPulseEvery;
}

void Hook_StartShake(uint8_t *pad, int16_t ms, uint8_t strength)
{
	if (Muted(pad))
		return;
	const uint32_t from = uint32_t(reinterpret_cast<uintptr_t>(__builtin_return_address(0)) -
	                               reinterpret_cast<uintptr_t>(hooks::ImageBase()));
	for (uint32_t call : kSuspensionCalls)
		if (from == call)
			return Play(std::min<int>(ms, kSuspensionMaxMs), strength * kSuspensionScale);
	Play(ms, strength);
}

// Tommy's fist, foot or melee weapon landing on someone.
// Explosions and the like: fading with distance from the camera.
void Hook_StartShake_Distance(uint8_t *pad, int16_t ms, uint8_t strength, float x, float y, float z)
{
	if (Muted(pad))
		return;
	const CVector &cam = *reinterpret_cast<const CVector *>(api.theCamera + 0x30); // its matrix's position
	const float d = std::sqrt((cam.x - x) * (cam.x - x) + (cam.y - y) * (cam.y - y) + (cam.z - z) * (cam.z - z));
	if (d < kShakeRange)
		Play(ms, strength * (1.0f - 0.7f * d / kShakeRange));
}

} // namespace

void Damaged(const uint8_t *ped, const uint8_t *by, int32_t weapon, float damage)
{
	const uint8_t *player = api.FindPlayerPed();
	if (gInstalled && player && by == player && ped != player && weapon < kFirstThrownWeapon && !Muted(api.GetPad(0)))
		Play(kHitMs, std::min(kHitBase + damage * kHitPerDamage, kHitMax));
}

void Drive(const uint8_t *vehicle, float gas, float brake)
{
	if (!gInstalled)
		return;
	const CFTimeInterval now = CFAbsoluteTimeGetCurrent();
	const float dt = float(std::min(now - gLastDrive, 0.1));
	gLastDrive = now;
	if (!vehicle || *gCutscene || *gIntro || dt <= 0.0f) {
		gLastSpeed = NAN;
		gDriveLevel = 0;
		DriveLevel(0);
		return;
	}
	// Speed along the car (CPhysical::m_vecMoveSpeed is per 1/50 s), and how fast it changes.
	const CVector &v = *reinterpret_cast<const CVector *>(vehicle + 0x84), &f = EntityForward(vehicle);
	const float speed = (v.x * f.x + v.y * f.y + v.z * f.z) * 50.0f;
	if (!std::isnan(gLastSpeed))
		gAccel += ((speed - gLastSpeed) / dt - gAccel) * std::min(1.0f, dt / kAccelSmoothing);
	gLastSpeed = speed;
	const bool bike = *reinterpret_cast<const int32_t *>(vehicle + 0x348) == 5;
	float level = 0;
	if (gas > 0.3f && gAccel > kPullFrom)
		level = std::min((gAccel - kPullFrom) / kPullRange, 1.0f) * gas * (bike ? kBikePull : kCarPull);
	if (brake > 0.3f && speed > 3.0f && gAccel < -kBrakeFrom)
		level = std::max(level, std::min((-gAccel - kBrakeFrom) / kBrakeRange, 1.0f) * brake * kBrake);
	if (gas > 0.6f && brake > 0.6f && std::fabs(speed) < 2.0f)
		level = std::max(level, kBurnout);
	if (bike && gas > 0.5f)
		level = std::max(level, kBikeIdle * gas);
	gDriveLevel += (level - gDriveLevel) * std::min(1.0f, dt / (level > gDriveLevel ? kRise : kFall));
	DriveLevel(gDriveLevel);
}

bool Install()
{
	if (!gConfig.rumble)
		return true;
	gCutscene = static_cast<const uint8_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN12CCutsceneMgr10ms_runningE"));
	gIntro = static_cast<const uint8_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN5CGame12playingIntroE"));
	if (!gCutscene || !gIntro)
		return false;
	// Replacing them outright: the PC game never counts the shake down, so after
	// the first one CPad::StartShake would ignore anything shorter.
	if (!hooks::Install("_ZN4CPad10StartShakeEsh", Hook_StartShake, orig_StartShake) ||
	    !hooks::Install("_ZN4CPad19StartShake_DistanceEshfff", Hook_StartShake_Distance, orig_StartShake_Distance)) {
		Log("rumble: hooks missing (re-run install.sh)");
		return true;
	}
	gInstalled = true;
	if (!player::DamageHooked())
		Log("rumble: punches don't rumble (re-run install.sh)");
	return true;
}

} // namespace rumble

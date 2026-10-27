#pragma once
// Where the mod puts things on the pad. controls.cpp reads these to remap the
// pad and bindings.cpp to name them, so moving a button here moves it in
// both: the hints and the Controls page can't fall behind the controls.
#include "bindings.h"

namespace layout {

using bindings::Pad;

// On foot
constexpr Pad kAim = Pad::kLT;          // the game's lock-on (its RB)
constexpr Pad kFire = Pad::kRT;         // the game's fire (its B)
constexpr Pad kWeaponWheel = Pad::kLB;  // held; a tap is the game's own LB (its action button)
constexpr Pad kPrevWeapon = Pad::kLeft; // the game's previous weapon, or target while aiming (its LT)
constexpr Pad kNextWeapon = Pad::kRight;
constexpr Pad kMeleeReload = Pad::kB;   // a punch or swing, a reload with a gun
constexpr Pad kGreet = Pad::kX;         // while a prompt is up
constexpr Pad kInsult = Pad::kB;
constexpr Pad kBulletTime = Pad::kR3;   // clicked while aiming

// Any situation
constexpr Pad kAnswerCall = Pad::kUp;   // while the phone rings
constexpr Pad kExpandedMap = Pad::kDown;

// Vehicles
constexpr Pad kSideMission = Pad::kUp;      // the game's R3
constexpr Pad kRadio = Pad::kLeftRight;     // a tap retunes, a hold opens the radio wheel
constexpr Pad kGas = Pad::kRT;              // the game's A
constexpr Pad kBrake = Pad::kLT;            // the game's X
constexpr Pad kLookBehind = Pad::kR3;       // the game's two triggers
constexpr Pad kCinematic = Pad::kB;         // held
constexpr Pad kDriveByAim = Pad::kLB;       // held
constexpr Pad kDriveByFire = Pad::kRB;      // while aiming a drive-by
constexpr Pad kVehicleGun = Pad::kRB;       // an armed vehicle's gun (the game's B)
constexpr Pad kArmedHandbrake = Pad::kA;    // an armed vehicle's handbrake (its RB fires)
constexpr Pad kCarGun = Pad::kA;            // any other's own gun (the game's B): a car bomb's arming, a bike's guns

// Aircraft
constexpr Pad kClimb = Pad::kRT;         // a plane's throttle
constexpr Pad kDescend = Pad::kLT;
constexpr Pad kYawLeft = Pad::kLB;       // the game's look left / right (its triggers)
constexpr Pad kYawRight = Pad::kRB;
constexpr Pad kAircraftGun = Pad::kA;    // the gun, the bombs (the game's B or RB)
constexpr Pad kHunterRockets = Pad::kX;  // the Hunter's rockets (the game's B)

} // namespace layout

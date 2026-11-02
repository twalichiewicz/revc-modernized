#pragma once
// The viewmodel's animations, done in code as the old viewmodel's kicks and
// dips were: the gun drawn (rising in from below), holstered (dropping
// away), kicking as it fires (vmkick: each class of gun its own) with its
// slide or bolt cycling, a pump racked (the whole gun and his left hand with
// it), a revolver's cylinder turning a chamber on, a minigun's barrels
// spinning, and a flash at the muzzle, turned and sized afresh each shot; its slide held back when it's
// empty, reloaded (lifted in and tilted, his left hand pulling the magazine
// out and away and bringing a new one up and in, the slide let go), and
// carried as a runner carries it while he sprints, swinging with his
// stride. A gun's moving parts are known by name (PartMove). No idle sway or
// lag: standing, the gun stays crisp; it bobs with his steps (GunBob), not
// aimed. A melee weapon is held up ready and swung in time with the game's
// attack (vmmelee); switching away from one, it's gone at once (the game
// takes its model from his hand). Times are game time (bullet time slows them).
// Unit-tested (tests/test_vmanim.cpp).
#include "vmgun.h"
#include "vmkick.h"
#include "vmmelee.h"

namespace vm {

// How he carries a gun running (as Vice City holds it): a pistol or SMG in
// one hand, up by his ear; a long gun in two, across his chest; a heavy one
// at the hip; a melee weapon lowered a little, swinging with his arm.
enum class Carry { kOneHanded, kTwoHanded, kHeavy, kMelee };
Carry CarryFor(int weapon);

// A move of the gun about his grip: m in the view's frame (x right, y ahead,
// z up); degrees up, over to the right, and its muzzle to the left.
struct Move {
	Vec3 offset;
	float pitch, roll, yaw;
};

struct AnimInput {
	int weapon;           // his weapon now (eWeaponType)
	bool covered;         // it has a viewmodel of its own
	int clip;             // rounds left in it
	bool reloading;
	float reloadProgress; // 0..1 through the game's reload
	bool sprinting;
	bool shot;            // it fired this frame
	bool triggerHeld;
	float interval;       // s between its shots (its attack's loop; 0: not known)
	float dt;             // s
	float stepPhase;      // 0..2 through his stride, a step each 1
	float stepSpeed;      // m/s on the ground
	float aimed;          // 0..1 (LT)
	Attack attack;        // a melee weapon's swing, as the game has it this frame
};

struct AnimOutput {
	bool visible;         // the new viewmodel is drawn (else his real arms show)
	bool ready;           // up, and the gun he has: it may fire
	int weapon;           // the gun drawn (the old one while it's holstered)
	Vec3 offset;          // m, the view's frame: x right, y ahead, z up
	float pitch, roll;    // degrees: up, and over to the right
	float yaw;            // degrees, its muzzle to the left
	Move carry;           // carried sprinting: his arms follow it (the moves above, they don't)
	KickMove kick;        // firing: the gun and his forearm turned by it about his forearm (aimed, less)
	float slideBack;      // m, along each part's own axis
	float boltBack;
	float pumpBack;
	float triggerBack;
	float magazineDrop;
	bool magazineShown;
	float cylinderTurn;   // degrees about its axis
	float barrelsTurn;
	float flash;          // 0..1
	float flashTurn;      // rad, this shot's
	float flashSize;      // of the usual (a shotgun's bigger, a flamethrower's none)
	float light;          // 0..1: the shot's light on his hands and the gun (a flamethrower's while it burns)
	Grip grip;            // a melee weapon: his hand in the view, ready or swinging (the moves above about its wrist)
};

// The gun's figure-eight with his steps (`phase` 0..2 through his stride, a
// step each 1; `speed` m/s): side to side once a stride, dipping at each
// side; none standing, barely walking, most sprinting.
struct GunSway {
	Vec3 offset;            // m, the view's frame
	float pitch, roll, yaw; // degrees
};
GunSway GunBob(float phase, float speed);

// His left hand through a reload of a gun with a magazine: how far it's gone
// from its hold to the magazine (0..1; it moves with the magazine there), how
// far it's gone down out of view (0..1), and whether it's drawn. A two-handed
// gun's hand leaves the handguard for the magazine, takes it out and away and
// brings the new one in, and goes back; a one-handed gun's comes up with the
// new magazine, puts it in, and goes.
struct ReloadHand {
	float atMagazine, away;
	bool shown;
};
ReloadHand HandForReload(bool reloading, float progress, bool twoHanded);

// Where a part of the gun is this frame (in the gun's frame, `scale` its
// model's): slid along its axis, or turned about it through its pivot, as its
// name says; a part with any other name stays put.
Frame PartMove(const Part &part, const AnimOutput &out, float scale);

class Anim {
public:
	AnimOutput Update(const AnimInput &in);

private:
	int shown_ = -1;     // the gun on screen
	float draw_ = 1.0f;  // 0 (below) .. 1 (up), drawing
	float holster_ = -1.0f; // s into holstering (< 0: not)
	Recoil recoil_;
	uint32_t seed_ = 0x9e3779b9u; // each shot's randomness (its own, so a test sees the same)
	float flashTurn_ = 0.0f, flashJitter_ = 1.0f;
	float sinceShot_ = 1e9f;
	float sprint_ = 0.0f;
	bool empty_ = false, emptyAtReload_ = false, wasReloading_ = false;
	float trigger_ = 0.0f;
	float cylinder_ = 0.0f, cylinderTo_ = 0.0f;
	float barrels_ = 0.0f, spin_ = 0.0f;
	MeleeHand melee_;
};

} // namespace vm

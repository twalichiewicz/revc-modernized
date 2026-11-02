#pragma once
// Melee weapons in first person: the game's own model of each, in his right
// hand as the game holds it (a weapon's model is in his hand bone's frame),
// held up ready where you can see it, and swung in time with the game's own
// attack (its animation's clock and its hit frame). The swing is a few held
// grips the hand passes through: drawn back, through the hit, followed
// through, and back to ready; quick through the hit, slower out of it. A
// knife's combo comes back the other way, then over the top; a club's second
// swing is brought down on someone on the ground. Times are game time.
// Unit-tested (tests/test_vmmelee.cpp).
#include "vmmath.h"

namespace vm {

// eWeaponType 1-11: brass knuckles, screwdriver, golf club, nightstick, knife,
// baseball bat, hammer, cleaver, machete, katana, chainsaw.
bool IsMelee(int weapon);

enum class MeleeKind {
	kKnuckles, // a fist (brass knuckles): a guard, and punches
	kKnife,    // knife, screwdriver: held point forward, stabbing
	kBlade,    // cleaver, machete, katana: slashing
	kClub,     // nightstick, hammer: one-handed, clubbing
	kLong,     // bat, golf club: long, swung harder and wider
	kChainsaw, // low at his hip in his right hand, its bar running in
};
MeleeKind MeleeKindFor(int weapon);

// His right hand, in the view's frame (x right, y ahead, z up, the eye at the
// origin): the wrist, the way the weapon runs out of his fist (his hand's z,
// its thumb side), and the way his knuckles point (its x; made square to the
// first).
struct Grip {
	Vec3 wrist, along, knuckles;
};
Frame GripFrame(const Grip &grip);
Grip Mix(const Grip &a, const Grip &b, float t);

// Held up ready.
Grip ReadyGrip(MeleeKind kind);

// His right arm reaching the wrist from his shoulder (below and right of the
// eye), the elbow out and down: the ways its forearm runs back from the wrist
// and its upper arm from the elbow (Pose's ArmAngles), for arm bones this long.
void ArmTo(Vec3 wrist, float forearm, float upperArm, Vec3 &foreWay, Vec3 &upperWay);

// The game's attack this frame, from his animations: which swing (0 none, 1
// the first, 2 the second: a combo's, or at someone on the ground; 3 a
// combo's third; a fist's jab, hook or uppercut), how far into it (s), and
// when it hits (s, weapon.dat's fire frame, or the fight move's).
struct Attack {
	int which = 0;
	float time = 0.0f, strike = 0.0f;
};

// Where his hand is that far into a swing (ReadyGrip when there's none).
Grip AttackGrip(MeleeKind kind, const Attack &attack);

// His hand through the swings: each played as it is, and where one starts
// over (the game loops it while the button's held), the next follows on or
// one's cut short, the jump from where his hand was fades out instead
// (inertialization, as Gears of War 4 blends its animations); and the
// chainsaw's buzz.
class MeleeHand {
public:
	Grip Update(MeleeKind kind, const Attack &attack, float dt);

private:
	bool started_ = false;
	Attack last_{};
	Grip at_{}, speed_{};   // where his hand is, and how fast it's going (per s)
	Grip gap_{}, gapSpeed_{}; // the jump still to fade
	float buzz_ = 0.0f, phase_ = 0.0f;
};

} // namespace vm

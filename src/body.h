#pragma once
// Posing the player bone by bone, for what Vice City has no animation for
// (swimming, climbing). A pose turns bones of his standing pose (idle_stance):
// each turn is in the ped's own frame (x right, y forward, z up) and applied
// to the bone about its joint, and a bone without one turns with its parent.
// The whole body can then turn about the root, and the root move.
//
// The skeleton is measured the first time (per model): the standing pose is
// written into the keyframes, the game rebuilds the bone matrices from it,
// and each bone's parent, its standing orientation and joint position, and
// which way round the keyframes hold their rotations are read back.
#include "game.h"

namespace body {

enum Bone {
	kRoot, kPelvis, kSpine, kSpine1, kNeck, kHead,
	kClavicleL, kUpperArmL, kForearmL, kHandL, kFingerL,
	kClavicleR, kUpperArmR, kForearmR, kHandR, kFingerR,
	kThighL, kCalfL, kFootL, kThighR, kCalfR, kFootR,
	kBones
};

struct Quat {
	float x = 0, y = 0, z = 0, w = 1;
};
Quat operator*(const Quat &a, const Quat &b); // b, then a
Quat Conjugate(const Quat &q);
Quat AxisAngle(CVector axis, float radians);
CVector Rotate(const Quat &q, const CVector &v);
Quat Slerp(Quat a, const Quat &b, float t);
// The shortest turn from one direction to another.
Quat Arc(CVector from, CVector to);
// About the ped's axes: right, forward, up.
Quat X(float radians);
Quat Y(float radians);
Quat Z(float radians);

CVector Add(const CVector &a, const CVector &b);
CVector Sub(const CVector &a, const CVector &b);
CVector Scale(const CVector &a, float s);
float Dot(const CVector &a, const CVector &b);
CVector Cross(const CVector &a, const CVector &b);
CVector Normalised(const CVector &v);
CVector Lerp(const CVector &a, const CVector &b, float t);
float Smooth(float t); // smoothstep of t clamped to 0..1

struct Pose {
	Quat turn[kBones];
	bool set[kBones] = {};
	Quat whole;           // the whole body, about the root, after the turns
	CVector rootMove{};   // the root's move from where it stands
	void Turn(Bone b, const Quat &q)
	{
		turn[b] = q;
		set[b] = true;
	}
};

// Measures the player's skeleton if it hasn't been (or his model changed):
// false if it couldn't be, and then nothing is posed.
bool Prepare(uint8_t *ped);
// After Prepare: a joint's place in a pose (ped frame), a bone's standing
// direction (towards its child) and length.
CVector Joint(const Pose &pose, Bone b);
CVector RestDirection(Bone b);
float Length(Bone b);
// Points a bone (in the pose's frame, before `whole`) the way given.
void Point(Pose &pose, Bone b, const CVector &direction);
// Each turn of one pose `t` of the way to the other's.
Pose Mix(const Pose &a, const Pose &b, float t);
// Two-bone reach: the elbow, bent towards `pole`, for the hand at `hand` (it
// stops short where the arm can't reach), `weight` of the way from the pose's arm.
void Reach(Pose &p, Bone upper, Bone fore, CVector hand, const CVector &pole, float weight = 1.0f);
// From CPed::PreRender, before the skeleton is rebuilt from the keyframes:
// poses him `weight` (0..1) of the way from his animation.
void Apply(uint8_t *ped, const Pose &pose, float weight);
// As Apply, but only the bones the pose sets (each laid as the pose has it,
// whatever its parent does); the rest keep the animation, following their
// posed parents (the arms holding a gun as the game has them, on a posed
// torso). The root moves only if it's set.
void ApplyOver(uint8_t *ped, const Pose &pose, float weight);
// A joint's place (ped frame) as ApplyOver would pose him (from CPed::PreRender,
// the animation's keyframes as they are).
CVector JointOver(uint8_t *ped, const Pose &pose, Bone b);
// Sets a bone to where ApplyOver would have it, turned `by` (in the ped's
// frame): its unset children follow, as animated.
void TurnOver(uint8_t *ped, Pose &pose, Bone b, const Quat &by);

// Some bones as an animation has them, held relative to another (the arms to
// the chest, say): read off the keyframes as they are (from CPed::PreRender),
// and laid so again on another animation's, `weight` of the way, about the
// base bone as that one has it. The base comes before them in kBones.
struct Held {
	Quat rel[kBones];
	bool set[kBones] = {};
	Bone base = kRoot;
};
bool Hold(uint8_t *ped, Held &held, Bone base, const Bone *bones, int count);
void Restore(uint8_t *ped, const Held &held, float weight);
// As Hold and Restore, but held relative to a frame given (a turn in the ped's
// frame) instead of a bone: the arms to the way he aims, say.
bool HoldIn(uint8_t *ped, Held &held, const Quat &frame, const Bone *bones, int count);
void RestoreIn(uint8_t *ped, const Held &held, const Quat &frame, float weight);

} // namespace body

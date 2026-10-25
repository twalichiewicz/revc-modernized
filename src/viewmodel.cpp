#include "viewmodel.h"
#include "camera.h"
#include "climb.h"
#include "config.h"
#include "game.h"
#include "log.h"
#include "swim.h"

#include <algorithm>
#include <cmath>
#include <dlfcn.h>

using namespace game;

namespace viewmodel {
namespace {

// Bone tags: right arm (upper, forearm, hand, fingers), left arm.
constexpr int kRight[4] = {22, 23, 24, 25}, kLeft[4] = {32, 33, 34, 35};
constexpr size_t kRwObject = 0x58, kWeaponModel = 0x290; // CEntity::m_rwObject, CPed::m_pWeaponModel
constexpr int32_t kWeaponFiring = 1; // CWeapon state
constexpr int32_t kStateFall = 0x2a, kStateGetUp = 0x2b, kStateDie = 0x36, kStateDead = 0x37;
constexpr int32_t kMoveStill = 1, kMoveSprint = 5; // CPed::m_nMoveState

enum class Kind { kPistol, kSmg, kLong, kHeavy, kThrown };
// A weapon's model in his hand (the hand's frame: x along the barrel, z up):
// how high its top (the sights) is over the hand, its length (weapon.dat's
// fire offset), how much it's tilted up in the hand (rad; the minigun and
// flamethrower are, for the pose the game holds them in; the top is then
// with it held level), and how much higher it's held.
struct Gun {
	Kind kind;
	float top, length;
	bool twoHanded;
	float tilt = 0.0f;
	float lift = 0.0f;
};
constexpr int32_t kFirstGun = 12, kLastGun = 33; // grenades on (eWeaponType)
constexpr Gun kGuns[kLastGun - kFirstGun + 1] = {
    {Kind::kThrown, 0.0f, 0.1f, false}, // grenade
    {Kind::kThrown, 0.0f, 0.1f, false}, // remote grenade
    {Kind::kThrown, 0.0f, 0.1f, false}, // tear gas
    {Kind::kThrown, 0.0f, 0.1f, false}, // molotov
    {Kind::kThrown, 0.0f, 0.0f, false}, // a rocket (never in his hand)
    {Kind::kPistol, 0.13f, 0.30f, false}, // Colt 45
    {Kind::kPistol, 0.16f, 0.41f, false}, // Python
    {Kind::kLong, 0.22f, 0.86f, true},    // shotgun
    {Kind::kLong, 0.24f, 0.85f, true},    // SPAS 12
    {Kind::kLong, 0.21f, 0.55f, true},    // stubby
    {Kind::kSmg, 0.14f, 0.45f, false},    // Tec 9
    {Kind::kSmg, 0.15f, 0.45f, false},    // Uzi
    {Kind::kSmg, 0.17f, 0.36f, false},    // Ingram
    {Kind::kSmg, 0.26f, 0.51f, true},     // MP5
    {Kind::kLong, 0.20f, 0.88f, true},    // M4
    {Kind::kLong, 0.20f, 1.00f, true},    // Ruger
    {Kind::kLong, 0.18f, 0.76f, true, 0.0f, 0.03f}, // sniper rifle (scoped, the game's own view)
    {Kind::kLong, 0.26f, 0.81f, true, 0.0f, 0.03f}, // laser scope sniper
    {Kind::kHeavy, 0.16f, 0.42f, false},  // rocket launcher (scoped, the game's)
    {Kind::kHeavy, 0.17f, 1.30f, true, 0.40f},  // flamethrower
    {Kind::kHeavy, 0.30f, 1.00f, true},         // M60
    {Kind::kHeavy, 0.02f, 1.28f, true, 0.519f}, // minigun (its barrels, as CPed::Render turns them: 29.7 deg)
};
// Where he holds it, in the view (m): his hand to the right and ahead, the
// gun's top below the eye. Aiming brings it closer and nearer the middle,
// its top just under the crosshair.
struct Hold {
	float right, top, forward;
};
constexpr Hold kHolds[5] = {
    {0.18f, -0.075f, 0.34f}, // pistol: arms out
    {0.19f, -0.11f, 0.30f},  // SMG
    {0.20f, -0.12f, 0.26f},  // rifle or shotgun
    {0.20f, -0.09f, 0.30f},  // heavy: up where it's seen
    {0.20f, -0.16f, 0.30f},  // something to throw (its top: the hand)
};
constexpr float kAimIn = 0.5f, kAimCloser = 0.25f; // of the way, aiming
constexpr float kAimTop = -0.035f;                 // m: the gun's top, aiming
constexpr float kConverge = 12.0f;    // m: where the shouldered gun points, ahead of the eye
constexpr float kForegrip = 0.45f;    // of the gun's length, from his right hand
constexpr float kBobUp = 0.006f, kBobSide = 0.004f; // m at a run
constexpr float kKick = 0.035f, kKickTurn = 0.07f, kKickFade = 12.0f; // m back, rad up, per second
constexpr float kReloadDrop = 0.12f, kReloadTurn = 0.45f; // m, rad down
constexpr float kSprintDrop = 0.04f, kSprintTurn = 0.25f;

void *(*gHierarchy)(void *clump);
int (*gBoneIndex)(void *hierarchy, int tag);
float *(*gBoneMatrices)(void *hierarchy);
bool gReady;
int32_t gLastClip = -1, gLastType = -1;
float gKick, gReload, gSprint;
// How the game's own pose holds each gun with the left hand: in the right
// hand's frame (x, y, z axes, then the position). Taken while he aims (both
// hands are on the gun then), so first person holds it as he does in third.
float gGrip[kLastGun - kFirstGun + 1][12];
bool gGripKnown[kLastGun - kFirstGun + 1];

CVector Add(const CVector &a, const CVector &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
CVector Sub(const CVector &a, const CVector &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
CVector Mul(const CVector &a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(const CVector &a, const CVector &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float Length(const CVector &a) { return std::sqrt(Dot(a, a)); }
CVector Cross(const CVector &a, const CVector &b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
CVector Normal(const CVector &a)
{
	const float l = Length(a);
	return l > 1e-6f ? Mul(a, 1.0f / l) : a;
}
CVector Turn(const CVector &v, const CVector &axis, float angle)
{
	const float c = std::cos(angle), s = std::sin(angle);
	return Add(Add(Mul(v, c), Mul(Cross(axis, v), s)), Mul(axis, Dot(axis, v) * (1.0f - c)));
}

// A bone's matrix (RwMatrix: x, y, z axes, then its position; 4 floats each).
CVector Axis(const float *m, int i) { return {m[i * 4], m[i * 4 + 1], m[i * 4 + 2]}; }
void SetAxis(float *m, int i, const CVector &v)
{
	m[i * 4] = v.x;
	m[i * 4 + 1] = v.y;
	m[i * 4 + 2] = v.z;
}
CVector Pos(const float *m) { return Axis(m, 3); }

// `child` moved as `before` became `after` (it's held by it: fingers by the hand).
void Carry(float *child, const float *before, const float *after)
{
	// Into `before`'s frame, out of `after`'s.
	auto carry = [&](const CVector &v, bool point) {
		const CVector d = point ? Sub(v, Pos(before)) : v;
		const CVector local{Dot(d, Axis(before, 0)), Dot(d, Axis(before, 1)), Dot(d, Axis(before, 2))};
		CVector out = Add(Add(Mul(Axis(after, 0), local.x), Mul(Axis(after, 1), local.y)), Mul(Axis(after, 2), local.z));
		return point ? Add(out, Pos(after)) : out;
	};
	for (int i = 0; i < 3; i++)
		SetAxis(child, i, carry(Axis(child, i), false));
	SetAxis(child, 3, carry(Pos(child), true));
}

// How a bone is laid along its arm: which of its axes (and which way) points
// along it, and which the elbow bends about. Read off the animation while
// his elbow is bent (the skeleton's own conventions), per bone.
struct Lay {
	int along = 0, hinge = 2;
	float alongSign = 1.0f, hingeSign = 1.0f;
};
struct ArmLay {
	Lay upper, fore;
};
ArmLay gLays[2]; // right, left

void LearnLay(Lay &lay, const float *bone, const CVector &along, const CVector &hinge)
{
	auto best = [&](const CVector &v, int skip, float &sign) {
		int pick = -1;
		float most = -1.0f;
		for (int i = 0; i < 3; i++) {
			const float d = Dot(Axis(bone, i), v);
			if (i != skip && std::fabs(d) > most) {
				most = std::fabs(d);
				pick = i;
				sign = d >= 0.0f ? 1.0f : -1.0f;
			}
		}
		return pick;
	};
	lay.along = best(along, -1, lay.alongSign);
	lay.hinge = best(hinge, lay.along, lay.hingeSign);
}

// A bone laid along `along` (unit), bending about `hinge` (unit, across it), at `at`.
void LayBone(float *bone, const Lay &lay, const CVector &along, const CVector &hinge, const CVector &at)
{
	CVector axes[3];
	axes[lay.along] = Mul(along, lay.alongSign);
	axes[lay.hinge] = Mul(hinge, lay.hingeSign);
	const int third = 3 - lay.along - lay.hinge; // a right-handed frame: e[k] = e[k+1] x e[k+2]
	axes[third] = Cross(axes[(third + 1) % 3], axes[(third + 2) % 3]);
	for (int i = 0; i < 3; i++)
		SetAxis(bone, i, axes[i]);
	SetAxis(bone, 3, at);
}

// The arm's hand to `wrist`, the elbow bent towards `pole`. The hand takes
// `hand` axes if given (x, y, z), else points `pointing` (laid as the
// forearm), else keeps its turn on the forearm as the animation has it.
void Reach(float *const *bones, ArmLay &lay, const CVector &wrist, const CVector &pole, const CVector *hand,
           const CVector *pointing = nullptr)
{
	float *upper = bones[0], *fore = bones[1], *palm = bones[2], *fingers = bones[3];
	const CVector shoulder = Pos(upper), elbow0 = Pos(fore), wrist0 = Pos(palm);
	const float l1 = Length(Sub(elbow0, shoulder)), l2 = Length(Sub(wrist0, elbow0));
	if (l1 < 1e-3f || l2 < 1e-3f)
		return;
	// The animation's bend, while it's bent enough to tell.
	const CVector bent = Cross(Normal(Sub(elbow0, shoulder)), Normal(Sub(wrist0, elbow0)));
	if (Length(bent) > 0.25f) {
		LearnLay(lay.upper, upper, Normal(Sub(elbow0, shoulder)), Normal(bent));
		LearnLay(lay.fore, fore, Normal(Sub(wrist0, elbow0)), Normal(bent));
	}
	const CVector along = Normal(Sub(wrist, shoulder));
	const float reach = std::clamp(Length(Sub(wrist, shoulder)), std::fabs(l1 - l2) + 0.01f, l1 + l2 - 0.005f);
	const CVector target = Add(shoulder, Mul(along, reach));
	const float cosA = std::clamp((l1 * l1 + reach * reach - l2 * l2) / (2.0f * l1 * reach), -1.0f, 1.0f);
	const CVector out = Normal(Sub(pole, Mul(along, Dot(pole, along))));
	const CVector elbow = Add(shoulder, Add(Mul(along, l1 * cosA), Mul(out, l1 * std::sqrt(1.0f - cosA * cosA))));
	const CVector hinge = Normal(Cross(out, along)); // as cross(upper arm, forearm) for a bent elbow

	float foreBefore[16], palmBefore[16];
	std::copy(fore, fore + 16, foreBefore);
	std::copy(palm, palm + 16, palmBefore);
	LayBone(upper, lay.upper, Normal(Sub(elbow, shoulder)), hinge, shoulder);
	LayBone(fore, lay.fore, Normal(Sub(target, elbow)), hinge, elbow);
	if (hand) {
		for (int i = 0; i < 3; i++)
			SetAxis(palm, i, hand[i]);
		SetAxis(palm, 3, target);
	} else if (pointing) { // laid as the forearm is, pointing that way
		LayBone(palm, lay.fore, *pointing, Normal(Sub(hinge, Mul(*pointing, Dot(hinge, *pointing)))), target);
	} else {
		Carry(palm, foreBefore, fore);
	}
	if (fingers)
		Carry(fingers, palmBefore, palm);
}

float Ease(float value, float to, float dt, float rate) { return value + (to - value) * std::min(1.0f, dt * rate); }

// `m`'s axes and position in `frame`'s, and back.
void Into(const float *frame, const float *m, float *out)
{
	for (int i = 0; i < 4; i++) {
		const CVector v = i < 3 ? Axis(m, i) : Sub(Pos(m), Pos(frame));
		out[i * 3] = Dot(v, Axis(frame, 0));
		out[i * 3 + 1] = Dot(v, Axis(frame, 1));
		out[i * 3 + 2] = Dot(v, Axis(frame, 2));
	}
}
CVector OutOf(const float *frame, const float *local, int i)
{
	const CVector v = Add(Add(Mul(Axis(frame, 0), local[i * 3]), Mul(Axis(frame, 1), local[i * 3 + 1])),
	                      Mul(Axis(frame, 2), local[i * 3 + 2]));
	return i < 3 ? v : Add(v, Pos(frame));
}

} // namespace

bool Install()
{
	gHierarchy = reinterpret_cast<void *(*)(void *)>(dlsym(RTLD_MAIN_ONLY, "_Z29GetAnimHierarchyFromSkinClumpPN2rw5ClumpE"));
	gBoneIndex = reinterpret_cast<int (*)(void *, int)>(dlsym(RTLD_MAIN_ONLY, "_Z17RpHAnimIDGetIndexPN2rw14HAnimHierarchyEi"));
	gBoneMatrices =
	    reinterpret_cast<float *(*)(void *)>(dlsym(RTLD_MAIN_ONLY, "_Z30RpHAnimHierarchyGetMatrixArrayPN2rw14HAnimHierarchyE"));
	gReady = gHierarchy && gBoneIndex && gBoneMatrices;
	return gReady;
}

void Apply(uint8_t *ped, bool aimed)
{
	if (!gReady || !*reinterpret_cast<void *const *>(ped + kWeaponModel))
		return;
	uint8_t *weapon = PedWeapon(ped, PedCurrentWeaponSlot(ped));
	const int32_t type = WeaponType(weapon);
	if (type < kFirstGun || type > kLastGun || kGuns[type - kFirstGun].length <= 0.0f)
		return;
	const int32_t state = PedState(ped);
	if (state == kStateFall || state == kStateGetUp || state == kStateDie || state == kStateDead || swim::Swimming() ||
	    climb::Active())
		return;
	void *clump = *reinterpret_cast<void *const *>(ped + kRwObject);
	void *hierarchy = clump ? gHierarchy(clump) : nullptr;
	if (!hierarchy)
		return;
	float *matrices = gBoneMatrices(hierarchy);
	float *right[4], *left[4];
	for (int i = 0; i < 4; i++) {
		const int r = gBoneIndex(hierarchy, kRight[i]), l = gBoneIndex(hierarchy, kLeft[i]);
		if ((r < 0 || l < 0) && i < 3)
			return;
		right[i] = r >= 0 ? matrices + r * 16 : nullptr;
		left[i] = l >= 0 ? matrices + l * 16 : nullptr;
	}

	const Gun &gun = kGuns[type - kFirstGun];
	const Hold &hold = kHolds[int(gun.kind)];
	const float dt = *api.timeStep / 50.0f;
	// The view: the game's "right" row is its left.
	const CVector eye = *reinterpret_cast<const CVector *>(api.theCamera + 0x30);
	const CVector forward = *reinterpret_cast<const CVector *>(api.theCamera + 0x10);
	const CVector up = *reinterpret_cast<const CVector *>(api.theCamera + 0x20);
	const CVector side = Normal(Cross(forward, up));

	// It kicks as it fires (the clip goes down), dips while he reloads, drops a
	// little as he sprints.
	const int32_t clip = *reinterpret_cast<const int32_t *>(weapon + 0x8);
	if (type == gLastType && clip < gLastClip)
		gKick = 1.0f;
	gLastClip = clip;
	gLastType = type;
	gKick *= std::exp(-kKickFade * dt);
	gReload = Ease(gReload, *reinterpret_cast<const int32_t *>(weapon + 4) == kWeaponReloading ? 1.0f : 0.0f, dt, 8.0f);
	gSprint = Ease(gSprint, PedMoveState(ped) == kMoveSprint ? 1.0f : 0.0f, dt, 6.0f);

	const float aim = gun.kind == Kind::kHeavy ? 0.0f : aimed ? 1.0f : camera::AimBlend();
	const float r = hold.right * (1.0f - kAimIn * aim);
	const float u = hold.top + (kAimTop - hold.top) * aim + gun.lift - gun.top; // the hand, its top so high
	const float f = hold.forward * (1.0f - kAimCloser * aim);
	const float pi = 3.14159265f, phase = camera::StepPhase(), steps = camera::StepAmount() * (1.0f - 0.7f * aim);
	const float bob = kBobUp * steps * std::sin(phase * 2.0f * pi), sway = kBobSide * steps * std::sin(phase * pi);
	CVector offset = Add(Add(Mul(side, r + sway), Mul(up, u + bob - kReloadDrop * gReload - kSprintDrop * gSprint)),
	                     Mul(forward, f - kKick * gKick));
	const CVector grip = Add(eye, offset);
	// Pointing a little in, to where you look, at the shoulder; straight along it aiming.
	CVector barrel = Normal(Add(Mul(Normal(Sub(Add(eye, Mul(forward, kConverge)), grip)), 1.0f - aim), Mul(forward, aim)));
	barrel = Turn(barrel, side, kKickTurn * gKick - kReloadTurn * gReload - kSprintTurn * gSprint);
	const CVector gunUp = Normal(Sub(up, Mul(barrel, Dot(up, barrel))));
	// The hand: x along the barrel, z up (y = z x x), tilted down by as much
	// as the model is tilted up in it.
	const CVector along = Turn(barrel, side, -gun.tilt), handZ = Turn(gunUp, side, -gun.tilt);
	const CVector hand[3] = {along, Cross(handZ, along), handZ};

	// The game's grip for this gun, while its hands are both on it.
	const int g = type - kFirstGun;
	if (gun.twoHanded) {
		// On the gun: ahead of the right hand along the barrel (tilted up in
		// the hand as the model is), close to its line; a heavy gun's handles
		// stand off it.
		float local[12];
		Into(right[2], left[2], local);
		const CVector at{local[9], local[10], local[11]}, line{std::cos(gun.tilt), 0.0f, std::sin(gun.tilt)};
		const float along = Dot(at, line), off = Length(Sub(at, Mul(line, along)));
		const bool heavy = gun.kind == Kind::kHeavy;
		// Only from his aiming or firing pose (walking with the gun down, the
		// animation's left hand wanders; a heavy gun's is on it standing too),
		// and eased: it holds still.
		const bool posed = camera::AimBlend() > 0.95f || *reinterpret_cast<const int32_t *>(weapon + 4) == kWeaponFiring ||
		                   (heavy && PedMoveState(ped) <= kMoveStill);
		if (posed && along > 0.05f && along < gun.length && off < (heavy ? 0.45f : 0.15f)) {
			for (int i = 0; i < 12; i++)
				gGrip[g][i] = gGripKnown[g] ? gGrip[g][i] + (local[i] - gGrip[g][i]) * 0.2f : local[i];
			gGripKnown[g] = true;
		}
	}
	const CVector down = Mul(up, -1.0f);
	Reach(right, gLays[0], grip, Add(Add(down, Mul(side, 0.7f)), Mul(forward, -0.2f)), hand);
	if (gun.twoHanded) {
		// On the barrel where the arm reaches it: where his aiming pose holds
		// it (or a foregrip), slid back along the barrel if that's too far.
		const CVector leftPole = Add(Add(down, Mul(side, -0.7f)), Mul(forward, -0.2f));
		const CVector wanted = gGripKnown[g] ? OutOf(right[2], gGrip[g], 3)
		                                     : Add(Add(grip, Mul(barrel, gun.length * kForegrip)), Mul(gunUp, -0.03f));
		const float t0 = Dot(Sub(wanted, grip), barrel);
		const CVector base = Sub(wanted, Mul(barrel, t0)); // the same spot, at the right hand
		const CVector shoulder = Pos(left[0]);
		const float arm = Length(Sub(Pos(left[1]), shoulder)) + Length(Sub(Pos(left[2]), Pos(left[1]))) - 0.03f;
		const CVector d = Sub(base, shoulder);
		const float db = Dot(d, barrel), disc = db * db - (Dot(d, d) - arm * arm);
		const float reachable = disc >= 0.0f ? -db + std::sqrt(disc) : -db;
		const float t = std::clamp(std::min(t0, reachable), 0.08f, std::max(t0, 0.08f));
		const CVector foregrip = Add(base, Mul(barrel, t));
		if (gGripKnown[g]) {
			const CVector held[3] = {OutOf(right[2], gGrip[g], 0), OutOf(right[2], gGrip[g], 1), OutOf(right[2], gGrip[g], 2)};
			Reach(left, gLays[1], foregrip, leftPole, held);
		} else {
			// Fingers forward and over to the right, round the barrel.
			const CVector fingers = Normal(Add(Add(Mul(barrel, 0.75f), Mul(side, 0.55f)), Mul(gunUp, 0.2f)));
			Reach(left, gLays[1], foregrip, leftPole, nullptr, &fingers);
		}
	}
}

} // namespace viewmodel

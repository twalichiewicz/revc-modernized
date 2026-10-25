#include "swim.h"
#include "climb.h"
#include "config.h"
#include "draw.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "player.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <dlfcn.h>

using namespace game;
using namespace body;

namespace swim {
namespace {

using Clock = std::chrono::steady_clock;

constexpr float kPi = 3.14159265f;
constexpr int32_t kWeaponDrowning = 43;
constexpr uint32_t kDrowning = 1u << 16; // CPed, at +0x190: ProcessBuoyancy's "too deep"
constexpr uint16_t kInWater = 1u << 4;   // CPhysical, at +0x156
// Water depth (m): he swims from here, or where the game finds it too deep from
// the second; he stands up again below the third. Shallower, the game's "too
// deep" doesn't count: he's wading.
constexpr float kSwimFrom = 1.4f, kDeepFrom = 1.25f, kStandBelow = 1.2f;
constexpr float kFloat = 0.25f;                       // his position (about his hips) under the surface
constexpr float kSpeed = 1.6f, kSprintSpeed = 2.7f;   // m/s
constexpr float kAccel = 2.5f, kDecel = 1.8f;         // m/s/s
constexpr float kTurn = 3.2f, kSprintTurn = 2.0f;     // rad/s
constexpr float kCrawlFrom = 0.3f;                    // m/s: slower, he treads water
// Under the surface.
constexpr float kDiveFrom = 2.0f;                      // m of water it takes
constexpr float kDiveFor = 0.55f, kDivePitch = -0.95f; // s and radians of the dive itself
constexpr float kUnderSpeed = 1.3f, kUnderSprint = 2.2f, kUpSpeed = 1.6f; // m/s
constexpr float kDrift = 0.25f;                        // m/s he floats up when he isn't swimming
constexpr float kSurfaceAt = 0.45f;                    // m under it, on his way up, he's at the surface
constexpr float kAboveBottom = 0.8f;                   // m: as close as his position gets to the bottom
constexpr float kCameraLift = 0.2f;                    // rad: the follow camera looks down at him this much
constexpr float kBreath = 20.0f, kRefill = 4.0f;       // s of breath; refilled this many times faster
constexpr float kDrownPerSecond = 8.0f;                // health, once he's out of breath
constexpr uint32_t kCarUnderFor = 250;                 // ms after the last drowning damage a sunk car dealt
constexpr float kStartUnder = 1.2f;                    // m under the surface: in the water that deep, he's under
// The jump and fall animations he may have gone in with (JUMP_launch to FALL_collapse).
constexpr uint32_t kAirAnims[] = {141, 142, 143, 144, 145, 146, 147};
// The wading splash (CPed::PlayFootSteps on shallow water) and the splash of falling in.
constexpr int kParticleSplash = 0x2e, kParticleSpray = 0x44;
constexpr uint16_t kSoundSplash = 0xa3;
constexpr CRGBA kUnderwaterTint{16, 66, 78, 255};

void (*orig_ProcessBuoyancy)(uint8_t *ped);
bool (*orig_IsTargetInWater)(uint8_t *cam, const CVector &source);
bool (*orig_CanPedExitCar)(uint8_t *vehicle, bool jumpExit);
void (*orig_PlayFootSteps)(uint8_t *ped);
void (*gAddParticle)(int type, const CVector &pos, const CVector &velocity, uint8_t *entity, float size,
                     const CRGBA &colour, int rotationSpeed, int rotation, int frame, int life);
void (*gPlayOneShot)(uint8_t *audio, int entity, uint16_t sound, float volume);

bool gInstalled, gInBuoyancy, gSwimming;
bool gWater; // there's water where he is, this ProcessBuoyancy: its drowning damage is refused
uint8_t *gPed;      // whom it's about (the player ped changes on a load)
float gLevel;       // the surface where he swims
float gBottom;      // the bottom under him (far down if there's none in reach)
float gSpeed;       // m/s
float gHeading;     // his, as he swims (the game's controls turn him too)
int gHolstered = -1; // the weapon slot put away
int gLogged, gDivesLogged;
// Under the surface.
bool gUnder;
float gDiveLeft;    // s of the dive itself
CVector gDir{0, 1, 0}; // where he swims, eased
float gPitch;       // of his body, up positive
float gBreath = kBreath;
uint32_t gCarUnderAt; // game time a sinking vehicle last tried to drown him
bool gCarUnderSeen;
// The pose.
Clock::time_point gLastPose;
float gWeight, gCrawl, gCycle, gTread, gUnderBlend;
int gCycles; // crawl strokes done: he breathes every other one
Im2DCanvas gCanvas;

// How deep the water is under him (very, if there's no bottom in reach).
float Depth(const CVector &p, float level)
{
	uint8_t point[64] = {};
	uint8_t *entity = nullptr;
	if (!api.ProcessVerticalLine(p, level - 40.0f, point, entity, true, false, false, true, false, false, nullptr))
		return 1e9f;
	return level - reinterpret_cast<const CVector *>(point)->z;
}

void RestoreWeapon(uint8_t *ped)
{
	if (gHolstered > 0 && PedCurrentWeaponSlot(ped) == 0 && WeaponType(PedWeapon(ped, gHolstered)) != 0)
		api.SetCurrentWeapon(ped, gHolstered);
	gHolstered = -1;
}

void Start(uint8_t *ped, float depth)
{
	gSwimming = true;
	// Out of a car that sank, say: under the surface, to swim up.
	gUnder = EntityPos(ped).z < gLevel - kStartUnder;
	gDiveLeft = 0.0f;
	gDir = {0.0f, 0.0f, 1.0f};
	const CVector &v = EntitySpeed(ped);
	gSpeed = std::min(std::hypot(v.x, v.y) * 50.0f, kSpeed);
	gHeading = *reinterpret_cast<const float *>(ped + 0x46c);
	uint8_t *clump = EntityClump(ped);
	for (uint32_t anim : kAirAnims)
		if (uint8_t *assoc = clump ? api.GetAssociation(clump, anim) : nullptr)
			*reinterpret_cast<float *>(assoc + 0x2c) = -8.0f; // fading out
	if (gLogged < 3) {
		gLogged++;
		Log("swim: in the water (%.1f m deep)", std::min(depth, 99.0f));
	}
}

void Stop(uint8_t *ped)
{
	gSwimming = gUnder = false;
	if (!climb::Active()) // climbing out, it comes back once he's up
		RestoreWeapon(ped);
}

// After the game's own: whether he swims, and if so, no drowning and no fall.
void Hook_ProcessBuoyancy(uint8_t *ped)
{
	if (!gInstalled || ped != api.FindPlayerPed()) {
		orig_ProcessBuoyancy(ped);
		return;
	}
	if (ped != gPed) {
		gPed = ped;
		gSwimming = gUnder = false;
		gHolstered = -1;
		gBreath = kBreath;
	}
	uint64_t &flags = PedFlags(ped);
	flags &= ~kPedReachingDryLand; // he swims rather than being put ashore
	const CVector &p = EntityPos(ped);
	float level = 0.0f;
	gWater = !PedInVehicle(ped) && api.GetWaterLevel(p.x, p.y, p.z, &level, true);
	gInBuoyancy = true;
	orig_ProcessBuoyancy(ped);
	gInBuoyancy = false;

	uint32_t &more = *reinterpret_cast<uint32_t *>(ped + 0x190);
	const bool inWater = *reinterpret_cast<const uint16_t *>(ped + 0x156) & kInWater, deep = more & kDrowning;
	const int32_t state = PedState(ped);
	bool wading = false;
	if (Health(ped) <= 0.0f || state == kPedDie || state == kPedDead || PedInVehicle(ped) || !inWater ||
	    !api.GetWaterLevel(p.x, p.y, p.z, &level, true)) {
		if (gSwimming)
			Stop(ped);
	} else {
		const float depth = Depth(p, level);
		gLevel = level;
		if (!gSwimming && (depth > kSwimFrom || (deep && depth > kDeepFrom)))
			Start(ped, depth);
		else if (gSwimming && depth < kStandBelow)
			Stop(ped);
		gBottom = level - std::min(depth, 1000.0f);
		wading = !gSwimming && depth <= kDeepFrom; // on the bottom, whatever the game makes of it
	}
	if (gSwimming || wading || climb::Active()) {
		more &= ~kDrowning;
		flags = (flags | kPedStanding | kPedWasStanding) & ~(kPedInTheAir | kPedLanding);
	}
}

// The camera watches a ped or car that's in the water from where it was
// (MODE_PLAYER_FALLEN_WATER, CCamera::CamControl): not him swimming, or the
// car he's sinking in (he can get out).
bool Hook_IsTargetInWater(uint8_t *cam, const CVector &source)
{
	const uint8_t *target = *reinterpret_cast<uint8_t *const *>(cam + 0x1b0);
	const uint8_t *player = gInstalled ? api.FindPlayerPed() : nullptr;
	if (player && target &&
	    ((target == player && (gSwimming || climb::Active())) || (PedInVehicle(player) && target == PedVehicle(player))))
		return false;
	return orig_IsTargetInWater(cam, source);
}

// He can get out of a vehicle in the water. The game lets him out only once
// it has all but stopped turning (CVehicle::CanPedExitCar), which a car
// bobbing and sinking never does, so he was stuck in it.
bool Hook_CanPedExitCar(uint8_t *vehicle, bool jumpExit)
{
	if (orig_CanPedExitCar(vehicle, jumpExit))
		return true;
	const uint8_t *player = api.FindPlayerPed();
	return player && PedInVehicle(player) && PedVehicle(player) == vehicle &&
	       (*reinterpret_cast<const uint16_t *>(vehicle + 0x156) & kInWater);
}

// No footsteps while he swims: the game plays them off the walk and run
// animations under the swimming pose (he's kept standing, CPed::PlayFootSteps).
void Hook_PlayFootSteps(uint8_t *ped)
{
	if (gSwimming && ped == api.FindPlayerPed())
		return;
	orig_PlayFootSteps(ped);
}

// In a vehicle that's sunk (its ProcessBuoyancy drowns those inside).
bool InSunkVehicle(const uint8_t *ped) { return gCarUnderSeen && PedInVehicle(ped) && *api.timeMs - gCarUnderAt < kCarUnderFor; }

float Random(float from, float to) { return from + (to - from) * float(std::rand()) / float(RAND_MAX); }

// The splash of a hand going in (or a kick), at the surface over a joint of the pose.
void Splash(uint8_t *ped, const body::Pose &pose, Bone joint, float size, bool sound)
{
	if (!gAddParticle)
		return;
	const CVector j = Joint(pose, joint), f = HeadingForward(gHeading), r{f.y, -f.x, 0.0f};
	const CVector &at = EntityPos(ped);
	const CVector pos{at.x + r.x * j.x + f.x * j.y, at.y + r.y * j.x + f.y * j.y, gLevel + 0.05f};
	const CVector up{f.x * 0.01f, f.y * 0.01f, Random(0.01f, 0.03f)};
	gAddParticle(kParticleSplash, pos, up, nullptr, size * 0.5f, {0, 0, 0, 0}, 0, 0, 0, 0);
	gAddParticle(kParticleSpray, pos, up, nullptr, size, {255, 255, 255, 196}, 0, 0, 0, 0);
	if (sound && gPlayOneShot)
		gPlayOneShot(api.dmAudio, *reinterpret_cast<const int32_t *>(ped + 0x78), kSoundSplash, 0.0f);
}

// `at` came round between `from` and `to` (a phase going 0..1 and wrapping).
bool Passed(float from, float to, float at)
{
	return from <= to ? from < at && at <= to : at > from || at <= to;
}

// From the swimmer's frame (x his right, y towards his head, z out of his back)
// to his standing one, which `whole` lays flat.
CVector Standing(const CVector &v, float side) { return Normalised({v.x * side, -v.z, v.y}); }

CVector Between(const CVector &a, const CVector &b, float t) { return Rotate(Slerp({}, Arc(a, b), t), Normalised(a)); }

struct Key {
	float at;
	CVector upper, fore;
};

// One arm through a stroke's keys, `c` of the way round (right arm; the left mirrors it).
template <size_t N>
void Stroke(body::Pose &p, const Key (&keys)[N], Bone upper, Bone fore, float side, float c)
{
	size_t i = 0;
	while (i + 2 < N && c >= keys[i + 1].at)
		i++;
	const Key &a = keys[i], &b = keys[i + 1];
	const float t = Smooth((c - a.at) / (b.at - a.at));
	Point(p, upper, Standing(Between(a.upper, b.upper, t), side));
	Point(p, fore, Standing(Between(a.fore, b.fore, t), side));
}

constexpr Key kCrawl[] = {
    {0.00f, {0.12f, 1.00f, -0.05f}, {0.10f, 1.00f, -0.12f}},  // reaching ahead
    {0.22f, {0.18f, 0.50f, -0.85f}, {0.02f, -0.10f, -1.00f}}, // the catch: elbow high, hand under him
    {0.42f, {0.22f, -0.80f, -0.55f}, {0.12f, -1.00f, -0.10f}}, // pushing back past his hip
    {0.58f, {0.40f, -0.80f, 0.45f}, {0.25f, -0.55f, -0.80f}},  // elbow out of the water first
    {0.80f, {0.55f, 0.15f, 0.82f}, {0.28f, 0.60f, -0.75f}},    // swinging forward over it, hand low
    {1.00f, {0.12f, 1.00f, -0.05f}, {0.10f, 1.00f, -0.12f}},
};
constexpr Key kBreaststroke[] = {
    {0.00f, {0.10f, 1.00f, -0.05f}, {0.05f, 1.00f, -0.02f}},  // gliding, arms out ahead
    {0.20f, {0.55f, 0.82f, -0.12f}, {0.50f, 0.85f, -0.15f}},  // sweeping out
    {0.40f, {0.80f, 0.20f, -0.55f}, {-0.35f, 0.55f, -0.75f}}, // pulling in, elbows wide
    {0.55f, {0.45f, 0.20f, -0.85f}, {-0.70f, 0.70f, 0.05f}},  // hands together under his chin
    {0.75f, {0.20f, 0.80f, -0.55f}, {0.05f, 0.95f, -0.25f}},  // shooting them forward
    {1.00f, {0.10f, 1.00f, -0.05f}, {0.05f, 1.00f, -0.02f}},
};

// Flutter kick: a straight-ish leg from the hip, the knee giving on the up-kick, toes pointed.
void Kick(body::Pose &p, Bone thigh, Bone calf, Bone foot, float k)
{
	const float hip = 0.1f + 0.22f * k, knee = 0.2f + 0.3f * std::max(0.0f, -k);
	p.Turn(thigh, X(hip));
	p.Turn(calf, X(hip - knee));
	p.Turn(foot, X(hip - knee - 0.9f));
}

// The front crawl, `c` of the way through a stroke (right arm first).
body::Pose Crawl(float c, bool breathes)
{
	body::Pose p;
	const float roll = 0.45f * std::sin(2 * kPi * c); // onto the side of the arm that's pulling
	p.whole = X(-1.35f) * Z(roll);
	p.rootMove = {0.0f, 0.0f, kFloat - 0.13f}; // his back at the surface
	Stroke(p, kCrawl, kUpperArmR, kForearmR, 1.0f, c);
	Stroke(p, kCrawl, kUpperArmL, kForearmL, -1.0f, std::fmod(c + 0.5f, 1.0f));
	// Looking ahead, and turning his head out to breathe as his right arm comes over.
	const float breathe = breathes && c > 0.55f && c < 0.95f ? -1.1f * std::sin(kPi * (c - 0.55f) / 0.4f) : 0.0f;
	p.Turn(kNeck, Z(0.4f * breathe - 0.5f * roll) * X(0.35f));
	p.Turn(kHead, Z(breathe - 0.8f * roll) * X(0.8f));
	const float k = std::sin(2 * kPi * 3 * c); // six kicks a stroke
	Kick(p, kThighL, kCalfL, kFootL, k);
	Kick(p, kThighR, kCalfR, kFootR, -k);
	return p;
}

// Treading water: upright, hands sculling, legs cycling; at the surface his head is out.
body::Pose Tread(float t, bool surface)
{
	body::Pose p;
	p.whole = X(surface ? -0.12f : -0.6f);
	p.rootMove = {0.0f, 0.0f, surface ? kFloat - 0.58f : 0.0f};
	const float scull = 0.45f * std::sin(2 * kPi * t);
	for (float side : {1.0f, -1.0f}) {
		const bool right = side > 0;
		Point(p, right ? kUpperArmR : kUpperArmL, Rotate(Z(side * scull), {0.62f * side, 0.55f, -0.55f}));
		Point(p, right ? kForearmR : kForearmL, Rotate(Z(side * scull * 1.4f), {0.25f * side, 0.95f, -0.2f}));
		const float a = 2 * kPi * t + (right ? 0.0f : kPi);
		Point(p, right ? kThighR : kThighL, {0.3f * side, 0.55f, -0.78f});
		Point(p, right ? kCalfR : kCalfL, {side * (0.35f + 0.25f * std::cos(a)), -0.25f + 0.3f * std::sin(a), -0.85f});
	}
	p.Turn(kHead, X(surface ? 0.15f : 0.4f));
	return p;
}

// The breaststroke under the surface, laid along his way (pitch up positive), with a frog kick.
body::Pose Breaststroke(float c, float pitch)
{
	body::Pose p;
	p.whole = X(-kPi / 2 + pitch);
	Stroke(p, kBreaststroke, kUpperArmR, kForearmR, 1.0f, c);
	Stroke(p, kBreaststroke, kUpperArmL, kForearmL, -1.0f, c);
	// Knees drawn up and out while the arms come in, then the kick as they go forward.
	const float bend = Smooth((c - 0.45f) / 0.3f) * (1 - Smooth((c - 0.75f) / 0.15f));
	for (float side : {1.0f, -1.0f}) {
		const bool right = side > 0;
		const Quat out = Y(-side * 0.45f * bend);
		const float hip = 0.1f + 0.7f * bend, knee = 1.9f * bend;
		p.Turn(right ? kThighR : kThighL, out * X(hip));
		p.Turn(right ? kCalfR : kCalfL, out * X(hip - knee));
		p.Turn(right ? kFootR : kFootL, out * X(hip - knee + 0.6f * bend - 0.5f * (1 - bend)));
	}
	p.Turn(kNeck, X(0.3f));
	p.Turn(kHead, X(0.6f));
	return p;
}

void Turn(float &heading, float want, float most)
{
	heading = std::remainder(heading + std::clamp(std::remainder(want - heading, 2 * kPi), -most, most), 2 * kPi);
}

} // namespace

bool Swimming() { return gInstalled && gSwimming; }
bool Underwater() { return gInstalled && gSwimming && gUnder; }
bool Posing() { return gInstalled && (gSwimming || gWeight > 0.0f); }
float Surface() { return gLevel; }
float Breath() { return gBreath / kBreath; }
bool ShowsBreath()
{
	const uint8_t *player = api.FindPlayerPed();
	return gInstalled && (Underwater() || gBreath < kBreath - 0.01f || (player && InSunkVehicle(player)));
}

bool RefusesDamage(const uint8_t *ped, int32_t weapon)
{
	if (!gInstalled || weapon != kWeaponDrowning)
		return false;
	if (gInBuoyancy) // his own, in the water
		return gWater;
	// A sinking car, bike or boat drowning him inside it (CAutomobile::ProcessBuoyancy
	// and the like, a point of health every 1/50 s): his breath goes instead.
	uint8_t *player = api.FindPlayerPed();
	if (ped == player && PedInVehicle(ped)) {
		gCarUnderAt = *api.timeMs;
		gCarUnderSeen = true;
		const float dt = std::clamp(*api.timeStep, 0.01f, 3.0f) / 50.0f;
		gBreath = std::max(0.0f, gBreath - dt);
		if (gBreath <= 0.0f)
			player::Damage(player, kWeaponDrowning, kDrownPerSecond * dt);
		return true;
	}
	return false;
}

void Dive(uint8_t *ped)
{
	if (!Swimming() || gUnder || gLevel - gBottom < kDiveFrom || ped != gPed)
		return;
	gUnder = true;
	gDiveLeft = kDiveFor;
	const CVector f = HeadingForward(gHeading);
	gDir = {f.x * std::cos(kDivePitch), f.y * std::cos(kDivePitch), std::sin(kDivePitch)};
	gSpeed = std::max(gSpeed, 1.5f);
	if (gDivesLogged++ < 2)
		Log("swim: diving (%.0f m of water)", std::min(gLevel - gBottom, 99.0f));
}

void AfterControl(uint8_t *ped)
{
	if (!gInstalled || ped != gPed)
		return;
	const float step = std::clamp(*api.timeStep, 0.01f, 3.0f), dt = step / 50.0f;
	if (gSwimming && PedInVehicle(ped)) // into a boat
		Stop(ped);
	// His breath: it goes under the surface (in a sunk car, as it drowns him:
	// RefusesDamage), and comes back above it.
	if (Underwater()) {
		gBreath = std::max(0.0f, gBreath - dt);
		if (gBreath <= 0.0f)
			player::Damage(ped, kWeaponDrowning, kDrownPerSecond * dt);
	} else if (!InSunkVehicle(ped)) {
		gBreath = std::min(kBreath, gBreath + dt * kRefill);
	}
	if (!gSwimming) {
		if (gHolstered >= 0 && !climb::Active())
			RestoreWeapon(ped);
		return;
	}
	const int32_t state = PedState(ped);
	if (climb::Active() || (state != kPedIdle && state != kPedJump && state != kPedFall && state != kPedGetUp &&
	                        state != 0)) // getting into a boat, say: the game moves him
		return;
	if (const int slot = PedCurrentWeaponSlot(ped)) { // nothing in his hands
		gHolstered = slot;
		api.SetCurrentWeapon(ped, 0);
	}
	if (state == kPedJump || state == kPedFall || state == kPedGetUp)
		SetPedState(ped, kPedIdle);
	PedFlags(ped) = (PedFlags(ped) | kPedStanding | kPedWasStanding) & ~(kPedInTheAir | kPedLanding);

	// Where the stick points, as the camera looks; faster sprinting.
	uint8_t *pad = api.GetPad(0);
	float sx = 0, sy = 0;
	if (!PadControlsDisabled(pad)) {
		sx = PadNew(pad, kPadLeftStickX) / 128.0f;
		sy = -PadNew(pad, kPadLeftStickY) / 128.0f; // up is forward
	}
	float push = std::min(1.0f, std::hypot(sx, sy));
	if (push < 0.12f)
		push = 0.0f;
	const bool sprint = push > 0.5f && PadNew(pad, kPadCross) && Stamina(ped) > 0.0f;
	const CVector camFlat = HeadingForward(CameraHeading()), camRight{camFlat.y, -camFlat.x, 0.0f};
	CVector &v = EntitySpeed(ped);
	const CVector &pos = EntityPos(ped);

	if (gUnder) {
		// Where the camera looks, up and down too (allowing for it looking down at him),
		// or straight up while the jump button is held.
		const bool up = PadNew(pad, kPadSquare) != 0;
		CVector want = gDir;
		float speed = 0.0f;
		if (gDiveLeft > 0.0f) {
			gDiveLeft -= dt;
			speed = 2.0f;
		} else {
			const CVector &front = *reinterpret_cast<const CVector *>(ActiveCam() + 0x168);
			const float pitch = std::clamp(std::asin(std::clamp(front.z, -1.0f, 1.0f)) + kCameraLift, -1.3f, 1.3f);
			const CVector ahead{camFlat.x * std::cos(pitch), camFlat.y * std::cos(pitch), std::sin(pitch)};
			want = Add(Scale(ahead, sy), Scale(camRight, sx));
			if (up)
				want = Add(Scale(want, 0.4f), {0.0f, 0.0f, 1.0f});
			speed = std::max(push * (sprint ? kUnderSprint : kUnderSpeed), up ? kUpSpeed : 0.0f);
		}
		gSpeed += std::clamp(speed - gSpeed, -kDecel * dt, kAccel * dt);
		if (Dot(want, want) > 1e-4f)
			gDir = Normalised(Lerp(gDir, Normalised(want), std::min(1.0f, dt * 4.0f)));
		if (std::hypot(gDir.x, gDir.y) > 0.15f)
			Turn(gHeading, HeadingOf(gDir.x, gDir.y), (sprint ? kSprintTurn : kTurn) * dt);
		FaceHeading(ped, gHeading);
		const float wantPitch = gSpeed > 0.2f ? std::asin(std::clamp(gDir.z, -1.0f, 1.0f)) : 0.0f;
		gPitch += (wantPitch - gPitch) * std::min(1.0f, dt * 3.0f);
		v = Scale(gDir, gSpeed / 50.0f);
		if (gSpeed < kDrift && gDiveLeft <= 0.0f)
			v.z = std::max(v.z, kDrift / 50.0f); // floating up
		const float floor = gBottom + kAboveBottom;
		if (pos.z + v.z * step < floor)
			v.z = std::max(v.z, (floor - pos.z) / step);
		if (gDiveLeft <= 0.0f && pos.z > gLevel - kSurfaceAt && v.z >= 0.0f) {
			gUnder = false; // up for air
			gPitch = 0.0f;
		}
		return;
	}

	gSpeed += std::clamp(push * (sprint ? kSprintSpeed : kSpeed) - gSpeed, -kDecel * dt, kAccel * dt);
	if (push > 0.0f)
		Turn(gHeading, HeadingOf(camFlat.x * sy + camRight.x * sx, camFlat.y * sy + camRight.y * sx),
		     (sprint ? kSprintTurn : kTurn) * dt);
	FaceHeading(ped, gHeading);

	// Along the surface, and back up to it (with a dip) after he falls in or comes up.
	const CVector f = HeadingForward(gHeading);
	const float rise = gLevel - kFloat - pos.z;
	v.z = std::clamp(v.z + (rise * 0.06f - v.z * 0.3f) * step, -0.4f, 0.06f);
	v.x = f.x * gSpeed / 50.0f;
	v.y = f.y * gSpeed / 50.0f;
}

float Pose(body::Pose &pose)
{
	const Clock::time_point now = Clock::now();
	const float dt = std::min(std::chrono::duration<float>(now - gLastPose).count(), 0.1f);
	gLastPose = now;
	gWeight = gSwimming ? std::min(1.0f, gWeight + dt / 0.35f) : std::max(0.0f, gWeight - dt / 0.3f);
	if (gWeight <= 0.0f)
		return 0.0f;
	const bool under = gSwimming && gUnder;
	gUnderBlend = under ? std::min(1.0f, gUnderBlend + dt / 0.4f) : std::max(0.0f, gUnderBlend - dt / 0.4f);
	gCrawl = gSpeed > kCrawlFrom ? std::min(1.0f, gCrawl + dt / 0.5f) : std::max(0.0f, gCrawl - dt / 0.6f);
	const float before = gCycle;
	gCycle += dt * (under ? 0.5f + 0.25f * gSpeed : 0.35f + 0.22f * gSpeed);
	if (gCycle >= 1.0f) {
		gCycle -= 1.0f;
		gCycles++;
	}
	gTread = std::fmod(gTread + dt * 0.8f, 1.0f);
	const body::Pose surface = Mix(Tread(gTread, true), Crawl(gCycle, gCycles % 2 == 0), Smooth(gCrawl));
	pose = gUnderBlend <= 0.0f
	           ? surface
	           : Mix(surface, Mix(Tread(gTread, false), Breaststroke(gCycle, gPitch), Smooth(gCrawl)),
	                 Smooth(gUnderBlend));
	// A splash as each hand goes in (the sound with the right one), and from the kick when he's going fast.
	if (gSwimming && !gUnder && !climb::Active() && gCrawl > 0.5f && gPed) {
		if (Passed(before, gCycle, 0.97f))
			Splash(gPed, pose, kHandR, 0.6f, true);
		if (Passed(before, gCycle, 0.47f))
			Splash(gPed, pose, kHandL, 0.6f, false);
		if (gSpeed > 2.0f && (Passed(before, gCycle, 0.2f) || Passed(before, gCycle, 0.7f)))
			Splash(gPed, pose, gCycle < 0.5f ? kFootR : kFootL, 0.35f, false);
	}
	return Smooth(gWeight);
}

void DrawUnderwater()
{
	const uint8_t *player = api.FindPlayerPed();
	if (!Swimming() && !(player && InSunkVehicle(player)))
		return;
	const CVector &cam = *reinterpret_cast<const CVector *>(api.theCamera + 0x30); // its matrix's position
	float level = 0.0f;
	if (!api.GetWaterLevel(cam.x, cam.y, cam.z, &level, true) || cam.z > level - 0.05f)
		return;
	const float W = float(ScreenWidth()), H = float(ScreenHeight());
	const CRGBA tint{kUnderwaterTint.r, kUnderwaterTint.g, kUnderwaterTint.b,
	                 uint8_t(std::min(170.0f, 95.0f + (level - cam.z) * 7.0f))};
	SetHudRenderStates();
	gCanvas.SetTexture(nullptr);
	gCanvas.Quad({0, 0}, {W, 0}, {W, H}, {0, H}, tint);
	gCanvas.Flush();
}

bool Install()
{
	if (!gConfig.swim)
		return true;
	if (!api.timeStep || !api.GetWaterLevel || !api.ProcessVerticalLine || !player::ControlHooked() ||
	    !player::DamageHooked() || !player::PoseHooked())
		return false;
	gInstalled = hooks::Install("_ZN4CPed15ProcessBuoyancyEv", Hook_ProcessBuoyancy, orig_ProcessBuoyancy);
	if (!gInstalled)
		return false;
	if (!hooks::Install("_ZN4CCam15IsTargetInWaterERK7CVector", Hook_IsTargetInWater, orig_IsTargetInWater))
		Log("swim: the camera stops following him in the water (re-run install.sh)");
	if (!hooks::Install("_ZN4CPed13PlayFootStepsEv", Hook_PlayFootSteps, orig_PlayFootSteps))
		Log("swim: his footsteps sound while he swims (re-run install.sh)");
	if (!hooks::Install("_ZN8CVehicle13CanPedExitCarEb", Hook_CanPedExitCar, orig_CanPedExitCar))
		Log("swim: he can't get out of a car in the water (re-run install.sh)");
	gAddParticle = reinterpret_cast<decltype(gAddParticle)>(
	    dlsym(RTLD_MAIN_ONLY, "_ZN9CParticle11AddParticleE13tParticleTypeRK7CVectorS3_P7CEntityfRKN2rw4RGBAEiiii"));
	gPlayOneShot = reinterpret_cast<decltype(gPlayOneShot)>(dlsym(RTLD_MAIN_ONLY, "_ZN8cDMAudio11PlayOneShotEitf"));
	Log("swim: Tommy swims and dives");
	return true;
}

} // namespace swim

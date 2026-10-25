#include "climb.h"
#include "config.h"
#include "draw.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "phone.h"
#include "player.h"
#include "prompt.h"
#include "swim.h"
#include "text.h"

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace game;
using namespace body;

namespace climb {
namespace {

using Clock = std::chrono::steady_clock;

enum class Kind { kNone, kVault, kStepUp, kMantle, kOutOfWater };

constexpr float kFeet = 1.04f;   // his position above his feet (FEET_OFFSET)
// How far ahead a wall counts: standing, running (faster than kRunning m/s), mid-jump.
constexpr float kReach = 1.0f, kRunReach = 1.5f, kAirReach = 0.85f, kRunning = 3.0f;
constexpr float kStepFrom = 0.5f, kStepTo = 1.3f, kMantleTo = 2.4f; // above his feet
constexpr float kAirFrom = 0.4f, kAirTo = 2.0f;                     // ... mid-jump: a ledge he can grab
constexpr float kOutFrom = -0.3f, kOutTo = 1.9f;                    // above the water
constexpr float kGrabFor = 1.3f; // s after he jumps that he grabs a ledge he reaches
constexpr float kStandoff = 0.4f; // from the wall's face while he climbs it
constexpr float kStillVehicle = 0.02f; // a vehicle slower than this (per 1/50 s) is a ledge
// The player's controls' CPed::SetJump calls (PlayerControlZelda,
// PlayerControl1stPersonRunAround), as return addresses.
constexpr uint32_t kJumpSites[] = {0x1d5644, 0x1d2bcc};

struct Ledge {
	Kind kind = Kind::kNone;
	CVector edge;    // where the top meets the wall's face, in front of him
	CVector inward;  // into the wall, level
	float top = 0;   // the top's height
	float height = 0;
	CVector landing; // a vault's: his position as he lands
};

struct Climb {
	Kind kind = Kind::kNone;
	float time = 0, length = 1;
	CVector start, edge, inward, landing;
	float top = 0, heading = 0, exitSpeed = 0;
	uint32_t collision = 0;
	uint8_t *ped = nullptr;
} gClimb;

void (*orig_SetJump)(uint8_t *ped);
bool gInstalled, gActive;
float gGrabFor;        // s left of a jump that grabs a ledge he reaches
int gWhyLogged;
float gProgress;       // of the climb, 0..1, for the pose
float gEnding;         // its pose fading out once done, 1..0
Clock::time_point gLastPose;
int gLogged;
// The climb-out prompt.
bool gCanClimbOut;
int gCheckIn;
float gPromptAlpha;
Clock::time_point gLastDraw;
Im2DCanvas gCanvas;

struct Hit {
	CVector point, normal;
	uint8_t *entity = nullptr;
};

bool Still(const uint8_t *entity)
{
	if (!entity || !IsVehicle(entity))
		return true;
	const CVector &v = *reinterpret_cast<const CVector *>(entity + 0x84);
	return std::sqrt(Dot(v, v)) < kStillVehicle;
}

// The first building, object or (`vehicles`) standing vehicle on a line.
bool Line(const CVector &a, const CVector &b, bool vehicles, Hit &hit)
{
	uint8_t point[64] = {};
	if (!api.ProcessLineOfSight(a, b, point, hit.entity, true, vehicles, false, true, false, false, false, false))
		return false;
	hit.point = *reinterpret_cast<const CVector *>(point);
	hit.normal = *reinterpret_cast<const CVector *>(point + 0x10);
	return Still(hit.entity);
}

bool Down(const CVector &from, float toZ, bool vehicles, Hit &hit)
{
	uint8_t point[64] = {};
	if (!api.ProcessVerticalLine(from, toZ, point, hit.entity, true, vehicles, false, true, false, false, nullptr))
		return false;
	hit.point = *reinterpret_cast<const CVector *>(point);
	hit.normal = *reinterpret_cast<const CVector *>(point + 0x10);
	return Still(hit.entity);
}

bool Clear(const CVector &a, const CVector &b)
{
	return api.GetIsLineOfSightClear(a, b, true, true, false, true, false, false, false);
}

CVector At(const CVector &xy, float z) { return {xy.x, xy.y, z}; }

// Where he looks for a ledge from: standing, mid-jump (grabbing one he
// reaches), or swimming.
enum class From { kGround, kAir, kWater };

// What he could climb in front of him, if anything; `why` says what was missing.
bool FindLedge(uint8_t *ped, From from, Ledge &out, const char *&why)
{
	const CVector p = EntityPos(ped);
	CVector f = EntityForward(ped);
	f.z = 0;
	why = "not facing anywhere";
	if (Dot(f, f) < 0.25f)
		return false;
	f = Normalised(f);
	const bool water = from == From::kWater;
	const float base = water ? swim::Surface() : p.z - kFeet; // what heights are measured from
	const float lowest = water ? kOutFrom : from == From::kAir ? kAirFrom : kStepFrom;
	const float highest = water ? kOutTo : from == From::kAir ? kAirTo : kMantleTo;
	const CVector &v = EntitySpeed(ped);
	const float reach = from == From::kAir ? kAirReach : std::hypot(v.x, v.y) * 50.0f > kRunning ? kRunReach : kReach;

	// The wall: the nearest thing straight ahead, from about his knees to his reach.
	Hit wall;
	float dist = reach;
	bool found = false;
	for (float h = std::max(lowest, -0.2f) + 0.1f; h <= highest + 0.05f; h += 0.35f) {
		const CVector a{p.x, p.y, base + h};
		Hit hit;
		if (Line(a, Add(a, Scale(f, reach)), water, hit)) {
			const float d = Dot(Sub(hit.point, a), f);
			if (d < dist) {
				dist = d;
				wall = hit;
				found = true;
			}
		}
	}
	why = "no wall in reach";
	if (!found)
		return false;
	why = "the wall slopes";
	if (std::fabs(wall.normal.z) > 0.5f)
		return false;
	const CVector in = Normalised({-wall.normal.x, -wall.normal.y, 0});
	why = "the wall is side-on";
	if (Dot(in, f) < 0.6f)
		return false;

	// Its top: straight down just past the face.
	Hit top;
	const CVector face = At(wall.point, 0);
	why = "no top to the wall (too high?)";
	if (!Down(At(Add(face, Scale(in, 0.25f)), base + highest + 0.6f), base + lowest - 0.6f, water, top))
		return false;
	why = "the top slopes";
	if (top.normal.z < 0.75f)
		return false;
	const float height = top.point.z - base;
	why = height > highest ? "too high" : "too low";
	if (height < lowest || height > highest)
		return false;
	const float topZ = top.point.z;

	// Room above him on the way up.
	why = "something above him";
	if (!Clear({p.x, p.y, p.z + 0.6f}, {p.x, p.y, topZ + 1.85f}))
		return false;
	const CVector below = Sub(face, Scale(in, kStandoff));
	Ledge ledge;
	ledge.edge = At(face, topZ);
	ledge.inward = in;
	ledge.top = topZ;
	ledge.height = height;

	// Deep enough to stand on, or a wall to go over?
	Hit far;
	const bool thick = Down(At(Add(face, Scale(in, 0.7f)), topZ + 0.4f), topZ - 0.4f, water, far) &&
	                   std::fabs(far.point.z - topZ) < 0.25f;
	if (thick) {
		// Room to stand where he'll get up, and to get there over the edge.
		const CVector stand = At(Add(face, Scale(in, 0.45f)), topZ + kFeet);
		why = "no room to stand on top";
		for (float z : {topZ + 0.5f, topZ + 1.25f})
			if (api.TestSphereAgainstWorld({stand.x, stand.y, z}, 0.3f, ped, true, true, false, true, false, false))
				return false;
		why = "something over the edge";
		if (!Clear(At(below, topZ + 1.3f), At(stand, topZ + 1.3f)))
			return false;
		ledge.kind = water ? Kind::kOutOfWater
		             : from == From::kGround && height <= kStepTo ? Kind::kStepUp
		                                                           : Kind::kMantle;
	} else {
		why = "too thin to stand on";
		if (from != From::kGround || height > kStepTo)
			return false;
		// A vault: somewhere to land not far below the top, and nothing in the way over it.
		Hit ground;
		const CVector beyond = Add(face, Scale(in, 1.0f));
		why = "nowhere to land beyond it";
		if (!Down(At(beyond, topZ + 0.3f), topZ - 4.0f, false, ground) || ground.point.z > topZ - 0.4f ||
		    ground.normal.z < 0.7f || !Clear(At(below, topZ + 0.7f), At(beyond, topZ + 0.7f)))
			return false;
		ledge.kind = Kind::kVault;
		ledge.landing = At(Add(beyond, Scale(in, 0.2f)), ground.point.z + kFeet);
	}
	out = ledge;
	return true;
}

const char *Name(Kind k)
{
	switch (k) {
	case Kind::kVault: return "vault";
	case Kind::kStepUp: return "step up";
	case Kind::kMantle: return "climb";
	case Kind::kOutOfWater: return "climb out";
	default: return "none";
	}
}

void Start(uint8_t *ped, const Ledge &l)
{
	Climb &c = gClimb;
	c = Climb{};
	c.kind = l.kind;
	c.start = EntityPos(ped);
	c.edge = l.edge;
	c.inward = l.inward;
	c.landing = l.landing;
	c.top = l.top;
	c.heading = HeadingOf(l.inward.x, l.inward.y);
	const CVector &v = EntitySpeed(ped);
	const float speed = std::hypot(v.x, v.y) * 50.0f;
	switch (l.kind) {
	case Kind::kVault:
		c.length = speed > 3.0f ? 0.5f : 0.6f;
		c.exitSpeed = std::clamp(speed, 2.0f, 6.0f);
		break;
	case Kind::kStepUp: c.length = 0.6f; break;
	case Kind::kMantle: c.length = 0.8f + 0.25f * std::max(0.0f, l.top + kFeet - c.start.z); break; // by the rise
	default: c.length = 0.95f + 0.3f * std::clamp(l.height, 0.0f, kOutTo); break;
	}
	// Out of the jump he may be in (JUMP_launch to FALL_collapse).
	if (uint8_t *clump = EntityClump(ped))
		for (uint32_t anim = 141; anim <= 147; anim++)
			if (uint8_t *assoc = api.GetAssociation(clump, anim))
				*reinterpret_cast<float *>(assoc + 0x2c) = -8.0f; // fading out
	gGrabFor = 0;
	c.ped = ped;
	c.collision = EntityFlags(ped) & kEntityUsesCollision;
	EntityFlags(ped) &= ~kEntityUsesCollision;
	FaceHeading(ped, c.heading);
	gActive = true;
	gProgress = 0;
	gEnding = 0;
	if (gLogged < 5) {
		gLogged++;
		Log("climb: %s, %.2f m up, %.1f s", Name(l.kind), l.height, c.length);
	}
}

void Finish(uint8_t *ped)
{
	gActive = false;
	gEnding = 1;
	EntityFlags(ped) |= gClimb.collision;
	const CVector f = HeadingForward(gClimb.heading);
	EntitySpeed(ped) = Scale(f, gClimb.exitSpeed / 50.0f);
	PedFlags(ped) = (PedFlags(ped) | kPedStanding | kPedWasStanding) & ~(kPedInTheAir | kPedLanding);
}

float Part(float s, float from, float to) { return Smooth((s - from) / (to - from)); }
float Linear(float s, float from, float to) { return std::clamp((s - from) / (to - from), 0.0f, 1.0f); }

// Where he is `s` of the way through the climb.
CVector Path(float s)
{
	const Climb &c = gClimb;
	const CVector face = Sub(c.edge, Scale(c.inward, kStandoff));
	const CVector stand = At(Add(c.edge, Scale(c.inward, 0.45f)), c.top + kFeet);
	switch (c.kind) {
	case Kind::kVault: {
		const CVector over = At(Add(c.edge, Scale(c.inward, 0.15f)), c.top + 0.55f);
		if (s < 0.45f) {
			const float t = s / 0.45f;
			return At(Lerp(c.start, over, t), c.start.z + (over.z - c.start.z) * (1 - (1 - t) * (1 - t)));
		}
		const float t = (s - 0.45f) / 0.55f;
		return At(Lerp(over, c.landing, t), over.z + (c.landing.z - over.z) * t * t);
	}
	case Kind::kStepUp: {
		const CVector lifted = At(Add(c.edge, Scale(c.inward, -0.15f)), c.top + kFeet + 0.1f);
		if (s < 0.5f)
			return At(Lerp(c.start, lifted, Linear(s, 0, 0.5f)), c.start.z + (lifted.z - c.start.z) * Part(s, 0, 0.5f));
		return Lerp(lifted, stand, Part(s, 0.5f, 1));
	}
	default: { // pulling up
		const float hang = std::max(c.start.z, c.top - 1.05f); // hands on the edge, arms up
		if (s < 0.18f)
			return At(Lerp(c.start, face, Part(s, 0, 0.18f)), c.start.z + (hang - c.start.z) * Part(s, 0, 0.18f));
		if (s < 0.55f)
			return At(face, hang + (c.top - 0.15f - hang) * Part(s, 0.18f, 0.55f));
		const CVector knee = At(Add(c.edge, Scale(c.inward, 0.05f)), c.top + 0.78f);
		if (s < 0.8f)
			return Lerp(At(face, c.top - 0.15f), knee, Part(s, 0.55f, 0.8f));
		return Lerp(knee, stand, Part(s, 0.8f, 1));
	}
	}
}

// Two-bone reach: the elbow, bent towards `pole`, for the hand at `hand` (it
// stops short where the arm can't reach).
void Reach(body::Pose &p, Bone upper, Bone fore, CVector hand, const CVector &pole, float weight)
{
	if (weight <= 0.0f)
		return;
	const CVector shoulder = Joint(p, upper);
	const float a = Length(upper), b = Length(fore);
	CVector d = Sub(hand, shoulder);
	const float len = std::clamp(std::sqrt(Dot(d, d)), std::fabs(a - b) + 0.01f, a + b - 0.002f);
	const CVector u = Normalised(d);
	hand = Add(shoulder, Scale(u, len));
	const float x = (a * a - b * b + len * len) / (2 * len), y = std::sqrt(std::max(0.0f, a * a - x * x));
	const CVector v = Normalised(Sub(pole, Scale(u, Dot(pole, u))));
	const CVector elbow = Add(shoulder, Add(Scale(u, x), Scale(v, y)));
	body::Pose reached = p;
	Point(reached, upper, Sub(elbow, shoulder));
	Point(reached, fore, Sub(hand, elbow));
	const body::Pose mixed = Mix(p, reached, weight);
	p.Turn(upper, mixed.turn[upper]);
	p.Turn(fore, mixed.turn[fore]);
}

void Leg(body::Pose &p, Bone thigh, Bone calf, float hip, float knee, float swing = 0)
{
	p.Turn(thigh, Z(swing) * X(hip));
	p.Turn(calf, Z(swing) * X(hip - knee));
}

// The climb's pose, `s` of the way through, his hands on the edge.
body::Pose ClimbPose(uint8_t *ped, float s)
{
	body::Pose p;
	const Climb &c = gClimb;
	const CVector at = EntityPos(ped), f = HeadingForward(c.heading), r{f.y, -f.x, 0};
	auto local = [&](const CVector &w) {
		const CVector d = Sub(w, at);
		return CVector{Dot(d, r), Dot(d, f), d.z};
	};
	auto hand = [&](float side, float along) { // a wrist just above the edge
		return local(Add(Add(Add(c.edge, Scale(r, side)), Scale(c.inward, along)), {0.0f, 0.0f, 0.05f}));
	};
	switch (c.kind) {
	case Kind::kVault: {
		const float lean = 0.35f * Part(s, 0.1f, 0.3f) * (1 - Part(s, 0.55f, 0.8f));
		p.Turn(kSpine, X(-lean));
		const float tuck = Part(s, 0.15f, 0.35f) * (1 - Part(s, 0.6f, 0.85f));
		Leg(p, kThighL, kCalfL, 1.1f * tuck, 1.5f * tuck, -0.5f * tuck);
		Leg(p, kThighR, kCalfR, 1.0f * tuck, 1.6f * tuck, -0.5f * tuck);
		const float out = Part(s, 0.1f, 0.3f) * (1 - Part(s, 0.6f, 0.9f));
		if (out > 0)
			p.Turn(kUpperArmR, Slerp(X(-lean), Arc(RestDirection(kUpperArmR), {0.9f, 0.1f, 0.3f}), out));
		Reach(p, kUpperArmL, kForearmL, hand(-0.12f, 0.12f), {-1, -0.5f, 0}, Part(s, 0.02f, 0.15f) * (1 - Part(s, 0.45f, 0.6f)));
		break;
	}
	case Kind::kStepUp: {
		const float lean = 0.3f * Part(s, 0.05f, 0.3f) * (1 - Part(s, 0.6f, 0.95f));
		p.Turn(kSpine, X(-lean));
		const float knee = Part(s, 0.05f, 0.3f) * (1 - Part(s, 0.5f, 0.85f));
		Leg(p, kThighR, kCalfR, 1.25f * knee, 1.4f * knee);
		Leg(p, kThighL, kCalfL, -0.15f * knee, 0.3f * knee);
		Reach(p, kUpperArmR, kForearmR, hand(0.2f, 0.1f), {1, -0.6f, 0}, Part(s, 0.0f, 0.15f) * (1 - Part(s, 0.5f, 0.7f)));
		break;
	}
	default: { // pulling up: arms up to the edge, chest over it, a knee up, standing
		const float lean = 0.45f * Part(s, 0.15f, 0.45f) * (1 - Part(s, 0.75f, 1.0f));
		p.Turn(kSpine, X(-lean));
		const float knee = Part(s, 0.5f, 0.68f) * (1 - Part(s, 0.82f, 0.98f));
		const float hang = 1 - Part(s, 0.8f, 1.0f);
		Leg(p, kThighR, kCalfR, 0.15f * hang + 1.35f * knee, 0.3f * hang + 1.4f * knee);
		Leg(p, kThighL, kCalfL, 0.1f * hang, 0.35f * hang);
		const float hold = Part(s, 0.0f, 0.15f) * (1 - Part(s, 0.8f, 0.95f));
		const CVector pole{0, -0.4f - 0.6f * Part(s, 0.3f, 0.6f), -0.2f}; // elbows out, then back as he presses up
		Reach(p, kUpperArmL, kForearmL, hand(-0.22f, 0.06f), Add(pole, {-1, 0, 0}), hold);
		Reach(p, kUpperArmR, kForearmR, hand(0.22f, 0.06f), Add(pole, {1, 0, 0}), hold);
		break;
	}
	}
	return p;
}

void Hook_SetJump(uint8_t *ped)
{
	if (gInstalled && ped == api.FindPlayerPed()) {
		if (gActive)
			return;
		const uint32_t site = uint32_t(reinterpret_cast<uintptr_t>(__builtin_return_address(0)) -
		                               reinterpret_cast<uintptr_t>(hooks::ImageBase()));
		const bool pressed = site == kJumpSites[0] || site == kJumpSites[1];
		if (swim::Swimming()) { // no jumping out of the water: a climb out, or a dive
			Ledge ledge;
			const char *why;
			if (pressed && !swim::Underwater()) {
				if (FindLedge(ped, From::kWater, ledge, why))
					Start(ped, ledge);
				else
					swim::Dive(ped);
			}
			return;
		}
		if (pressed && !PedInVehicle(ped)) {
			Ledge ledge;
			const char *why;
			if (FindLedge(ped, From::kGround, ledge, why)) {
				Start(ped, ledge);
				return;
			}
			if (gWhyLogged < 20) {
				gWhyLogged++;
				Log("climb: nothing to climb from the jump (%s); grabbing a ledge if he reaches one", why);
			}
			gGrabFor = kGrabFor;
		}
	}
	orig_SetJump(ped);
}

} // namespace

bool Active() { return gActive; }
bool Posing() { return gActive || gEnding > 0; }

void BeforeControl(uint8_t *ped)
{
	if (!gActive)
		return;
	if (ped != gClimb.ped) { // a new player ped: whatever the old one was doing is gone
		gActive = false;
		gEnding = 0;
		return;
	}
	const int32_t state = PedState(ped);
	if (Health(ped) <= 0.0f || state == kPedDie || state == kPedDead || PedInVehicle(ped) ||
	    PadControlsDisabled(api.GetPad(0)) || gClimb.time > gClimb.length + 1.0f) {
		Finish(ped);
		gEnding = 0;
		return;
	}
	if (state == kPedJump || state == kPedFall || state == kPedGetUp)
		SetPedState(ped, kPedIdle);
	PedFlags(ped) = (PedFlags(ped) | kPedStanding | kPedWasStanding) & ~(kPedInTheAir | kPedLanding);
}

void AfterControl(uint8_t *ped)
{
	if (!gActive && gGrabFor > 0.0f && ped == api.FindPlayerPed()) {
		// Mid-jump: a ledge he reaches, he grabs.
		gGrabFor -= std::clamp(*api.timeStep, 0.01f, 3.0f) / 50.0f;
		const bool airborne = PedState(ped) == kPedJump || (PedFlags(ped) & kPedInTheAir);
		Ledge ledge;
		const char *why;
		if (!airborne || swim::Swimming() || PedInVehicle(ped) || Health(ped) <= 0.0f)
			gGrabFor = std::min(gGrabFor, 0.25f); // still taking off, or done
		else if (FindLedge(ped, From::kAir, ledge, why))
			Start(ped, ledge);
	}
	if (!gActive || ped != gClimb.ped)
		return;
	if (gClimb.time >= gClimb.length) { // he got there last frame
		Finish(ped);
		return;
	}
	const float step = std::clamp(*api.timeStep, 0.01f, 3.0f);
	gClimb.time += step / 50.0f;
	const float s = std::min(1.0f, gClimb.time / gClimb.length);
	gProgress = s;
	// Without collision nothing moves him by his speed (CPhysical::ProcessCollision
	// only re-sorts him into the world's sectors), so he's put where the climb has
	// got to; the speed is only for whatever reads it.
	CVector &at = *reinterpret_cast<CVector *>(ped + 0x38); // his matrix's position
	const CVector to = Path(s);
	EntitySpeed(ped) = Scale(Sub(to, at), 1.0f / step);
	at = to;
	FaceHeading(ped, gClimb.heading);
	PedFlags(ped) = (PedFlags(ped) | kPedStanding | kPedWasStanding) & ~(kPedInTheAir | kPedLanding);
}

float Pose(body::Pose &pose, float under)
{
	const Clock::time_point now = Clock::now();
	const float dt = std::min(std::chrono::duration<float>(now - gLastPose).count(), 0.1f);
	gLastPose = now;
	uint8_t *ped = api.FindPlayerPed();
	if (!ped || (!gActive && gEnding <= 0))
		return under;
	const float s = gActive ? gProgress : 1.0f;
	if (!gActive)
		gEnding = std::max(0.0f, gEnding - dt / 0.25f);
	const body::Pose climbing = ClimbPose(ped, s);
	const float in = Part(s, 0.0f, 0.15f), weight = gActive ? in : Smooth(gEnding);
	if (under > 0.0f) { // from the water: out of the swimming pose
		pose = Mix(pose, climbing, gActive ? in : 1.0f);
		return std::max(under, weight);
	}
	pose = climbing;
	return weight;
}

void Draw()
{
	const Clock::time_point now = Clock::now();
	const float dt = std::min(std::chrono::duration<float>(now - gLastDraw).count(), 0.1f);
	gLastDraw = now;
	uint8_t *ped = api.FindPlayerPed();
	const bool swimming = gInstalled && ped && swim::Swimming() && !swim::Underwater() && !gActive;
	if (swimming && --gCheckIn <= 0) {
		gCheckIn = 8;
		Ledge ledge;
		const char *why;
		gCanClimbOut = !swim::Underwater() && FindLedge(ped, From::kWater, ledge, why);
	}
	const bool show = swimming && gCanClimbOut && !PadControlsDisabled(api.GetPad(0));
	gPromptAlpha = show ? std::min(1.0f, gPromptAlpha + dt / 0.15f) : std::max(0.0f, gPromptAlpha - dt / 0.15f);
	if (gPromptAlpha <= 0.0f)
		return;
	const float H = float(ScreenHeight()), h = prompt::Height(H), bottom = H - gConfig.margin * H;
	SetHudRenderStates();
	gCanvas.SetTexture(nullptr);
	prompt::Draw(gCanvas, phone::LeftEdge(), bottom - h * 0.5f, H, prompt::Button::kFaceWest, "CLIMB", gPromptAlpha);
	text::Flush();
}

bool Install()
{
	if (!gConfig.climb)
		return true;
	if (!api.timeStep || !api.ProcessVerticalLine || !api.TestSphereAgainstWorld || !player::ControlHooked() ||
	    !player::PoseHooked())
		return false;
	for (uint32_t site : kJumpSites)
		if (!hooks::IsCallTo(site, "_ZN4CPed7SetJumpEv"))
			return false;
	gInstalled = hooks::Install("_ZN4CPed7SetJumpEv", Hook_SetJump, orig_SetJump);
	if (gInstalled)
		Log("climb: on the jump button");
	return gInstalled;
}

} // namespace climb

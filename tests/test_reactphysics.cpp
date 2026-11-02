#include "reactphysics.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
using namespace reactphysics;

static Pose Standing()
{
	return {{{0, 0, 0.95f},
			 {0, 0, 1.35f},
			 {0, 0, 1.7f},
			 {-0.22f, 0, 1.4f},
			 {-0.35f, 0, 1.1f},
			 {-0.4f, 0, 0.8f},
			 {0.22f, 0, 1.4f},
			 {0.35f, 0, 1.1f},
			 {0.4f, 0, 0.8f},
			 {-0.12f, 0, 0.9f},
			 {-0.12f, 0, 0.5f},
			 {-0.12f, 0, 0.1f},
			 {0.12f, 0, 0.9f},
			 {0.12f, 0, 0.5f},
			 {0.12f, 0, 0.1f}}};
}
static Contact FloorAndWall(Vec3, Vec3 p, float radius, void *)
{
	Contact c{p, {}, false};
	if (p.z < radius) {
		c.position.z = radius;
		c.normal = {0, 0, 1};
		c.hit = true;
	}
	if (p.x > 0.7f - radius) {
		c.position.x = 0.7f - radius;
		c.normal = {-1, 0, 0};
		c.hit = true;
	}
	return c;
}
static Contact OptionalFloor(Vec3 from, Vec3 to, float radius, void *context)
{
	return *static_cast<bool *>(context) ? FloorAndWall(from, to, radius, nullptr) : Contact{to, {}, false};
}
static float Distance(Vec3 a, Vec3 b)
{
	return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}
static Contact Slope(Vec3, Vec3 p, float radius, void *)
{
	const Vec3 n = {-0.242535625f, 0, 0.970142500f};
	const float separation = p.x * n.x + p.z * n.z;
	if (separation >= radius)
		return {p, {}, false};
	const float push = radius - separation;
	return {{p.x + n.x * push, p.y, p.z + n.z * push}, n, true};
}
static Contact GentleSlope(Vec3, Vec3 p, float radius, void *)
{
	const Vec3 n = {-.049937617f, 0, .998752338f};
	const float separation = p.x * n.x + p.z * n.z;
	if (separation >= radius)
		return {p, {}, false};
	const float push = radius - separation;
	return {{p.x + n.x * push, p.y, p.z + n.z * push}, n, true};
}
static Pose RootRelativeGetUp(const Pose &standing, Vec3 root, float seconds)
{
	// Native samples have a prone torso with feet .14 m above its pelvis.
	// A get-up tucks the feet underneath before lifting off torso support.
	const float tuck = std::clamp((seconds - .35f) / .45f, 0.0f, 1.0f);
	const float rise = std::clamp((seconds - .8f) / .9f, 0.0f, 1.0f);
	const float angle = (1 - rise) * 1.46f;
	Pose target = standing;
	for (Vec3 &p : target) {
		const float y = p.y - standing[Pelvis].y, z = p.z - standing[Pelvis].z;
		p = {root.x + p.x, root.y + y * std::cos(angle) - z * std::sin(angle),
			 root.z + y * std::sin(angle) + z * std::cos(angle)};
	}
	for (auto leg : {std::array<Joint, 3>{HipL, KneeL, FootL}, {HipR, KneeR, FootR}}) {
		const Vec3 hip = target[leg[0]];
		Vec3 &foot = target[leg[2]], &knee = target[leg[1]];
		foot = {hip.x, root.y + .78f * (1 - tuck), root.z + (1 - tuck) * .14f - tuck * (.1f + .74f * rise)};
		const float dy = foot.y - hip.y, dz = foot.z - hip.z;
		const float distance = std::sqrt(dy * dy + dz * dz);
		const float bend = std::sqrt(std::max(0.0f, .4f * .4f - distance * distance * .25f));
		knee = {hip.x, (hip.y + foot.y) * .5f - dz / distance * bend,
				(hip.z + foot.z) * .5f + dy / distance * bend};
	}
	// The entity root is placed using its original standing pelvis offset;
	// native prone animation lowers the pelvis about one metre below it.
	for (Vec3 &p : target)
		p.z -= 1.02f * (1 - rise);
	return target;
}
int main()
{
	int fail = 0;
	const Pose pose = Standing();
	Body pointHit, singleHit;
	pointHit.Reset(pose);
	singleHit.Reset(pose);
	const Vec3 pointDelta{2,1,-.3f};
	pointHit.Impact(KneeL,FootL,.8f,pointDelta);
	singleHit.Impulse(FootL,pointDelta);
	const Vec3 kneeVelocity=pointHit.Velocity(KneeL), footVelocity=pointHit.Velocity(FootL);
	const Vec3 pointVelocity{.2f*kneeVelocity.x+.8f*footVelocity.x,.2f*kneeVelocity.y+.8f*footVelocity.y,
		.2f*kneeVelocity.z+.8f*footVelocity.z};
	if(Distance(pointVelocity,pointDelta)>.0001f || footVelocity.x<=kneeVelocity.x ||
		std::fabs(pointHit.Balance()-singleHit.Balance())>.0001f || Distance(pointHit.Velocity(FootR),{})>.0001f) {
		puts("FAIL: localized segment hit lost its point velocity, side, or counted balance twice");
		fail++;
	}
	// A native knockdown (#25): any push, even a gentle trip, takes the body off
	// its feet at once (the game has already decided he falls), carrying exactly
	// that push, alive; a non-finite push is ignored.
	Body knocked, ignored;
	knocked.Reset(pose);
	knocked.Knockdown({0, -1.5f, 0});
	ignored.Reset(pose);
	ignored.Knockdown({std::nanf(""), 0, 0});
	if (knocked.State() != Mode::Falling || knocked.Dead() || !knocked.Active() ||
	    Distance(knocked.Velocity(Pelvis), {0, -1.5f, 0}) > 1e-4f || Distance(knocked.Velocity(HandL), {0, -1.5f, 0}) > 1e-4f ||
	    ignored.Active() || ignored.State() != Mode::Balanced) {
		puts("FAIL: a native knockdown must take the living body off its feet with exactly its push");
		fail++;
	}
	Body local;
	local.Reset(pose);
	local.Impulse(ShoulderL, {-1.0f, 0, 0});
	if (!(local.Velocity(ShoulderL).x < local.Velocity(ShoulderR).x - 0.1f)) {
		puts("FAIL: impacts must act on their own limb");
		fail++;
	}
	for (int i = 0; i < 180; i++)
		local.Advance(1.0f / 60, pose, FloorAndWall, nullptr);
	if (local.Dead() || local.State() == Mode::Falling || local.Balance() < 0.7f) {
		puts("FAIL: ordinary nonfatal hit should recover");
		fail++;
	}
	// Actual nondamaging native contacts currently share the damage impulse
	// path. Repeated modest shin/car contacts should recruit support and
	// protective hands, rather than spend a synthetic health-like balance.
	Body supported;
	supported.Reset(pose);
	bool minorFall = false;
	float handReach = 0, footStep = 0;
	for (int frame=0;frame<100;frame++) {
		if (frame%15==0 && frame<76)
			supported.ContactResponse(KneeL,{0,1.2f,0},{0,1,0});
		supported.Advance(1.0f/60,pose,FloorAndWall,nullptr);
		minorFall |= supported.State()==Mode::Falling;
		handReach=std::max(handReach,std::fabs(supported.Positions()[HandL].y-pose[HandL].y));
		footStep=std::max(footStep,supported.Positions()[FootR].y-pose[FootR].y);
	}
	if(minorFall || supported.Positions()[Pelvis].z<.8f || handReach<.025f || footStep<.015f) {
		printf("FAIL: supported minor contacts collapsed or failed to brace/step: fall=%d pelvis=%.3f hand=%.3f step=%.3f\n",minorFall,supported.Positions()[Pelvis].z,handReach,footStep);
		fail++;
	}
	Body obstruction;
	obstruction.Reset(pose);
	obstruction.ContactResponse(FootL,{0,-3,0},{0,-1,0},{0,3,0});
	bool obstructionFall=false;
	for(int frame=0;frame<30;frame++) {
		obstruction.Advance(1.0f/60,pose,FloorAndWall,nullptr);
		obstructionFall |= obstruction.State()==Mode::Falling;
	}
	if(!obstructionFall) {
		puts("FAIL: a stopped foot beneath moving native COM never defeats support");
		fail++;
	}
	Body movingSupport;
	movingSupport.Reset(pose,{0,3,0});
	movingSupport.ContactResponse(Chest,{.4f,0,0},{1,0,0},{0,3,0});
	bool movingFall=false;
	for(int frame=0;frame<90;frame++) {
		Pose walking=pose;
		for(Vec3 &p:walking) p.y+=3.0f*(frame+1)/60;
		movingSupport.Advance(1.0f/60,walking,FloorAndWall,nullptr);
		movingFall |= movingSupport.State()==Mode::Falling;
	}
	if(movingFall || std::fabs(movingSupport.Positions()[Pelvis].y-4.5f)>.2f) {
		printf("FAIL: supported native locomotion lost mesh support or trailed root: fall=%d pelvisY=%.3f\n",movingFall,movingSupport.Positions()[Pelvis].y);
		fail++;
	}
	Body repeated;
	repeated.Reset(pose);
	for (int i = 0; i < 4; i++)
		repeated.Impulse(KneeL, {0, -1.4f, 0});
	if (repeated.State() != Mode::Falling) {
		puts("FAIL: repeated leg hits never defeat balance");
		fail++;
	}
	for (int i = 0; i < 600; i++)
		repeated.Advance(1.0f / 60, pose, FloorAndWall, nullptr);
	if (repeated.Dead() || repeated.State() == Mode::Falling || repeated.Positions()[Pelvis].z < .8f) {
		puts("FAIL: a surviving fallen NPC never stands back up");
		fail++;
	}
	// In game, animation targets are rebuilt from the physical root. A
	// fixed-world target hides a recovery feedback loop that leaves him low.
	Body feedback;
	feedback.Reset(pose);
	for (int i = 0; i < 4; i++)
		feedback.Impulse(KneeL, {0, -1.4f, 0});
	for (int i = 0; i < 600; i++) {
		Pose target = pose;
		const Vec3 root = feedback.Positions()[Pelvis];
		for (Vec3 &p : target) {
			p.x += root.x - pose[Pelvis].x;
			p.y += root.y - pose[Pelvis].y;
			p.z += root.z - pose[Pelvis].z;
		}
		feedback.Advance(1.0f / 60, target, FloorAndWall, nullptr);
	}
	if (feedback.Positions()[Pelvis].z < .8f || feedback.Active()) {
		puts("FAIL: root-relative animation feedback left recovery collapsed");
		fail++;
	}
	for (Sweep floor : {FloorAndWall, GentleSlope}) {
		Body getUp;
		getUp.Reset(pose);
		for (int i = 0; i < 4; i++)
			getUp.Impulse(KneeL, {0, -1.4f, 0});
		bool beganRecovery = false, fellAgain = false, crossedSurface = false;
		float recoverySeconds = 0, earlyPelvisHeight = 0;
		Mode previous = getUp.State();
		for (int i = 0; i < 600; i++) {
			if (getUp.State() == Mode::Recovering && previous != Mode::Recovering) {
				beganRecovery = true;
				recoverySeconds = 0;
			}
			previous = getUp.State();
			const Pose target = RootRelativeGetUp(pose, getUp.Positions()[Pelvis], recoverySeconds);
			getUp.Advance(1.0f / 60, target, floor, nullptr);
			for (int joint = 0; joint < Count; joint++) {
				const Vec3 p = getUp.Positions()[size_t(joint)];
				crossedSurface |= Distance(p, floor(p, p, Body::Radius(Joint(joint)), nullptr).position) > .001f;
			}
			if (beganRecovery) {
				if (recoverySeconds < .35f)
					earlyPelvisHeight = std::max(earlyPelvisHeight, getUp.Positions()[Pelvis].z);
				fellAgain |= getUp.State() == Mode::Falling;
				recoverySeconds += 1.0f / 60;
			}
		}
		if (!beganRecovery || fellAgain || earlyPelvisHeight > .45f || getUp.Active() ||
			getUp.Positions()[Pelvis].z < .8f || crossedSurface) {
			printf("FAIL: native prone-to-upright GetUp lost ground support (slope=%d began=%d fell=%d "
				   "earlypelvis=%.3f active=%d pelvis=%.3f crossed=%d)\n", floor == GentleSlope, beganRecovery,
				   fellAgain, earlyPelvisHeight, getUp.Active(), getUp.Positions()[Pelvis].z, crossedSurface);
			fail++;
		}
	}
	Body lostSupport;
	lostSupport.Reset(pose);
	for (int i = 0; i < 4; i++)
		lostSupport.Impulse(KneeL, {0, -1.4f, 0});
	bool floorPresent = true;
	for (int i = 0; i < 360 && lostSupport.State() != Mode::Recovering; i++)
		lostSupport.Advance(1.0f / 60, pose, OptionalFloor, &floorPresent);
	for (int i = 0; i < 18; i++)
		lostSupport.Advance(1.0f / 60, RootRelativeGetUp(pose, lostSupport.Positions()[Pelvis], 0),
							OptionalFloor, &floorPresent);
	const bool supportedRecovery = lostSupport.State() == Mode::Recovering;
	floorPresent = false;
	for (int i = 0; i < 18; i++)
		lostSupport.Advance(1.0f / 60, RootRelativeGetUp(pose, lostSupport.Positions()[Pelvis], 0),
							OptionalFloor, &floorPresent);
	if (!supportedRecovery || lostSupport.State() != Mode::Falling) {
		puts("FAIL: recovery hovered after its actual ground support disappeared");
		fail++;
	}
	Body overbalanced;
	overbalanced.Reset(pose);
	for (int i = 0; i < 4; i++)
		overbalanced.Impulse(KneeL, {0, -1.4f, 0});
	for (int i = 0; i < 360 && overbalanced.State() != Mode::Recovering; i++)
		overbalanced.Advance(1.0f / 60, pose, FloorAndWall, nullptr);
	bool toppledWithFootSupport = false;
	for (int i = 0; i < 180 && overbalanced.State() == Mode::Recovering; i++) {
		Pose leaning = pose;
		const Vec3 root = overbalanced.Positions()[Pelvis];
		for (Vec3 &p : leaning) {
			const float height = p.z - pose[Pelvis].z;
			p = {root.x + p.x, root.y - height * std::sin(.9f), root.z + height * std::cos(.9f)};
		}
		overbalanced.Advance(1.0f / 60, leaning, FloorAndWall, nullptr);
		if (overbalanced.State() == Mode::Falling)
			for (Joint foot : {FootL, FootR}) {
				const Vec3 p = overbalanced.Positions()[foot];
				toppledWithFootSupport |= FloorAndWall(p, {p.x, p.y, p.z - .12f}, Body::Radius(foot), nullptr).hit;
			}
	}
	if (!toppledWithFootSupport) {
		puts("FAIL: recovery balanced a pelvis outside its actual ground support region");
		fail++;
	}
	Body blast;
	blast.Reset(pose);
	blast.Impulse(ShoulderL, {.5f, 0, 0});
	blast.Momentum({4, 1, 0});
	if (blast.Velocity(FootL).x < 3.9f || blast.State() != Mode::Falling) {
		puts("FAIL: a second native blast's whole-body momentum was discarded");
		fail++;
	}
	Body livingBrace, deadLimp;
	livingBrace.Reset(pose);
	deadLimp.Reset(pose);
	livingBrace.Impulse(Chest, {6, 0, 0});
	deadLimp.Impulse(Chest, {6, 0, 0}, true);
	for (int i = 0; i < 12; i++) {
		livingBrace.Advance(1.0f / 60, pose, FloorAndWall, nullptr);
		deadLimp.Advance(1.0f / 60, pose, FloorAndWall, nullptr);
	}
	if (livingBrace.Positions()[HandL].x <= deadLimp.Positions()[HandL].x + .02f) {
		puts("FAIL: living falling NPC does not brace with its arms");
		fail++;
	}
	Body dead;
	dead.Reset(pose);
	dead.Impulse(Chest, {4, 1, 0}, true);
	for (int i = 0; i < 180; i++)
		dead.Advance(1.0f / 60, pose, FloorAndWall, nullptr);
	if (!dead.Dead() || dead.State() == Mode::Balanced || dead.Positions()[Pelvis].z > 0.7f) {
		puts("FAIL: dead body must fall, never recover");
		fail++;
	}
	for (int i = 0; i < Count; i++) {
		const Vec3 p = dead.Positions()[size_t(i)];
		if (!std::isfinite(p.x) || p.z < Body::Radius(Joint(i)) - 0.001f ||
			p.x > 0.7f - Body::Radius(Joint(i)) + 0.001f) {
			puts("FAIL: physical joints crossed floor/wall");
			fail++;
			break;
		}
	}
	if (std::fabs(Distance(dead.Positions()[ShoulderL], dead.Positions()[ElbowL]) -
				  Distance(pose[ShoulderL], pose[ElbowL])) > 0.06f) {
		puts("FAIL: arm length was not constrained");
		fail++;
	}
	dead.Advance(5.0f, pose, FloorAndWall, nullptr);
	if (dead.LastSteps() > 4) {
		puts("FAIL: long frame did unbounded catch-up");
		fail++;
	}
	Body slope;
	slope.Reset(pose);
	slope.Impulse(Chest, {2, 0, 0}, true);
	for (int i = 0; i < 180; i++)
		slope.Advance(1.0f / 60, pose, Slope, nullptr);
	for (int i = 0; i < Count; i++) {
		const Vec3 p = slope.Positions()[size_t(i)];
		if (-.242535625f * p.x + .970142500f * p.z < Body::Radius(Joint(i)) - .001f) {
			puts("FAIL: joint passed through sloped ground");
			fail++;
			break;
		}
	}
	Body other;
	other.Reset(pose);
	if (other.Dead() || other.Active() || other.Positions()[Pelvis].z != 0.95f) {
		puts("FAIL: NPC state leaked across bodies");
		fail++;
	}
	if (fail) {
		printf("reactphysics: %d failures\n", fail);
		return 1;
	}
	puts("reactphysics: all passed");
}

#include "reactself.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace reactphysics;
static const float kMass[Count] = {.14f, .2f, .5f, 1, 1, 2, 1, 1, 2, .5f, .6f, 1, .5f, .6f, 1};
static Pose Standing()
{
	return {{{0, 0, .95f}, {0, 0, 1.35f}, {0, 0, 1.7f},
			 {-.22f, 0, 1.4f}, {-.35f, 0, 1.1f}, {-.4f, 0, .8f},
			 {.22f, 0, 1.4f}, {.35f, 0, 1.1f}, {.4f, 0, .8f},
			 {-.12f, 0, .9f}, {-.12f, 0, .5f}, {-.12f, 0, .1f},
			 {.12f, 0, .9f}, {.12f, 0, .5f}, {.12f, 0, .1f}}};
}
static Pose CapturedStanding()
{
	// All 15 native joints from npc-fatal-after/standing.json, relative to
	// the captured pelvis. Unlike the older generic fixture, spine1 lies
	// 23 cm below the shoulders and is not the top of the ribcage.
	Pose pose = {{{0, 0, 0}, {-.01520f, -.03488f, .30322f}, {.00967f, .04941f, .70463f},
		{-.20114f, .04895f, .53999f}, {-.25980f, .04785f, .25084f}, {-.24911f, .15939f, -.01254f},
		{.18842f, -.05707f, .53415f}, {.25501f, -.10797f, .25127f}, {.26801f, -.07086f, -.03224f},
		{-.10812f, .02698f, -.00260f}, {-.17133f, .05917f, -.45424f}, {-.19968f, -.08502f, -.83878f},
		{.09149f, -.01865f, -.00664f}, {.11612f, -.01993f, -.46314f}, {.10626f, -.07153f, -.87143f}}};
	for (Vec3 &p : pose) p.z += 1.02f;
	return pose;
}
static Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static Vec3 Mul(Vec3 a, float n) { return {a.x * n, a.y * n, a.z * n}; }
static float Distance(Vec3 a, Vec3 b)
{
	const Vec3 d = Sub(a, b);
	return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
}
static float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static Vec3 Normal(Vec3 v) { return Mul(v, 1 / std::max(Distance(v, {}), 1e-7f)); }
struct BodyFrame { Vec3 side, forward, up; };
static BodyFrame Axes(const Pose &pose)
{
	const Vec3 mid = Mul(Add(pose[ShoulderL], pose[ShoulderR]), .5f);
	const Vec3 up = Normal(Sub(mid, pose[Pelvis]));
	const Vec3 across = Sub(pose[ShoulderR], pose[ShoulderL]);
	const Vec3 side = Normal(Sub(across, Mul(up, Dot(up, across))));
	return {side, Cross(up, side), up};
}
// Independent dense sampling is sufficient to detect a segment passing
// through a body volume; expectations below use hand-measured clearances.
static float SegmentDistance(Vec3 a, Vec3 b, Vec3 c, Vec3 d)
{
	float minimum = 100;
	for (int i = 0; i <= 80; i++)
		for (int j = 0; j <= 80; j++)
			minimum = std::min(minimum, Distance(Add(a, Mul(Sub(b, a), i / 80.0f)),
												 Add(c, Mul(Sub(d, c), j / 80.0f))));
	return minimum;
}
static void Project(Pose &pose, const Pose &rest, int passes = 40, const float *mass = kMass)
{
	for (int i = 0; i < passes; i++)
		reactself::Project(pose, rest, mass);
}
static Vec3 Rotate(Vec3 p)
{
	// An exact component permutation isolates rotation of contact normals
	// from roundoff introduced by large world-coordinate translations.
	return {p.z, p.x, p.y};
}
static Contact FloorAndWall(Vec3, Vec3 p, float radius, void *)
{
	Contact result{p, {}, false};
	if (p.z < radius) { result.position.z = radius; result.normal = {0, 0, 1}; result.hit = true; }
	if (p.x > .7f - radius) { result.position.x = .7f - radius; result.normal = {-1, 0, 0}; result.hit = true; }
	return result;
}
static Contact CountedFloorAndWall(Vec3 from, Vec3 p, float radius, void *context)
{
	++*static_cast<int *>(context);
	return FloorAndWall(from, p, radius, nullptr);
}
static Contact SkinFloor(Vec3 from, Vec3 to, float radius, void *)
{
	// Native sphere sweeps first depenetrate the old point with a skin,
	// then advance its remaining motion. This exposes correction energy
	// that a simple clamp-at-the-destination test floor conceals.
	const float surface = radius + std::clamp(radius * .05f, .003f, .01f) + .0002f;
	Contact contact{to, {}, false};
	if (from.z < surface) {
		contact.position.z = std::max(surface,to.z);
		contact.normal = {0, 0, 1}; contact.hit = true;
	} else if (to.z < surface) {
		contact.position.z = surface;
		contact.normal = {0, 0, 1}; contact.hit = true;
	}
	return contact;
}
static Contact FlatFloor(Vec3, Vec3 to, float radius, void *)
{
	if (to.z >= radius) return {to, {}, false};
	to.z = radius;
	return {to, {0, 0, 1}, true};
}
struct ThinPost { Vec3 center{}; int hits = 0, calls = 0; };
static Contact Post(Vec3 from, Vec3 to, float radius, void *context)
{
	ThinPost &post = *static_cast<ThinPost *>(context);
	++post.calls;
	const float r = radius + .015f;
	const Vec3 a = Sub(from, post.center), d = Sub(to, from);
	auto dot = [](Vec3 x, Vec3 y) { return x.x * y.x + x.y * y.y + x.z * y.z; };
	const float aa = dot(d, d), bb = dot(a, d), cc = dot(a, a) - r * r;
	if (cc < 0) {
		const float length = Distance(a, {});
		const Vec3 normal = length > 1e-8f ? Mul(a, 1 / length) : Vec3{0, -1, 0};
		++post.hits;
		return {Add(post.center, Mul(normal, r)), normal, true};
	}
	const float discriminant = bb * bb - aa * cc;
	if (aa > 1e-10f && discriminant >= 0) {
		const float t = (-bb - std::sqrt(discriminant)) / aa;
		if (t >= 0 && t <= 1) {
			const Vec3 point = Add(from, Mul(d, t));
			++post.hits;
			return {point, Mul(Sub(point, post.center), 1 / r), true};
		}
	}
	return {to, {}, false};
}
int main()
{
	int failed = 0;
	auto check = [&](bool good, const char *message) {
		if (!good) { printf("FAIL: %s\n", message); ++failed; }
	};
	const Pose rest = Standing();
	Pose untouched = rest;
	Project(untouched, rest);
	for (int i = 0; i < Count; i++)
		check(Distance(untouched[size_t(i)], rest[size_t(i)]) < 1e-5f,
			  "natural connected-joint overlap must preserve the measured rig");
	for (bool knee : {false, true}) {
		Pose bentRest = rest;
		const Joint root = knee ? HipL : ShoulderL, middle = knee ? KneeL : ElbowL, tip = knee ? FootL : HandL;
		bentRest[root] = {-.5f, .4f, knee ? .9f : 1.4f};
		bentRest[middle] = {-.5f, .4f, knee ? .5f : 1.05f};
		bentRest[tip] = {-.5f, knee ? .8f : .7f, knee ? .5f : 1.05f};
		if (knee) bentRest[FootR] = {.5f, -.8f, .1f};
		Pose bent = bentRest;
		float tipMass[Count]{};
		tipMass[tip] = 2;
		Project(bent, bentRest, 40, tipMass);
		check(Distance(bent[tip], bentRest[tip]) < 1e-5f,
			  knee ? "a measured right-angle knee must preserve its natural joint overlap" :
					 "a measured right-angle elbow must preserve its natural joint overlap");
		bent = bentRest;
		Project(bent, rest, 40, tipMass);
		check(Distance(bent[tip], bentRest[tip]) < 1e-5f,
			  "a limb must be able to bend normally after a straight initial pose");
		Body posed;
		posed.Reset(bentRest);
		posed.Impulse(tip, {});
		for (int frame = 0; frame < 60; frame++)
			posed.Advance(1.0f / 60, bentRest, FloorAndWall, nullptr);
		check(Distance(posed.Positions()[tip], bentRest[tip]) < .005f,
			  "an unchanged bent animation target must not acquire contact straightening torque");
		bent[tip] = bent[root];
		Project(bent, bentRest, 40, tipMass);
		check(Distance(bent[tip], bent[root]) > .08f,
			  "adjacent segment attachment allowances must still reject a fully folded limb");
	}
	Pose folded = rest;
	folded[ElbowL] = {0, 0, 1.25f};
	folded[HandL] = {0, 0, 1.0f};
	float armMass[Count]{};
	armMass[ElbowL] = 1;
	armMass[HandL] = 2;
	Project(folded, rest, 40, armMass);
	check(SegmentDistance(folded[ElbowL], folded[HandL], folded[Pelvis], folded[Chest]) > .19f,
		  "folded forearms must leave the chest capsule even with a fixed shoulder and torso");
	check(Distance(folded[Chest], rest[Chest]) < 1e-6f,
		  "zero inverse mass must keep the torso fixed");
	Pose headFold = rest;
	headFold[Head] = rest[Pelvis];
	float headMass[Count]{};
	headMass[Head] = 1;
	Project(headFold, rest, 40, headMass);
	check(SegmentDistance(headFold[Head], headFold[Head], headFold[Pelvis], headFold[Chest]) > .28f,
		  "a head folded onto the pelvis must collide with its attached torso");
	Pose crossed = rest;
	crossed[ElbowL] = {-.25f, .4f, 1.2f}; crossed[HandL] = {.25f, .4f, 1.2f};
	crossed[ElbowR] = {.25f, .4f, 1.2f}; crossed[HandR] = {-.25f, .4f, 1.2f};
	const Pose initialCrossed = crossed;
	Project(crossed, rest);
	check(SegmentDistance(crossed[ElbowL], crossed[HandL], crossed[ElbowR], crossed[HandR]) > .115f,
		  "coincident opposite forearms must separate along their full segments");
	Pose perpendicular = rest;
	perpendicular[ElbowL] = {-.25f, .4f, 1.2f}; perpendicular[HandL] = {.25f, .4f, 1.2f};
	perpendicular[ElbowR] = {0, .15f, 1.2f}; perpendicular[HandR] = {0, .65f, 1.2f};
	Project(perpendicular, rest);
	check(SegmentDistance(perpendicular[ElbowL], perpendicular[HandL], perpendicular[ElbowR], perpendicular[HandR]) > .115f,
		  "opposing limbs crossing between their joints must collide");
	Pose weighted = rest;
	weighted[ElbowL] = {0, 2, 1.4f}; weighted[HandL] = {0, 2, 1};
	weighted[ElbowR] = {0, 2.04f, 1.4f}; weighted[HandR] = {0, 2.04f, 1};
	float handMass[Count]{};
	handMass[HandL] = 1; handMass[HandR] = 4;
	const Pose beforeWeighted = weighted;
	reactself::Project(weighted, rest, handMass);
	const float leftMove = Distance(weighted[HandL], beforeWeighted[HandL]);
	const float rightMove = Distance(weighted[HandR], beforeWeighted[HandR]);
	check(leftMove > .001f && std::fabs(rightMove / leftMove - 4) < .001f,
		  "contact correction must distribute displacement in proportion to inverse mass");
	Pose repeated = initialCrossed;
	Project(repeated, rest);
	for (int i = 0; i < Count; i++) {
		check(Distance(crossed[size_t(i)], repeated[size_t(i)]) < 1e-6f,
			  "coincident segment fallback must be deterministic");
		check(std::isfinite(crossed[size_t(i)].x) && Distance(crossed[size_t(i)], rest[size_t(i)]) < 1.0f,
			  "coincident segments must resolve without unbounded correction");
	}
	Pose knees = rest;
	knees[KneeL] = {0, 0, 1.3f}; knees[FootL] = {-.05f, .05f, 1.0f};
	Project(knees, rest);
	check(SegmentDistance(knees[KneeL], knees[FootL], knees[Pelvis], knees[Chest]) > .18f,
		  "folded calves must collide with the torso despite their attached hips");
	Pose scaledRest = rest;
	for (Vec3 &p : scaledRest) p = Add(Mul(p, .5f), {3, -2, .1f});
	Pose scaled = scaledRest;
	scaled[ElbowL] = {3, -2, .725f}; scaled[HandL] = {3, -2, .6f};
	Project(scaled, scaledRest, 40, armMass);
	check(SegmentDistance(scaled[ElbowL], scaled[HandL], scaled[Pelvis], scaled[Chest]) > .095f,
		  "collision volumes must follow the measured rig scale");
	Pose bounded = initialCrossed;
	reactself::Project(bounded, rest, kMass);
	for (int i = 0; i < Count; i++)
		check(Distance(bounded[size_t(i)], initialCrossed[size_t(i)]) <= .06001f,
			  "one solver pass must bound total displacement per joint");
	Pose rotatedRest = rest, rotated = initialCrossed;
	for (Vec3 &p : rotatedRest) p = Rotate(p);
	for (Vec3 &p : rotated) p = Rotate(p);
	Project(rotated, rotatedRest);
	for (int i = 0; i < Count; i++)
		check(Distance(rotated[size_t(i)], Rotate(crossed[size_t(i)])) < 2e-4f,
			  "coincident contact normals must rotate with the measured body frame");
	Pose stable = crossed;
	Project(stable, rest, 200);
	for (int i = 0; i < Count; i++)
		check(Distance(stable[size_t(i)], crossed[size_t(i)]) < .001f,
			  "resolved self contacts must not keep injecting position jitter");
	Pose locked = initialCrossed;
	float fixedMass[Count]{};
	Project(locked, rest, 10, fixedMass);
	for (int i = 0; i < Count; i++)
		check(Distance(locked[size_t(i)], initialCrossed[size_t(i)]) < 1e-6f,
			  "fully fixed coincident segments must remain finite and unmoved");
	for (float gap : {.119f, .12f, .123f}) for (int motion : {-2, -1, 1, 2}) {
		const float direction = motion < 0 ? -1.0f : 1.0f;
		Pose start = rest;
		start[ShoulderL] = {-.45f, 1.5f, 1}; start[ElbowL] = {-.15f, 1.5f, 1}; start[HandL] = {.15f, 1.5f, 1};
		start[ShoulderR] = {0, 1.05f, 1 + direction * gap};
		start[ElbowR] = {0, 1.35f, 1 + direction * gap};
		start[HandR] = {0, 1.65f, 1 + direction * gap};
		Pose end = start;
		for (Joint joint : {ShoulderL, ElbowL, HandL}) end[joint].z += direction * .246f;
		if (std::abs(motion) == 2)
			for (Joint joint : {ShoulderR, ElbowR, HandR}) end[joint].z -= direction * .246f;
		for (int pass = 0; pass < 8; pass++) reactself::Project(end, start, kMass, &start);
		const float leftZ = (end[ElbowL].z + end[HandL].z) * .5f;
		const float rightZ = (end[ElbowR].z + end[HandR].z) * .5f;
		check(direction * (rightZ - leftZ) > .11f,
			  "fast separated forearms must retain their contact side when crossing within one fixed step");
		check(SegmentDistance(end[ElbowL], end[HandL], end[ElbowR], end[HandR]) > .11f,
			  "swept contact must leave crossed limb capsules separated");
		Pose outward = start;
		for (Joint joint : {ShoulderL, ElbowL, HandL}) outward[joint].z -= direction * .246f;
		const Pose beforeOutward = outward;
		for (int pass = 0; pass < 8; pass++) reactself::Project(outward, start, kMass, &start);
		for (Joint joint : {ElbowL, HandL})
			check(Distance(outward[joint], beforeOutward[joint]) < 1e-5f,
				  "touching or slightly overlapping limbs must remain free to move outward");
		if (motion == 2) for (float speed : {5.0f, 10.0f, 15.0f}) {
			Body crossingBody;
			crossingBody.Reset(start);
			for (Joint joint : {ShoulderL, ElbowL, HandL}) crossingBody.Impulse(joint, {0, 0, speed}, true);
			for (Joint joint : {ShoulderR, ElbowR, HandR}) crossingBody.Impulse(joint, {0, 0, -speed}, true);
			crossingBody.Advance(1.0f / 60, start, nullptr, nullptr);
			const Pose &result = crossingBody.Positions();
			check((result[ElbowR].z + result[HandR].z - result[ElbowL].z - result[HandL].z) > 0 &&
				  SegmentDistance(result[ElbowL], result[HandL], result[ElbowR], result[HandR]) > .11f,
				  "opposing bounded impulses must not exchange the sides of initially touching arms");
		}
	}
	Pose chestStart = rest;
	chestStart[ShoulderL] = {-.45f, -.245f, 1.1f};
	chestStart[ElbowL] = {-.15f, -.245f, 1.1f}; chestStart[HandL] = {.15f, -.245f, 1.1f};
	Pose chestEnd = chestStart;
	for (Joint joint : {ShoulderL, ElbowL, HandL}) chestEnd[joint].y += .25f;
	for (int pass = 0; pass < 8; pass++) reactself::Project(chestEnd, rest, armMass, &chestStart);
	check((chestEnd[ElbowL].y + chestEnd[HandL].y) * .5f < -.23f,
		  "a fast forearm ending inside the chest must resolve on its incoming side");
	const Pose capturedRest = CapturedStanding();
	Pose nativeUnchanged = capturedRest;
	Project(nativeUnchanged, capturedRest);
	for (int joint = 0; joint < Count; joint++)
		check(Distance(nativeUnchanged[size_t(joint)], capturedRest[size_t(joint)]) < 1e-5f,
			  "full torso volume must preserve the captured rig's natural attachment offsets");
	Pose upperChest = capturedRest;
	upperChest[ElbowL] = {-.15f, 0, 1.56f}; upperChest[HandL] = {.15f, 0, 1.56f};
	Project(upperChest, capturedRest, 40, armMass);
	const Vec3 upperCenter = Mul(Add(upperChest[ShoulderL], upperChest[ShoulderR]), .5f);
	check(SegmentDistance(upperChest[ElbowL], upperChest[HandL], upperChest[Pelvis], upperCenter) > .19f,
		  "the actual rig's upper ribcage between spine1 and shoulders must have collision volume");
	Body nativeDrop;
	nativeDrop.Reset(capturedRest);
	nativeDrop.Impulse(Chest, {0, .65f, 0}, true);
	int maxQueries = 0;
	for (int frame = 0; frame < 600; frame++) {
		int queries = 0;
		nativeDrop.Advance(1.0f / 60, capturedRest, CountedFloorAndWall, &queries);
		maxQueries = std::max(maxQueries, queries);
	}
	check(maxQueries <= Body::kMaxStepSweeps, "full body capsule certification must retain its bounded world calls per fixed step");
	const Pose &nativeFinal = nativeDrop.Positions();
	const Vec3 nativeShoulders = Mul(Add(nativeFinal[ShoulderL], nativeFinal[ShoulderR]), .5f);
	check(nativeShoulders.z > .15f,
		  "the captured rig's full upper ribcage must rest above the floor instead of on tiny shoulder spheres");
	const Vec3 nativeHead = Sub(nativeFinal[Head], nativeShoulders), nativeUp = Sub(nativeShoulders, nativeFinal[Pelvis]);
	const Vec3 restShoulders = Mul(Add(capturedRest[ShoulderL], capturedRest[ShoulderR]), .5f);
	check(std::fabs(Distance(nativeFinal[Head], nativeShoulders) - Distance(capturedRest[Head], restShoulders)) < .025f,
		  "a captured-rig fatal drop must retain the short anatomical neck span instead of a spine1 head lever");
	const float alignment = (nativeHead.x * nativeUp.x + nativeHead.y * nativeUp.y + nativeHead.z * nativeUp.z) /
		(Distance(nativeHead, {}) * Distance(nativeUp, {}));
	check(alignment > .44f, "a captured-rig fatal drop must keep the skull above its ribcage in the body's frame");
	const BodyFrame nativeFrame = Axes(nativeFinal);
	for (auto leg : {std::array<Joint, 2>{HipL, KneeL}, {HipR, KneeR}}) {
		const Vec3 thigh = Normal(Sub(nativeFinal[leg[1]], nativeFinal[leg[0]]));
		const float side = leg[0] == HipL ? -1.0f : 1.0f;
		const float flex = std::atan2(Dot(thigh, nativeFrame.forward), -Dot(thigh, nativeFrame.up)) * 57.2957795f;
		const float abduction = std::asin(std::clamp(side * Dot(thigh, nativeFrame.side), -1.0f, 1.0f)) * 57.2957795f;
		check(flex >= -33 && flex <= 128, "a small fatal hit must not fold a hip backward or through the ribcage");
		check(abduction >= -18 && abduction <= 58, "a small fatal hit must not splay the hips outside their swing limits");
	}
	for (bool arm : {false, true}) {
		Pose hingeRest = rest;
		hingeRest[ElbowL] = {-.22f, 0, 1.1f}; hingeRest[HandL] = {-.22f, .1f, .82f};
		for (Vec3 &p : hingeRest) p.z += 2;
		const Joint root = arm ? ShoulderL : HipL, middle = arm ? ElbowL : KneeL, tip = arm ? HandL : FootL;
		Body hinge;
		hinge.Reset(hingeRest);
		hinge.Impulse(tip, {6, 0, 0}, true);
		hinge.Advance(1.0f / 60, hingeRest, nullptr, nullptr);
		const Pose &p = hinge.Positions();
		const BodyFrame frame = Axes(p);
		const Vec3 upper = Normal(Sub(p[middle], p[root]));
		const Vec3 plane = Normal(Cross(upper, frame.forward));
		check(std::fabs(Dot(Sub(p[tip], p[middle]), plane)) < .025f,
			  arm ? "an elbow must resist a sideways kick outside its hinge plane" :
					"a knee must resist a sideways kick outside its hinge plane");
	}
	Body skinnedGround;
	skinnedGround.Reset(capturedRest);
	skinnedGround.Impulse(Chest, {0, .65f, 0}, true);
	for (int frame = 0; frame < 600; frame++) skinnedGround.Advance(1.0f / 60, capturedRest, SkinFloor, nullptr);
	float verticalCore = 0;
	for (Joint joint : {Pelvis, Chest, ShoulderL, ShoulderR})
		verticalCore = std::max(verticalCore, std::fabs(skinnedGround.Velocity(joint).z));
	check(verticalCore < .08f, "terrain depenetration must not keep injecting upward core velocity into a resting ragdoll");
	Body groundedSurvivor;
	groundedSurvivor.Reset(rest);
	for (int hit = 0; hit < 4; hit++) groundedSurvivor.Impulse(KneeL, {0, -1.4f, 0});
	bool recoveredOnFlatGround = false;
	for (int frame = 0; frame < 360; frame++) {
		groundedSurvivor.Advance(1.0f / 60, rest, FlatFloor, nullptr);
		recoveredOnFlatGround |= groundedSurvivor.State() == Mode::Recovering;
	}
	check(recoveredOnFlatGround, "a stationary survivor must settle into recovery on flat ground without needing a wall");
	Pose movingRig = capturedRest;
	for (Vec3 &p : movingRig) p.y -= .2f;
	ThinPost post{restShoulders};
	Body fastTorso;
	fastTorso.Reset(movingRig, {0, 25, 0});
	fastTorso.Impulse(Chest, {}, true);
	fastTorso.Advance(1.0f / 60, movingRig, Post, &post);
	const Vec3 stoppedUpper = Mul(Add(fastTorso.Positions()[ShoulderL], fastTorso.Positions()[ShoulderR]), .5f);
	check(stoppedUpper.y < post.center.y - .17f && post.hits > 0,
		  "the broad upper ribcage must sweep against thin obstacles missed by every joint sphere");
	check(post.calls <= Body::kMaxStepSweeps, "full body capsule certification must retain its bounded world budget");
	for (Joint hit : {HandL, Head, KneeR}) {
		Body body;
		body.Reset(rest);
		body.Impulse(hit, {5, 2, -3}, true);
		for (int frame = 0; frame < 600; frame++)
			body.Advance(1.0f / 60, rest, FloorAndWall, nullptr);
		const Pose &settled = body.Positions();
		for (int i = 0; i < Count; i++) {
			check(settled[size_t(i)].z >= Body::Radius(Joint(i)) - 1e-5f &&
				  settled[size_t(i)].x <= .7f - Body::Radius(Joint(i)) + 1e-5f,
				  "self collision must preserve floor and wall contact bounds");
			check(Distance(body.Velocity(Joint(i)), {}) < .7f,
				  "self collision must settle without explosive velocity or jitter");
		}
		for (auto link : {std::array<Joint, 2>{Pelvis, Chest}, {Chest, Head}, {Chest, ShoulderL}, {Chest, ShoulderR},
				{Pelvis, HipL}, {Pelvis, HipR}, {ShoulderL, ElbowL}, {ElbowL, HandL},
				{ShoulderR, ElbowR}, {ElbowR, HandR}, {HipL, KneeL}, {KneeL, FootL}, {HipR, KneeR}, {KneeR, FootR}})
			check(std::fabs(Distance(settled[link[0]], settled[link[1]]) - Distance(rest[link[0]], rest[link[1]])) < .025f,
				  "alternating self collision and length constraints must retain every measured bone length");
		check(SegmentDistance(settled[Head], settled[Head], settled[Pelvis], settled[Chest]) > .25f,
			  "a supported fatal ragdoll must keep its head outside the torso");
		for (auto arm : {std::array<Joint, 2>{ElbowL, HandL}, {ElbowR, HandR}})
			check(SegmentDistance(settled[arm[0]], settled[arm[1]], settled[Pelvis], settled[Chest]) > .20f,
				  "supported forearms must remain outside the torso after world projection");
	}
	if (failed) { printf("reactself: %d failures\n", failed); return 1; }
	puts("reactself: all passed");
}

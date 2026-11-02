#include "reactworld.h"
#include "reactrig.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <vector>

namespace {
using namespace reactfixture;
void DiagonalOverlapAndTangent()
{
	Reset();
	const Vec3 n = Unit({1, 1, 0}), t = Unit({1, -1, 0});
	Plane(n);
	const float radius = .18f;
	Vec3 from = Mul(n, .15f);
	// All six old axis rays have normal reach .1273m, below the .15m wall separation.
	assert(radius * std::max(std::fabs(n.x), std::fabs(n.y)) < .15f);
	auto still = reactcontact::Sweep(world, from, from, radius, nullptr);
	assert(still.hit && Dot(still.position, n) >= radius - 0.0001f);
	assert(Dot(still.normal, n) > .99f);
	auto tangent = reactcontact::Sweep(world, from, Add(from, Mul(t, .4f)), radius, nullptr);
	assert(tangent.hit && Dot(tangent.position, n) >= radius - 0.0001f);
	assert(Dot(tangent.position, t) > .39f);
}
void ThinObstacleAndCornerGrazing()
{
	Reset();
	Plane({1, 0, 0}, .37f);
	assert(!NativeSphere({-2, 0, 0}, .18f, nullptr, true, true, false, true, false, false));
	assert(!NativeSphere({2, 0, 0}, .18f, nullptr, true, true, false, true, false, false));
	auto across = reactcontact::Sweep(world, {-2, 0, 0}, {2, 0, 0}, .18f, nullptr);
	assert(across.hit && across.position.x <= .191f);
	Reset();
	objects.push_back({{{{0, 0, -1}, {0, 1, -1}, {0, 0, 1}}}, false, false});
	// The centerline stays outside the triangle's y>=0 footprint, while the sphere hits its edge.
	auto edge = reactcontact::Sweep(world, {-1, -.17f, 0}, {1, -.17f, 0}, .18f, nullptr);
	assert(edge.hit);
	assert(Length(Sub(edge.position, {1, -.17f, 0})) > .005f);
}
void CornerAndSlopeVehicle()
{
	Reset();
	Plane({1, 0, 0});
	Plane({0, 1, 0});
	auto corner = reactcontact::Sweep(world, {.1f, .1f, 0}, {.1f, .1f, 0}, .18f, nullptr);
	assert(corner.hit && corner.position.x >= .1799f && corner.position.y >= .1799f);
	Reset();
	const Vec3 n = Unit({0, .5f, 1});
	Plane(n, 0, true);
	auto slope = reactcontact::Sweep(world, Mul(n, .5f), Mul(n, -.5f), .12f, nullptr);
	assert(slope.hit && Dot(slope.position, n) >= .119f && Dot(slope.normal, n) > .99f);
}
void InitialOverlapPreservesAbsoluteTarget()
{
	Reset(); Plane({0, 0, 1});
	const Vec3 from{0, 0, .17f}, goal{.2f, 0, .5f};
	const auto clear = reactcontact::Sweep(world, from, goal, .18f, nullptr);
	assert(clear.hit && Length(Sub(clear.position, goal)) < 1e-5f);
	const auto still = reactcontact::Sweep(world, from, from, .18f, nullptr);
	assert(still.hit && still.position.z >= .18f && still.position.z < .2f);
	const auto tangent = reactcontact::Sweep(world, from, {.4f, 0, .17f}, .18f, nullptr);
	assert(tangent.hit && tangent.position.z >= .18f && tangent.position.x > .399f);
	const auto into = reactcontact::Sweep(world, from, {.4f, 0, -.2f}, .18f, nullptr);
	assert(into.hit && into.position.z >= .18f && into.position.x > .399f);
}
void AcuteCornerEscape()
{
	Reset();
	Plane({1, 0, 0});
	Plane({-.8660254f, .5f, 0});
	const Vec3 from{.17f, .6344486f, 1}, goal{.3f, 1.3f, 1};
	reactcontact::Stats stats;
	const auto escaped = reactcontact::Sweep(world, from, goal, .18f, nullptr, &stats);
	assert(escaped.hit && !stats.unresolved && !stats.exhausted);
	assert(escaped.position.x >= .1799f);
	assert(Dot(escaped.position, {-.8660254f, .5f, 0}) >= .1799f);
	assert(escaped.position.y > 1.25f);
	assert(stats.queries < 64);
	// Separation must feed the swept path, including an obstruction farther
	// down the otherwise clear exit. Endpoint-only escape would cross it.
	Plane({0, 1, 0}, 1.1f);
	const auto stopped = reactcontact::Sweep(world, from, goal, .18f, nullptr, &stats);
	assert(stopped.hit && stopped.position.y <= .921f && stopped.position.y > .7f);
	Reset();
	Plane({1, 0, 0}); Plane({1, 0, 0}, .3f);
	const Vec3 trapped{.15f, 0, 1};
	const auto impossible = reactcontact::Sweep(world, trapped, trapped, .18f, nullptr, &stats);
	assert(impossible.hit && stats.unresolved && Length(Sub(impossible.position, trapped)) < 1e-6f);
}
void ResolutionStatus()
{
	Reset(); objects.push_back({{}, true, true});
	const Vec3 still{0, 0, 1};
	const auto unknown = reactcontact::Sweep(world, still, still, .18f, nullptr);
	assert(unknown.hit && !unknown.resolved && Length(Sub(unknown.position, still)) < 1e-6f);
	Reset(); Plane({0, 0, 1});
	const auto floor = reactcontact::Sweep(world, {0, 0, .17f}, {0, 0, .17f}, .18f, nullptr);
	assert(floor.hit && floor.resolved && floor.position.z >= .18f);
	Reset();
	const auto air = reactcontact::Sweep(world, still, still, .18f, nullptr);
	assert(!air.hit && air.resolved);
	const reactcontact::World missing{};
	const auto unavailable = reactcontact::Sweep(missing, still, still, .18f, nullptr);
	assert(unavailable.hit && !unavailable.resolved);
}
void NativeBufferAndIgnore()
{
	Reset();
	Plane({1, 1, 0});
	nativePoint = {{12, 13, 14}, 0, {0, 1, 0}, 0, 1, 2, 3, 4, .09f};
	const auto saved = nativePoint;
	auto *ped = reinterpret_cast<uint8_t *>(&objects[0]);
	auto ignored = reactcontact::Sweep(world, {.1f, .1f, 0}, {.2f, .2f, 0}, .18f, ped);
	assert(!ignored.hit && lastIgnore == ped);
	assert(std::memcmp(&saved, &nativePoint, sizeof saved) == 0);
	Reset();
	objects.push_back({{}, true, true});
	nativePoint = saved;
	uint8_t unrelatedPed = 0;
	ped = &unrelatedPed;
	reactcontact::Stats stats;
	auto ghost = reactcontact::Sweep(world, {0, 0, 0}, {1, 0, 0}, .18f, ped, &stats);
	assert(ghost.hit && stats.unresolved && Length(ghost.position) < .0001f);
	assert(Length(ghost.normal) < .0001f); // stale valid normal/depth must never become a fresh contact
	assert(std::memcmp(&saved, &nativePoint, sizeof saved) == 0);
}
void BoundsAndBudget()
{
	Reset();
	Plane({0, 0, 1});
	reactcontact::Stats stats;
	auto slide = reactcontact::Sweep(world, {0, 0, .18f}, {4, 0, .18f}, .18f, nullptr, &stats);
	assert(slide.position.z >= .179f && slide.position.x <= 4.001f);
	assert(stats.queries <= reactcontact::kMaxQueries && calls == stats.queries);
	if (stats.exhausted)
		assert(slide.position.x < 3.99f); // exhaustion cannot authorize the unqueried suffix
	Reset();
	Plane({0, 0, 1});
	Plane({1, 0, 0}, 2);
	auto wallAfterFloor = reactcontact::Sweep(world, {0, 0, .18f}, {4, 0, .18f}, .18f, nullptr, &stats);
	assert(wallAfterFloor.position.x < 1.821f && wallAfterFloor.position.z >= .179f);
	assert(stats.queries <= reactcontact::kMaxQueries);
	Reset();
	Plane({1, 0, 0});
	auto first = reactcontact::Sweep(world, {.1f, 0, 0}, {.1f, 0, 0}, .18f, nullptr);
	assert(first.normal.x > .99f);
	Reset();
	Plane({0, 1, 0}); // fixture entity storage is reused, without caching its previous contact
	auto reused = reactcontact::Sweep(world, {0, .1f, 0}, {0, .1f, 0}, .18f, nullptr);
	assert(reused.normal.y > .99f && std::fabs(reused.normal.x) < .001f);
	Reset();
	for (float radius : {0.0f, -1.0f, 1.01f, std::numeric_limits<float>::quiet_NaN()}) {
		const Vec3 origin{0, 0, 2};
		const auto invalid = reactcontact::Sweep(world, origin, {1, 0, 2}, radius, nullptr, &stats);
		assert(invalid.hit && !invalid.resolved && stats.unresolved && stats.queries == 0 && Length(Sub(invalid.position, origin)) < 1e-6f);
	}
	reactcontact::Stats airStats;
	auto air = reactcontact::Sweep(world, {0, 0, 2}, {3, 1, 2}, .18f, nullptr, &airStats);
	assert(!air.hit && Length(Sub(air.position, {3, 1, 2})) < .0001f && airStats.queries < 8);
}

void CapturedSupportedContacts()
{
 using namespace reactphysics;
 const auto rig=CapturedRig();
 const Pose rest=reactrig::Capture(rig);
 const Vec3 neck{rig[4][12],rig[4][13],rig[4][14]}, axis{rig[5][0],rig[5][1],rig[5][2]};
 Reset(); Plane({.000074f,-.002572f,.999997f});
 Body survivor;
 survivor.Reset(rest,{},&neck,&axis);
 bool fell=false;
 float reach=0,step=0;
 for(int frame=0;frame<100;frame++) {
  if(frame%15==0 && frame<76) survivor.ContactResponse(KneeL,{0,1.2f,0},{0,1,0});
  survivor.Advance(1.0f/60,rest,NativeSweep,nullptr);
  fell |= survivor.State()==Mode::Falling;
  reach=std::max(reach,std::fabs(survivor.Positions()[HandL].y-rest[HandL].y));
  step=std::max(step,survivor.Positions()[FootR].y-rest[FootR].y);
 }
 if(fell || survivor.Positions()[Pelvis].z<rest[Pelvis].z-.15f || reach<.025f || step<.015f) {
  printf("FAIL: captured survivor did not resist supported contacts: fall=%d reach=%.3f step=%.3f\n",fell,reach,step);
  std::exit(1);
 }
 Body trip;
 trip.Reset(rest,{},&neck,&axis);
 // Native point braking at a low obstruction stops the foot while COM
 // still carries 3m/s. The actual world sweep also sees the finite curb.
 objects.push_back({{{{-.5f,.05f,0},{.5f,.05f,0},{.5f,.05f,.25f}},
                     {{-.5f,.05f,0},{.5f,.05f,.25f},{-.5f,.05f,.25f}}},false,false});
 trip.ContactResponse(FootL,{0,-3,0},{0,-1,0},{0,3,0});
 bool tripped=false;
 for(int frame=0;frame<30;frame++) {
  trip.Advance(1.0f/60,rest,NativeSweep,nullptr);
  tripped |= trip.State()==Mode::Falling;
 }
 if(!tripped) { puts("FAIL: captured moving COM remained upright over a genuinely obstructed foot"); std::exit(1); }
 Reset();
 Body unsupported;
 unsupported.Reset(rest,{},&neck,&axis);
 unsupported.ContactResponse(Chest,{.7f,0,0},{1,0,0});
 for(int frame=0;frame<40;frame++) unsupported.Advance(1.0f/60,rest,NativeSweep,nullptr);
 if(unsupported.State()!=Mode::Falling || unsupported.Positions()[Pelvis].z>rest[Pelvis].z-.25f) {
  puts("FAIL: captured unsupported contact recruited nonexistent ground"); std::exit(1);
 }
}
void CapturedHeadFade()
{
 using namespace reactphysics;
 auto native=CapturedRig();
 const Pose physical=reactrig::Capture(native);
 const Vec3 neck{native[4][12],native[4][13],native[4][14]};
 const Vec3 headAxis{native[5][0],native[5][1],native[5][2]};
 // A native post-animation head look is preserved as input. While a
 // physical brace fades, its final skin axes must retain the neck limit.
 for(int axis=0;axis<3;axis++) {
  native[5][axis*4]=-native[5][axis*4];
  native[5][axis*4+2]=-native[5][axis*4+2];
 }
 for(float weight:{.25f,.5f,1.0f}) {
  const auto blended=reactrig::Apply(native,physical,weight,&neck,&headAxis);
  const Pose joints=reactrig::Capture(blended);
  const Vec3 torso=Unit(Sub(Mul(Add(joints[ShoulderL],joints[ShoulderR]),.5f),joints[Pelvis]));
  const Vec3 axis=Unit({blended[5][0],blended[5][1],blended[5][2]});
  if(Dot(axis,torso)<.499f) {
   printf("FAIL: final partial head blend folded into chest (weight=%.2f axis=%.3f)\n",weight,Dot(axis,torso));
   std::exit(1);
  }
 }
}
void CapturedWorldAndAnatomy()
{
 using namespace reactphysics;
 const auto rig = CapturedRig();
 const Pose rest = reactrig::Capture(rig);
 const Vec3 measuredNeck{rig[4][12],rig[4][13],rig[4][14]};
 const Vec3 measuredHeadAxis{rig[5][0],rig[5][1],rig[5][2]};
 const std::array<Joint,6> joints{Chest,Chest,Head,Pelvis,KneeL,KneeL};
 const std::array<Vec3,6> impacts{Vec3{.65f,.1f,0},Vec3{6,0,0},Vec3{4,0,-3},Vec3{0,-6,-1},Vec3{3,2,0},Vec3{.8074f,-2.1243f,4.0413f}};
 for(size_t scenario=0; scenario<impacts.size();scenario++) {
  Reset();
  const Vec3 floorNormal = scenario==5 ? Vec3{-.148862243f,-.215701938f,.965045393f} : Vec3{.000074f,-.002572f,.999997f};
  Plane(floorNormal);
  if(scenario) Plane({1,0,0},.65f);
  Body body;
  body.Reset(rest, {}, &measuredNeck, &measuredHeadAxis);
  body.Impulse(joints[scenario], impacts[scenario], true);
  float worstNeck=0, worstUp=1, worstGround=0, worstWall=0, worstJoint=0, worstCore=0;
  for (int frame=0; frame<240; ++frame) {
   body.Advance(1.0f/60, rest, NativeSweep, nullptr);
   const Vec3 physicalNeck=body.Neck(), physicalHeadAxis=body.HeadAxis();
   const auto rendered = reactrig::Apply(rig, body.Positions(), 1, &physicalNeck, &physicalHeadAxis);
   const Vec3 neck{rendered[4][12],rendered[4][13],rendered[4][14]};
   const Vec3 head = body.Positions()[Head];
   worstNeck=std::max(worstNeck,std::fabs(Length(Sub(head,neck))-Length(Sub(rest[Head],measuredNeck))));
   const Vec3 upper=Mul(Add(body.Positions()[ShoulderL],body.Positions()[ShoulderR]),.5f);
   Vec3 up=Unit(Sub(upper,body.Positions()[Pelvis]));
   const Vec3 headAxis{rendered[5][0],rendered[5][1],rendered[5][2]};
   worstUp=std::min(worstUp,Dot(up,headAxis));
   const Vec3 n=Unit(floorNormal);
   const Vec3 skull=Add(head,Mul(Unit(headAxis),.105f));
   worstGround=std::max(worstGround,.105f-Dot(skull,n));
   const float coreRadius=std::min(Length(Sub(rest[ShoulderL],rest[ShoulderR]))*.4f,Length(Sub(rest[Chest],rest[Pelvis]))*.55f);
   worstCore=std::max(worstCore,coreRadius-Dot(upper,n));
   if(scenario) {
    worstWall=std::max(worstWall,skull.x+.105f-.65f);
    worstCore=std::max(worstCore,upper.x+coreRadius-.65f);
   }
   for(int joint=0;joint<Count;joint++) {
    worstJoint=std::max(worstJoint,Body::Radius(Joint(joint))-Dot(body.Positions()[joint],n));
    if(scenario) worstJoint=std::max(worstJoint,body.Positions()[joint].x+Body::Radius(Joint(joint))-.65f);
   }
  }
  if(worstNeck>.018f || worstUp<.499f || worstGround>.002f || worstWall>.002f || worstJoint>.002f || worstCore>.002f) {
   printf("FAIL: captured native rig final anatomy/world scenario=%zu neck=%.4f axis=%.4f skullfloor=%.4f skullwall=%.4f joint=%.4f core=%.4f\n",scenario,worstNeck,worstUp,worstGround,worstWall,worstJoint,worstCore);
   std::exit(1);
  }
 }
}

} // namespace
int main(int argc, char **)
{
	if (argc == 1) {
		CapturedSupportedContacts();
		CapturedHeadFade();
		CapturedWorldAndAnatomy();
	}
	InitialOverlapPreservesAbsoluteTarget();
	AcuteCornerEscape();
	DiagonalOverlapAndTangent();
	ThinObstacleAndCornerGrazing();
	CornerAndSlopeVehicle();
	ResolutionStatus();
	NativeBufferAndIgnore();
	BoundsAndBudget();
	puts("all native reaction contact adapter tests passed");
	return 0;
}

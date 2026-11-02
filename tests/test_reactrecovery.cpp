// Reproduce native-rig living landings through complete world volume sweeps.
#include "reactworld.h"
#include "reactshape.h"
#include <array>
using namespace reactfixture;
using namespace reactphysics;
namespace {
int failures;
unsigned sweeps;
void Check(bool good,const char *message) {if(!good){std::printf("FAIL: %s\n",message);++failures;}}
Contact WorldSweep(Vec3 from,Vec3 to,float radius,void *context)
{
 ++sweeps;
 return NativeSweep(from,to,radius,context);
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


void SurvivingFall(Joint joint,Vec3 impulse)
{
 Reset();Plane({0,0,1});
 const auto rig=CapturedRig();const Pose rest=reactrig::Capture(rig);
 const Vec3 neck{rig[4][12],rig[4][13],rig[4][14]},axis{rig[5][0],rig[5][1],rig[5][2]};
 Body body;body.Reset(rest,{},&neck,&axis);body.Impulse(joint,impulse);
 Check(body.State()==Mode::Falling,"fixture must begin with an actual surviving fall");
 constexpr int parent[Count]={-1,Pelvis,Chest,Chest,ShoulderL,ElbowL,Chest,ShoulderR,ElbowR,Pelvis,HipL,KneeL,Pelvis,HipR,KneeR};
 bool beganRecovery=false,fellAgain=false;float recoverySeconds=0,worstLength=0,worstWorld=0;
 unsigned worstSweeps=0;
 for(int frame=0;frame<900;frame++) {
  const Mode previous=body.State();
  const Pose target=RootRelativeGetUp(rest,body.Positions()[Pelvis],recoverySeconds);
  sweeps=0;body.Advance(1.f/60,target,WorldSweep,nullptr);
  worstSweeps=std::max(worstSweeps,sweeps);
  if(body.State()==Mode::Recovering&&previous!=Mode::Recovering) {beganRecovery=true;recoverySeconds=0;}
  fellAgain|=beganRecovery&&body.State()==Mode::Falling;
  if(body.State()==Mode::Recovering) recoverySeconds+=1.f/60;
  const Pose &pose=body.Positions();
  worstLength=std::max(worstLength,std::fabs(Length(Sub(pose[Head],body.Neck()))-Length(Sub(rest[Head],neck))));
  for(int i=1;i<Count;i++) if(i!=Head)
   worstLength=std::max(worstLength,std::fabs(Length(Sub(pose[i],pose[parent[i]]))-Length(Sub(rest[i],rest[parent[i]]))));
  reactshape::Cover(rest,[&](const reactshape::Sphere &sphere){const Vec3 point=sphere.Position(pose);worstWorld=std::max(worstWorld,Length(Sub(point,NativeSweep(point,point,sphere.radius,nullptr).position)));},&pose);
  const Vec3 skull=reactshape::Skull(pose,body.HeadAxis());
  worstWorld=std::max(worstWorld,Length(Sub(skull,NativeSweep(skull,skull,reactshape::kSkullRadius,nullptr).position)));
 }
 std::printf("recovery joint=%d state=%d active=%d pelvis=%.3f length=%.6f world=%.6f sweeps=%u\n",joint,int(body.State()),body.Active(),body.Positions()[Pelvis].z,worstLength,worstWorld,worstSweeps);
 Check(beganRecovery&&!fellAgain,"a supported native-rig survivor must enter and retain get-up support");
 Check(!body.Dead()&&!body.Active()&&body.Positions()[Pelvis].z>.8f,"a supported native-rig survivor must complete recovery within fifteen seconds");
 Check(worstLength<=.01f,"recovery must preserve measured bone lengths within one centimetre");
 Check(worstWorld<=.0005f,"recovery must certify clearance for all skin volumes");
 Check(worstSweeps<=Body::kMaxStepSweeps,"recovery must retain the bounded per-step sweep budget");
}
}
int main()
{
 SurvivingFall(ElbowR,{1.676817f,-6.765371f,-2.177554f});
 SurvivingFall(HandR,{3.854358f,-.698760f,5.110011f});
 SurvivingFall(KneeR,{.357519f,.760758f,-4.441707f});
 std::printf("reactrecovery: %d failures\n",failures);return failures?1:0;
}

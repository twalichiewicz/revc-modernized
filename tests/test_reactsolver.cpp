#include "reactworld.h"
#include "reactshape.h"
#include <array>
#include <random>

using namespace reactfixture;
using namespace reactphysics;

static int failures;
static void Check(bool good, const char *message)
{
	if (!good) { std::printf("FAIL: %s\n",message); ++failures; }
}

static void LandingAnatomy()
{
	const auto rig=CapturedRig(); const Pose rest=reactrig::Capture(rig);
	const Vec3 neck{rig[4][12],rig[4][13],rig[4][14]},axis{rig[5][0],rig[5][1],rig[5][2]};
	constexpr int parent[Count]={-1,Pelvis,Chest,Chest,ShoulderL,ElbowL,Chest,ShoulderR,ElbowR,Pelvis,HipL,KneeL,Pelvis,HipR,KneeR};
	std::mt19937 random(24191); std::uniform_real_distribution<float> impulse(-7,7);
	float worstLength=0,worstGround=0,lowestPelvis=rest[Pelvis].z;
	for(int trial=0;trial<25;trial++) {
		Reset(); Plane({0,0,1});
		Body body; body.Reset(rest,{},&neck,&axis);
		const Vec3 hit=trial?Vec3{impulse(random),impulse(random),impulse(random)}:Vec3{};
		body.Impulse(Joint(trial%Count),hit,true);
		Pose lastSecond{};
		for(int frame=0;frame<600;frame++) {
			body.Advance(1.f/60,rest,NativeSweep,nullptr);
			const auto &pose=body.Positions();
			if(frame==540) lastSecond=pose;
			lowestPelvis=std::min(lowestPelvis,pose[Pelvis].z);
			for(int j=0;j<Count;j++) {
				Check(std::isfinite(Length(pose[j])),"fall must keep every joint finite");
				worstGround=std::max(worstGround,Body::Radius(Joint(j))-pose[j].z);
				if(j>0&&j!=Head) worstLength=std::max(worstLength,std::fabs(Length(Sub(pose[j],pose[parent[j]]))-Length(Sub(rest[j],rest[parent[j]]))));
			}
		}
		Check(body.Dead(),"terrain correction cannot revive a fatal body");
		Check(body.Active(),"a feasible fatal fall must keep simulating instead of surrendering its pose");
        const auto &finalPose=body.Positions();
        const float axialClearance=std::min({finalPose[Pelvis].z-Body::Radius(Pelvis),
            finalPose[Chest].z-Body::Radius(Chest),finalPose[Head].z-Body::Radius(Head)});
        Check(axialClearance<.1f,"every fatal trial must ground an axial body volume instead of freezing upright on its feet");
        Check(body.State()==Mode::Settled,"every supported fatal trial must settle into a stable corpse");
		float drift=0,speed=0;
		for(int j=0;j<Count;j++) {
			drift=std::max(drift,Length(Sub(body.Positions()[j],lastSecond[j])));
			speed=std::max(speed,Length(body.Velocity(Joint(j))));
		}
		Check(drift<.0005f&&speed<.001f,"a settled corpse must actually sleep without residual joint drift");
	}
	std::printf("landing anatomy: worst bone error %.6fm; ground penetration %.6fm; lowest pelvis %.6fm\n",worstLength,worstGround,lowestPelvis);
	Check(worstLength<=.01f,"every landing frame must retain measured bone lengths within one centimetre");
	Check(worstGround<=.002f,"anatomy constraints cannot push joints through the ground");
	Check(lowestPelvis<.45f,"anatomy preservation must allow a real collapse to the floor");
}

static void LostSleepingSupport()
{
	const auto rig=CapturedRig(); const Pose rest=reactrig::Capture(rig);
	const Vec3 neck{rig[4][12],rig[4][13],rig[4][14]},axis{rig[5][0],rig[5][1],rig[5][2]};
	Reset();Plane({0,0,1});
	Body body;body.Reset(rest,{},&neck,&axis);body.Impulse(Chest,{},true);
	for(int i=0;i<600;i++) body.Advance(1.f/60,rest,NativeSweep,nullptr);
	const float z=body.Positions()[Pelvis].z;
	Reset();
	for(int i=0;i<30;i++) body.Advance(1.f/60,rest,NativeSweep,nullptr);
	Check(body.State()==Mode::Falling&&body.Positions()[Pelvis].z<z-.3f,
		"sleeping corpse must wake and fall when its supporting geometry disappears");
}

static void TranslationRetainsSkin()
{
	Reset();Plane({0,1,0},-.3f);Plane({0,-1,0},-.3f);
	Pose pose=reactrig::Capture(CapturedRig());for(auto &p:pose) p.z+=2;
	Body body;body.Reset(pose);body.Momentum({0,4,0});body.Momentum({0,-4,0});
	Check(body.Translate({0,-.1f,0},NativeSweep,nullptr)>0,"a body can approach the corridor wall");
	float residual=0;
	reactshape::Cover(body.Reference(),[&](const reactshape::Sphere &sphere) {
		const Vec3 point=sphere.Position(body.Positions());
		const Contact contact=NativeSweep(point,point,sphere.radius,nullptr);
		residual=std::max(residual,Length(Sub(point,contact.position)));
	},&body.Positions());
	Check(residual<=.0005f,"rigid translation must certify destination skin as well as its swept path");
	Check(body.Translate({.25f,0,0},NativeSweep,nullptr)>.99f,"a wall contact must preserve a genuinely clear tangent escape");
}

static void PassiveHeadImpactSettles()
{
	Reset();Plane({0,0,1});
	const auto rig=CapturedRig();Pose pose=reactrig::Capture(rig);
	for(auto &p:pose) p=Add(p,{1,-2,0});
	const Vec3 neck{rig[4][12]+1,rig[4][13]-2,rig[4][14]},axis{rig[5][0],rig[5][1],rig[5][2]};
	std::mt19937 random(24191);std::uniform_real_distribution<float> modest(-2.5f,2.5f);
	Vec3 impulse{};for(int i=0;i<2;i++) impulse={modest(random),modest(random),modest(random)};
	Body body;body.Reset(pose,{},&neck,&axis);body.Impulse(Head,impulse,true);
	for(int i=0;i<600;i++) body.Advance(1.f/60,pose,NativeSweep,nullptr);
	float speed=0;for(int i=0;i<Count;i++) speed=std::max(speed,Length(body.Velocity(Joint(i))));
	Check(body.Active()&&body.State()==Mode::Settled&&speed<.001f,
		"passive head-impact constraints must dissipate motion instead of sustaining a corpse limit cycle");
}

static void WholeShinCollision()
{
	const auto rig=CapturedRig(); Pose rest=reactrig::Capture(rig);
	for(auto &p:rest) p.y-=.2f;
	const Vec3 neck{rig[4][12],rig[4][13]-.2f,rig[4][14]},axis{rig[5][0],rig[5][1],rig[5][2]};
	Reset();
	objects.push_back({{{{-.6f,0,.29f},{.6f,0,.29f},{.6f,0,.43f}},{{-.6f,0,.29f},{.6f,0,.43f},{-.6f,0,.43f}}},false,false});
	Body body; body.Reset(rest,{0,25,0},&neck,&axis); body.Impulse(Chest,{},true);
	body.Advance(1.f/60,rest,NativeSweep,nullptr);
	const Vec3 middle=Mul(Add(body.Positions()[KneeL],body.Positions()[FootL]),.5f);
	std::printf("thin shin wall: middle y=%.6f z=%.6f\n",middle.y,middle.z);
	Check(middle.y<-.04f||middle.z<.20f||middle.z>.52f,"a fast shin must not cross a finite wall between clear endpoint spheres");
}

int main()
{
	LandingAnatomy(); WholeShinCollision(); LostSleepingSupport(); TranslationRetainsSkin(); PassiveHeadImpactSettles();
	std::printf("reactsolver: %d failures\n",failures);
	return failures?1:0;
}

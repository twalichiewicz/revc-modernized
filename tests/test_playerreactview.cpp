// Exercise fpgun's actual frame preparation with the measured arm fixture.
#include "vmtestarm.h"
#ifndef PLAYERREACT_VIEW_SOURCE
#define PLAYERREACT_VIEW_SOURCE "../src/fpgun.cpp"
#endif
#include PLAYERREACT_VIEW_SOURCE
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
namespace game {Api api{};}
Config gConfig;
void Log(const char *,...){}
namespace hooks {void *Install(const char *,void *){return nullptr;}}
static playerreact::Response gResponse;
static bool gFirstPerson=true,gSwimming=false,gClimbing=false,gDodge=false;
static float gAimed;
static vm::Vec3 gViewRight{1,0,0},gViewForward{0,1,0};
namespace playerreact {
Response Current(uint8_t *){return gResponse;}
vm::Frame ViewTransform(uint8_t *){return ViewTransform(gResponse,{gViewRight.x,gViewRight.y,gViewRight.z},{gViewForward.x,gViewForward.y,gViewForward.z},gAimed>.5f);}
vm::Frame GuardHand(uint8_t *,bool left){return GuardHand(gResponse,{gViewRight.x,gViewRight.y,gViewRight.z},{gViewForward.x,gViewForward.y,gViewForward.z},left);}
}
namespace camera {bool FirstPersonOnFoot(){return gFirstPerson;}bool InSeat(){return false;}float AimBlend(){return gAimed;}float StepPhase(){return 0;}float StepSpeed(){return 0;}}
namespace swim {bool Swimming(){return gSwimming;}}
namespace climb {bool Active(){return gClimbing;}}
namespace shootdodge {bool Posing(){return gDodge;}}
namespace fpshoot {bool Shooting(){return false;}bool TriggerHeld(){return false;}float Interval(int32_t){return .2f;}}
namespace steering {bool Grips(uint8_t *,wheelgrip::Grip &,wheelgrip::Grip &){return false;}}
namespace shaders {bool CameraMatrices(float *,float *){return true;}bool Ready(){return true;}SavedStates::SavedStates(){for(auto &value:values_)value=nullptr;}SavedStates::~SavedStates(){}}
RwRaster *CreateRaster(int,int,const std::vector<uint8_t>&){return reinterpret_cast<RwRaster *>(1);}
namespace vm {const Embedded *FindEmbedded(int weapon){static Embedded embedded{17,"fixture",nullptr,nullptr};return weapon>=17&&weapon<=33?&embedded:nullptr;}}
static int failures;
#define CHECK(c)do{if(!(c)){std::printf("FAIL line %d: %s\n",__LINE__,#c);++failures;}}while(0)
static uint8_t gPed[0x900]{},gClump[4]{},gGeometry[4]{},gMaterial[4]{},gTexture[4]{},gCam[0x900]{};
static uint32_t gTime=1000,gFrame=1;
static float gStep=1,gRig[64*16]{};
static void *Atomics(void *,void *(*cb)(void *,void *),void *data){return cb(gGeometry,data);}
static void *Geometry(const void *){return gGeometry;}
static void *Material(const void *,int){return gMaterial;}
static void *Texture(const void *){return gTexture;}
static void *Raster(const void *){return gTexture;}
static int Count(const void *){return 100;}
static void *Hierarchy(void *){return gClump;}
static float *Matrices(void *){return gRig;}
static int Bone(void *,int tag){return tag>=0&&tag<64?tag:-1;}
static uint8_t *Player(){return gPed;}
static void Frame(){++gFrame;gTime+=20;for(int b=0;b<64;++b)vm::ToRw(vm::Identity(),gRig+b*16);}
int main()
{
	using namespace fpgun;
	game::api.FindPlayerPed=Player;game::api.timeMs=&gTime;game::api.timeStep=&gStep;game::api.theCamera=gCam;
	*reinterpret_cast<uint8_t **>(gPed+0x58)=gClump;*reinterpret_cast<float *>(gPed+0x44c)=100;
	*reinterpret_cast<int32_t *>(gPed+0x2fc)=game::kPedIdle;
	gConfig.firstPersonGuns=true;
	gReady=true;gFrameCounter=&gFrame;
	rw.ForAllAtomics=Atomics;rw.AtomicGeometry=Geometry;rw.Material=Material;rw.MaterialTexture=Texture;rw.TextureRaster=Raster;
	rw.NumVertices=Count;rw.NumTriangles=Count;rw.Hierarchy=Hierarchy;rw.BoneMatrices=Matrices;rw.BoneIndex=Bone;
	gBuilt.key={gGeometry,gTexture,0,100,100};gBuilt.arms=vm::Articulate(vmtest::Body());gBuilt.ok=gBuilt.arms.ok;
	gBuilt.indices.resize(1);gBuilt.indicesLeft.resize(1);
	CHECK(gBuilt.ok);
	gResponse={.8f,{0,.38f,1.4f},{0,-1,0}};
	Frame();
	CHECK(Prepare(gPed));
	CHECK(gDraw && gLeftShown && !Ready());
	for(bool left:{false,true}){
		const vm::Arm &arm=left?gBuilt.arms.left:gBuilt.arms.right;
		const vm::Frame *matrices=left?gMatricesLeft:gMatrices;
		const auto wrist=vm::Apply(matrices[vm::kHand],arm.bind[vm::kHand].pos);
		CHECK(vm::Length(vm::Sub(wrist,playerreact::GuardHand(gPed,left).pos))<.0001f);
	}
	// A left palm must retain its own shoulder; the melee ArmTo helper assumes the right shoulder.
	for(float heading:{0.0f,1.2f,2.8f})for(float angle:{0.0f,1.5707963f,-1.5707963f,3.1415926f}) {
		const auto turn=vm::TurnAbout({0,0,1},heading,{});
		gViewRight=vm::Rotate(turn,{1,0,0});gViewForward=vm::Rotate(turn,{0,1,0});
		const auto normal=vm::Rotate(turn,{std::sin(angle),-std::cos(angle),0});
		gResponse={.8f,{0,.38f,1.4f},{normal.x,normal.y,normal.z}};
		gDraw=false;Frame();CHECK(Prepare(gPed));
		for(bool left:{false,true}) {
			const auto &arm=left?gBuilt.arms.left:gBuilt.arms.right;
			const auto *matrix=left?gMatricesLeft:gMatrices;
			const auto shoulder=vm::Apply(matrix[vm::kUpper],arm.bind[vm::kUpper].pos);
			const auto elbow=vm::Apply(matrix[vm::kFore],arm.bind[vm::kFore].pos);
			const auto wrist=vm::Apply(matrix[vm::kHand],arm.bind[vm::kHand].pos);
			CHECK(std::fabs(shoulder.x-(left?-.19f:.19f))<.001f);
			CHECK(elbow.y>=shoulder.y-.002f && wrist.y>shoulder.y+.10f);
		}
	}
	gViewRight={1,0,0};gViewForward={0,1,0};
	// Draw consumes the prepared frame. Disabling the response releases its arms immediately.
	gDraw=false;gResponse={};Frame();CHECK(!Prepare(gPed));CHECK(!gActive);
	for(bool *owner:{&gSwimming,&gClimbing,&gDodge}){
		gResponse={.8f,{0,.38f,1.4f},{0,-1,0}};*owner=true;Frame();CHECK(!Prepare(gPed));*owner=false;
	}
	gResponse={.8f,{0,.38f,1.4f},{0,-1,0}};gFirstPerson=false;Frame();CHECK(!Prepare(gPed));gFirstPerson=true;
	// The actual gun frame and its posed wrist receive the same bounded contact transform.
	*reinterpret_cast<int32_t *>(gPed+0x550)=17;
	GunModel pistol;
	pistol.ok=true;pistol.fittedFor=gBuilt.key;pistol.atlas=reinterpret_cast<RwRaster *>(1);
	pistol.gun.hold=vm::Identity();pistol.gun.holdLeft=vm::Identity();
	pistol.gun.framing={{0,-.30f,.04f},0,0,.15f,.02f};
	pistol.gun.sightFront={0,.30f,0};pistol.gun.muzzle={0,.4f,0};
	gGuns[17]=pistol;
	gResponse={};
	for(int i=0;i<60;++i){gDraw=false;Frame();Prepare(gPed);}
	CHECK(gActive && gGun);
	const auto ordinary=gGunView;
	gResponse={.8f,{0,.38f,1.4f},{0,-1,0}};gDraw=false;Frame();CHECK(Prepare(gPed));
	CHECK(vm::Length(vm::Sub(ordinary.pos,gGunView.pos))>.02f);
	const auto wrist=vm::Apply(gMatrices[vm::kHand],gBuilt.arms.right.bind[vm::kHand].pos);
	CHECK(vm::Length(vm::Sub(wrist,vm::HandFor(gGun->gun,gGunView).pos))<.0001f);
	gResponse={};gAimed=1;
	for(int i=0;i<60;++i){gDraw=false;Frame();Prepare(gPed);}
	const auto aimed=gGunView;
	gResponse={.8f,{0,.38f,1.4f},{0,-1,0}};gDraw=false;Frame();CHECK(Prepare(gPed));
	CHECK(vm::Length(vm::Sub(aimed.y,gGunView.y))<.0001f);
	// Real one- and two-handed gun framing: contact moves hands/gun around their
	// measured shoulders, across front/side/rear contacts and world-view headings.
	float worstShoulderDrift=0;
	int badShoulders=0,weaponCases=0;
	for(int type=17;type<=33;++type)for(float aimedAmount:{0.0f,1.0f}) {
		const char *names[]={"colt45","python","chromegun","shotgspa","buddyshot","tec9","uzi","ingramsl","mp5lng","m4","ruger","sniper","laser","rocketla","flame","m60","minigun"};
		const std::string path=std::string("assets/guns/")+names[type-17]+".vmg";
		std::ifstream asset(path,std::ios::binary);
		const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(asset)),std::istreambuf_iterator<char>());
		std::string why;CHECK(!bytes.empty() && vm::ParseGun(bytes.data(),bytes.size(),pistol.gun,why));
		*reinterpret_cast<int32_t *>(gPed+0x550)=type;
		gGuns[type]=pistol;gAimed=aimedAmount;gResponse={};gViewRight={1,0,0};gViewForward={0,1,0};
		for(int i=0;i<60;++i){gDraw=false;Frame();Prepare(gPed);}
		vm::Vec3 shoulders[2],elbows[2],wrists[2];
		const auto baseGun=gGunView;
		for(bool left:{false,true}) {
			const auto &arm=left?gBuilt.arms.left:gBuilt.arms.right;
			const auto *matrices=left?gMatricesLeft:gMatrices;
			shoulders[left]=vm::Apply(matrices[vm::kUpper],arm.bind[vm::kUpper].pos);
			elbows[left]=vm::Apply(matrices[vm::kFore],arm.bind[vm::kFore].pos);
			wrists[left]=vm::Apply(matrices[vm::kHand],arm.bind[vm::kHand].pos);
		}
		for(float weight:{.05f,.4f,.8f,.95f})for(float heading:{0.0f,1.2f,2.8f})for(float angle:{0.0f,1.5707963f,-1.5707963f,3.1415926f}) {
			const auto turn=vm::TurnAbout({0,0,1},heading,{});
			gViewRight=vm::Rotate(turn,{1,0,0});gViewForward=vm::Rotate(turn,{0,1,0});
			const auto normal=vm::Rotate(turn,{std::sin(angle),-std::cos(angle),0});
			gResponse={weight,{0,.38f,1.4f},{normal.x,normal.y,normal.z}};gDraw=false;Frame();CHECK(Prepare(gPed));
			++weaponCases;
			if(aimedAmount>0)CHECK(vm::Length(vm::Sub(gGunView.y,baseGun.y))<.0001f);
			for(bool left:{false,true}) {
				if(left&&!gLeftShown)continue;
				const auto &arm=left?gBuilt.arms.left:gBuilt.arms.right;
				const auto *matrices=left?gMatricesLeft:gMatrices;
				const auto shoulder=vm::Apply(matrices[vm::kUpper],arm.bind[vm::kUpper].pos);
				const auto elbow=vm::Apply(matrices[vm::kFore],arm.bind[vm::kFore].pos);
				const auto hand=vm::Apply(matrices[vm::kHand],arm.bind[vm::kHand].pos);
				const float drift=vm::Length(vm::Sub(shoulder,shoulders[left]));
				worstShoulderDrift=std::max(worstShoulderDrift,drift);badShoulders+=drift>.001f;
				const float upper=vm::Length(vm::Sub(arm.bind[vm::kFore].pos,arm.bind[vm::kUpper].pos));
				const float fore=vm::Length(vm::Sub(arm.bind[vm::kHand].pos,arm.bind[vm::kFore].pos));
				CHECK(std::fabs(vm::Length(vm::Sub(elbow,shoulder))-upper)<.0001f);
				CHECK(std::fabs(vm::Length(vm::Sub(hand,elbow))-fore)<.0001f);
				CHECK(vm::Length(vm::Sub(hand,shoulder))<=std::max(vm::Length(vm::Sub(wrists[left],shoulders[left])),upper+fore-.002f)+.0001f);
				const auto expected=left?vm::LeftPlace(gGun->gun,gGunView,gGunView,gParts,gGun->fitLeft.part[vm::kDigitIndex],{0,0,false}).hand:vm::HandFor(gGun->gun,gGunView);
				const auto actual=vm::Compose(matrices[vm::kHand],arm.bind[vm::kHand]);
				CHECK(vm::Length(vm::Sub(actual.pos,expected.pos))<.0001f);
				CHECK(vm::Length(vm::Sub(actual.x,expected.x))<.0001f && vm::Length(vm::Sub(actual.y,expected.y))<.0001f);
				CHECK(elbow.y-shoulder.y>=std::min(0.0f,elbows[left].y-shoulders[left].y)-.002f);
				CHECK(hand.y>shoulder.y+.04f);
			}
		}
	}
	if(badShoulders){std::printf("FAIL: %d arm poses drag anchored shoulders, worst %.6fm across %d real-asset contact cases\n",badShoulders,worstShoulderDrift,weaponCases);++failures;}
	else std::printf("viewmodel anatomy: %d real-asset contact cases, max shoulder drift %.6fm\n",weaponCases,worstShoulderDrift);
	if(failures)return 1;
	std::puts("all native first-person contact arm frame tests passed");
}

// Drive the actual shared player control/render hooks with native collision and rig fixtures.
#include "../src/player.cpp"
#include "playerreact.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>

namespace game { Api api{}; }
void Log(const char *,...) {}
namespace hooks { void *Install(const char *,void *) {return nullptr;} }
static bool gSwimming,gClimbing,gDodge,gCrouch,gAim;
namespace climb {void BeforeControl(uint8_t *){} void AfterControl(uint8_t *){} bool Active(){return gClimbing;} bool Posing(){return gClimbing;} float Pose(body::Pose &,float){return 0;} }
namespace swim {void AfterControl(uint8_t *){} bool Swimming(){return gSwimming;} bool Posing(){return gSwimming;} float Pose(body::Pose &){return 0;} bool RefusesDamage(const uint8_t *,int32_t){return false;} }
namespace crouch {void AfterControl(uint8_t *){} bool Posing(){return gCrouch;} float Pose(uint8_t *,body::Pose &){return 0;} }
namespace shootdodge {void AfterControl(uint8_t *){} bool Posing(){return gDodge;} float Pose(uint8_t *,body::Pose &){return 0;} }
namespace aimcam {bool BeforeControl(){return false;}void AfterControl(uint8_t *,bool){}void Lower(uint8_t *){}void AfterPlayerWeapon(uint8_t *,uint8_t *){}void PoseArms(uint8_t *){} bool Aiming(){return gAim;} }
namespace fpwalk {bool BeforeControl(){return false;}void AfterControl(uint8_t *,bool){}bool BeforeWeapon(){return false;}void AfterWeapon(bool){} }
namespace drivers {void Frame(){} }
namespace gaze {void Frame(uint8_t *){} }
namespace reactions {void Frame(){}void BeforePreRender(uint8_t *){}void AfterPreRender(uint8_t *){}void Hit(uint8_t *,uint8_t *,int,int){} }
namespace achievements { void Damaged(uint8_t *, uint8_t *, int, bool) {} }
namespace driveby {void PilotHolster(uint8_t *){}void PreRender(uint8_t *){} }
namespace seatgun {void BeforePose(uint8_t *){}void Pose(uint8_t *){} }
namespace body {bool Prepare(uint8_t *){return false;}void Apply(uint8_t *,const Pose &,float){}void ApplyOver(uint8_t *,const Pose &,float){} }
namespace steering {void PoseDriver(uint8_t *){} }
namespace jetboardingpose {bool BeforePedRender(uint8_t *){return false;}void AfterPedRender(uint8_t *){} }
namespace fpshoot {void AfterWeapon(uint8_t *,uint8_t *){}bool Shooting(){return false;} }
namespace falls {float Land(uint8_t *,int32_t,float &){return 0;}void Landed(uint8_t *,float){} }
namespace combat {void Hurt(uint8_t *,uint8_t *,bool){} }
namespace rumble {void Damaged(const uint8_t *,const uint8_t *,int32_t,float){} }
namespace fpfeedback {bool CanPose(float h,int,bool){return h>0;}DamageFacts ObserveDamage(bool b,float,float,float,float){return {b,false,0,0};} }
static uint8_t gPed[0x900]{},gOther[0x900]{},gClump[4]{},gNewClump[4]{},gCam[0x900]{};
alignas(16) static uint8_t gPad[0x200]{};
static bool gPadAvailable=true;
static uint8_t *gPlayer=gPed;
static int gGeneration=17;
static bool gValidReference=true;
class CPed;
class CPools {public:static int GetPedRef(CPed *);};
int CPools::GetPedRef(CPed *ped)
{
	return gValidReference && reinterpret_cast<uint8_t *>(ped)==gPlayer ? gGeneration : -1;
}
static uint8_t *Lookup(int handle){return gValidReference && handle==gGeneration?gPlayer:nullptr;}
static uint32_t gTime=1000;
static float gStep=1;
static float gRig[playerreact::kBones*16]{};
static playerreact::Matrices gNative,gCameraSaved;
static bool gHadCamera,gDoContact=true;
static int gNativeControls,gNativeRenders;
static int failures;
#define CHECK(c) do {if(!(c)){std::printf("FAIL line %d: %s\n",__LINE__,#c);++failures;}}while(0)
static uint8_t *Player(){return gPlayer;}
static uint8_t *Pad(int index){return index==0 && gPadAvailable?gPad:nullptr;}
static uint8_t *Hierarchy(uint8_t *){return gClump;}
static float *Matrices(uint8_t *){return gRig;}
static int BoneIndex(uint8_t *,int tag)
{
	const int tags[]={0,1,2,3,4,5,31,32,33,34,35,21,22,23,24,25,41,42,43,51,52,53};
	for(int i=0;i<playerreact::kBones;++i)if(tag==tags[i])return i;
	return -1;
}
namespace camera {
void BeforePreRender(uint8_t *ped){if(ped==gPlayer && gHadCamera)std::memcpy(gRig,gCameraSaved.data(),sizeof gRig);}
void AfterPreRender(uint8_t *ped){if(ped==gPlayer){std::memcpy(gCameraSaved.data(),gRig,sizeof gRig);gHadCamera=true;gRig[5*16]=.001f;}}
void Damaged(uint8_t *,uint8_t *,float,float){}
float AimBlend(){return gAim?1:0;}
}
static void NativeControl(uint8_t *ped)
{
	++gNativeControls;
	// Native contact input. This uses the same public seam as ProcessEntityCollision's adapter.
	game::EntitySpeed(ped)={.03f,.04f,.005f};
	if(gDoContact)playerreact::Contact(ped,{0,.38f,1.4f},{0,-1,0},{0,3,0});
}
static void NativePreRender(uint8_t *)
{
	++gNativeRenders;
	// The native animation must receive original bone matrices, including after camera restores its saved guard.
	CHECK(!std::memcmp(gRig,gNative.data(),sizeof gRig));
	std::memcpy(gRig,gNative.data(),sizeof gRig);
}
static void Init()
{
	game::api.FindPlayerPed=Player;game::api.PoolsGetPed=Lookup;game::api.timeMs=&gTime;game::api.timeStep=&gStep;
	game::api.GetAnimHierarchyFromSkinClump=Hierarchy;game::api.HAnimMatrixArray=Matrices;game::api.HAnimIDGetIndex=BoneIndex;
	game::api.theCamera=gCam;game::api.GetPad=Pad;
	const vm::Vec3 p[playerreact::kBones]={{0,0,1},{0,0,1},{0,0,1.13f},{0,0,1.35f},{0,0,1.5f},{0,0,1.7f},
	 {-.12f,0,1.4f},{-.22f,0,1.4f},{-.36f,.03f,1.12f},{-.40f,.10f,.9f},{-.43f,.13f,.87f},
	 {.12f,0,1.4f},{.22f,0,1.4f},{.36f,.03f,1.12f},{.40f,.10f,.9f},{.43f,.13f,.87f},
	 {-.12f,0,.9f},{-.12f,0,.5f},{-.12f,0,.1f},{.12f,0,.9f},{.12f,0,.5f},{.12f,0,.1f}};
	for(int i=0;i<playerreact::kBones;++i){auto f=vm::Identity();f.pos=p[i];vm::ToRw(f,gNative[size_t(i)].data());}
	std::memcpy(gRig,gNative.data(),sizeof gRig);
	*reinterpret_cast<uint8_t **>(gPed+0x58)=gClump;*reinterpret_cast<float *>(gPed+0x44c)=100;
	*reinterpret_cast<int32_t *>(gPed+0x2fc)=1;*reinterpret_cast<uint32_t *>(gPed+0x60)=3|game::kEntityUsesCollision;
	*reinterpret_cast<CVector *>(gPed+0x18)={0,1,0};
	*reinterpret_cast<CVector *>(gCam)={1,0,0};*reinterpret_cast<CVector *>(gCam+0x10)={0,1,0};
	gCam[0x63]=1; // ordinary camera: CCamera::m_bLookingAtPlayer
	player::orig_ProcessControl=NativeControl;player::orig_PreRender=NativePreRender;
}
int main()
{
	Init();
	CHECK(dlsym(RTLD_MAIN_ONLY,"_ZN6CPools9GetPedRefEP4CPed")!=nullptr);
	std::array<uint8_t,sizeof gPed> before;std::memcpy(before.data(),gPed,sizeof gPed);
	player::Hook_ProcessControl(gPed);
	CHECK(gNativeControls==1);
	CHECK(game::EntitySpeed(gPed).x==.03f && game::EntitySpeed(gPed).y==.04f);
	CHECK(game::Health(gPed)==100 && game::PedState(gPed)==game::kPedIdle);
	player::Hook_PreRender(gPed);
	CHECK(gNativeRenders==1);
	CHECK(std::fabs(gRig[5*16+13]-gNative[5][13])>.025f);
	CHECK(gRig[9*16+13]>gNative[9][13]+.10f);
	CHECK(gRig[14*16+13]>gNative[14][13]+.10f);
	CHECK(playerreact::Current(gPed).weight>.3f);
	// Every ped field except native-published speed remains byte-for-byte the same.
	std::memcpy(before.data()+0x84,gPed+0x84,sizeof(CVector));
	CHECK(!std::memcmp(before.data(),gPed,sizeof gPed));
	// Repeated native render never accumulates bends or scale.
	player::Hook_PreRender(gPed);
	CHECK(gNativeRenders==2);
	// Script cameras can own Tommy without widescreen. A live guard must yield
	// through the actual control/render sequence and restore his native pose.
	gCam[0x63]=0;
	gCam[0x75]=0;
	gTime+=20;
	player::Hook_ProcessControl(gPed);
	player::Hook_PreRender(gPed);
	CHECK(playerreact::Current(gPed).weight==0);
	CHECK(!std::memcmp(gCameraSaved.data(),gNative.data(),sizeof gRig));
	CHECK(game::Health(gPed)==100 && game::PedState(gPed)==game::kPedIdle);
	CHECK(game::EntitySpeed(gPed).x==.03f && game::EntitySpeed(gPed).y==.04f);
	// Returning to the ordinary camera accepts a fresh native contact.
	gCam[0x63]=1;
	gTime+=20;
	player::Hook_ProcessControl(gPed);
	player::Hook_PreRender(gPed);
	CHECK(playerreact::Current(gPed).weight>.3f);
	CHECK(gCameraSaved[9][13]>gNative[9][13]+.10f);
	// Scripts can disable player controls while the ordinary player camera stays up.
	*reinterpret_cast<uint16_t *>(gPad+0xf0)=1;
	gTime+=20;
	player::Hook_ProcessControl(gPed);
	player::Hook_PreRender(gPed);
	CHECK(playerreact::Current(gPed).weight==0);
	CHECK(!std::memcmp(gCameraSaved.data(),gNative.data(),sizeof gRig));
	CHECK(game::Health(gPed)==100 && game::PedState(gPed)==game::kPedIdle);
	CHECK(game::EntitySpeed(gPed).x==.03f && game::EntitySpeed(gPed).y==.04f);
	*reinterpret_cast<uint16_t *>(gPad+0xf0)=0;
	gDoContact=false;
	gTime+=20;
	player::Hook_ProcessControl(gPed);
	player::Hook_PreRender(gPed);
	CHECK(playerreact::Current(gPed).weight==0); // no pre-script guard replay
	gDoContact=true;
	gTime+=20;
	player::Hook_ProcessControl(gPed);
	player::Hook_PreRender(gPed);
	CHECK(playerreact::Current(gPed).weight>.3f);
	// Missing pad context also yields ownership, without dereferencing a null API/pad.
	for(bool missingFunction:{true,false}){
		game::api.GetPad=missingFunction?nullptr:Pad;
		gPadAvailable=missingFunction;
		gTime+=20;
		player::Hook_ProcessControl(gPed);
		player::Hook_PreRender(gPed);
		CHECK(playerreact::Current(gPed).weight==0);
		CHECK(!std::memcmp(gCameraSaved.data(),gNative.data(),sizeof gRig));
		game::api.GetPad=Pad;gPadAvailable=true;
		gDoContact=false;
		gTime+=20;
		player::Hook_ProcessControl(gPed);
		player::Hook_PreRender(gPed);
		CHECK(playerreact::Current(gPed).weight==0);
		gDoContact=true;
		gTime+=20;
		player::Hook_ProcessControl(gPed);
		player::Hook_PreRender(gPed);
		CHECK(playerreact::Current(gPed).weight>.3f);
	}
	gDoContact=false;
	for(int i=0;i<60;++i){gTime+=20;player::Hook_ProcessControl(gPed);player::Hook_PreRender(gPed);}
	CHECK(playerreact::Current(gPed).weight==0);
	CHECK(!std::memcmp(gCameraSaved.data(),gNative.data(),sizeof gRig));
	for(int state:{game::kPedFall,game::kPedGetUp,game::kPedJump,game::kPedDriving,game::kPedDie,game::kPedDead}){
		game::SetPedState(gPed,state);playerreact::Contact(gPed,{0,.38f,1.4f},{0,-1,0},{0,3,0});
		CHECK(playerreact::Current(gPed).weight==0);
	}
	game::SetPedState(gPed,game::kPedIdle);
	for(bool *owner:{&gSwimming,&gClimbing,&gDodge,&gCrouch}){
		*owner=true;playerreact::Contact(gPed,{0,.38f,1.4f},{0,-1,0},{0,3,0});
		CHECK(playerreact::Current(gPed).weight==0);*owner=false;
	}
	gTime+=20;playerreact::Contact(gPed,{0,.38f,1.4f},{0,-1,0},{0,3,0});
	CHECK(playerreact::Current(gPed).weight>0);
	// A world load can reuse every address and keep a nearby clock: only the pool generation changes.
	player::Hook_PreRender(gPed);
	++gGeneration;
	gRig[2*16+12]=.123f; // a fresh generation's animation at the reused buffer
	playerreact::BeforeControl(gPed);
	CHECK(gRig[2*16+12]==.123f); // no stale snapshot write into the new generation
	CHECK(playerreact::Current(gPed).weight==0);
	std::memcpy(gRig,gNative.data(),sizeof gRig);gHadCamera=false;
	playerreact::Contact(gPed,{0,.38f,1.4f},{0,-1,0},{0,3,0});
	CHECK(playerreact::Current(gPed).weight>0);
	playerreact::AfterPreRender(gPed);
	gValidReference=false;
	gRig[2*16+12]=.777f;
	playerreact::BeforePreRender(gPed);
	CHECK(gRig[2*16+12]==.777f);
	CHECK(playerreact::Current(gPed).weight==0);
	playerreact::Contact(gPed,{0,.38f,1.4f},{0,-1,0},{0,3,0});
	CHECK(playerreact::Current(gPed).weight==0);
	gValidReference=true;
	playerreact::Contact(gPed,{0,.38f,1.4f},{0,-1,0},{0,3,0});
	gTime=10;playerreact::AfterControl(gPed);CHECK(playerreact::Current(gPed).weight==0);
	gTime+=20;playerreact::Contact(gPed,{0,.38f,1.4f},{0,-1,0},{0,3,0});
	*reinterpret_cast<uint8_t **>(gPed+0x58)=gNewClump;
	playerreact::AfterControl(gPed);CHECK(playerreact::Current(gPed).weight==0);
	gTime+=20;playerreact::Contact(gPed,{0,.38f,1.4f},{0,-1,0},{0,3,0});
	gPlayer=gOther;playerreact::AfterControl(gOther);CHECK(playerreact::Current(gOther).weight==0);
	if(failures)return 1;
	std::puts("all native player control/contact/pose hook tests passed");
}

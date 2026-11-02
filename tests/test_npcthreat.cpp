// The actual Frame path with arm64-native object layouts. Only the external
// engine calls are substituted; perception, ownership and timing are real.
#include "../src/drivers.cpp"
#include <cstdio>
#include <cstring>
namespace game { Api api{}; }
Config gConfig;
void Log(const char *, ...) {}
static bool gAimed=true,gVisible=true,gControlled=true,gCut=false,gMissingNative=false;
static int32_t gCurrentArea=0;
namespace aimcam { bool Aiming() { return gAimed; } }
namespace driveby { bool Aiming() { return false; } }
namespace camera { CVector ViewForward() { return {0,1,0}; } }
alignas(16) static uint8_t gPlayer[0x8d0],gPeople[3][0x8d0],gCar[0x6f0],gTargetCar[0x6f0],gPad[1024],gCamera[4096];
alignas(8) static uint8_t gCarsPool[32],gPeoplePool[32],gStats[3][0x38];
static uint8_t *gCars=gCarsPool,*gPeds=gPeoplePool,*gIgnored;
static uint8_t gCarFlags[1],gPedFlags[3];
static uint32_t gNow=1000;
static int gLeave,gFight,gFlee,gHands,gRam,gPhysics,gWantedLevel;
static uint8_t gWanted[128];
static CVector gSightFrom;
static uint8_t *Player() { return gPlayer; }
static uint8_t *Pad(int) { return gPad; }
static bool Target(uint8_t *) { return false; }
static uint8_t *GetCar(int) { return gCarFlags[0]&0x80?nullptr:gCar; }
static uint8_t *GetPed(int handle) { int index=(handle>>8)-1; return index>=0 && index<3 && !(gPedFlags[index]&0x80) && gPedFlags[index]==(handle&0xff)?gPeople[index]:nullptr; }
static int CarRef(uint8_t *) { return 257; }
static int PedRef(uint8_t *ped) { for(int i=0;i<3;i++) if(ped==gPeople[i]) return ((i+1)<<8)|gPedFlags[i]; return -1; }
static bool Controlled(uint8_t *) { return gControlled; }
static bool Sight(const CVector &from,const CVector &,bool,bool,bool,bool,bool,bool,bool) { gSightFrom=from; return gVisible; }
static void Objective(uint8_t *ped,int objective,void *target) {
	if(target!=gPlayer && !(objective==16 && target==gCar)) std::abort();
	if(objective==16) gLeave++;
	if(objective==8) gFight++;
	if(objective==10) gFlee++;
	*reinterpret_cast<int32_t *>(ped+0x19c)=objective;
}
static void Speech(uint8_t *,uint16_t) {}
static void PointGun(uint8_t *ped,uint8_t *threat) {
	if(threat!=gPlayer) std::abort();
	// Native ReactToPointGun refuses driving/uncontrolled actors and threats
	// farther than15m. Keep those external constraints visible in the seam.
	if(PedState(ped)>38 || !gControlled || std::hypot(EntityPos(ped).x-EntityPos(threat).x,EntityPos(ped).y-EntityPos(threat).y)>15) return;
	gHands++; *reinterpret_cast<int32_t *>(ped+0x310)=17;
}
static void WantedLevel(uint8_t *wanted,int level) { if(wanted!=gWanted) std::abort(); gWantedLevel=std::max(gWantedLevel,level); }
static void ClearWait(uint8_t *ped) { *reinterpret_cast<int32_t *>(ped+0x310)=0; }
static void Ram(uint8_t *car,uint8_t *target) { if(car!=gCar || target!=gTargetCar) std::abort(); gRam++; }
static void Physics(uint8_t *car) { if(car!=gCar) std::abort(); gPhysics++; }
extern "C" void *dlsym(void *,const char *name) {
	if(gMissingNative && !std::strcmp(name,"_ZN7CWanted20SetWantedLevelNoDropEi")) return nullptr;
#define BIND(n,p) if(!std::strcmp(name,n)) return reinterpret_cast<void *>(p)
	BIND("_ZN7CWanted20SetWantedLevelNoDropEi",WantedLevel);
	BIND("_ZN6CPools15ms_pVehiclePoolE",&gCars);
	BIND("_ZN6CPools11ms_pPedPoolE",&gPeds);
	BIND("_ZN6CPools13GetVehicleRefEP8CVehicle",CarRef);
	BIND("_ZN6CPools9GetPedRefEP4CPed",PedRef);
	BIND("_ZN12CCutsceneMgr10ms_runningE",&gCut);
	BIND("_ZN5CGame8currAreaE",&gCurrentArea);
	BIND("_ZN4CPed15ReactToPointGunEP7CEntity",PointGun);
	BIND("_ZN4CPed14ClearWaitStateEv",ClearWait);
	BIND("_ZN8CCarCtrl26SwitchVehicleToRealPhysicsEP8CVehicle",Physics);
	BIND("_ZN6CCarAI20TellCarToRamOtherCarEP8CVehicleS1_",Ram);
	return nullptr;
#undef BIND
}
static void Reset(bool inCar=false,int type=4,int fear=45,int temper=30,bool armed=false) {
	std::memset(gPlayer,0,sizeof(gPlayer)); std::memset(gPeople,0,sizeof(gPeople)); std::memset(gCar,0,sizeof(gCar));
	std::memset(gPad,0,sizeof(gPad)); std::memset(gStats,0,sizeof(gStats));
	gLeave=gFight=gFlee=gHands=gRam=gPhysics=gWantedLevel=0; gVisible=gControlled=gAimed=true; gCurrentArea=0; gCut=false;
	gNow+=11000; drivers::gDrivers.clear(); drivers::gLast=gNow;
	gPedFlags[0]=1; gPedFlags[1]=gPedFlags[2]=0x80; gCarFlags[0]=inCar?1:0x80;
	*reinterpret_cast<CVector *>(gPlayer+0x38)={10,20,1}; *reinterpret_cast<uint8_t **>(gPlayer+0x778)=gWanted;
	*reinterpret_cast<float *>(gPlayer+0x44c)=100; *reinterpret_cast<int32_t *>(gPlayer+0x550)=17;
	*reinterpret_cast<CVector *>(gCamera+0x30)={10,20,2};
	*reinterpret_cast<CVector *>(gPeople[0]+0x38)={10,28,1.5f}; *reinterpret_cast<CVector *>(gPeople[0]+0x18)={0,-1,0};
	*reinterpret_cast<float *>(gPeople[0]+0x44c)=100; *reinterpret_cast<int32_t *>(gPeople[0]+0x4f8)=type;
	gPeople[0][0x19a]=1; *reinterpret_cast<int32_t *>(gPeople[0]+0x2fc)=inCar?0x32:1;
	*reinterpret_cast<uint8_t **>(gPeople[0]+0x500)=gStats[0]; gStats[0][0x24]=fear; gStats[0][0x25]=temper;
	if(armed) { *reinterpret_cast<int32_t *>(gPeople[0]+0x550)=17; *reinterpret_cast<int32_t *>(gPeople[0]+0x55c)=30; }
	*reinterpret_cast<CVector *>(gCar+0x18)={0,-1,0}; *reinterpret_cast<uint8_t **>(gCar+0x218)=inCar?gPeople[0]:nullptr; gCar[0x294]=1;
	gPeople[0][0x4c0]=inCar; *reinterpret_cast<uint8_t **>(gPeople[0]+0x4b8)=inCar?gCar:nullptr;
}
static void Aim() { for(int i=0;i<8;i++) { gNow+=100; drivers::Frame(); } }
int main() {
	*reinterpret_cast<uint8_t **>(gCarsPool)=gCar; *reinterpret_cast<uint8_t **>(gCarsPool+8)=gCarFlags; *reinterpret_cast<int32_t *>(gCarsPool+0x10)=1;
	*reinterpret_cast<uint8_t **>(gPeoplePool)=gPeople[0]; *reinterpret_cast<uint8_t **>(gPeoplePool+8)=gPedFlags; *reinterpret_cast<int32_t *>(gPeoplePool+0x10)=3;
	api.timeMs=&gNow; api.theCamera=gCamera; api.FindPlayerPed=Player; api.GetPad=Pad; api.PadGetTarget=Target;
	api.PoolsGetVehicle=GetCar; api.PoolsGetPed=GetPed; api.IsPedInControl=Controlled; api.GetIsLineOfSightClear=Sight; api.ignoreEntity=&gIgnored;
	api.SetObjective=Objective; api.Say=Speech;
	if(!drivers::Install()) return 2;
	int failures=0;
#define CHECK(c,msg) do { if(!(c)) { std::printf("FAIL: %s\n",msg); failures++; } } while(0)
	Reset(); Aim(); CHECK(gHands==1 && gLeave==0,"on-foot civilian never enters native hands-up response");
	CHECK(gSightFrom.x==10 && gSightFrom.y==20 && gSightFrom.z==2 && !gIgnored,"perception origin/ignore reference corrupted");
	Reset(false,4,90,20); Aim(); CHECK(gFlee==1 && !gLeave,"frightened pedestrian never flees through native objective");
	Reset(false,7,20,70,true); Aim(); CHECK(gFight==1 && !gLeave,"armed gang member never fights back");
	Reset(true,4,90,20); Aim(); CHECK(!gLeave && gPhysics==1 && gCar[0x19d]==2,"fearful civilian driver always exits rather than driving away");
	Reset(true,4,20,80,true); Aim(); CHECK(gFight==1 && !gLeave,"armed aggressive driver cannot exit to fight");
	Reset(true,6,10,30,true); Aim(); CHECK(gFight==1 && !gLeave && gWantedLevel==2,"police driver does not engage/register armed on-foot threat");
	Reset(true,6,10,30,true); gPlayer[0x4c0]=1; *reinterpret_cast<uint8_t **>(gPlayer+0x4b8)=gTargetCar; Aim();
	CHECK(gRam==1 && !gLeave && gPhysics==1 && gWantedLevel==2,"police driver does not ram/register vehicle threat");
	Reset(); Aim();
	gAimed=false; for(int i=0;i<30;i++) { gNow+=100; drivers::Frame(); }
	gAimed=true; *reinterpret_cast<int32_t *>(gPeople[0]+0x310)=0; Aim();
	CHECK(gHands==2,"pedestrian decision never rearms after a separated threat");
	Reset(); gNow+=100; drivers::Frame(); gNow+=100; drivers::Frame();
	gPedFlags[0]=2; // same pool slot, new native generation: no accumulated dwell
	gNow+=100; drivers::Frame(); gNow+=100; drivers::Frame();
	CHECK(!gHands,"replacement pedestrian inherits a previous generation's aim dwell");
	Aim(); CHECK(gHands==1,"replacement pedestrian never makes its own decision");
	Reset(true); *reinterpret_cast<int32_t *>(gPeople[0]+0x2fc)=0x34; Aim();
	CHECK(!gHands && !gFight && !gFlee && !gLeave && !gPhysics,"driver exiting vehicle loses native transition ownership");
	for(int gate=0;gate<10;gate++) {
		Reset();
		if(gate==0) gPeople[0][0x19a]=2;
		if(gate==1) gCut=true;
		if(gate==2) gCurrentArea=1;
		if(gate==3) gVisible=false;
		if(gate==4) gControlled=false;
		if(gate==5) *reinterpret_cast<uint8_t **>(gPeople[0]+0x720)=gCar;
		if(gate==6) *reinterpret_cast<int32_t *>(gPlayer+0x550)=36; // camera is not a credible gun
		if(gate==7) *reinterpret_cast<CVector *>(gPeople[0]+0x18)={0,1,0};
		if(gate==8) *reinterpret_cast<uint8_t **>(gPeople[0]+0x1c8)=gPlayer;
		if(gate==9) *reinterpret_cast<int32_t *>(gPeople[0]+0x4f8)=13;
		Aim(); CHECK(!gHands && !gFlee && !gFight && !gLeave,"protected or imperceptible pedestrian seized");
	}
	Reset(true,6,10,30,true); gMissingNative=true;
	CHECK(!drivers::Install(),"missing native police binding reported success"); Aim();
	CHECK(!gFight && !gRam && !gLeave,"failed installation still changes native NPC behavior");
	gMissingNative=false; CHECK(drivers::Install(),"bindings did not recover after failed installation");
	if(failures) return 1;
	std::puts("npcthreat: all passed");
}

// Actual camera callback consumes the native pad fields; flight callbacks must
// see their original values again. No running game or real input is used.
static bool gTestDriveBy, gTestOwnedPlane;
static void (*gFlightFrameObserver)();
#include <dlfcn.h>
static void *CameraInputSymbol(void *, const char *);
#define dlsym CameraInputSymbol
#include "../src/camera.cpp"
#undef dlsym
#include "../src/flightinput.h"
#include <array>
#include <stdexcept>
#include <cstdio>
#include <cstring>
Config gConfig;
void Log(const char *, ...) {}
namespace hooks
{
void *Install(const char *, void *) { return nullptr; }
void *Chain(const char *, void *) { return nullptr; }
} // namespace hooks
namespace playerreact
{
void BeforeControl(uint8_t *) {}
void AfterControl(uint8_t *) {}
void BeforePreRender(uint8_t *) {}
void AfterPreRender(uint8_t *) {}
} // namespace playerreact
namespace climb
{
void BeforeControl(uint8_t *) {}
void AfterControl(uint8_t *) {}
bool Active() { return false; }
bool Posing() { return false; }
float Pose(body::Pose &, float) { return 0; }
} // namespace climb
namespace swim
{
void AfterControl(uint8_t *) {}
bool Swimming() { return false; }
bool Posing() { return false; }
float Pose(body::Pose &) { return 0; }
bool RefusesDamage(const uint8_t *, int32_t) { return false; }
bool EyePosition(uint8_t *, CVector &) { return false; }
} // namespace swim
namespace crouch
{
void AfterControl(uint8_t *) {}
bool Posing() { return false; }
float Pose(uint8_t *, body::Pose &) { return 0; }
} // namespace crouch
namespace shootdodge
{
void AfterControl(uint8_t *) {}
bool Posing() { return false; }
float Pose(uint8_t *, body::Pose &) { return 0; }
} // namespace shootdodge
namespace aimcam
{
bool BeforeControl() { return false; }
void AfterControl(uint8_t *, bool) {}
void Lower(uint8_t *) {}
void AfterPlayerWeapon(uint8_t *, uint8_t *) {}
void PoseArms(uint8_t *) {}
bool Aiming() { return false; }
} // namespace aimcam
namespace fpwalk
{
bool BeforeControl() { return false; }
void AfterControl(uint8_t *, bool) {}
bool BeforeWeapon() { return false; }
void AfterWeapon(bool) {}
} // namespace fpwalk
namespace drivers
{
void Frame() {}
} // namespace drivers
namespace reactions
{
void Frame() {}
void BeforePreRender(uint8_t *) {}
void AfterPreRender(uint8_t *) {}
void Hit(uint8_t *, uint8_t *, int, int) {}
bool PlayerDown() { return false; }
bool PlayerHeadView(Vec3 &, Vec3 &, Vec3 &, Vec3 &) { return false; }
} // namespace reactions
namespace driveby
{
void PilotHolster(uint8_t *) {}
void PreRender(uint8_t *) {}
bool Aiming() { return gTestDriveBy; }
CVector AimDirection() { return {0, 1, 0}; }
} // namespace driveby
namespace seatgun
{
void BeforePose(uint8_t *) {}
void Pose(uint8_t *) {}
} // namespace seatgun
namespace body
{
bool Prepare(uint8_t *) { return false; }
void Apply(uint8_t *, const Pose &, float) {}
void ApplyOver(uint8_t *, const Pose &, float) {}
} // namespace body
namespace steering
{
void PoseDriver(uint8_t *) {}
} // namespace steering
namespace fpshoot
{
void AfterWeapon(uint8_t *, uint8_t *) {}
bool Shooting() { return false; }
void Tick(float) {}
void Punch(float &a, float &b) { a = b = 0; }
void Jolt(float &a, float &b, float &c) { a = b = c = 0; }
} // namespace fpshoot
namespace falls
{
float Land(uint8_t *, int32_t, float &) { return 0; }
void Landed(uint8_t *, float) {}
} // namespace falls
namespace combat
{
void Hurt(uint8_t *, uint8_t *, bool) {}
} // namespace combat
namespace rumble
{
void Damaged(const uint8_t *, const uint8_t *, int32_t, float) {}
} // namespace rumble
namespace wanted
{
void Update() {}
} // namespace wanted
namespace traffic
{
void AfterCamera() {}
} // namespace traffic
namespace police
{
void Update() {}
} // namespace police
namespace heli
{
void Update() {}
} // namespace heli
namespace planeflight {
bool Owns(const uint8_t *) { return gTestOwnedPlane; }
void Frame(uint32_t) { if(gFlightFrameObserver)gFlightFrameObserver(); }
}
namespace flightmodel { void Frame(uint32_t) { if(gFlightFrameObserver)gFlightFrameObserver(); } }
namespace airportjets { void Frame(uint32_t) {} }
namespace airportheli { void Frame(uint32_t) {} }
namespace jetwash { void Frame(uint32_t) {} }
namespace doorgrab
{
void Update() {}
} // namespace doorgrab
namespace watch
{
void Frame() {}
} // namespace watch
namespace phone
{
void AfterWorld() {}
} // namespace phone
namespace sniper
{
bool TakeLeftView(CVector &) { return false; }
bool Wanted() { return false; }
bool Active() { return false; }
void Frame(float) {}
const scope::Output &Now()
{
	static scope::Output s{};
	return s;
}
} // namespace sniper
namespace fpgun
{
void CancelFrame() {}
} // namespace fpgun
namespace controls
{
bool LookFrozen() { return false; }
bool LookTurn(float &, float &) { return false; }
} // namespace controls
namespace seateye
{
float Lift(const uint8_t *, Vec3, bool, float) { return 0; }
} // namespace seateye
alignas(16) static uint8_t gInputPed[0x900], gInputCam[0x1000], gInputVehicles[2][0x6f0], gInputPad[0x200], gPool[0x18];
alignas(16) static uint8_t gInputHandling[0xdc];
static uint8_t gPoolFlags[2]={17,18}, *gPoolPointer=gPool, gAffected=1;
static bool gMission,gCutscene,gThrowCamera,gObserveRaw,gRejectReference;
static uint32_t gInputNow=1000;
static float gInputStep=1,gMouse[4];
static int gSnaps,gFollowReads,gPostFrames,gFailures;
static float gRudder,gLookX,gLookY,gMouseX;
static int16_t gCameraLeft,gCameraRight;
#define CHECK(c) do{if(!(c)){std::printf("FAIL line%d: %s\n",__LINE__,#c);++gFailures;}}while(0)
static uint8_t *InputPlayer(){return gInputPed;}
static uint8_t *InputPad(int){return gInputPad;}
static uint8_t *InputVehicle(int ref){const int i=ref>>8;return !gRejectReference&&i>=0&&i<2&&gPoolFlags[i]==uint8_t(ref)&&!(gPoolFlags[i]&0x80)?gInputVehicles[i]:nullptr;}
static bool Mission(){return gMission;}
static void *CameraInputSymbol(void *,const char *name)
{
 if(!std::strcmp(name,"_ZN6CPools15ms_pVehiclePoolE"))return &gPoolPointer;
 if(!std::strcmp(name,"_ZN11CTheScripts18IsPlayerOnAMissionEv"))return reinterpret_cast<void *>(Mission);
 if(!std::strcmp(name,"_ZN12CCutsceneMgr10ms_runningE"))return &gCutscene;
 return nullptr; // no RenderWare: the test exercises the original camera callback scope only
}
// Native CPad getters0x108b9c/0x108bc4/0x108bec, including disabled input.
static bool LookLeft(){return !game::PadControlsDisabled(gInputPad)&&game::PadNew(gInputPad,game::kPadLT)&&!game::PadNew(gInputPad,game::kPadRT);}
static bool LookRight(){return !game::PadControlsDisabled(gInputPad)&&game::PadNew(gInputPad,game::kPadRT)&&!game::PadNew(gInputPad,game::kPadLT);}
static bool LookBehind(){return !game::PadControlsDisabled(gInputPad)&&game::PadNew(gInputPad,game::kPadLT)&&game::PadNew(gInputPad,game::kPadRT);}
static int Appearance()
{
 // Exact native GetVehicleAppearance0x27dc48 and table0x32c9b8.
 constexpr int table[]={1,2,3,0,5,0,0,0,4};
 const auto *handling=*reinterpret_cast<uint8_t *const *>(gInputVehicles[0]+0x160);
 const unsigned flag=*reinterpret_cast<const uint16_t *>(handling+0xce)&15;
 return flag<9?table[flag]:0;
}
static void NativeCamera(uint8_t *camera)
{
 CHECK(camera==gInputCam);
 // CCam::Process0xb5b4c..0xb5b60 fills a newly initialized null target.
 auto &target=*reinterpret_cast<uint8_t **>(game::ActiveCam()+0x1b0);
 if(!target)target=*reinterpret_cast<uint8_t **>(camera+0x860);
 gLookX=game::PadNew(gInputPad,game::kPadRightStickX);
 gLookY=game::PadNew(gInputPad,game::kPadRightStickY);
 gMouseX=gMouse[2];
 gCameraLeft=game::PadNew(gInputPad,game::kPadLT);gCameraRight=game::PadNew(gInputPad,game::kPadRT);
 // FollowCar's0xbdd90/bdd9c and0xbe14c/be158 consume the same getters
 // before CCam::Process's0xb6718..0xb6770 chooses/stores its side direction.
 const int mode=*reinterpret_cast<uint16_t *>(game::ActiveCam()+0xc);
 if(mode==18)gFollowReads+=LookLeft()||LookRight()||LookBehind();
 int &direction=*reinterpret_cast<int *>(game::ActiveCam()+0x28);
 if(mode==3||mode==16||mode==18||mode==22){
  // Appearance3 helicopters already skip CCam's discrete side look, but
  // FollowCar still consumes their rudders in its look-priority branches.
  if(LookBehind()){++gSnaps;direction=0;}
  else if(Appearance()!=3&&LookLeft()){++gSnaps;direction=1;}
  else if(Appearance()!=3&&LookRight()){++gSnaps;direction=2;}
  else direction=3;
 }
 if(gThrowCamera)throw std::runtime_error("native camera fixture");
}
static void PostFlight()
{
 ++gPostFrames;
 const auto axes=flightinput::Read(gInputPad,game::PadNew(gInputPad,game::kPadLeftStickY),float(LookRight())-float(LookLeft()),-game::PadNew(gInputPad,game::kPadRightStickY));
 gRudder=axes.yaw;
 if(gObserveRaw)CHECK(game::PadNew(gInputPad,game::kPadLT)||game::PadNew(gInputPad,game::kPadRT));
}
static void Prepare(int model=190,int kind=1)
{
 std::memset(gInputPed,0,sizeof gInputPed);std::memset(gInputCam,0,sizeof gInputCam);std::memset(gInputPad,0,sizeof gInputPad);std::memset(gInputVehicles,0,sizeof gInputVehicles);
 gMission=gCutscene=gThrowCamera=gTestDriveBy=gTestOwnedPlane=gRejectReference=false;gAffected=1;gSnaps=gFollowReads=gPostFrames=0;gRudder=0;gObserveRaw=true;
 gPoolFlags[0]=17;gPoolFlags[1]=18;
 *reinterpret_cast<uint8_t **>(gPool)=gInputVehicles[0];*reinterpret_cast<uint8_t **>(gPool+8)=gPoolFlags;*reinterpret_cast<int *>(gPool+0x10)=2;
 *reinterpret_cast<float *>(gInputPed+0x44c)=100;*reinterpret_cast<int *>(gInputPed+0x2fc)=game::kPedDriving;gInputPed[0x4c0]=1;
 *reinterpret_cast<uint8_t **>(gInputPed+0x4b8)=gInputVehicles[0];
 auto *v=gInputVehicles[0];*reinterpret_cast<int *>(v+0x60)=2;*reinterpret_cast<int16_t *>(v+0x6a)=int16_t(model);*reinterpret_cast<int *>(v+0x348)=kind;v[0x294]=3;*reinterpret_cast<float *>(v+0x2a0)=1000;*reinterpret_cast<uint8_t **>(v+0x218)=gInputPed;
 const bool heli=model==155||model==177||model==199||model==217||model==218||model==227;
 *reinterpret_cast<uint8_t **>(v+0x160)=gInputHandling;*reinterpret_cast<uint16_t *>(gInputHandling+0xce)=model==190?4:heli?2:0;
 game::api.FindPlayerPed=InputPlayer;game::api.GetPad=InputPad;game::api.PoolsGetVehicle=InputVehicle;game::api.theCamera=gInputCam;game::api.timeMs=&gInputNow;game::api.timeStep=&gInputStep;game::api.padAffectedByController=&gAffected;game::api.newMouseState=reinterpret_cast<uint8_t *>(gMouse);
 gInputCam[0x63]=1;*reinterpret_cast<uint8_t **>(gInputCam+0x860)=v;*reinterpret_cast<uint8_t **>(game::ActiveCam()+0x1b0)=v;
 *reinterpret_cast<uint16_t *>(game::ActiveCam()+0xc)=18;*reinterpret_cast<int *>(game::ActiveCam()+0x28)=1; // stale side cache must become forward, not be replayed
 game::PadNew(gInputPad,game::kPadLT)=255;game::PadNew(gInputPad,game::kPadRightStickX)=43;game::PadNew(gInputPad,game::kPadRightStickY)=42;game::PadNew(gInputPad,game::kPadLeftStickY)=126;gMouse[2]=.35f;
 gConfig.firstPerson=false;gConfig.planes=true;gConfig.cameraShake=0;camera::orig_Process=NativeCamera;gFlightFrameObserver=PostFlight;
}
static void Process(bool masked)
{
	 std::array<uint8_t,sizeof gInputPad> before{};std::memcpy(before.data(),gInputPad,before.size());
 camera::Hook_Process(gInputCam);
 CHECK(!std::memcmp(gInputPad,before.data(),before.size()));
 CHECK(gPostFrames==2&&gLookX==43&&gLookY==42&&gMouseX==.35f);
 CHECK(gCameraLeft==(masked?0:game::PadNew(gInputPad,game::kPadLT))&&gCameraRight==(masked?0:game::PadNew(gInputPad,game::kPadRT)));
 if(masked){CHECK(gSnaps==0&&gFollowReads==0);CHECK(*reinterpret_cast<int *>(game::ActiveCam()+0x28)==3);}
 CHECK(gRudder==(LookRight()?1:LookLeft()?-1:0));
}

// Camera placement consumes the same native seated head as the game, rather
// than the model's (car-only) front-seat dummy. These CAR_sit first-key head
// coordinates come from shipped ped.ifp + player.dff. Their neck evaluates to
// (-.009864,-.221146,.479002), matching the recorded native Learjet pose.
static CVector gSeatedHead;
alignas(16) static float gSeatRwMatrix[16];
alignas(16) static uint8_t gSeatScene[16];
static int gSeatDerived, gSeatVisibility;
static float gSeatNear;
static void SeatMatrix(void *) {}
static void SeatEntity(uint8_t *) {}
static void SeatComponent(void *ik,CVector *out,uint32_t node)
{
 CHECK(ik==gInputPed+0x2c0&&node==2);
 *out=gSeatedHead;
}
static void *SeatCameraFrame(const void *) {return gSeatRwMatrix;}
static float *SeatFrameMatrix(void *frame) {CHECK(frame==gSeatRwMatrix);return gSeatRwMatrix;}
static void SeatMatrixUpdate(float *) {}
static void SeatFrameUpdate(void *) {}
static void SeatNear(void *,float near) {gSeatNear=near;}
static void SeatDerived(uint8_t *cam) {CHECK(cam==gInputCam);++gSeatDerived;}
static int32_t SeatVisibility(uint8_t *entity)
{
 CHECK(entity==gInputVehicles[0]);
 // The native bumper-camera visibility gate has to see a follow mode while
 // it decides whether to draw the aircraft enclosing the pilot.
 CHECK(*reinterpret_cast<uint16_t *>(game::ActiveCam()+0xc)==4);
 ++gSeatVisibility;return 1;
}
static void SeatPlacement(CVector seatedHead={-.014334f,-.251221f,.605372f})
{
 Prepare(248,4);gFlightFrameObserver=nullptr;gObserveRaw=false;gTestOwnedPlane=true;
 gConfig.firstPerson=true;camera::gFirstPerson=false;camera::gHeadReady=false;
 camera::gLookYaw=camera::gLookPitch=0;gSeatNear=0;
 auto *vehicle=gInputVehicles[0];
 *reinterpret_cast<CVector *>(vehicle+8)={1,0,0};
 *reinterpret_cast<CVector *>(vehicle+0x18)={0,1,0};
 *reinterpret_cast<CVector *>(vehicle+0x28)={0,0,1};
 *reinterpret_cast<CVector *>(vehicle+0x38)={100,200,20};
 *reinterpret_cast<CVector *>(gInputPed+0x38)={99.68f,205,19.35f};
 gSeatedHead={99.68f+seatedHead.x,205+seatedHead.y,19.35f+seatedHead.z};
 *reinterpret_cast<CVector *>(gInputCam+0x10)={0,1,0};
 *reinterpret_cast<CVector *>(gInputCam+0x20)={0,0,1};
 *reinterpret_cast<CVector *>(gInputCam+0x30)={100,208,21}; // native bumper source
 *reinterpret_cast<uint16_t *>(game::ActiveCam()+0xc)=16;
 *reinterpret_cast<void **>(gInputCam+0x858)=gSeatRwMatrix;
 *reinterpret_cast<void **>(gSeatScene+8)=gSeatRwMatrix;
 camera::gScene=gSeatScene;camera::gUpdateRW=SeatMatrix;
 camera::gUpdateRwFrame=camera::gUpdateRpHAnim=SeatEntity;camera::gComponentPosition=SeatComponent;
 camera::gCameraFrame=SeatCameraFrame;camera::gFrameMatrix=SeatFrameMatrix;
 camera::gMatrixUpdate=SeatMatrixUpdate;camera::gFrameUpdateObjects=camera::gFrameOrthoNormalize=SeatFrameUpdate;
 camera::gSetNearClip=SeatNear;camera::gDerivedValues=SeatDerived;
 camera::orig_SetupEntityVisibility=SeatVisibility;gSeatDerived=gSeatVisibility=0;
 camera::Hook_Process(gInputCam);
 const CVector eye=camera::Row(0x30);
 CHECK(camera::SeatVehicle()==vehicle&&gSeatDerived==1);
 CHECK(std::fabs(eye.x-99.665666f)<.00002f&&std::fabs(eye.y-204.748779f)<.00002f&&std::fabs(eye.z-20.003372f)<.00002f);
 CHECK(std::fabs(gSeatNear-.08f)<.00001f);
 const auto source=*reinterpret_cast<CVector *>(game::ActiveCam()+0x174);
 CHECK(source.x==eye.x&&source.y==eye.y&&source.z==eye.z);
 CHECK(gSeatRwMatrix[12]==eye.x&&gSeatRwMatrix[13]==eye.y&&gSeatRwMatrix[14]==eye.z);
 CHECK(camera::Hook_SetupEntityVisibility(vehicle)==1&&gSeatVisibility==1);
 CHECK(*reinterpret_cast<uint16_t *>(game::ActiveCam()+0xc)==16);
 game::ActiveCam()[7]=1;
 camera::Hook_Process(gInputCam);
 const CVector behind=camera::Row(0x30);
 CHECK(std::fabs(behind.x-100)<.00002f&&std::fabs(behind.y-205.098779f)<.00002f&&std::fabs(behind.z-20.085372f)<.00002f);
 CHECK(gSeatRwMatrix[12]==behind.x&&gSeatRwMatrix[13]==behind.y&&gSeatRwMatrix[14]==behind.z);
}
int main()
{
 camera::Install(); // optional scope bindings; absent rendering dependencies keep install false
 for(int model:{190,155,177,199,217,218,227}){
  Prepare(model,model==190?1:0);Process(true);
 }
 Prepare(181,4);gTestOwnedPlane=true;Process(true);
 Prepare(240,4);gTestOwnedPlane=true;Process(true); // registered airport jet retains rudder without side look
 Prepare(240,4);Process(false); // unowned model ID alone is insufficient
 Prepare(240,0);gTestOwnedPlane=true;Process(false); // allocation class remains mandatory
 Prepare(181,4);gTestOwnedPlane=true;gConfig.planes=false;Process(true); // owned native CPlane retains control for a safe landing
 Prepare();game::PadNew(gInputPad,game::kPadLT)=0;game::PadNew(gInputPad,game::kPadRT)=255;Process(true);
 Prepare();game::PadNew(gInputPad,game::kPadRT)=255;Process(true); // both rudders must not become legacy look-behind
 Prepare();*reinterpret_cast<uint8_t **>(game::ActiveCam()+0x1b0)=nullptr;Process(true);
 for(int mode:{3,16,22}){Prepare();*reinterpret_cast<uint16_t *>(game::ActiveCam()+0xc)=uint16_t(mode);Process(true);}
 Prepare(130,0);Process(false);CHECK(gSnaps==1&&*reinterpret_cast<int *>(game::ActiveCam()+0x28)==1);
 Prepare(130,0);game::PadNew(gInputPad,game::kPadRT)=255;Process(false);CHECK(gSnaps==1&&*reinterpret_cast<int *>(game::ActiveCam()+0x28)==0);
 for(int refusal=0;refusal<22;++refusal){
  Prepare();
  switch(refusal){
  case 0:*reinterpret_cast<int16_t *>(gInputVehicles[0]+0x6a)=130;*reinterpret_cast<int *>(gInputVehicles[0]+0x348)=0;break;
  case 1:gMission=true;break;case 2:gCutscene=true;break;case 3:gInputCam[0x75]=1;break;case 4:gInputCam[0x63]=0;break;
  case 5:*reinterpret_cast<uint16_t *>(gInputPad+0xec)=3;break;
  case 6:*reinterpret_cast<uint8_t **>(gInputVehicles[0]+0x218)=gInputPed+8;break;
  case 7:*reinterpret_cast<int *>(gInputVehicles[0]+0x60)=2|(5<<3);break;
  case 8:*reinterpret_cast<float *>(gInputPed+0x44c)=0;break;
  case 9:*reinterpret_cast<uint8_t **>(game::ActiveCam()+0x1b0)=gInputVehicles[1];break;
  case 10:*reinterpret_cast<uint16_t *>(game::ActiveCam()+0xc)=6;break;
  case 11:gTestDriveBy=true;break;case 12:gConfig.planes=false;break;
  case 13:gPoolFlags[0]=0x80;break;
  case 14:*reinterpret_cast<int16_t *>(gInputVehicles[0]+0x6a)=181;*reinterpret_cast<int *>(gInputVehicles[0]+0x348)=4;break;
  case 15:gInputCam[0x57]=1;break;
  case 16:gRejectReference=true;break;
  case 17:*reinterpret_cast<float *>(gInputVehicles[0]+0x2a0)=0;break;
  case 18:*reinterpret_cast<int *>(gInputPed+0x2fc)=0x3c;break; // native exit-car controller
  case 19:*reinterpret_cast<uint16_t *>(gInputPad+0xf0)=1;break;
  case 20:*reinterpret_cast<uint8_t **>(gInputCam+0x860)=gInputVehicles[1];break;
  case 21:*reinterpret_cast<int *>(gInputVehicles[0]+0x348)=4;break;
  }
  Process(false);
 }
 Prepare();*reinterpret_cast<uint16_t *>(gInputPad+0xec)=3;gAffected=0;Process(true); // keyboard mode does not inherit stale controller Mode3
 Prepare();gThrowCamera=true;
 try{camera::Hook_Process(gInputCam);CHECK(false);}catch(const std::runtime_error &){}
 CHECK(game::PadNew(gInputPad,game::kPadLT)==255&&gPostFrames==0);
 CHECK(*reinterpret_cast<int *>(game::ActiveCam()+0x28)==3);
 SeatPlacement();
 if(!gFailures)std::puts("aircraft camera hook: all passed");return gFailures?1:0;
}

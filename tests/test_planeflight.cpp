// Exercise the real fixed-wing native adapter and physical impulse ABI.
#include <dlfcn.h>
static void *PlaneSymbol(void *,const char *);
#define dlsym PlaneSymbol
#include "../src/planeflight.cpp"
#undef dlsym
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

Config gConfig;
namespace game {Api api;}
void Log(const char *,...){}
static int failures;
#define CHECK(c) do{if(!(c)){std::printf("FAIL %d: %s\n",__LINE__,#c);++failures;}}while(0)
alignas(16) static uint8_t gBoat[0x6f0]{},gPlayer[0x8d0]{},gPad[0x160]{};
static uint8_t gControls[0x1b00]{};
static bool gKeys[1100]{};
static float gStep=1;
static int gRoll,gPitch,gGun,gGunYaw,gThrottle=255,gBrake,gLeft,gRight,gMoves,gTurns;
static void *(*gExtraSymbol)(const char *);
static void *(*gChainOriginal)(const char *,void *);
static const char *gMissingSymbol;
static bool gFlightMission,gFlightCutscene;
static bool gNativePad;
static uint8_t gControllerInput=1;
static bool FlightMission(){return gFlightMission;}
namespace hooks {void *Chain(const char *name,void *hook){return gChainOriginal?gChainOriginal(name,hook):nullptr;}}
static Vec3 gImpulse{};
static CVector &V(uint8_t *p,size_t o){return *reinterpret_cast<CVector *>(p+o);}
static uint8_t *Player(){return gPlayer;}
static uint8_t *Pad(int){return gPad;}
static bool KeyDown(uint8_t *,int key){return key>=0&&key<int(sizeof gKeys/sizeof *gKeys)&&gKeys[key];}
static void KeyBinding(bindings::Action action,int slot,int key){std::memcpy(gControls+0xec0+32*int(action)+8*slot,&key,sizeof key);}
static int16_t Roll(uint8_t *){return int16_t(gRoll);}
// Characterized native consumer, verified at0x10880c/0x1088a4: disabled
// controls return0, mode0/2 steering combines leftY and Dpad; mode0/1/2
// gun reads merged rightY. This deliberately retains the native camera axis.
static int PadField(const uint8_t *pad,size_t at){return *reinterpret_cast<const int16_t *>(pad+at);}
static int PadMode(const uint8_t *pad){return game::api.padAffectedByController&&*game::api.padAffectedByController==1?PadField(pad,0xec):0;}
static int16_t Pitch(uint8_t *pad)
{
	if(!gNativePad)return int16_t(gPitch);
	if(PadField(pad,0xf0))return 0;
	const int mode=PadMode(pad),axis=PadField(pad,2),dpad=(PadField(pad,0x12)-PadField(pad,0x10))/2;
	return int16_t(mode==0||mode==2?(std::abs(axis)>std::abs(dpad)?axis:dpad):mode==1||mode==3?axis:0);
}
static int16_t Gun(uint8_t *pad)
{
	if(!gNativePad)return int16_t(gGun);
	if(PadField(pad,0xf0))return 0;
	const int mode=PadMode(pad);
	return int16_t(mode<3?PadField(pad,6):mode==3?(PadField(pad,0x10)-PadField(pad,0x12))/2:0);
}
static int16_t GunYaw(uint8_t *pad)
{
	if(!gNativePad)return int16_t(gGunYaw);
	if(PadField(pad,0xf0))return 0;
	const int mode=PadMode(pad);
	// Exact native GetCarGunLeftRight0x1088fc: Mode3 Dpad yaw; normal
	// controller modes return the physical right LOOK stick instead.
	return int16_t(mode<3?PadField(pad,4):mode==3?(PadField(pad,0x16)-PadField(pad,0x14))/2:0);
}
static int16_t Throttle(uint8_t *){return int16_t(gThrottle);}
static int16_t Brake(uint8_t *){return int16_t(gBrake);}static bool Left(uint8_t *){return gLeft!=0;}
static bool Right(uint8_t *){return gRight!=0;}
static void Move(uint8_t *v,float x,float y,float z)
{
	++gMoves;gImpulse.x+=x;gImpulse.y+=y;gImpulse.z+=z;
	const float mass=*reinterpret_cast<float *>(v+0xcc);
	V(v,0x84).x+=x/mass;V(v,0x84).y+=y/mass;V(v,0x84).z+=z/mass;
}
static void Turn(uint8_t *v,float x,float y,float z,float px,float py,float pz)
{
	++gTurns;
	const auto &r=V(v,8),&f=V(v,0x18),&u=V(v,0x28),&c=V(v,0xe4);
	px-=r.x*c.x+f.x*c.y+u.x*c.z;py-=r.y*c.x+f.y*c.y+u.y*c.z;pz-=r.z*c.x+f.z*c.y+u.z*c.z;
	const float mass=*reinterpret_cast<float *>(v+0xd0);
	V(v,0x90).x+=(py*z-pz*y)/mass;V(v,0x90).y+=(pz*x-px*z)/mass;V(v,0x90).z+=(px*y-py*x)/mass;
}
static void *PlaneSymbol(void *,const char *name)
{
	if(gMissingSymbol&&std::strstr(name,gMissingSymbol))return nullptr;
#define B(n,f) if(std::strstr(name,n))return reinterpret_cast<void *>(f)
	B("ApplyMoveForceEfff",Move);B("ApplyTurnForceEffffff",Turn);
	B("GetSteeringLeftRight",Roll);B("GetSteeringUpDown",Pitch);B("GetCarGunUpDown",Gun);B("GetCarGunLeftRight",GunYaw);
	B("GetAccelerate",Throttle);B("GetBrake",Brake);B("GetLookLeft",Left);B("GetLookRight",Right);
#undef B
	if(gExtraSymbol)if(void *extra=gExtraSymbol(name))return extra;
	if(std::strstr(name,"IsPlayerOnAMission"))return reinterpret_cast<void *>(FlightMission);
	if(std::strstr(name,"ms_running"))return &gFlightCutscene;
	return nullptr;
}
static void Reset()
{
	std::memset(gBoat,0,sizeof gBoat);std::memset(gPlayer,0,sizeof gPlayer);std::memset(gPad,0,sizeof gPad);gNativePad=false;
	std::memset(gControls,0,sizeof gControls);std::memset(gKeys,0,sizeof gKeys);
	gConfig.planes=true;gStep=1;gRoll=gPitch=gGun=gGunYaw=gBrake=gLeft=gRight=gMoves=gTurns=0;gThrottle=255;gImpulse={};
	gFlightMission=gFlightCutscene=false;
	gControllerInput=1;game::api.padAffectedByController=&gControllerInput;
	game::api.controlsManager=gControls;game::api.GetIsKeyboardKeyDown=KeyDown;
	V(gBoat,8)={1,0,0};V(gBoat,0x18)={0,1,0};V(gBoat,0x28)={0,0,1};V(gBoat,0x38)={0,0,3000};V(gBoat,0x84)={0,1.2f,0};
	*reinterpret_cast<uint32_t *>(gBoat+0x60)=2|0x100;
	*reinterpret_cast<int16_t *>(gBoat+0x6a)=190;*reinterpret_cast<int32_t *>(gBoat+0x348)=1;
	*reinterpret_cast<float *>(gBoat+0xcc)=5000;*reinterpret_cast<float *>(gBoat+0xd0)=10000;
	// Native Skimmer CBoat ctor: handling width1 * height.8 / mass5000.
	*reinterpret_cast<float *>(gBoat+0xd8)=.00016f;
	*reinterpret_cast<float *>(gBoat+0x2a0)=1000;gBoat[0x294]=1;gBoat[0x295]=0x10;
	*reinterpret_cast<uint8_t **>(gBoat+0x218)=gPlayer;
	gPlayer[0x4c0]=1;*reinterpret_cast<uint8_t **>(gPlayer+0x4b8)=gBoat;
	game::api.FindPlayerPed=Player;game::api.GetPad=Pad;game::api.timeStep=&gStep;
}
static float Lift(float angle,float speed)
{
	Reset();V(gBoat,0x84)={0,speed*std::cos(angle)/50,-speed*std::sin(angle)/50};
	CHECK(planeflight::Apply(gBoat,3));return gImpulse.z/5000*2500;
}
static CVector Cross(CVector a,CVector b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static CVector Plus(CVector a,CVector b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static CVector Times(CVector a,float k){return {a.x*k,a.y*k,a.z*k};}
static float Magnitude(CVector a){return std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);}
static CVector Normal(CVector a){return Times(a,1/std::max(.0001f,Magnitude(a)));}
static void NativeStep(float dt,bool support=false)
{
	gStep=dt*50;
	// CBoat::ProcessControl calls CPhysical gravity, then buoyancy/water
	// resistance, then FlyingControl. World integrates move/turn afterwards.
	V(gBoat,0x84).z-=.008f*gStep;
	// CPhysical applies native air resistance and its per-call angular .99
	// before CBoat's buoyancy/FlyingControl. Do not credit the new adapter
	// with an envelope obtained by silently omitting these native consumers.
	const float air=*reinterpret_cast<float *>(gBoat+0xd8);
	const float resistance=std::pow(1/std::fabs(1+air*.5f*planeflight::Dot(V(gBoat,0x84),V(gBoat,0x84))),gStep);
	V(gBoat,0x84)=Times(V(gBoat,0x84),resistance);V(gBoat,0x90)=Times(V(gBoat,0x90),.99f);
	if(support&&V(gBoat,0x38).z<=1.4f){
		V(gBoat,0x38).z=1.4f;V(gBoat,0x84).z=std::max(0.0f,V(gBoat,0x84).z);
		// Flat support approximates the launch surface. Native buoyancy/waves
		// are deliberately outside this offline flight-envelope fixture.
	}
	CHECK(planeflight::Apply(gBoat,3));
	V(gBoat,0x38)=Plus(V(gBoat,0x38),Times(V(gBoat,0x84),gStep));
	const auto angular=Times(V(gBoat,0x90),gStep);
	V(gBoat,0x18)=Normal(Plus(V(gBoat,0x18),Cross(angular,V(gBoat,0x18))));
	V(gBoat,0x28)=Normal(Plus(V(gBoat,0x28),Cross(angular,V(gBoat,0x28))));
	V(gBoat,8)=Normal(Cross(V(gBoat,0x18),V(gBoat,0x28)));
	V(gBoat,0x28)=Normal(Cross(V(gBoat,8),V(gBoat,0x18)));
}
static CVector Climb(float dt)
{
	Reset();V(gBoat,0x38)={0,0,1.4f};V(gBoat,0x84)={};gPitch=20;
	for(int frame=0;frame<int(360/dt);++frame)NativeStep(dt,true);
	CHECK(V(gBoat,0x38).z>2800&&Magnitude(V(gBoat,0x84))*50<100);
	return V(gBoat,0x38);
}
static void GlideRecoveryApproach()
{
	Reset();V(gBoat,0x38).z=1000;V(gBoat,0x84)={0,1,0};gThrottle=0;
	for(int frame=0;frame<1200;frame++)NativeStep(1/60.0f);
	CHECK(V(gBoat,0x38).z>800&&V(gBoat,0x38).z<950);
	CHECK(Magnitude(V(gBoat,0x84))*50>25&&V(gBoat,0x84).z*50>-10); // powered-off glide, not free fall
	Reset();V(gBoat,0x38).z=1000;V(gBoat,0x84)={0,.5f,0};
	const float angle=.9f;V(gBoat,0x18)={0,std::cos(angle),std::sin(angle)};V(gBoat,0x28)={0,-std::sin(angle),std::cos(angle)};
	for(int frame=0;frame<600;frame++){gPitch=frame<120?-32:0;NativeStep(1/60.0f);}
	const auto speed=Times(V(gBoat,0x84),50);
	const float attack=std::atan2(-planeflight::Dot(speed,V(gBoat,0x28)),planeflight::Dot(speed,V(gBoat,0x18)));
	CHECK(std::fabs(attack)<.3f&&Magnitude(speed)>30&&Magnitude(speed)<100&&V(gBoat,0x38).z>850);
	// The same actual force/control seam can approach and flare. Stop at the
	// support boundary: native COL landing contact is a separate game check.
	Reset();V(gBoat,0x38).z=200;V(gBoat,0x84)={0,1,0};gThrottle=0;
	int frame=0;
	for(;frame<7200&&V(gBoat,0x38).z>1.4f;frame++){gPitch=V(gBoat,0x38).z<18?15:0;NativeStep(1/60.0f);}
	CHECK(frame<7200&&V(gBoat,0x84).z*50>-4&&V(gBoat,0x84).z<0);
	CHECK(Magnitude(V(gBoat,0x84))*50>20&&Magnitude(V(gBoat,0x84))*50<40);
}
static void CapturedControllerPitch()
{
	// Actual D702 PID6435 Skimmer telemetry: leftY126 (climb), rightY42
	// (cockpit look), parked-created3, health1000, native type1/status0.
	Reset();gBoat[0x294]=3;gRoll=21;gNativePad=true;
	const int16_t captured[21]={21,126,43,42,0,0,0,0,0,0,0,0,0,0,0,0,255,0,0,0,0};
	std::memcpy(gPad,captured,sizeof captured);
	V(gBoat,8)={-.96555f,-.2573f,-.038869f};V(gBoat,0x18)={.25737f,-.922217f,-.288576f};V(gBoat,0x28)={.038405f,-.288638f,.956668f};
	V(gBoat,0x38)={-659.442261f,205.785965f,56.717159f};
	V(gBoat,0x84)=Times({-6.446072f,-42.249062f,-11.849727f},1/50.0f);
	V(gBoat,0x90)=Times({.12556f,-.207404f,.191845f},1/50.0f);
	*reinterpret_cast<float *>(gBoat+0xd0)=21041.666016f;
	const CVector before=V(gBoat,0x90);CHECK(planeflight::Apply(gBoat,3));
	const auto delta=Plus(V(gBoat,0x90),Times(before,-1));
	CHECK(delta.x*V(gBoat,8).x+delta.y*V(gBoat,8).y+delta.z*V(gBoat,8).z>0);
	Reset();CHECK(planeflight::Apply(gBoat,3));const auto neutral=V(gBoat,0x90);
	Reset();gGun=42;CHECK(planeflight::Apply(gBoat,3));
	CHECK(Magnitude(Plus(V(gBoat,0x90),Times(neutral,-1)))<1e-7f); // looking around cannot steer neutral controller pitch
	// The actual configured keyboard slots survive CPad::Update clearing
	// all PCTemp states. They also work with a connected controller.
	Reset();gGun=42;KeyBinding(bindings::Action::kVehicleTurretdown,1,1049);gKeys[1049]=true;CHECK(planeflight::Apply(gBoat,3));
	CHECK(V(gBoat,0x90).x>neutral.x);
	Reset();gControllerInput=0;gPitch=96;gGun=96;KeyBinding(bindings::Action::kVehicleTurretup,0,80);gKeys[80]=true;CHECK(planeflight::Apply(gBoat,3));
	CHECK(V(gBoat,0x90).x>neutral.x); // primary wins over conflicting keyboard auxiliary pitch
	Reset();KeyBinding(bindings::Action::kVehicleTurretright,0,81);gKeys[81]=true;CHECK(planeflight::Apply(gBoat,3));
	CHECK(V(gBoat,0x90).z<neutral.z); // configured keyboard rudder reaches actual force ABI
	Reset();game::api.padAffectedByController=nullptr;game::api.controlsManager=nullptr;gGun=42;CHECK(planeflight::Apply(gBoat,3));
	CHECK(Magnitude(Plus(V(gBoat,0x90),Times(neutral,-1)))<1e-7f);
	Reset();gNativePad=true;*reinterpret_cast<int16_t *>(gPad+0xec)=3;*reinterpret_cast<int16_t *>(gPad+0x12)=255;
	CHECK(planeflight::Apply(gBoat,3));CHECK(V(gBoat,0x90).x>neutral.x); // native Mode3 Dpad pitch remains a flight control
}
static void NativeMode3Yaw()
{
	Reset();CHECK(planeflight::Apply(gBoat,3));const auto neutral=V(gBoat,0x90);
	for(int side:{-1,1}){
		Reset();gNativePad=true;*reinterpret_cast<int16_t *>(gPad+0xec)=3;
		*reinterpret_cast<int16_t *>(gPad+(side>0?0x16:0x14))=255;
		CHECK(planeflight::Apply(gBoat,3));CHECK(V(gBoat,0x90).z*side<0);
	}
	for(int mode:{0,1,2}){
		Reset();gNativePad=true;*reinterpret_cast<int16_t *>(gPad+0xec)=int16_t(mode);
		*reinterpret_cast<int16_t *>(gPad+4)=127;
		CHECK(planeflight::Apply(gBoat,3));CHECK(Magnitude(Plus(V(gBoat,0x90),Times(neutral,-1)))<1e-7f);
	}
	Reset();gNativePad=true;gRight=1;*reinterpret_cast<int16_t *>(gPad+0xec)=3;*reinterpret_cast<int16_t *>(gPad+0x14)=255;
	CHECK(planeflight::Apply(gBoat,3));CHECK(V(gBoat,0x90).z<0); // primary native pedal overrides conflicting Dpad fallback
}
static airframemodel::Effects DamageEffects(const uint8_t *){airframemodel::Effects e;e.managed=true;e.power=0;e.leftLift=.15f;e.pitch=0;return e;}
int main()
{
	for(const char *missing:{"ApplyTurnForce","IsPlayerOnAMission","ms_running","GetCarGunLeftRight"}){
		Reset();gMissingSymbol=missing;CHECK(!planeflight::Install());
		CHECK(!planeflight::Apply(gBoat,3)&&gMoves==0&&gTurns==0); // missing force/session ABI leaves original in charge
	}
	gMissingSymbol=nullptr;CHECK(!planeflight::Install()); // absent roster hooks retain the working Skimmer force seam
	// Native SEAPLANE enum3 limits lift to .9*g above80m at0x27b1a4.
	// The actual adapter must retain lift above the2600m cloud top.
	CHECK(planeflight::Apply(gBoat,3));CHECK(gMoves>0&&gTurns>0&&gImpulse.z>5000*.008f);
	Reset();gPitch=96;CHECK(planeflight::Apply(gBoat,3));CHECK(V(gBoat,0x90).x>0);
	CapturedControllerPitch();
	NativeMode3Yaw();
	Reset();gRoll=96;CHECK(planeflight::Apply(gBoat,3));CHECK(V(gBoat,0x90).y>0);
	Reset();gRight=1;CHECK(planeflight::Apply(gBoat,3));CHECK(V(gBoat,0x90).z<0);
	CHECK(Lift(.70f,50)<Lift(.12f,50)*.6f); // deep stall loses useful lift
	CHECK(Lift(0,0)<.01f); // no vertical takeoff from thrust alone
	Reset();const auto before=V(gBoat,0x84);CHECK(planeflight::Apply(gBoat,3));const float powered=V(gBoat,0x84).y-before.y;
	Reset();gBoat[0x295]=0;CHECK(planeflight::Apply(gBoat,3));CHECK(V(gBoat,0x84).y-before.y<powered);
	for(int mode:{0,1,2,4,5,6}){Reset();CHECK(!planeflight::Apply(gBoat,mode));CHECK(gMoves==0&&gTurns==0);}
	// Native CREATE_CAR selects CBoat(model190,MISSION_VEHICLE) even in a
	// free-roam SCM context. Constructor provenance is not current ownership.
	Reset();gBoat[0x294]=2;CHECK(planeflight::Apply(gBoat,3));CHECK(gMoves>0&&gImpulse.z>5000*.008f);
	Reset();gBoat[0x294]=2;gFlightMission=true;CHECK(!planeflight::Apply(gBoat,3));CHECK(gMoves==0);
	Reset();gFlightMission=true;CHECK(!planeflight::Apply(gBoat,3));CHECK(gMoves==0);
	Reset();gFlightCutscene=true;CHECK(!planeflight::Apply(gBoat,3));CHECK(gMoves==0);
	Reset();*reinterpret_cast<uint8_t **>(gBoat+0x218)=nullptr;CHECK(!planeflight::Apply(gBoat,3));CHECK(gMoves==0);
	Reset();gConfig.planes=false;CHECK(!planeflight::Apply(gBoat,3));CHECK(gMoves==0);
	Reset();V(gBoat,8).x=std::numeric_limits<float>::infinity();CHECK(!planeflight::Apply(gBoat,3));CHECK(gMoves==0);
	Reset();gStep=0;CHECK(planeflight::Apply(gBoat,3));CHECK(gMoves==0&&gTurns==0);
	Reset();gBoat[0x3c8]=3;gBoat[0x3c9]=1;
	std::array<uint8_t,sizeof gBoat> waterState{};std::memcpy(waterState.data(),gBoat,sizeof gBoat);
	CHECK(planeflight::Apply(gBoat,3));std::memcpy(waterState.data()+0x84,gBoat+0x84,0x18);
	CHECK(!std::memcmp(waterState.data(),gBoat,sizeof gBoat)); // native water/health/ownership state remains native
	const auto slow=Climb(1/30.0f),fast=Climb(1/120.0f);
	CHECK(std::fabs(slow.z-fast.z)<100);
	GlideRecoveryApproach();
	std::printf("Skimmer360s climb30/120Hz: %.1f / %.1fm\n",slow.z,fast.z);
	if(!failures)std::puts("planeflight: all passed");Reset();V(gBoat,0x84)={0,1,0};planeflight::Apply(gBoat,3);const auto healthy=gImpulse;
 Reset();V(gBoat,0x84)={0,1,0};game::api.AirframeEffects=DamageEffects;planeflight::Apply(gBoat,3);CHECK(gImpulse.y<healthy.y&&gImpulse.z<healthy.z);game::api.AirframeEffects=nullptr;
 return failures?1:0;
}

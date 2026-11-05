#include "planeflight.h"
#include "config.h"
#include "game.h"
#include "flightinput.h"
#include "hooks.h"
#include "log.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <type_traits>
#include <vector>
using namespace game;
namespace planeflight {
namespace {
// Native physics uses metres per 1/50s and rad per 1/50s. Forces below are
// evaluated in SI, then submitted through the real CPhysical impulse methods.
constexpr float kHz=50,kGravity=20;
struct Native {
	void (*move)(uint8_t *,float,float,float),(*turn)(uint8_t *,float,float,float,float,float,float);
	int16_t (*roll)(uint8_t *),(*pitch)(uint8_t *),(*gun)(uint8_t *),(*gunYaw)(uint8_t *);
	int16_t (*throttle)(uint8_t *),(*brake)(uint8_t *);
	bool (*left)(uint8_t *),(*right)(uint8_t *);
	bool (*mission)();
	bool *cutscene;
} native{};
bool gForcesReady;
uint8_t **gVehiclePool;
bool (*gAirportOwner)(const uint8_t *);
bool (*gAirportWheelControl)(const uint8_t *);
bool InstallRoster();
bool Owned(const uint8_t *vehicle);
bool OnWheels(const uint8_t *vehicle);
float EngineThrottle(uint8_t *vehicle,float target,float dt,float speed);
CVector Add(CVector a,CVector b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
CVector Sub(CVector a,CVector b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
CVector Mul(CVector a,float k){return {a.x*k,a.y*k,a.z*k};}
float Dot(CVector a,CVector b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float Length(CVector a){return std::sqrt(Dot(a,a));}
bool Finite(CVector a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
CVector Unit(CVector a){const float length=Length(a);return length>1e-5f?Mul(a,1/length):CVector{};}
CVector Row(const uint8_t *v,size_t at){return *reinterpret_cast<const CVector *>(v+at);}
float Scalar(const uint8_t *v,size_t at){return *reinterpret_cast<const float *>(v+at);}
struct Controls {float throttle,pitch,roll,yaw,brake;};
struct Air {CVector acceleration;float alpha,speed,forward;};
Air Aerodynamics(CVector velocity,CVector r,CVector f,CVector u,float height,const Controls &input,float power,bool cleanJet=false,const airframemodel::Effects &damage={})
{
	const float speed=Length(velocity),forward=Dot(velocity,f);
	const float alpha=std::atan2(-Dot(velocity,u),std::max(.1f,forward));
	const float density=std::exp(-std::max(0.0f,height)/18000);
	const float attack=std::fabs(alpha),stall=std::exp(-7*std::max(0.0f,attack-.30f));
	const float cl=std::clamp(.70f+4.5f*alpha,-1.5f,1.9f)*stall;
	const float flow=std::clamp(forward,0.0f,140.0f);
	const float lift=density*kGravity*(flow*flow/(40*40))*cl*(damage.leftLift+damage.rightLift)*.5f;
	const CVector direction=speed>.01f?Mul(velocity,1/speed):f;
	const CVector liftDirection=Unit(Sub(u,Mul(direction,Dot(u,direction))));
	// The brake pedal operates the wheels; these aircraft have no deployed
	// speed-brake surface. Airborne lift, profile/induced drag and stalls still
	// govern the glide when engine power falls.
	// The independently registered jet sheds its low-speed profile drag as
	// it accelerates in clean flight. Keep the original runway/slow-flight
	// curve, engine thrust, induced drag and stalled-flow penalty unchanged.
	const float transition=cleanJet?std::clamp((forward-40)/35,0.0f,1.0f):0;
	const float clean=transition*transition*(3-2*transition);
	const float profile=.0013f-.00085f*clean;
	const float drag=density*(profile+.0007f*cl*cl+.008f*(1-stall)+.004f*damage.drag)*std::min(speed*speed,19600.0f);
	const float thrust=8.0f*input.throttle*power*std::sqrt(density);
	const float side=Dot(velocity,r);
	return {Add(Add(Mul(f,thrust),Mul(liftDirection,lift)),Add(Mul(direction,-drag),Mul(r,-.025f*side*std::fabs(side)))),alpha,speed,forward};
}
template<class T> bool Bind(T &fn,const char *name){fn=reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY,name));return fn!=nullptr;}
}
bool Install()
{
	gForcesReady=Bind(native.move,"_ZN9CPhysical14ApplyMoveForceEfff")&&
	    Bind(native.turn,"_ZN9CPhysical14ApplyTurnForceEffffff")&&
	    Bind(native.roll,"_ZN4CPad20GetSteeringLeftRightEv")&&Bind(native.pitch,"_ZN4CPad17GetSteeringUpDownEv")&&
	    Bind(native.gun,"_ZN4CPad15GetCarGunUpDownEv")&&Bind(native.gunYaw,"_ZN4CPad18GetCarGunLeftRightEv")&&Bind(native.throttle,"_ZN4CPad13GetAccelerateEv")&&
	    Bind(native.brake,"_ZN4CPad8GetBrakeEv")&&Bind(native.left,"_ZN4CPad11GetLookLeftEv")&&Bind(native.right,"_ZN4CPad12GetLookRightEv")&&
	    Bind(native.mission,"_ZN11CTheScripts18IsPlayerOnAMissionEv")&&Bind(native.cutscene,"_ZN12CCutsceneMgr10ms_runningE");
	// Missing identity data leaves the stateless native force adapter usable;
	// it must never create a throttle lease keyed only by a recycled pointer.
	Bind(gVehiclePool,"_ZN6CPools15ms_pVehiclePoolE");
	return gForcesReady&&InstallRoster();
}
bool Owns(const uint8_t *vehicle){return Owned(vehicle)||(gAirportOwner&&gAirportOwner(vehicle));}
void SetAirportOwner(bool (*query)(const uint8_t *)){gAirportOwner=query;}
void SetAirportWheelControl(bool (*query)(const uint8_t *)){gAirportWheelControl=query;}
bool Apply(uint8_t *vehicle,int flightModel)
{
	const bool owned=vehicle&&Owns(vehicle);
	const bool skimmer=vehicle&&flightModel==3&&EntityModel(vehicle)==190&&*reinterpret_cast<const int32_t *>(vehicle+0x348)==1;
	if(!gForcesReady||(!gConfig.planes&&!owned)||!vehicle||(!skimmer&&!(owned&&flightModel==5))||
	    (EntityFlags(vehicle)&7)!=2||Wrecked(vehicle)||!api.FindPlayerPed||!api.GetPad||!api.timeStep)return false;
	uint8_t *player=api.FindPlayerPed(),*pad=api.GetPad(0);
	if(!player||!pad||!PedInVehicle(player)||PedVehicle(player)!=vehicle||
	    *reinterpret_cast<uint8_t *const *>(vehicle+0x218)!=player||(vehicle[0x60]>>3)!=0)return false;
	// Stock free-roam SCM also constructs Skimmers with CreatedBy2. Current
	// player/session ownership decides the force seam, not that historic tag.
	// Our separate CPlane actor keeps its physical owner for a safe landing.
	if(skimmer&&(native.mission()||*native.cutscene))return false;
	const CVector r=Unit(Row(vehicle,8)),f=Unit(Row(vehicle,0x18)),u=Unit(Row(vehicle,0x28));
	const CVector start=Mul(Row(vehicle,0x84),kHz),angular=Mul(Row(vehicle,0x90),kHz),com=Row(vehicle,0xe4);
	const float mass=Scalar(vehicle,0xcc),turnMass=Scalar(vehicle,0xd0),health=VehicleHealth(vehicle),height=EntityPos(vehicle).z;
	if(!Finite(start)||!Finite(angular)||!Finite(com)||!Finite(r)||!Finite(f)||!Finite(u)||Length(r)<.9f||Length(f)<.9f||Length(u)<.9f||
	    !std::isfinite(height)||!std::isfinite(health)||!std::isfinite(mass)||!std::isfinite(turnMass)||mass<=0||turnMass<=0||
	    !std::isfinite(*api.timeStep)||*api.timeStep<0)return false;
	const float dt=std::min(*api.timeStep,3.0f)/kHz;
	if(dt==0)return true;
	const auto axes=flightinput::Read(pad,native.pitch(pad),float(native.right(pad))-float(native.left(pad)),native.gun(pad),native.gunYaw(pad));
	const Controls input{std::clamp(native.throttle(pad)/255.0f,0.0f,1.0f),
	    axes.pitch,
	    std::clamp(native.roll(pad)/128.0f,-1.0f,1.0f),axes.yaw,
	    std::clamp(native.brake(pad)/255.0f,0.0f,1.0f)};
	const auto damage=api.AirframeEffects?api.AirframeEffects(vehicle):airframemodel::Effects{};
 const float power=(vehicle[0x295]&0x10)?(damage.managed?damage.power:std::clamp((health-100)/650,0.0f,1.0f)):0;
 if(api.AirframeFlightUsed)api.AirframeFlightUsed(vehicle);
	// Registered jets have physical, retractable gear. Its contact query owns
	// ground classification even when it returns false; the broad COL keeps
	// the extended tire bounds and cannot stand in for a deployed wheel.
	const bool personalJet=owned&&EntityModel(vehicle)!=181&&gAirportOwner&&gAirportOwner(vehicle);
	const bool wheelController=personalJet&&gAirportWheelControl;
	const bool wheels=owned&&(wheelController?gAirportWheelControl(vehicle):OnWheels(vehicle));
	const bool rolling=wheelController&&wheels;
	Controls engine=input;engine.throttle=EngineThrottle(vehicle,power>0?input.throttle:0,dt,Length(start));
	CVector speed=start;
	const int steps=std::max(1,int(std::ceil(dt/.01f)));const float step=dt/steps;
	Air air{};
	for(int i=0;i<steps;i++){air=Aerodynamics(speed,r,f,u,height,engine,power,personalJet&&!wheels,damage);speed=Add(speed,Mul(air.acceleration,step));}
	if(wheels&&!rolling&&input.brake>0){const CVector horizontal{speed.x,speed.y,0};speed=Sub(speed,Mul(Unit(horizontal),std::min(Length(horizontal),7*input.brake*dt)));}
	const CVector impulse=Mul(Sub(speed,start),mass/kHz);
	native.move(vehicle,impulse.x,impulse.y,impulse.z);
	const float authority=std::clamp(air.forward/25,0.0f,1.3f);
	const float slip=std::atan2(Dot(speed,r),std::max(5.0f,air.forward));
	const float pitch=(.55f*input.pitch-1.3f*(air.alpha-.06f))*authority*damage.pitch;
	const float roll=wheels?0:(.8f*input.roll+.12f*r.z*(1-std::fabs(input.roll)))*authority*damage.roll+(damage.leftLift-damage.rightLift)*.7f*authority;
	// Positive body-right slip needs negative world-up yaw: +Z turns the
	// forward +Y nose away from +X velocity and creates positive feedback.
	const float yaw=wheels?(rolling?0:-(.7f*input.yaw+.6f*input.roll)*std::clamp(air.forward/3,0.0f,1.0f)):
	    (-.35f*input.yaw-.8f*slip+r.z*kGravity/std::max(15.0f,air.forward))*authority*damage.yaw;
	const CVector desired=Add(Add(Mul(r,pitch),Mul(f,roll)),Mul(u,yaw));
	const CVector change=Mul(Sub(desired,angular),1-std::exp(-3*dt));
	const CVector centre=Add(Add(Mul(r,com.x),Mul(f,com.y)),Mul(u,com.z));
	auto torque=[&](CVector force,CVector lever,float amount){force=Mul(force,amount*turnMass/kHz);lever=Add(centre,lever);native.turn(vehicle,force.x,force.y,force.z,lever.x,lever.y,lever.z);};
	torque(u,f,Dot(change,r));torque(r,u,Dot(change,f));
	// Ground steering torque belongs to ProcessWheel. A zero desired yaw is
	// still an active brake on the rate generated by its nose-wheel contact.
	if(!rolling)torque(f,r,Dot(change,u));
	return true;
}
namespace {
constexpr int kDodo=181;
constexpr size_t kPlaneSize=0x388,kVehicleStride=0x6f0;
constexpr CVector kParking{-1585,-1130,0},kSeat{-.32f,1.25f,.05f};
struct RosterNative {
	void *(*allocate)(size_t);
	void (*construct)(uint8_t *,int,uint8_t),(*destroy)(uint8_t *);
	void (*add)(uint8_t *),(*remove)(uint8_t *),(*physical)(uint8_t *);
	void (*warp)(uint8_t *,uint8_t *),(*out)(void *,void *),(*teleport)(uint8_t *,CVector);
	void (*updateMatrix)(void *),(*updateFrame)(void *);
	bool (*mission)(),(*exit)(uint8_t *);
	void (*damage)(uint8_t *,uint8_t *,int,float,CVector),(*kill)(uint8_t *);
	bool (*explosion)(uint8_t *,uint8_t *,int,const CVector &,uint32_t,bool);
	void (*deletable)(int);
} roster{};
uint8_t *gStreaming;
int32_t *gArea;
const uint32_t *gFrameCounter;
bool *gCutscene,gRosterReady;
void (*orig_Control)(uint8_t *),(*orig_Seat)(uint8_t *);
void (*orig_Enter)(uint8_t *,uint8_t *,uint32_t),(*orig_Exit)(uint8_t *,uint8_t *,uint32_t);
void (*orig_Damage)(uint8_t *,uint8_t *,int,float,CVector);
struct Plane {int ref=-1;uint8_t *entity=nullptr;void *vtable=nullptr;uint32_t boarded=0,burning=0;} gPlane;
uint32_t gPrevious,gAttempt;
struct RequestPins {
	uint8_t *stream=nullptr,**table=nullptr,*model=nullptr;
	int txd=-1;
	bool modelPin=false,txdPin=false;
	std::array<uint8_t,21> name{};
} gRequestPins;
bool SameRequestMetadata(const RequestPins &pins)
{
	return pins.model&&pins.stream==gStreaming&&pins.table==api.modelInfoPtrs&&api.modelInfoPtrs&&
	    api.modelInfoPtrs[kDodo]==pins.model&&pins.model[0x1d]==6&&
	    *reinterpret_cast<const int16_t *>(pins.model+0x2e)==pins.txd&&
	    !std::memcmp(pins.name.data(),pins.model+8,pins.name.size());
}
void ReleaseRequestPins()
{
	const auto pins=gRequestPins;gRequestPins={};
	if(!SameRequestMetadata(pins)||!roster.deletable)return;
	// Native references own a constructed actor. Return only the keep bits we
	// acquired; SetModelIsDeletable preserves script/priority/other flag bits.
	if(pins.modelPin&&(gStreaming[kDodo*0x20+0x11]&1))roster.deletable(kDodo);
	if(pins.txdPin&&(gStreaming[size_t(6500+pins.txd)*0x20+0x11]&1))roster.deletable(6500+pins.txd);
}
void RequestDodo()
{
	if(gRequestPins.model&&!SameRequestMetadata(gRequestPins))ReleaseRequestPins();
	uint8_t *model=api.modelInfoPtrs?api.modelInfoPtrs[kDodo]:nullptr;
	if(!model||model[0x1d]!=6||!roster.deletable)return;
	const int txd=*reinterpret_cast<const int16_t *>(model+0x2e);
	if(txd<0||txd>=1385)return;
	if(!gRequestPins.model){
		gRequestPins.stream=gStreaming;gRequestPins.table=api.modelInfoPtrs;gRequestPins.model=model;gRequestPins.txd=txd;
		std::memcpy(gRequestPins.name.data(),model+8,gRequestPins.name.size());
	}
	const uint8_t beforeModel=gStreaming[kDodo*0x20+0x11],beforeTxd=gStreaming[size_t(6500+txd)*0x20+0x11];
	// An unloaded native RequestModel overwrites its flags. Request the TXD
	// first with its existing flags so the recursive DFF request cannot erase
	// another owner's texture pin or script bit. Keep both across async polls.
	api.RequestModel(6500+txd,beforeTxd|1);
	api.RequestModel(kDodo,beforeModel|1);
	if(!SameRequestMetadata(gRequestPins)){gRequestPins={};return;}
	gRequestPins.modelPin|=!(beforeModel&1)&&(gStreaming[kDodo*0x20+0x11]&1);
	gRequestPins.txdPin|=!(beforeTxd&1)&&(gStreaming[size_t(6500+txd)*0x20+0x11]&1);
}
uint8_t *Live()
{
	uint8_t *v=gPlane.ref>=0&&api.PoolsGetVehicle?api.PoolsGetVehicle(gPlane.ref):nullptr;
	// Pool generation and the constructed native actor own physics. A clump
	// can be rebuilt by streaming while that same actor still needs control.
	return v&&v==gPlane.entity&&*reinterpret_cast<void *const *>(v)==gPlane.vtable&&EntityModel(v)==kDodo&&
	    *reinterpret_cast<const int32_t *>(v+0x348)==4&&v[0x294]==1?v:nullptr;
}
bool Owned(const uint8_t *v){return v&&v==Live();}
struct Pool {uint8_t *entries,*flags;int capacity;};
Pool Vehicles()
{
	const uint8_t *p=gVehiclePool?*gVehiclePool:nullptr;
	if(!p)return {};
	Pool pool{*reinterpret_cast<uint8_t *const *>(p),*reinterpret_cast<uint8_t *const *>(p+8),*reinterpret_cast<const int *>(p+16)};
	return pool.entries&&pool.flags&&pool.capacity>0&&pool.capacity<10000?pool:Pool{};
}
int Reference(const uint8_t *vehicle)
{
	const Pool pool=Vehicles();if(!pool.entries)return -1;
	const uintptr_t offset=reinterpret_cast<uintptr_t>(vehicle)-reinterpret_cast<uintptr_t>(pool.entries);
	if(offset%kVehicleStride||offset/kVehicleStride>=size_t(pool.capacity))return -1;
	const size_t index=offset/kVehicleStride;if(pool.flags[index]&0x80)return -1;
	const int ref=int(index*256)|pool.flags[index];return api.PoolsGetVehicle(ref)==vehicle?ref:-1;
}
struct Engine {
	uint8_t *entity=nullptr;void *vtable=nullptr;int ref=-1,model=-1;
	uint32_t seen=0;float throttle=0;
};
std::vector<Engine> gEngines;
const uint8_t *gEngineEntries,*gEngineFlags;uint32_t gEngineClock;bool gEngineClockSeen;
float EngineThrottle(uint8_t *vehicle,float target,float dt,float speed)
{
	if(!api.timeMs||!api.PoolsGetVehicle)return target;
	const Pool pool=Vehicles();const int ref=Reference(vehicle);
	if(ref<0)return target;
	const uint32_t now=*api.timeMs;
	if(gEngineEntries!=pool.entries||gEngineFlags!=pool.flags||gEngines.size()!=size_t(pool.capacity)||
	    (gEngineClockSeen&&now<gEngineClock)){
		gEngines.assign(pool.capacity,{});gEngineEntries=pool.entries;gEngineFlags=pool.flags;
	}
	gEngineClock=now;gEngineClockSeen=true;
	Engine &engine=gEngines[size_t(ref>>8)];void *vtable=*reinterpret_cast<void **>(vehicle);const int model=EntityModel(vehicle);
	if(engine.entity!=vehicle||engine.ref!=ref||engine.vtable!=vtable||engine.model!=model||now-engine.seen>250){
		// A streamed/reloaded aircraft already in flight retains its operating
		// engine; a stopped new actor must build thrust from idle.
		engine={vehicle,vtable,ref,model,now,speed>15?target:0};
	}
	const float response=1-std::exp(-dt/(target>engine.throttle?2.0f:1.0f));
	engine.throttle+=(target-engine.throttle)*response;engine.seen=now;
	return engine.throttle;
}
CVector At(const uint8_t *v,CVector p){return Add(Row(v,0x38),Add(Add(Mul(Row(v,8),p.x),Mul(Row(v,0x18),p.y)),Mul(Row(v,0x28),p.z)));}
void SetPosition(uint8_t *v,CVector p){*reinterpret_cast<CVector *>(v+0x38)=p;roster.updateMatrix(v+8);roster.updateFrame(v);}
const uint8_t *Collision()
{
	uint8_t *model=api.modelInfoPtrs?api.modelInfoPtrs[kDodo]:nullptr;
	return model?*reinterpret_cast<uint8_t *const *>(model+0x20):nullptr;
}
float BaseHeight()
{
	const uint8_t *col=Collision();return col?-Row(col,0x10).z:0;
}
bool Ground(CVector from,float &height)
{
	alignas(16) uint8_t point[0x30]{};uint8_t *entity=nullptr;
	if(!api.ProcessVerticalLine(from,from.z-20,point,entity,true,false,false,false,false,false,nullptr)||
	    Row(point,0x10).z<.7f||!Finite(Row(point,0)))return false;
	height=Row(point,0).z;return true;
}
bool OnWheels(const uint8_t *v)
{
	const int model=EntityModel(v);const uint8_t *info=api.modelInfoPtrs&&model>=0&&model<6500?api.modelInfoPtrs[model]:nullptr;
	const uint8_t *col=info?*reinterpret_cast<uint8_t *const *>(info+0x20):nullptr;
	if(!col)return false;
	const float base=-Row(col,0x10).z;float ground=0;
	return std::isfinite(base)&&base>.1f&&base<10&&Row(v,0x28).z>.7f&&Ground(Row(v,0x38),ground)&&std::fabs(Row(v,0x38).z-ground-base)<.3f;
}
bool Parked(const uint8_t *v)
{
	float ground=0;
	return Length(Row(v,0x84))*kHz<3&&Row(v,0x28).z>.7f&&Ground(Row(v,0x38),ground)&&
	    std::fabs(Row(v,0x38).z-ground-BaseHeight())<1.0f;
}
bool SpawnClear(CVector point)
{
	const uint8_t *col=Collision();if(!col)return false;
	const int count=*reinterpret_cast<const int16_t *>(col+0x28);
	const uint8_t *spheres=*reinterpret_cast<uint8_t *const *>(col+0x38);
	if(count<=0||count>128||!spheres)return false;
	// Exact original COL spheres; neither the ambient plane's huge bounding
	// sphere nor an invented car hitbox authorizes a parked-aircraft spawn.
	for(int i=0;i<count;i++){
		const CVector local=Row(spheres+size_t(i)*20,0);const float radius=Scalar(spheres+size_t(i)*20,12);
		if(!Finite(local)||!std::isfinite(radius)||radius<=0||radius>5)return false;
		const CVector centre{point.x-local.x,point.y-local.y,point.z+local.z};
		if(api.TestSphereAgainstWorld(centre,radius,nullptr,true,true,true,true,false,false))return false;
	}
	return true;
}
bool Clear(const uint8_t *plane,CVector from,CVector to)
{
	struct Ignore {
		uint8_t *saved;
		explicit Ignore(const uint8_t *v):saved(*api.ignoreEntity){*api.ignoreEntity=const_cast<uint8_t *>(v);}
		~Ignore(){*api.ignoreEntity=saved;}
	} ignore(plane);
	return api.GetIsLineOfSightClear(from,to,true,true,false,true,false,false,false);
}
void Hook_Seat(uint8_t *ped)
{
	orig_Seat(ped);
	uint8_t *v=PedInVehicle(ped)?PedVehicle(ped):nullptr;
	if(ped!=api.FindPlayerPed()||!Owned(v)||*reinterpret_cast<uint8_t **>(v+0x218)!=ped)return;
	// Native CPlane models have no ped_frontseat dummy. Retain the native
	// seat orientation/animation/reference path, then place the pilot in its cabin.
	SetPosition(ped,At(v,kSeat));
}
bool Board(uint8_t *ped,uint8_t *v,uint32_t now)
{
	if(!Owned(v)||Wrecked(v)||VehicleHealth(v)<=0||ped!=api.FindPlayerPed()||PedInVehicle(ped)||Health(ped)<=0||
	    (PedState(ped)!=kPedIdle&&PedObjective(ped)!=18)||roster.mission()||*gCutscene||
	    *reinterpret_cast<uint8_t **>(v+0x218)||!Parked(v)||Length(Sub(EntityPos(ped),At(v,{-1.8f,.5f,0})))>4||
	    !Clear(v,EntityPos(ped),At(v,kSeat)))return false;
	api.SetObjective(ped,18,v);roster.warp(ped,v);
	if(!PedInVehicle(ped)||PedVehicle(ped)!=v||*reinterpret_cast<uint8_t **>(v+0x218)!=ped)return false;
	gPlane.boarded=now;Hook_Seat(ped);return true;
}
bool Leave(uint8_t *ped,uint8_t *v)
{
	if(!Owned(v)||ped!=api.FindPlayerPed()||!PedInVehicle(ped)||PedVehicle(ped)!=v||!Parked(v))return false;
	for(float side:{-1.8f,1.8f}){
		CVector at=At(v,{side,.5f,0});float ground=0;
		if(!Ground(at,ground))continue;
		at.z=ground+1.04f;
		if(api.TestSphereAgainstWorld(at,.4f,v,true,true,false,true,false,false)||!Clear(v,EntityPos(ped),at))continue;
		api.SetObjective(ped,16,v);roster.out(nullptr,ped);roster.teleport(ped,at);api.SetObjective(ped,0,nullptr);
		const bool left=!PedInVehicle(ped);
		// Native SetExitCar can precede Frame in the same tick. Consume this
		// action there too, so Frame cannot immediately board on the same press.
		if(left)gPlane.boarded=api.timeMs?*api.timeMs:gPrevious;
		return left;
	}
	return false;
}
void Hook_Enter(uint8_t *ped,uint8_t *v,uint32_t door)
{
	if(!Owned(v))return orig_Enter(ped,v,door);
	Board(ped,v,api.timeMs?*api.timeMs:gPrevious);
}
void Hook_Exit(uint8_t *ped,uint8_t *v,uint32_t door)
{
	if(!Owned(v))return orig_Exit(ped,v,door);
	Leave(ped,v);
}
void Crash(uint8_t *v,uint8_t *by)
{
	if(Wrecked(v))return;
	*reinterpret_cast<float *>(v+0x2a0)=0;v[0x295]&=~uint8_t(0x10);
	uint32_t &flags=*reinterpret_cast<uint32_t *>(v+0x60);flags=(flags&~0xf8u)|(5u<<3)|(1u<<20);
	roster.kill(v);roster.explosion(v,by,6,EntityPos(v),0,false);
}
void Hook_Damage(uint8_t *v,uint8_t *by,int weapon,float damage,CVector point)
{
	orig_Damage(v,by,weapon,damage,point);
	if(Owned(v)&&VehicleHealth(v)<=0&&!(weapon==31&&api.VehicleFireManaged&&api.VehicleFireManaged(v)))Crash(v,by);
}
void Hook_Control(uint8_t *v)
{
	if(!Owned(v))return orig_Control(v);
	// Ambient CPlane control directly overwrites its matrix from flight.dat.
	// An owned native CPlane retains the generic physical/collision world path.
	if(!Wrecked(v)&&v[0x384]){
		// TestRocketCollision publishes this accepted native hit separately
		// from CVehicle health/contact damage. Replacing the ambient path
		// controller must retain its 20-frame explosion consumer too.
		const uint32_t elapsed=*gFrameCounter-*reinterpret_cast<const uint32_t *>(v+0x380);
		if(elapsed>=20&&elapsed<0x80000000u)Crash(v,api.FindPlayerPed());
	}
	const float impact=Scalar(v,0x13c)*kHz/Scalar(v,0xcc);
	if(!Wrecked(v)&&std::isfinite(impact)&&impact>4)
		roster.damage(v,nullptr,39,std::min(1500.0f,(impact-4)*(impact-4)*12),EntityPos(v));
	const uint32_t now=api.timeMs?*api.timeMs:gPrevious;
	if(!Wrecked(v)&&VehicleHealth(v)<250&&!(api.VehicleFireManaged&&api.VehicleFireManaged(v))){
		if(!gPlane.burning)gPlane.burning=now;
		else if(now-gPlane.burning>5000)Crash(v,nullptr);
	}else gPlane.burning=0;
	float water=0;
	if(!Wrecked(v)&&api.GetWaterLevel&&api.GetWaterLevel(EntityPos(v).x,EntityPos(v).y,EntityPos(v).z,&water,true)&&EntityPos(v).z<water-.5f)Crash(v,nullptr);
	if(api.AirframeFlightUsed)api.AirframeFlightUsed(v);
	roster.physical(v);
	if(!Wrecked(v))Apply(v,5);
}
bool InstallRoster()
{
	if(gRosterReady)return true;
	const bool bound=Bind(roster.allocate,"_ZN8CVehiclenwEm")&&Bind(roster.construct,"_ZN6CPlaneC1Eih")&&
	    Bind(roster.destroy,"_ZN6CPlaneD0Ev")&&Bind(roster.add,"_ZN6CWorld3AddEP7CEntity")&&Bind(roster.remove,"_ZN6CWorld6RemoveEP7CEntity")&&
	    Bind(roster.physical,"_ZN9CPhysical14ProcessControlEv")&&Bind(roster.warp,"_ZN4CPed14WarpPedIntoCarEP8CVehicle")&&
	    Bind(roster.out,"_ZN4CPed14PedSetOutCarCBEP21CAnimBlendAssociationPv")&&Bind(roster.teleport,"_ZN4CPed8TeleportE7CVector")&&
	    Bind(roster.updateMatrix,"_ZN7CMatrix8UpdateRWEv")&&Bind(roster.updateFrame,"_ZN7CEntity13UpdateRwFrameEv")&&
	    Bind(roster.damage,"_ZN8CVehicle13InflictDamageEP7CEntity11eWeaponTypef7CVector")&&
	    Bind(roster.kill,"_ZN8CVehicle17KillPedsInVehicleEv")&&Bind(roster.explosion,"_ZN10CExplosion12AddExplosionEP7CEntityS1_14eExplosionTypeRK7CVectorjb")&&
	    Bind(roster.mission,"_ZN11CTheScripts18IsPlayerOnAMissionEv")&&Bind(roster.exit,"_ZN4CPad19ExitVehicleJustDownEv")&&
	    Bind(gVehiclePool,"_ZN6CPools15ms_pVehiclePoolE")&&Bind(gStreaming,"_ZN10CStreaming16ms_aInfoForModelE")&&
	    Bind(gArea,"_ZN5CGame8currAreaE")&&Bind(gCutscene,"_ZN12CCutsceneMgr10ms_runningE")&&
	    Bind(gFrameCounter,"_ZN6CTimer14m_FrameCounterE")&&
	    Bind(roster.deletable,"_ZN10CStreaming19SetModelIsDeletableEi")&&
	    api.FindPlayerPed&&api.GetPad&&api.timeStep&&api.PoolsGetVehicle&&api.modelInfoPtrs&&api.RequestModel&&api.ProcessVerticalLine&&api.TestSphereAgainstWorld&&api.SetObjective&&api.GetIsLineOfSightClear&&api.ignoreEntity;
	if(!bound){Log("plane flight: roster bindings unavailable; Skimmer force seam remains available");return false;}
	auto chain=[](const char *name,auto hook,auto &original){
		// A missing later symbol can leave earlier hooks installed. Retrying
		// must retain their originals; Chain correctly rejects a self-chain.
		if(original)return true;
		original=reinterpret_cast<std::decay_t<decltype(original)>>(hooks::Chain(name,reinterpret_cast<void *>(hook)));
		return original!=nullptr;
	};
	gRosterReady=chain("_ZN6CPlane14ProcessControlEv",Hook_Control,orig_Control)&&
	    chain("_ZN4CPed19SetPedPositionInCarEv",Hook_Seat,orig_Seat)&&
	    chain("_ZN4CPed11SetEnterCarEP8CVehiclej",Hook_Enter,orig_Enter)&&chain("_ZN4CPed10SetExitCarEP8CVehiclej",Hook_Exit,orig_Exit)&&
	    chain("_ZN8CVehicle13InflictDamageEP7CEntity11eWeaponTypef7CVector",Hook_Damage,orig_Damage);
	return gRosterReady;
}
}
float EnginePower(const uint8_t *vehicle)
{
 if(!vehicle||!api.timeMs||!api.PoolsGetVehicle||!gEngineClockSeen)return 0;
 const Pool pool=Vehicles();const int ref=Reference(vehicle);const uint32_t now=*api.timeMs;
 if(ref<0||pool.entries!=gEngineEntries||pool.flags!=gEngineFlags||gEngines.size()!=size_t(pool.capacity)||now<gEngineClock)return 0;
 const Engine &engine=gEngines[size_t(ref>>8)];
 if(engine.entity!=vehicle||engine.ref!=ref||engine.vtable!=*reinterpret_cast<void *const *>(vehicle)||
    engine.model!=EntityModel(vehicle)||now<engine.seen||now-engine.seen>250||!Owns(vehicle)||
    !IsVehicle(vehicle)||Wrecked(vehicle)||!(vehicle[0x295]&0x10))return 0;
 const uint8_t *player=api.FindPlayerPed?api.FindPlayerPed():nullptr;
 if(!player||!PedInVehicle(player)||PedVehicle(player)!=vehicle||
    *reinterpret_cast<uint8_t *const *>(vehicle+0x218)!=player||(vehicle[0x60]>>3)!=0)return 0;
 const float health=VehicleHealth(vehicle);
 if(!std::isfinite(health)||!std::isfinite(engine.throttle))return 0;
 return std::clamp(engine.throttle,0.0f,1.0f)*std::clamp((health-100)/650,0.0f,1.0f);
}
void Frame(uint32_t now)
{
	if(!gRosterReady){ReleaseRequestPins();return;}
	if(gRequestPins.model&&!SameRequestMetadata(gRequestPins))ReleaseRequestPins();
	if(now<gPrevious){
		// Streaming's persistent KEEP bit survives native ReInit. Release it
		// against the same model metadata before beginning a new request epoch.
		ReleaseRequestPins();
		// CPlane actors are not restored by the vehicle save path. A genuine
		// reload fails Live's generation/type check; a timer wrap alone must not
		// turn a still-occupied aircraft back into an ambient path follower.
		if(!Live())gPlane={};
		gPlane.boarded=now;gPlane.burning=0;gAttempt=0;
	}gPrevious=now;
	uint8_t *v=Live(),*player=api.FindPlayerPed?api.FindPlayerPed():nullptr;
	if(gPlane.ref>=0&&!v){gPlane={};gAttempt=now;}
	if(!player){ReleaseRequestPins();return;}
	const bool occupied=v&&PedInVehicle(player)&&PedVehicle(player)==v;
	bool hasOccupants=occupied;
	for(int i=0;v&&i<9;i++)hasOccupants=hasOccupants||*reinterpret_cast<uint8_t **>(v+0x218+size_t(i)*8)!=nullptr;
	if(v&&!hasOccupants&&(!gConfig.planes||*gArea!=0||Length(Sub(EntityPos(v),EntityPos(player)))>450)){
		roster.remove(v);roster.destroy(v);gPlane={};gAttempt=now;v=nullptr;
	}
	if(v){
		ReleaseRequestPins();
		if(gConfig.planes&&roster.exit(api.GetPad(0))&&now-gPlane.boarded>300){if(occupied)Leave(player,v);else Board(player,v,now);}
		return;
	}
	if(!gConfig.planes||*gArea!=0||*gCutscene||roster.mission()||Health(player)<=0||PedInVehicle(player)||
	    Length(Sub(EntityPos(player),kParking))>220||Length(Sub(EntityPos(player),kParking))<25){ReleaseRequestPins();return;}
	if(gAttempt&&now-gAttempt<2000)return;
	gAttempt=now;
	const Pool pool=Vehicles();if(!pool.entries){ReleaseRequestPins();return;}
	int free=0;for(int i=0;i<pool.capacity;i++)free+=(pool.flags[i]&0x80)!=0;
	if(free<5){ReleaseRequestPins();return;}
	if(gStreaming[size_t(kDodo)*0x20+0x10]!=1){RequestDodo();return;}
	CVector position=kParking;position.z=20;float ground=0;
	const float base=BaseHeight();if(base<.5f||base>3||!Ground(position,ground)){ReleaseRequestPins();return;}
	position.z=ground+base+.08f;if(!SpawnClear(position)){ReleaseRequestPins();return;}
	auto *plane=static_cast<uint8_t *>(roster.allocate(kPlaneSize));if(!plane){ReleaseRequestPins();return;}
	roster.construct(plane,kDodo,1);
	const int ref=Reference(plane);
	if(ref<0||!EntityClump(plane)||!*reinterpret_cast<void **>(plane)){roster.destroy(plane);ReleaseRequestPins();return;}
	// No class/type flip: constructor, vtable, native pool and CPlane remain.
	// Clear ambient BIG-building placement; ordinary native world collision
	// now owns the original Dodo COL, including landing contact.
	uint32_t &flags=*reinterpret_cast<uint32_t *>(plane+0x60);flags=(flags&~(0x400000u|0xf8u))|kEntityUsesCollision|(4u<<3);
	*reinterpret_cast<float *>(plane+0xcc)=2400;*reinterpret_cast<float *>(plane+0xd0)=8000;
	*reinterpret_cast<float *>(plane+0xd8)=.0002f;
	plane[0x295]|=8; // native CPlane::InitPlanes uses bIsLocked to prevent traffic despawn
	*reinterpret_cast<CVector *>(plane+8)={-1,0,0};*reinterpret_cast<CVector *>(plane+0x18)={0,-1,0};*reinterpret_cast<CVector *>(plane+0x28)={0,0,1};
	SetPosition(plane,position);gPlane={ref,plane,*reinterpret_cast<void **>(plane),0,0};roster.add(plane);
	ReleaseRequestPins();
	Log("plane flight: parked native Dodo ref%d near airport apron",ref);
}
}

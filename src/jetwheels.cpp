#include "jetwheels.h"
#include "aircraftbodyclient.h"
#include "airportjets.h"
#include "planeflight.h"
#include "hooks.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <vector>

using namespace game;
namespace jetwheels {namespace {
constexpr float kHz=50,kGravity=20;
void (*gControl)(uint8_t *),(*gMove)(uint8_t *,float,float,float),(*gTurn)(uint8_t *,float,float,float,float,float,float);
CVector (*gSpeed)(uint8_t *,const CVector &);
void (*gWheel)(uint8_t *,CVector &,CVector &,CVector &,CVector &,int,float,float,float,int8_t,float *,int32_t *,uint16_t);
float (*gRotation)(uint8_t *,int,const CVector &,const CVector &,float),(*gAdhesion)(uint8_t *),(*gWet)(uint8_t);
int16_t (*gBrake)(uint8_t *),(*gSteer)(uint8_t *);bool (*gMission)();bool *gCutscene;uint8_t **gPool;
bool gReady,gClockSeen;uint32_t gClock;
struct Identity {int ref=-1,model=-1;void *clump=nullptr,*vtable=nullptr;};
struct State {Identity id;std::array<WheelPose,3> pose{};std::array<int32_t,3> wheel{};std::array<float,3> speed{},rate{};};
std::vector<State> gStates;const uint8_t *gEntries,*gFlags;
CVector Add(CVector a,CVector b){return {a.x+b.x,a.y+b.y,a.z+b.z};}CVector Sub(CVector a,CVector b){return {a.x-b.x,a.y-b.y,a.z-b.z};}CVector Mul(CVector a,float s){return {a.x*s,a.y*s,a.z*s};}
float Dot(CVector a,CVector b){return a.x*b.x+a.y*b.y+a.z*b.z;}CVector Cross(CVector a,CVector b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float Length(CVector a){return std::sqrt(Dot(a,a));}bool Finite(CVector a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
CVector Unit(CVector a){const float n=Length(a);return n>1e-6f?Mul(a,1/n):CVector{};}
CVector Row(const uint8_t *v,int at){return *reinterpret_cast<const CVector *>(v+at);}float Scalar(const uint8_t *v,int at){return *reinterpret_cast<const float *>(v+at);}
CVector Basis(uint8_t *v,CVector a){return Add(Add(Mul(Row(v,8),a.x),Mul(Row(v,0x18),a.y)),Mul(Row(v,0x28),a.z));}
const std::array<Gear,3> &GearFor(const uint8_t *v){return EntityModel(v)==181?kDodoGear:kGear;}
bool Same(const Identity &a,const Identity &b){return a.ref==b.ref&&a.model==b.model&&a.clump==b.clump&&a.vtable==b.vtable;}
bool Identify(const uint8_t *v,Identity &id)
{
 if(api.timeMs){const uint32_t now=*api.timeMs;if(gClockSeen&&now<gClock){gStates.clear();gEntries=gFlags=nullptr;}gClock=now;gClockSeen=true;}
 if(!v||!gPool||!*gPool||!api.PoolsGetVehicle)return false;
 const uint8_t *pool=*gPool,*entries=*reinterpret_cast<uint8_t *const *>(pool),*flags=*reinterpret_cast<uint8_t *const *>(pool+8);
 const int count=*reinterpret_cast<const int *>(pool+16);const uintptr_t at=reinterpret_cast<uintptr_t>(v),base=reinterpret_cast<uintptr_t>(entries);
 if(!entries||!flags||count<=0||count>10000||at<base||(at-base)%0x6f0)return false;
 const size_t i=(at-base)/0x6f0;if(i>=size_t(count)||(flags[i]&128))return false;
 const int ref=int(i*256)|flags[i];if(api.PoolsGetVehicle(ref)!=v||!(airportjets::Owns(v)||(EntityModel(v)==181&&planeflight::Owns(v)))||*reinterpret_cast<const int32_t *>(v+0x348)!=4)return false;
 if(gEntries!=entries||gFlags!=flags||gStates.size()!=size_t(count)){gStates.assign(count,{});gEntries=entries;gFlags=flags;}
 id={ref,EntityModel(v),*reinterpret_cast<void *const *>(v+0x58),*reinterpret_cast<void *const *>(v)};return true;
}
bool Current(const uint8_t *v,const Identity &id){Identity now;return Identify(v,now)&&Same(id,now);}
State Load(const Identity &id){const State &old=gStates[size_t(id.ref>>8)];State out;if(Same(old.id,id))out=old;out.id=id;return out;}
bool Deployed(const uint8_t *v){if(EntityModel(v)==181)return true;const float down=airportjets::GearDown(v);return std::isfinite(down)&&down>=1.0f;}
struct Contact {bool hit=false;CVector point{},normal{},lever{};float compression=0;alignas(16) std::array<uint8_t,48> col{};};
int NativeWheelIndex(int i){return i==0?0:i==1?1:3;}
bool Query(uint8_t *v,const Identity &id,int i,Contact &contact,const AircraftBodySnapshot *material=nullptr)
{
 if((material&&!material->wheelPresent[NativeWheelIndex(i)])||!Deployed(v)||!api.ProcessVerticalLine)return false;
 const auto &gear=GearFor(v);const CVector hub=Add(EntityPos(v),Basis(v,gear[i].center)),up=Row(v,0x28);
 if(!Finite(hub)||!Finite(up)||up.z<.65f)return false;
 CVector from=hub;from.z+=kTravel;uint8_t *entity=nullptr;
 // A runway is native building geometry. Excluding vehicles prevents these
 // suspension rays from hitting their own aircraft COL or tire mesh.
 const bool hit=api.ProcessVerticalLine(from,hub.z-gear[i].radius-kTravel-.005f,contact.col.data(),entity,true,false,false,false,false,false,nullptr);
 if(!Current(v,id)||!Deployed(v)||!hit)return false;
 const CVector p=Row(contact.col.data(),0),normal=Unit(Row(contact.col.data(),0x10));
 if(!Finite(p)||!Finite(normal)||normal.z<.35f||Dot(up,normal)<.2f)return false;
 const float compression=(gear[i].radius-Dot(Sub(hub,p),normal))/Dot(up,normal);
 if(!std::isfinite(compression)||compression< -kTravel-.005f||compression>kTravel+.2f)return false;
 contact.hit=true;contact.point=p;contact.normal=normal;contact.lever=Sub(p,EntityPos(v));contact.compression=std::clamp(compression,-kTravel,kTravel);return true;
}
std::array<float,3> Loads(CVector com,const std::array<Gear,3> &gear)
{
 const CVector a=gear[0].center,b=gear[1].center,c=gear[2].center;
 const float d=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);
 std::array<float,3> w{{((b.y-c.y)*(com.x-c.x)+(c.x-b.x)*(com.y-c.y))/d,((c.y-a.y)*(com.x-c.x)+(a.x-c.x)*(com.y-c.y))/d,0}};
 w[2]=1-w[0]-w[1];float sum=0;for(float &weight:w){weight=std::max(0.0f,weight);sum+=weight;}for(float &weight:w)weight/=sum;return w;
}
void Impulse(uint8_t *v,CVector force,CVector lever){gMove(v,force.x,force.y,force.z);gTurn(v,force.x,force.y,force.z,lever.x,lever.y,lever.z);}
void Apply(uint8_t *v,const Identity &id)
{
 if(!api.timeStep||!std::isfinite(*api.timeStep)||*api.timeStep<=0||Wrecked(v)||!(v[0x156]&2))return;
 const float tick=std::min(*api.timeStep,3.0f),dt=tick/kHz,mass=Scalar(v,0xcc),turnMass=Scalar(v,0xd0),frequency=Scalar(v,0xd4);
 const CVector com=Row(v,0xe4),up=Row(v,0x28),forward=Row(v,0x18),right=Row(v,8);
 auto *handling=*reinterpret_cast<uint8_t **>(v+0x160);
 if(!handling||!Finite(com)||!Finite(up)||!Finite(forward)||!Finite(right)||!Finite(Row(v,0x84))||!Finite(Row(v,0x90))||
    Length(up)<.9f||Length(forward)<.9f||Length(right)<.9f||!std::isfinite(mass)||!std::isfinite(turnMass)||mass<=0||turnMass<=0||!std::isfinite(frequency)||frequency<=0)return;
 AircraftBodySnapshot material;const bool source=aircraftbodyclient::Read(v,material);
 State state=Load(id);std::array<Contact,3> contact;int grounded=0;
 for(int i=0;i<3;++i){if(Query(v,id,i,contact[i],source?&material:nullptr))++grounded;if(!Current(v,id))return;state.pose[i].grounded=contact[i].hit;state.pose[i].compression=contact[i].hit?contact[i].compression:-kTravel;}
 uint8_t *player=api.FindPlayerPed?api.FindPlayerPed():nullptr,*pad=api.GetPad?api.GetPad(0):nullptr;
 const bool pilot=player&&pad&&!PadControlsDisabled(pad)&&!gMission()&&!*gCutscene&&PedInVehicle(player)&&PedVehicle(player)==v&&*reinterpret_cast<uint8_t **>(v+0x218)==player&&(v[0x60]>>3)==0;
 const float brake=pilot?std::clamp(gBrake(pad)/255.0f,0.0f,1.0f):0;
 const float lock=Scalar(handling,0xa0),target=pilot&&std::isfinite(lock)?-std::clamp(gSteer(pad)/128.0f,-1.0f,1.0f)*std::clamp(lock,0.0f,45.0f)*.01745329252f:0;
 state.pose[0].steer+=std::clamp(target-state.pose[0].steer,-1.8f*dt,1.8f*dt);
 const auto &gear=GearFor(v);const auto weight=Loads(com,gear);const CVector centre=Basis(v,com);
 std::array<float,3> compression{},normalLoad{};for(int i=0;i<3;++i)compression[i]=contact[i].compression;
 const int steps=std::max(1,int(std::ceil(dt/.005f)));const float sub=dt/steps,stiffness=kGravity/kTravel,damping=2*std::sqrt(stiffness);
 for(int s=0;s<steps;++s){
  for(int i=0;i<3;++i)if(contact[i].hit){
   if(!Current(v,id)||!Deployed(v))return;
   // Gravity was already applied by the native CPhysical predecessor for
   // this entire tick. Reconstruct the remaining substep's feedback velocity
   // so static support does not count that one gravity impulse as a landing.
   const CVector velocity=Add(Mul(gSpeed(v,Sub(contact[i].lever,centre)),kHz),{0,0,kGravity*(dt-s*sub)});
   if(!Finite(velocity))return;
   const float normalSpeed=Dot(velocity,contact[i].normal);
   const float acceleration=std::clamp(kGravity*contact[i].normal.z+stiffness*compression[i]-damping*normalSpeed,0.0f,4*kGravity);
   const float force=mass*weight[i]*acceleration;normalLoad[i]=force;
   Impulse(v,Mul(contact[i].normal,force*sub/kHz),contact[i].lever);
   compression[i]=std::clamp(compression[i]-normalSpeed*sub/std::max(.2f,Dot(up,contact[i].normal)),-kTravel,kTravel);
  }
 }
 for(int i=0;i<3;++i){
  if(!Current(v,id))return;
  if(contact[i].hit&&normalLoad[i]>0&&Deployed(v)){
   const auto wheel=material.wheels[NativeWheelIndex(i)];const float angle=state.pose[i].steer*(source?wheel.steering:1)+(source?wheel.toe:0);
   CVector f=Add(Mul(forward,std::cos(angle)),Mul(Cross(up,forward),std::sin(angle)));f=Unit(Sub(f,Mul(contact[i].normal,Dot(f,contact[i].normal))));
   CVector r=Unit(Cross(f,contact[i].normal)),velocity=gSpeed(v,Sub(contact[i].lever,centre)),lever=contact[i].lever;
   contact[i].col[0x20]=29; // native Automobile wheel/runway surface pairing
   const float traction=Scalar(handling,0x30),deceleration=Scalar(handling,0x94);
   if(!std::isfinite(traction)||traction<0||!std::isfinite(deceleration)||deceleration<0)return;
   const float grip=.004f*traction*(normalLoad[i]/(mass*kGravity))/frequency*gAdhesion(contact[i].col.data())*gWet(contact[i].col[0x22]);
   if(!std::isfinite(grip)||grip<0||!Finite(velocity))return;
   // Native ProcessWheel owns longitudinal rolling resistance, brake locking,
   // lateral slip and the traction circle. Jet engines never drive the tires.
   gWheel(v,f,r,velocity,lever,grounded,0,deceleration*brake*tick*(source?wheel.brake:1),grip*(source?wheel.grip:1),int8_t(i),&state.speed[i],&state.wheel[i],0);
   if(!Current(v,id))return;
   const float rotation=gRotation(v,state.wheel[i],f,velocity,gear[i].radius);
   if(std::isfinite(rotation)){state.pose[i].spin=std::remainder(state.pose[i].spin+rotation,6.28318530718f);state.rate[i]=rotation/dt;}
  }else{state.pose[i].spin=std::remainder(state.pose[i].spin+state.rate[i]*dt,6.28318530718f);state.rate[i]*=std::exp(-.2f*dt);}
 }
 if(Current(v,id))gStates[size_t(id.ref>>8)]=state;
}
void Control(uint8_t *v){Identity id;const bool owned=Identify(v,id);gControl(v);if(owned&&Current(v,id))Apply(v,id);}
template<class T> bool Bind(T &value,const char *name){value=reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY,name));return value!=nullptr;}
}
bool Grounded(const uint8_t *vehicle)
{
 Identity id;if(!gReady||!Identify(vehicle,id)||!Deployed(vehicle)||Wrecked(vehicle)||!(vehicle[0x156]&2))return false;
 AircraftBodySnapshot material;const bool source=aircraftbodyclient::Read(vehicle,material);
 for(int i=0;i<3;++i){Contact c;if(Query(const_cast<uint8_t *>(vehicle),id,i,c,source?&material:nullptr))return true;if(!Current(vehicle,id))return false;}return false;
}
bool Snapshot(const uint8_t *vehicle,std::array<WheelPose,3> &out)
{
 out={};Identity id;if(!gReady||!Identify(vehicle,id))return false;const State &s=gStates[size_t(id.ref>>8)];if(!Same(s.id,id))return false;out=s.pose;
 AircraftBodySnapshot material;const bool source=aircraftbodyclient::Read(vehicle,material),deployed=Deployed(vehicle);
 for(int i=0;i<3;++i)if(!deployed||(source&&!material.wheelPresent[NativeWheelIndex(i)])){out[i].grounded=false;out[i].compression=-kTravel;}
 return true;
}
bool Install()
{
 if(gReady)return true;
 if(!Bind(gMove,"_ZN9CPhysical14ApplyMoveForceEfff")||!Bind(gTurn,"_ZN9CPhysical14ApplyTurnForceEffffff")||!Bind(gSpeed,"_ZN9CPhysical8GetSpeedERK7CVector")||
    !Bind(gWheel,"_ZN8CVehicle12ProcessWheelER7CVectorS1_S1_S1_ifffaPfP11tWheelStatet")||!Bind(gRotation,"_ZN8CVehicle20ProcessWheelRotationE11tWheelStateRK7CVectorS3_f")||
    !Bind(gAdhesion,"_ZN13CSurfaceTable16GetAdhesiveLimitER9CColPoint")||!Bind(gWet,"_ZN13CSurfaceTable16GetWetMultiplierEh")||!Bind(gBrake,"_ZN4CPad8GetBrakeEv")||
    !Bind(gSteer,"_ZN4CPad20GetSteeringLeftRightEv")||!Bind(gMission,"_ZN11CTheScripts18IsPlayerOnAMissionEv")||!Bind(gCutscene,"_ZN12CCutsceneMgr10ms_runningE")||!Bind(gPool,"_ZN6CPools15ms_pVehiclePoolE"))return false;
 if(!gControl)gControl=reinterpret_cast<decltype(gControl)>(hooks::Chain("_ZN6CPlane14ProcessControlEv",reinterpret_cast<void *>(Control)));
 if(!gControl)return false;gReady=true;planeflight::SetAirportWheelControl(Grounded);return true;
}
}
#ifdef VC_SOURCE_UUID_BYTES
extern "C" __attribute__((visibility("default"))) bool VCSourceDodoWheelState(const void *vehicle,float *spin,float *compression,int *grounded){
 std::array<jetwheels::WheelPose,3> pose{};if(!spin||!compression||!grounded||!jetwheels::Snapshot(static_cast<const uint8_t*>(vehicle),pose))return false;
 for(int i=0;i<3;++i){spin[i]=pose[i].spin;compression[i]=pose[i].compression;grounded[i]=pose[i].grounded;}return true;
}
#endif

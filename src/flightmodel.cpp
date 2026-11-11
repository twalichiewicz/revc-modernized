#include "flightmodel.h"
#include "flightinput.h"
#include "planeflight.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "heli.h"
#include "log.h"
#include "nativebullet.h"
#include "shotcollision.h"
#include "aircraftbodyclient.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <vector>

using namespace game;
namespace flightmodel { namespace {
constexpr float kHz=50,kGravity=20;
void (*gFlying)(uint8_t *,int),(*gControl)(uint8_t *),(*gFix)(uint8_t *);
void (*gImpact)(uint8_t *,uint8_t *,uint8_t *,CVector *,CVector *,uint8_t *,CVector2D),(*gBulletUpdate)();
uint8_t *gBullets,**gScriptedHelis;
void *(*gParticle)(int32_t,const CVector &,const CVector &,uint8_t *,float,int32_t,int32_t,int32_t,int32_t);
bool (*gBlade)(uint8_t *,CVector,uint8_t *,int16_t,float,float);
void (*gMove)(uint8_t *,float,float,float),(*gTurn)(uint8_t *,float,float,float,float,float,float);
int16_t (*gThrottle)(uint8_t *),(*gBrake)(uint8_t *),(*gPitch)(uint8_t *),(*gRoll)(uint8_t *),(*gGunPitch)(uint8_t *),(*gGunYaw)(uint8_t *);
bool (*gLeft)(uint8_t *),(*gRight)(uint8_t *),(*gHorn)(uint8_t *);
bool (*gOnMission)();bool *gCutscene;
uint8_t **gPool;
void *(*gAtomics)(void *,void *(*)(void *,void *),void *),*(*gAtomicFrame)(const void *),*(*gParent)(const void *);
float *(*gFrameMatrix)(void *);void *(*gRoot)(const void *);
const char *(*gName)(void *);
unsigned (*gFlags)(const void *);
void *(*gSetFlags)(void *,unsigned);
struct Hidden {void *atomic;unsigned flags;};
struct Rotor {int ref=-1,model=-1;bool failed=false,playerScript=false;uint8_t broken=0;int mainHits=0,tailHits=0;void *clump=nullptr;std::vector<Hidden> hidden,next;};
std::vector<Rotor> gRotors;
const uint8_t *gPoolEntries,*gPoolFlags;
uint32_t gLastClock=0;bool gClockSeen=false;
bool gInstalled;
void ObserveTime(uint32_t now)
{
 // A load can replace actor ownership before the after-world Frame callback.
 // Discard every old record before eligibility checks, including records that
 // the new mission/driver would exclude. Never replay old atomic flags here.
 if(gClockSeen&&now<gLastClock){gRotors.clear();gPoolEntries=gPoolFlags=nullptr;}
 gLastClock=now;gClockSeen=true;
}
CVector Add(CVector a,CVector b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
CVector Sub(CVector a,CVector b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
CVector Mul(CVector a,float k){return {a.x*k,a.y*k,a.z*k};}
float Dot(CVector a,CVector b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float Length(CVector a){return std::sqrt(Dot(a,a));}
bool Finite(CVector p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
CVector Row(const uint8_t *v,size_t o){return *reinterpret_cast<const CVector *>(v+o);}
float Scalar(const uint8_t *v,size_t o){return *reinterpret_cast<const float *>(v+o);}
void SetScalar(uint8_t *v,size_t o,float x){*reinterpret_cast<float *>(v+o)=x;}
bool Model(const uint8_t *v){switch(EntityModel(v)){case 155:case 177:case 199:case 217:case 218:case 227:return true;default:return false;}}
bool Eligible(const uint8_t *v){return v && (*reinterpret_cast<const uint32_t *>(v+0x60)&7)==2 && Model(v) && *reinterpret_cast<const int32_t *>(v+0x348)==0 && !Wrecked(v);}
bool PlayerOwns(uint8_t *v);
void Relinquish(uint8_t *v,Rotor &r);
bool FreePlay(){return gOnMission&&gCutscene&&!*gCutscene&&!gOnMission();}
bool DamageModel(const uint8_t *v)
{
 if(Model(v))return *reinterpret_cast<const int32_t *>(v+0x348)==0;
 return EntityModel(v)==165&&*reinterpret_cast<const int32_t *>(v+0x348)==3&&gScriptedHelis&&(gScriptedHelis[0]==v||gScriptedHelis[1]==v)&&v[0x3b1]==0;
}
Rotor *State(uint8_t *v,bool repairing=false)
{
 if(api.timeMs)ObserveTime(*api.timeMs);
 if(!v||!gPool||!*gPool||!api.PoolsGetVehicle)return nullptr;
 const uint8_t *pool=*gPool,*entries=*reinterpret_cast<uint8_t *const *>(pool),*flags=*reinterpret_cast<uint8_t *const *>(pool+8);
 const int capacity=*reinterpret_cast<const int *>(pool+0x10);
 const uintptr_t address=reinterpret_cast<uintptr_t>(v),base=reinterpret_cast<uintptr_t>(entries);
 if(!entries||!flags||capacity<=0||capacity>100000||address<base||(address-base)%0x6f0)return nullptr;
 const size_t index=(address-base)/0x6f0;if(index>=size_t(capacity)||(flags[index]&0x80))return nullptr;
 const int ref=int(index*256)|flags[index];if(api.PoolsGetVehicle(ref)!=v)return nullptr;
 if((*reinterpret_cast<const uint32_t *>(v+0x60)&7)!=2||!DamageModel(v)||(!repairing&&Wrecked(v)))return nullptr;
 if(gPoolEntries!=entries||gPoolFlags!=flags||gRotors.size()!=size_t(capacity)){gRotors.assign(capacity,{});gPoolEntries=entries;gPoolFlags=flags;}
 Rotor &r=gRotors[index];if(r.ref!=ref||r.model!=EntityModel(v)){r={};r.ref=ref;r.model=EntityModel(v);}
 if(v[0x294]==2){
  // SCM CREATE_CAR permanently labels free-roam aircraft as MISSION_VEHICLE.
  // Current driver/script ownership decides whether its controls are ours.
  const bool player=PlayerOwns(v)&&FreePlay();if(player)r.playerScript=true;
  const bool abandoned=r.playerScript&&FreePlay()&&(v[0x60]>>3)==4&&v[0x19e]==0&&!*reinterpret_cast<uint8_t **>(v+0x218);
  if(!player&&!abandoned&&!(repairing&&r.playerScript)){Relinquish(v,r);return nullptr;}
 }
 return &r;
}
int RotorFrame(void *frame)
{
 for(int depth=0;frame&&depth<16;++depth,frame=gParent(frame)){
  const char *name=gName(frame);
  if(name&&(!std::strcmp(name,"toprotor")||!std::strcmp(name,"bonnet_hi_ok")||!std::strcmp(name,"bonnet_hi_dam")))return 1;
  if(name&&(!std::strcmp(name,"rearrotor")||!std::strcmp(name,"backrotor")||!std::strcmp(name,"boot_hi_ok")||!std::strcmp(name,"boot_hi_dam")))return 2;
 }
 return 0;
}
void *HideAtomic(void *atomic,void *data)
{
 auto &state=*static_cast<Rotor *>(data);if(!(state.broken&RotorFrame(gAtomicFrame(atomic))))return atomic;
 const auto found=std::find_if(state.hidden.begin(),state.hidden.end(),[&](const Hidden &h){return h.atomic==atomic;});
 if(state.next.size()>=512)return nullptr;
 state.next.push_back({atomic,found==state.hidden.end()?gFlags(atomic):found->flags});
 gSetFlags(atomic,gFlags(atomic)&~4u);return atomic;
}
void Hide(uint8_t *v,Rotor &r)
{
 void *clump=EntityClump(v);if(r.clump!=clump){r.hidden.clear();r.clump=clump;}
 r.next.clear();if(clump)gAtomics(clump,HideAtomic,&r);r.hidden.swap(r.next);
}
void *RestoreAtomic(void *atomic,void *data)
{
 const auto &state=*static_cast<const Rotor *>(data);if(!(state.broken&RotorFrame(gAtomicFrame(atomic))))return atomic;
 const auto found=std::find_if(state.hidden.begin(),state.hidden.end(),[&](const Hidden &h){return h.atomic==atomic;});
 // Our suppression owns only RENDER. Retain native changes to other flags,
 // and never expose a solid/damaged component that was already hidden.
 if(found!=state.hidden.end())gSetFlags(atomic,(gFlags(atomic)&~4u)|(found->flags&4u));return atomic;
}
void Relinquish(uint8_t *v,Rotor &r)
{
 // State validated this pool generation/model. Enumerate only the current
 // matching clump: saved atomic addresses are identity tokens, never followed.
 if(r.clump&&r.clump==EntityClump(v))gAtomics(r.clump,RestoreAtomic,&r);
 const int ref=r.ref,model=r.model;r={};r.ref=ref;r.model=model;
}
struct Origin {void *root;CVector point{};bool found=false;};
void *FindOrigin(void *atomic,void *data)
{
 auto &out=*static_cast<Origin *>(data);void *frame=gAtomicFrame(atomic);const char *name=frame?gName(frame):nullptr;
 if(!name||std::strcmp(name,"toprotor"))return atomic;
 CVector p{};int depth=0;
 while(frame&&frame!=out.root&&depth++<32){
  const float *m=gFrameMatrix(frame);if(!m)return atomic;
  p={m[12]+m[0]*p.x+m[4]*p.y+m[8]*p.z,m[13]+m[1]*p.x+m[5]*p.y+m[9]*p.z,m[14]+m[2]*p.x+m[6]*p.y+m[10]*p.z};
  frame=gParent(frame);
 }
 if(frame==out.root&&Finite(p)){out.point=p;out.found=true;}return atomic;
}
bool RotorOrigin(uint8_t *v,CVector &point)
{
 void *clump=EntityClump(v);if(!clump)return false;Origin origin{gRoot(clump)};if(!origin.root)return false;
 gAtomics(clump,FindOrigin,&origin);if(origin.found)point=origin.point;return origin.found;
}
bool PlayerOwns(uint8_t *v)
{
 uint8_t *p=api.FindPlayerPed?api.FindPlayerPed():nullptr;
 return p&&PedInVehicle(p)&&PedVehicle(p)==v&&*reinterpret_cast<uint8_t **>(v+0x218)==p&&(v[0x60]>>3)==0;
}
bool Helicopter(uint8_t *v,int model)
{
 if(model!=6||!Eligible(v))return false;
 Rotor *rotor=State(v); // release prior suppression even at a direct native flight entry
 if(!(v[0x156]&2)||!PlayerOwns(v)||!api.GetPad||!api.timeStep||(gCutscene&&*gCutscene)||(v[0x294]==2&&!rotor))return false;
 if(gConfig.heliDamage&&rotor&&rotor->failed&&!aircraftbodyclient::Owned(v))return true;
 uint8_t *pad=api.GetPad(0);if(!pad)return false;
 const CVector r=Row(v,8),f=Row(v,0x18),u=Row(v,0x28),start=Mul(Row(v,0x84),kHz),angular=Mul(Row(v,0x90),kHz),com=Row(v,0xe4);
 const float mass=Scalar(v,0xcc),turnMass=Scalar(v,0xd0),height=EntityPos(v).z,health=VehicleHealth(v),tick=*api.timeStep;
 if(!Finite(r)||!Finite(f)||!Finite(u)||!Finite(start)||!Finite(angular)||!Finite(com)||
    Length(r)<.9f||Length(f)<.9f||Length(u)<.9f||!std::isfinite(mass)||!std::isfinite(turnMass)||mass<=0||turnMass<=0||
    !std::isfinite(height)||!std::isfinite(health)||!std::isfinite(Scalar(v,0x5f0))||!std::isfinite(tick)||tick<0)return false;
 const float dt=std::min(tick,3.0f)/kHz;if(dt==0)return true;
 float collective=std::clamp((gThrottle(pad)-gBrake(pad))/255.0f,-1.0f,1.0f);
 const auto axes=flightinput::Read(pad,gPitch(pad),float(gRight(pad))-float(gLeft(pad)),gGunPitch(pad),gGunYaw(pad));
 float pitch=axes.pitch*.45f;
 float roll=std::clamp(gRoll(pad)/128.0f,-1.0f,1.0f)*.45f;
 const float yaw=axes.yaw;
 if(gHorn(pad)){pitch=std::clamp(Dot(start,f)*.025f,-.35f,.35f);roll=std::clamp(-Dot(start,r)*.025f,-.35f,.35f);}
 float climb=collective*(collective>=0?12.0f:8.0f);
 // The cloud layer tops at2600m. Preserve useful hover/climb above it; the
 // upper service ceiling is a gradual climb reduction, never a position clamp.
 if(height>3200)climb=std::min(climb,std::max(0.0f,12*(3800-height)/600));
 if(height>3800)climb=std::min(climb,-std::min(8.0f,(height-3800)*.05f));
 const auto damage=api.AirframeEffects?api.AirframeEffects(v):airframemodel::Effects{};
 // Material blade loss starts at the tips, where thrust ~ integral c r^2 dr is
 // concentrated: surviving material fraction a leaves about a^3 of rotor thrust.
 const float material=std::clamp(damage.rotor,0.0f,1.0f),rotorThrust=aircraftbodyclient::Owned(v)?material*material*material:damage.rotor;
 const float power=(v[0x295]&0x10)?std::clamp(Scalar(v,0x5f0)/.22f,0.0f,1.0f)*(damage.managed?damage.power*rotorThrust:std::clamp((health-100)/500,0.0f,1.0f)):0;
 if(api.AirframeFlightUsed)api.AirframeFlightUsed(v);pitch*=damage.pitch;roll*=damage.roll;
 // Automobile calls CPhysical::ProcessControl (including gravity) before
 // FlyingControl. Reconstruct the start of that step for feedback; the output
 // below is compared with its actual post-gravity incoming velocity.
 CVector speed=Add(start,{0,0,kGravity*dt});const int steps=std::max(1,int(std::ceil(dt/.005f)));const float sub=dt/steps;
 for(int i=0;i<steps;++i){
  const float lift=std::clamp((kGravity+3*(climb-speed.z))/std::max(.65f,u.z),0.0f,42.0f)*power;
  const CVector acceleration=Add(Mul(u,lift),{-speed.x*.12f,-speed.y*.12f,-kGravity});
  speed=Add(speed,Mul(acceleration,sub));
 }
 const CVector impulse=Mul(Sub(speed,start),mass/kHz);gMove(v,impulse.x,impulse.y,impulse.z);
 const float pr=std::clamp(2.2f*(pitch-std::asin(std::clamp(f.z,-1.0f,1.0f))),-1.1f,1.1f);
 const float rr=std::clamp(2.2f*(roll+std::asin(std::clamp(r.z,-1.0f,1.0f))),-1.1f,1.1f);
 const CVector desired=Add(Add(Mul(r,pr),Mul(f,rr)),Mul(u,-yaw*.7f*damage.yaw*damage.antiTorque+(1-damage.antiTorque)*1.5f));
 const CVector change=Mul(Sub(Mul(desired,power),angular),1-std::exp(-5*dt));
 const CVector centre=Add(Add(Mul(r,com.x),Mul(f,com.y)),Mul(u,com.z));
 auto torque=[&](CVector force,CVector lever,float amount){force=Mul(force,amount*turnMass/kHz);lever=Add(centre,lever);gTurn(v,force.x,force.y,force.z,lever.x,lever.y,lever.z);};
 torque(u,f,Dot(change,r));torque(r,u,Dot(change,f));torque(f,r,Dot(change,u));return true;
}
void Hook_Flying(uint8_t *v,int model){if(!planeflight::Apply(v,model)&&!Helicopter(v,model))gFlying(v,model);}
bool Hook_Blade(uint8_t *v,CVector at,uint8_t *matrix,int16_t type,float radius,float damage)
{
 // A material rotor owns its strike outcome: the native disc test is clamped to
 // attached blades, and an accepted overlap becomes work on the struck blade.
 // Native slowdown/impulse stay; there is no binary failure or hidden rotor.
 if(aircraftbodyclient::Owned(v)){
  float attached=radius;if(type==3&&aircraftbodyclient::BladeRadius(v,attached)){if(!(attached>.05f))return false;radius=std::min(radius,attached);}
  const float before=Scalar(v,0x5f0);if(type==3&&Eligible(v)&&State(v))RotorOrigin(v,at);
  const bool result=gBlade(v,at,matrix,type,radius,damage);
  if(type==3&&before>.15f&&Scalar(v,0x5f0)==.15f&&aircraftbodyclient::Owned(v))aircraftbodyclient::BladeStrike(v,before,api.timeMs?*api.timeMs:0);
  return result;
 }
 Rotor *r=Eligible(v)?State(v):nullptr;const int ref=r?r->ref:-1;const float before=r?Scalar(v,0x5f0):0;
 if(r&&type==3)RotorOrigin(v,at);
 const bool result=gBlade(v,at,matrix,type,radius,damage);
 r=State(v);
 // Native BladeColSectorList ALWAYS returnsfalse. This exact store occurs
 // only after accepted solid contact, not after its ignored axial/ped cases.
 if(gConfig.heliDamage&&r&&r->ref==ref&&before>.15f&&Scalar(v,0x5f0)==.15f){r->failed=true;r->broken|=1;SetScalar(v,0x5f0,0);Hide(v,*r);}
 return result;
}
void Hook_Control(uint8_t *v)
{
 Rotor *r=Eligible(v)?State(v):nullptr;const int ref=r?r->ref:-1;
 if(gConfig.heliDamage&&r&&r->failed&&!aircraftbodyclient::Owned(v)){
  SetScalar(v,0x5f0,-1);if(v[0x19e]==20)v[0x19e]=0;
  if(r->broken==2&&api.timeStep&&std::isfinite(*api.timeStep)&&*api.timeStep>0){
   const CVector right=Row(v,8),forward=Row(v,0x18),up=Row(v,0x28),angular=Row(v,0x90),com=Row(v,0xe4);
   const float turnMass=Scalar(v,0xd0),dt=std::min(*api.timeStep,3.0f)/kHz;
   if(Finite(right)&&Finite(forward)&&Finite(up)&&Finite(angular)&&Finite(com)&&std::isfinite(turnMass)&&turnMass>0){
    const float impulse=(1.25f-Dot(angular,up)*kHz)*(1-std::exp(-1.2f*dt))*turnMass/kHz;
    const CVector force=Mul(forward,impulse),lever=Add(right,Add(Add(Mul(right,com.x),Mul(forward,com.y)),Mul(up,com.z)));
    gTurn(v,force.x,force.y,force.z,lever.x,lever.y,lever.z);
   }
  }
 }
 gControl(v);
 r=State(v);if(gConfig.heliDamage&&r&&r->ref==ref&&r->failed&&!aircraftbodyclient::Owned(v)){SetScalar(v,0x5f0,0);Hide(v,*r);}
}
void Hook_Fix(uint8_t *v)
{
 if(Rotor *r=State(v,true)){
  // Restore only the same vehicle clump before native Fix selects its repaired
  // components. No stale atomic pointer is followed after a reload/generation.
  if(r->clump&&r->clump==EntityClump(v))gAtomics(r->clump,RestoreAtomic,r);
  const int ref=r->ref,model=r->model;const bool playerScript=r->playerScript;
  *r={};r->ref=ref;r->model=model;r->playerScript=playerScript;
 }
 gFix(v);
}
struct Contact {
 uint8_t *vehicle=nullptr;int ref=-1,part=0;CVector at{},normal{};void *clump=nullptr;std::array<uint8_t,0x40> matrix{};
};
struct Ignore {
 uint8_t **slot=api.ignoreEntity,*saved=slot?*slot:nullptr;
 explicit Ignore(uint8_t *creator){if(slot)*slot=creator;}
 ~Ignore(){if(slot)*slot=saved;}
};
bool FindRotor(CVector from,CVector to,uint8_t *creator,Contact &contact)
{
 if(!gConfig.heliDamage||!gPool||!*gPool||!api.PoolsGetVehicle||!api.ProcessLineOfSight||!api.ignoreEntity||!Finite(from)||!Finite(to))return false;
 const CVector direction=Sub(to,from);const float length2=Dot(direction,direction);if(length2<1e-6f)return false;
 const uint8_t *pool=*gPool,*flags=*reinterpret_cast<uint8_t *const *>(pool+8);const int count=*reinterpret_cast<const int *>(pool+0x10);
 if(!flags||count<=0||count>100000)return false;
 float nearest=1;Contact best;
 for(int i=0;i<count;++i){
  if(flags[i]&0x80)continue;uint8_t *v=api.PoolsGetVehicle(i*256|flags[i]);if(!v||v==creator||!State(v))continue;
  const CVector offset=Sub(EntityPos(v),from);const float t=std::clamp(Dot(offset,direction)/length2,0.0f,1.0f);
  const CVector away=Sub(offset,Mul(direction,t));if(Dot(away,away)>400)continue; // candidate cull, never a hit shape
  shotcollision::MeshHit hit;
  if(!shotcollision::TraceVehicleMesh(v,{from.x,from.y,from.z},{to.x,to.y,to.z},hit)||!std::isfinite(hit.fraction)||hit.fraction<0||hit.fraction>=nearest)continue;
  Rotor *state=State(v);if(!state)continue;
  nearest=hit.fraction;best.vehicle=v;best.ref=state->ref;best.part=RotorFrame(const_cast<void *>(hit.frame));
  best.at={hit.position.x,hit.position.y,hit.position.z};best.normal={hit.normal.x,hit.normal.y,hit.normal.z};best.clump=EntityClump(v);std::memcpy(best.matrix.data(),v+8,best.matrix.size());
 }
 if(!best.vehicle||!best.part||!Finite(best.at)||!Finite(best.normal))return false; // nearer body owns its ray
 alignas(16) uint8_t point[0x30]{};uint8_t *victim=nullptr;bool hit;
 {const Ignore ignored(creator);hit=api.ProcessLineOfSight(from,to,point,victim,true,true,true,true,true,false,false,true);}
 const CVector p=*reinterpret_cast<CVector *>(point),distance=Sub(best.at,from),world=Sub(p,from);
 if(hit&&(!Finite(p)||(victim!=best.vehicle&&Dot(world,world)<=Dot(distance,distance))))return false;
 Rotor *state=State(best.vehicle);
 if(!state||state->ref!=best.ref||EntityClump(best.vehicle)!=best.clump||std::memcmp(best.matrix.data(),best.vehicle+8,best.matrix.size()))return false;
 contact=best;return true;
}
bool RotorContact(const Contact &c)
{
 Rotor *r=State(c.vehicle);if(!r||r->ref!=c.ref)return false;
 int &hits=c.part==1?r->mainHits:r->tailHits;
 const int beforeHits=hits,beforeModel=r->model;const bool beforeFailure=r->failed;const uint8_t beforeBroken=r->broken;
 bool broke=false;
 if(!(r->broken&c.part)&&++hits>=(c.part==1?3:6)){
  r->failed=true;r->broken|=c.part;
  if(EntityModel(c.vehicle)==165){if(!heli::FailRotor(c.vehicle,c.part==1)){
   r=State(c.vehicle);if(r&&r->ref==c.ref&&r->model==beforeModel){r->failed=beforeFailure;r->broken=beforeBroken;(c.part==1?r->mainHits:r->tailHits)=beforeHits;}return false;
  }}
  else SetScalar(c.vehicle,0x5f0,0);
  r=State(c.vehicle);if(r&&r->ref==c.ref)Hide(c.vehicle,*r);
  broke=true;
 }
 if(gParticle){
  gParticle(0,c.at,Mul(c.normal,.05f),nullptr,0,0,0,0,0);
  if(broke&&!(beforeModel==165&&c.part==2))for(int i=0;i<8;++i){const float a=i*.78539816f;gParticle(66,c.at,{std::cos(a)*.08f,std::sin(a)*.08f,.03f},nullptr,0,0,0,0,0);}
 }
 return true;
}
bool Gun(int t){return (t>=17&&t<=29)||t==32||t==33||t==35;}
bool BulletWeapon(uint8_t *weapon)
{
 if(!weapon||!Gun(WeaponType(weapon))||!api.GetWeaponInfo)return false;
 const uint8_t *info=api.GetWeaponInfo(WeaponType(weapon));int type=0;if(info)std::memcpy(&type,info,sizeof type);return type==1;
}
void Hook_Impact(uint8_t *weapon,uint8_t *shooter,uint8_t *victim,CVector *from,CVector *to,uint8_t *point,CVector2D ahead)
{
 // The component owner consumes the certified camera-ray receipt downstream.
 // Keep this older hit-counter path solely as the unmanaged fallback.
 if(victim&&api.AirframeEffects&&api.AirframeEffects(victim).managed){gImpact(weapon,shooter,victim,from,to,point,ahead);return;}
 Contact c;
 if(from&&to&&point&&BulletWeapon(weapon)&&FindRotor(*from,*to,shooter,c)){
  const CVector d=Sub(c.at,*from),native=Sub(*reinterpret_cast<CVector *>(point),*from);
  const bool managed=api.AirframeEffects&&api.AirframeEffects(c.vehicle).managed;
  if(!managed&&(!victim||victim==c.vehicle||Dot(native,native)>Dot(d,d))&&RotorContact(c))return;
 }
 gImpact(weapon,shooter,victim,from,to,point,ahead);
}
void Hook_BulletUpdate()
{
 if(gBullets&&gConfig.heliDamage&&api.timeStep&&std::isfinite(*api.timeStep)&&*api.timeStep>0){
  for(size_t i=0;i<nativebullet::kCount;++i){
   const auto b=nativebullet::Read(gBullets,i);if(b.active!=1||(b.type!=28&&b.type!=29))continue;
   CVector from{b.position.x,b.position.y,b.position.z},to=Add(from,Mul({b.velocity.x,b.velocity.y,b.velocity.z},*api.timeStep*.5f));Contact c;
   if(to.x<=-2400||to.x>=1600||std::fabs(to.y)>=2000||!FindRotor(from,to,b.creator,c))continue;
   if(api.AirframeEffects&&api.AirframeEffects(c.vehicle).managed)continue;
   auto current=nativebullet::Read(gBullets,i);if(std::memcmp(&current,&b,sizeof b)||!RotorContact(c))continue;
   current=nativebullet::Read(gBullets,i);if(!std::memcmp(&current,&b,sizeof b))gBullets[i*nativebullet::kStride+offsetof(nativebullet::Record,active)]=0;
  }
 }
 gBulletUpdate();
}
template<class T> bool Bind(T &fn,const char *n){fn=reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY,n));return fn!=nullptr;}
}
bool RotorFailed(uint8_t *v){Rotor *r=State(v);return gConfig.heliDamage&&r&&r->failed;}
void Frame(uint32_t now)
{
 ObserveTime(now);
 if(!gPool||!*gPool||!api.PoolsGetVehicle)return;
 const uint8_t *pool=*gPool,*flags=*reinterpret_cast<uint8_t *const *>(pool+8);const int count=*reinterpret_cast<const int *>(pool+0x10);
 if(!flags||count<=0||count>100000)return;
 for(int i=0;i<count;++i){
  if(flags[i]&0x80)continue;uint8_t *v=api.PoolsGetVehicle(i*256|flags[i]);Rotor *r=v?State(v):nullptr;if(!r)continue;
  if(!gConfig.heliDamage){if(r->clump&&r->clump==EntityClump(v))gAtomics(r->clump,RestoreAtomic,r);}
  else if(r->failed&&!aircraftbodyclient::Owned(v))Hide(v,*r);
 }
}
bool Install()
{
 if(gInstalled)return true;
 const bool bound=Bind(gMove,"_ZN9CPhysical14ApplyMoveForceEfff")&&Bind(gTurn,"_ZN9CPhysical14ApplyTurnForceEffffff")&&
 Bind(gThrottle,"_ZN4CPad13GetAccelerateEv")&&Bind(gBrake,"_ZN4CPad8GetBrakeEv")&&Bind(gPitch,"_ZN4CPad17GetSteeringUpDownEv")&&Bind(gRoll,"_ZN4CPad20GetSteeringLeftRightEv")&&
 Bind(gGunPitch,"_ZN4CPad15GetCarGunUpDownEv")&&Bind(gGunYaw,"_ZN4CPad18GetCarGunLeftRightEv")&&Bind(gLeft,"_ZN4CPad11GetLookLeftEv")&&Bind(gRight,"_ZN4CPad12GetLookRightEv")&&Bind(gHorn,"_ZN4CPad7GetHornEv")&&
 Bind(gAtomics,"_Z20RpClumpForAllAtomicsPN2rw5ClumpEPFPNS_6AtomicES3_PvES4_")&&Bind(gAtomicFrame,"_Z16RpAtomicGetFramePKN2rw6AtomicE")&&Bind(gName,"_Z16GetFrameNodeNamePN2rw5FrameE")&&
 Bind(gParent,"_Z16RwFrameGetParentPKN2rw5FrameE")&&Bind(gFlags,"_Z16RpAtomicGetFlagsPKN2rw6AtomicE")&&Bind(gSetFlags,"_Z16RpAtomicSetFlagsPN2rw6AtomicEj")&&
 Bind(gFrameMatrix,"_Z16RwFrameGetMatrixPN2rw5FrameE")&&Bind(gRoot,"_Z15RpClumpGetFramePKN2rw5ClumpE")&&
 Bind(gOnMission,"_ZN11CTheScripts18IsPlayerOnAMissionEv");
 gCutscene=static_cast<bool *>(dlsym(RTLD_MAIN_ONLY,"_ZN12CCutsceneMgr10ms_runningE"));
 gPool=static_cast<uint8_t **>(dlsym(RTLD_MAIN_ONLY,"_ZN6CPools15ms_pVehiclePoolE"));if(!bound||!gPool||!gCutscene)return false;
 gBullets=static_cast<uint8_t *>(dlsym(RTLD_MAIN_ONLY,"gaBulletInfo"));gScriptedHelis=static_cast<uint8_t **>(dlsym(RTLD_MAIN_ONLY,"_ZN5CHeli6pHelisE"));
 gParticle=reinterpret_cast<decltype(gParticle)>(dlsym(RTLD_MAIN_ONLY,"_ZN9CParticle11AddParticleE13tParticleTypeRK7CVectorS3_P7CEntityfiiii"));
 gFlying=reinterpret_cast<decltype(gFlying)>(hooks::Chain("_ZN8CVehicle13FlyingControlE12eFlightModel",reinterpret_cast<void *>(Hook_Flying)));
 gControl=reinterpret_cast<decltype(gControl)>(hooks::Chain("_ZN11CAutomobile14ProcessControlEv",reinterpret_cast<void *>(Hook_Control)));
 gBlade=reinterpret_cast<decltype(gBlade)>(hooks::Chain("_ZN8CVehicle16DoBladeCollisionE7CVectorR7CMatrixsff",reinterpret_cast<void *>(Hook_Blade)));
 gFix=reinterpret_cast<decltype(gFix)>(hooks::Chain("_ZN11CAutomobile3FixEv",reinterpret_cast<void *>(Hook_Fix)));
 gImpact=reinterpret_cast<decltype(gImpact)>(hooks::Chain("_ZN7CWeapon14DoBulletImpactEP7CEntityS1_P7CVectorS3_P9CColPoint9CVector2D",reinterpret_cast<void *>(Hook_Impact)));
 gBulletUpdate=reinterpret_cast<decltype(gBulletUpdate)>(hooks::Chain("_ZN11CBulletInfo6UpdateEv",reinterpret_cast<void *>(Hook_BulletUpdate)));
 gInstalled=gFlying&&gControl&&gBlade&&gFix&&gImpact&&gBulletUpdate&&gBullets;return gInstalled;
}
}

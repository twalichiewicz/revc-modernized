#include "jetwash.h"
#include "airportjets.h"
#include "config.h"
#include "game.h"
#include "planeflight.h"
#include "reactions.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <type_traits>

using namespace game;
namespace jetwash {
namespace {
constexpr size_t kVehicleStride=0x6f0,kPedStride=0x8d0;
constexpr int kVehicles=1024,kPeds=512,kSources=8,kRayBudget=96;
// Source636's measured twin exhaust rims: radius.2738, rear axis(0,-1,0).
// Move7cm past the measured rear opening so particles/rays leave the skin.
constexpr CVector kNozzles[2]={{-1.2424f,-5.80f,.2924f},{1.2424f,-5.80f,.2924f}};
uint8_t **gVehicles,**gPeds,*gParticleSystems;uint32_t *gFrame;bool *gCutscene;int32_t *gArea;
void (*gMove)(uint8_t *,float,float,float),(*gMoving)(uint8_t *),(*gReal)(uint8_t *);
void *(*gParticle)(int,const CVector &,const CVector &,uint8_t *,float,int,int,int,int);
bool gReady,gSeen;uint32_t gLastTime,gLastFrame;uint8_t *gVehicleEntries,*gVehicleFlags,*gPedEntries,*gPedFlags;
struct Pool {uint8_t *entries=nullptr,*flags=nullptr;int count=0;};
struct Actor {uint8_t *entity=nullptr,*clump=nullptr;void *vtable=nullptr;int ref=-1;int16_t model=-1;};
struct Source {Actor actor{};float particles=0;};
std::array<Source,kSources> gSources{};
CVector Add(CVector a,CVector b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
CVector Sub(CVector a,CVector b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
CVector Mul(CVector a,float s){return {a.x*s,a.y*s,a.z*s};}
float Dot(CVector a,CVector b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float Length(CVector a){return std::sqrt(Dot(a,a));}
bool Finite(CVector p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
CVector Row(const uint8_t *p,size_t at){return *reinterpret_cast<const CVector *>(p+at);}
float Scalar(const uint8_t *p,size_t at){return *reinterpret_cast<const float *>(p+at);}
Pool Read(uint8_t **binding,int limit){const auto *p=binding?*binding:nullptr;if(!p)return {};Pool out{*reinterpret_cast<uint8_t *const *>(p),*reinterpret_cast<uint8_t *const *>(p+8),*reinterpret_cast<const int32_t *>(p+16)};return out.entries&&out.flags&&out.count>0&&out.count<=limit?out:Pool{};}
bool SamePool(const Pool &pool,uint8_t **binding,int limit){const Pool current=Read(binding,limit);return pool.entries==current.entries&&pool.flags==current.flags&&pool.count==current.count;}
Actor At(const Pool &pool,int i,size_t stride,bool ped){
 // A previous native callback may have rebuilt and freed this snapshot.
 // Compare current metadata before touching either old flags or entries.
 if(!SamePool(pool,ped?gPeds:gVehicles,ped?kPeds:kVehicles)||i<0||i>=pool.count||(pool.flags[i]&128))return {};
 uint8_t *entity=pool.entries+size_t(i)*stride;const int ref=i*256+pool.flags[i];
 if((ped?api.PoolsGetPed(ref):api.PoolsGetVehicle(ref))!=entity||
    !SamePool(pool,ped?gPeds:gVehicles,ped?kPeds:kVehicles)||pool.flags[i]!=uint8_t(ref))return {};
 return {entity,EntityClump(entity),*reinterpret_cast<void **>(entity),ref,EntityModel(entity)};
}
bool Live(const Actor &a,const Pool &pool,bool ped){
 if(a.ref<0||!SamePool(pool,ped?gPeds:gVehicles,ped?kPeds:kVehicles))return false;
 const Actor b=At(pool,a.ref>>8,ped?kPedStride:kVehicleStride,ped);
 return a.entity==b.entity&&a.ref==b.ref&&a.clump==b.clump&&a.vtable==b.vtable&&a.model==b.model;
}
bool Basis(const uint8_t *entity){
 const CVector r=Row(entity,8),f=Row(entity,0x18),u=Row(entity,0x28);
 return Finite(EntityPos(entity))&&Finite(EntitySpeed(const_cast<uint8_t *>(entity)))&&Finite(r)&&Finite(f)&&Finite(u)&&std::fabs(Dot(r,r)-1)<.12f&&std::fabs(Dot(f,f)-1)<.12f&&std::fabs(Dot(u,u)-1)<.12f&&std::fabs(Dot(r,f))<.12f&&std::fabs(Dot(f,u))<.12f;
}
CVector World(const uint8_t *v,CVector p){return Add(EntityPos(v),Add(Mul(Row(v,8),p.x),Add(Mul(Row(v,0x18),p.y),Mul(Row(v,0x28),p.z))));}
bool Physical(uint8_t *p){
 const uint32_t flags=EntityFlags(p);const float mass=Scalar(p,0xcc);
 // Native blast refuses infinite-mass/static-waiting bodies. A parked
 // ordinary vehicle's static bit10 is separately woken, as native does.
 return (flags&kEntityUsesCollision)&&!(flags&(1u<<17))&&!(*reinterpret_cast<const uint16_t *>(p+0x64)&(1u<<11))&&std::isfinite(mass)&&mass>=20&&mass<=100000&&Finite(EntityPos(p))&&Finite(EntitySpeed(p));
}
bool PedEligible(uint8_t *p){const int state=PedState(p);return Physical(p)&&EntityClump(p)&&!PedFromMission(p)&&!PedInVehicle(p)&&!*reinterpret_cast<void **>(p+0x720)&&std::isfinite(Health(p))&&Health(p)>0&&state>0&&state<0x30;}
bool CarEligible(uint8_t *p){return Physical(p)&&EntityClump(p)&&IsVehicle(p)&&p[0x294]!=2&&!Wrecked(p)&&((EntityFlags(p)>>3)&31)<=4&&*reinterpret_cast<const int32_t *>(p+0x348)==0&&!airportjets::Owns(p);}
float Area(uint8_t *p,CVector axis){
 const int model=EntityModel(p);if(model<0||model>=6500||!api.modelInfoPtrs||!api.modelInfoPtrs[model])return 0;
 const auto *col=*reinterpret_cast<uint8_t *const *>(api.modelInfoPtrs[model]+0x20);if(!col)return 0;
 const CVector size=Sub(Row(col,0x1c),Row(col,0x10));if(!Finite(size)||size.x<=0||size.y<=0||size.z<=0||size.x>12||size.y>30||size.z>10)return 0;
 return std::clamp(std::fabs(Dot(axis,Row(p,8)))*size.y*size.z+std::fabs(Dot(axis,Row(p,0x18)))*size.x*size.z+std::fabs(Dot(axis,Row(p,0x28)))*size.x*size.y,.25f,12.0f);
}
CVector Exposure(uint8_t *p,bool ped,CVector origin,CVector axis){
 if(ped)return Add(EntityPos(p),{0,0,.55f}); // native root is below the exposed torso
 const int model=EntityModel(p);const uint8_t *col=*reinterpret_cast<uint8_t *const *>(api.modelInfoPtrs[model]+0x20);
 const CVector low=Row(col,0x10),high=Row(col,0x1c);
 // The upper part of a parked car can intersect wash while its root lies
 // below it. Sample the actual collision box nearest the plume axis.
 const CVector centre=World(p,Mul(Add(low,high),.5f));
 const CVector onAxis=Add(origin,Mul(axis,std::max(0.0f,Dot(Sub(centre,origin),axis))));
 const CVector relative=Sub(onAxis,EntityPos(p));
 return World(p,{std::clamp(Dot(relative,Row(p,8)),low.x,high.x),std::clamp(Dot(relative,Row(p,0x18)),low.y,high.y),std::clamp(Dot(relative,Row(p,0x28)),low.z,high.z)});
}
// Native LoadParticleData squares CR metres into entry+18; AddParticle
// compares that value with camera distance squared. The stock25m range
// rejects the normal wide aircraft camera (~27m behind either outlet).
// Only creation reads this field: live particles retain native lifespan.
struct ExhaustRange {
 uint8_t *entry=nullptr;float *range=nullptr,previous=0;bool changed=false;
 static bool Valid(const uint8_t *p){return p && *reinterpret_cast<const int32_t *>(p)==67 && !std::memcmp(p+4,"EXHAUST_FUMES",14) && *reinterpret_cast<const uint32_t *>(p+0x6c)==4;}
 ExhaustRange(){
  entry=gParticleSystems?gParticleSystems+67*0xa0:nullptr;
  if(!Valid(entry))return;
  range=reinterpret_cast<float *>(entry+0x18);previous=*range;
  // Preserve unlimited(0) and larger user ranges. The change lasts exactly
  // one native call, whose verified67 path has no particle callbacks.
  if(std::isfinite(previous)&&previous>0&&previous<10000){*range=10000;changed=true;}
 }
 ~ExhaustRange(){if(changed&&Valid(entry)&&*range==10000)*range=previous;}
};
struct Ignore {uint8_t *saved;explicit Ignore(uint8_t *p):saved(*api.ignoreEntity){*api.ignoreEntity=p;}~Ignore(){*api.ignoreEntity=saved;}};
bool Clear(uint8_t *source,uint8_t *target,CVector from,CVector to){
 alignas(16) uint8_t point[0x30]{};uint8_t *hit=nullptr;Ignore ignore(source);
 // ProcessLineOfSight identifies the endpoint recipient's own skin; a
 // boolean clear-to-center ray would incorrectly shield every solid car.
 const bool blocked=api.ProcessLineOfSight(from,to,point,hit,true,true,false,true,false,false,false,false);
 return !blocked||hit==target;
}
float Interception(CVector from,CVector axis,CVector target,float area,float power){
 const CVector delta=Sub(target,from);const float distance=Dot(delta,axis);
 if(distance<=0||distance>=24)return 0;
 const CVector radial=Sub(delta,Mul(axis,distance));const float radius=.45f+.16f*distance;
 const float cross=Length(radial)/radius;if(!std::isfinite(cross)||cross>=1)return 0;
 const float lateral=1-cross*cross,range=1-distance/24;
 const float throttle=std::clamp((power-.25f)/.75f,0.0f,1.0f);
 // Each engine supplies at most15kN. Intercepted projected area and plume
 // expansion set the recipient force; power and cone edges fade smoothly.
 return 15000*throttle*throttle*range*lateral*std::min(1.0f,area/(3.14159265f*radius*radius));
}
void Push(const Actor &recipient,bool ped,const Pool &pool,const Pool &vehicles,float dt,int &rays){
 uint8_t *target=recipient.entity;if(!target||(ped?!PedEligible(target):!CarEligible(target))||(!ped&&!Basis(target)))return;
 CVector force{};
 for(const Source &source:gSources){if(!Live(source.actor,vehicles,false))continue;uint8_t *jet=source.actor.entity;if(jet==target)continue;
  const float power=planeflight::EnginePower(jet);if(power<=.25f||!Basis(jet))continue;
  // Native vehicle seats are references in the common CVehicle header.
  if(ped){bool occupant=false;for(int seat=0;seat<9;++seat)occupant|=*reinterpret_cast<uint8_t **>(jet+0x218+size_t(seat)*8)==target;if(occupant)continue;}
  const CVector axis=Mul(Row(jet,0x18),-1);const float area=ped?.65f:Area(target,axis);if(area<=0)continue;
  for(CVector nozzle:kNozzles){const CVector origin=World(jet,nozzle),exposed=Exposure(target,ped,origin,axis);const float push=Interception(origin,axis,exposed,area,power);if(push<=.1f||rays>=kRayBudget)continue;
   ++rays;const bool clear=Clear(jet,target,origin,exposed);
   if(!Live(recipient,pool,ped)||!Live(source.actor,vehicles,false)||planeflight::EnginePower(jet)<=.25f)return;
   if(clear)force=Add(force,Mul(axis,push));
  }
 }
 const float mass=Scalar(target,0xcc),magnitude=Length(force);if(!std::isfinite(magnitude)||magnitude<.1f||!Live(recipient,pool,ped))return;
 // Limit recipients to16m/s² and.8m/s per native step. Native speed is
 // metres per1/50s; ApplyMoveForce adds impulse/mass, without a dt factor.
 CVector delta=Mul(force,std::min(dt/mass,std::min(16*dt,.8f)/magnitude));
 if(ped&&reactions::Wind(target,{delta.x,delta.y,delta.z}))return;
 if(!Live(recipient,pool,ped)||(ped?!PedEligible(target):!CarEligible(target)))return;
 if(!ped){
  if(EntityFlags(target)&(1u<<10)){EntityFlags(target)&=~(1u<<10);gMoving(target);if(!Live(recipient,pool,false))return;}
  if(((EntityFlags(target)>>3)&31)==2){EntityFlags(target)=(EntityFlags(target)&~0xf8u)|(3u<<3);gReal(target);if(!Live(recipient,pool,false))return;}
 }
 delta=Mul(delta,mass/50);gMove(target,delta.x,delta.y,delta.z);
}
}
bool Install(){
 auto bind=[](auto &out,const char *name){out=reinterpret_cast<std::decay_t<decltype(out)>>(dlsym(RTLD_MAIN_ONLY,name));return out!=nullptr;};
 bind(gParticleSystems,"mod_ParticleSystemManager");
 gReady=bind(gVehicles,"_ZN6CPools15ms_pVehiclePoolE")&&bind(gPeds,"_ZN6CPools11ms_pPedPoolE")&&bind(gFrame,"_ZN6CTimer14m_FrameCounterE")&&bind(gCutscene,"_ZN12CCutsceneMgr10ms_runningE")&&bind(gArea,"_ZN5CGame8currAreaE")&&bind(gMove,"_ZN9CPhysical14ApplyMoveForceEfff")&&bind(gMoving,"_ZN9CPhysical15AddToMovingListEv")&&bind(gReal,"_ZN8CCarCtrl26SwitchVehicleToRealPhysicsEP8CVehicle")&&bind(gParticle,"_ZN9CParticle11AddParticleE13tParticleTypeRK7CVectorS3_P7CEntityfiiii")&&api.PoolsGetPed&&api.PoolsGetVehicle&&api.ProcessLineOfSight&&api.ignoreEntity&&api.timeStep&&api.timeMs;
 return gReady;
}
void Frame(uint32_t now){
 if(!gReady)return;const Pool vehicles=Read(gVehicles,kVehicles),peds=Read(gPeds,kPeds);const uint32_t frame=*gFrame;
 const bool reset=!gSeen||now<gLastTime||frame<gLastFrame||now-gLastTime>250||vehicles.entries!=gVehicleEntries||vehicles.flags!=gVehicleFlags||peds.entries!=gPedEntries||peds.flags!=gPedFlags;
 if(!reset&&(frame==gLastFrame||now==gLastTime))return;
 // Publish the once-frame stamp before native calls, so reentry is inert.
 gSeen=true;gLastTime=now;gLastFrame=frame;gVehicleEntries=vehicles.entries;gVehicleFlags=vehicles.flags;gPedEntries=peds.entries;gPedFlags=peds.flags;
 if(reset)gSources={};
 if(reset||!gConfig.planes||*gCutscene||*gArea!=0||!vehicles.entries)return;
 const float step=*api.timeStep,dt=std::clamp(step/50,0.0f,.05f);if(!std::isfinite(step)||dt<=0)return;
 const auto poolsCurrent=[&](){return SamePool(vehicles,gVehicles,kVehicles)&&SamePool(peds,gPeds,kPeds);};
 std::array<Source,kSources> active{};int count=0;
 for(int i=0;i<vehicles.count&&count<kSources;++i){if(!poolsCurrent())return;Actor actor=At(vehicles,i,kVehicleStride,false);if(!poolsCurrent())return;if(!actor.entity||!airportjets::Owns(actor.entity)||!Basis(actor.entity)||planeflight::EnginePower(actor.entity)<=.01f)continue;
  Source &slot=active[size_t(count++)];slot.actor=actor;for(const Source &old:gSources)if(old.actor.entity==actor.entity&&old.actor.ref==actor.ref&&old.actor.clump==actor.clump&&old.actor.vtable==actor.vtable&&old.actor.model==actor.model)slot.particles=old.particles;
 }
 gSources=active;int particles=0;
 for(Source &source:gSources){if(!Live(source.actor,vehicles,false))continue;uint8_t *jet=source.actor.entity;const float power=planeflight::EnginePower(jet);source.particles=std::min(2.0f,source.particles+dt*(5+13*power));
  while(source.particles>=1&&particles<8){source.particles-=1;for(CVector nozzle:kNozzles){if(!Live(source.actor,vehicles,false)||planeflight::EnginePower(jet)<=.01f)break;
    const CVector velocity=Add(EntitySpeed(jet),Mul(Row(jet,0x18),-(.07f+.12f*power)));
    {ExhaustRange range;gParticle(67,World(jet,nozzle),velocity,nullptr,.07f+.07f*power,0,0,0,0);}
    ++particles;if(!poolsCurrent())return;
   }}
 }
 if(!count)return;int rays=0;
 for(int i=0;i<vehicles.count&&rays<kRayBudget;++i){if(!poolsCurrent())return;const Actor actor=At(vehicles,i,kVehicleStride,false);Push(actor,false,vehicles,vehicles,dt,rays);if(!poolsCurrent())return;}
 for(int i=0;i<peds.count&&rays<kRayBudget;++i){if(!poolsCurrent())return;const Actor actor=At(peds,i,kPedStride,true);Push(actor,true,peds,vehicles,dt,rays);if(!poolsCurrent())return;}
}
}

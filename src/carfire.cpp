#include "carfire.h"
#include "carmechanics.h"
#include "firemodel.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
namespace carfire {namespace {
constexpr size_t kSlots=256,kStride=0x6f0,kFireCount=40,kFireStride=0x38;
enum Gate {Countdown,Rollover,Lethal};
volatile uint64_t *gGates[3]{};void **gPool;uint8_t *gManager;bool gReady,*gCutscene;
bool (*gMission)();CVector (*gCoords)();int *gDuty;
void (*gControl)(uint8_t *),(*gDamage)(uint8_t *,float,uint16_t),(*gInflict)(uint8_t *,uint8_t *,int,float,CVector);
void (*gProcess)(uint8_t *),(*gExtinguish)(uint8_t *),(*gFix)(uint8_t *),(*gDelete)(uint8_t *),(*gRestart)(),(*gDispatch)();
uint8_t *(*gStart)(void *,uint8_t *,uint8_t *,float,uint8_t),*(*gNearest)(void *,CVector,float *);
int (*gEngine)(void *),(*gTotal)(const void *);void (*gSetEngine)(void *,unsigned);
uint64_t gSerial;
uint64_t Serial(){if(++gSerial==0)++gSerial;return gSerial;}
struct Identity {uint8_t *car=nullptr;const void *base=nullptr,*flags=nullptr,*vtable=nullptr;int ref=-1,model=-1;};
bool Same(const Identity &a,const Identity &b){return a.car==b.car&&a.base==b.base&&a.flags==b.flags&&a.vtable==b.vtable&&a.ref==b.ref&&a.model==b.model;}
bool Identify(uint8_t *v,Identity &id)
{
 if(!v||!gPool||!*gPool||!game::api.PoolsGetVehicle)return false;
 const auto *p=static_cast<const uint8_t *>(*gPool);auto *base=*reinterpret_cast<uint8_t *const *>(p),*flags=*reinterpret_cast<uint8_t *const *>(p+8);const int n=*reinterpret_cast<const int *>(p+16);
 const uintptr_t a=reinterpret_cast<uintptr_t>(v),b=reinterpret_cast<uintptr_t>(base);if(!base||!flags||n<=0||n>100000||a<b||(a-b)%kStride)return false;
 const size_t i=(a-b)/kStride;if(i>=size_t(n)||i>=kSlots||(flags[i]&0x80))return false;
 const int ref=int(i*256)|flags[i];if(game::api.PoolsGetVehicle(ref)!=v||(game::EntityFlags(v)&7)!=2)return false;
 id={v,base,flags,*reinterpret_cast<void **>(v),ref,game::EntityModel(v)};return true;
}
bool Eligible(const Identity &id)
{
 return gReady&&gConfig.carFire&&id.vtable&&id.model>=0&&id.model<6500&&id.model!=162&&id.model!=171&&!game::IsAircraft(id.car)&&
  *reinterpret_cast<int *>(id.car+0x348)==0&&!game::Wrecked(id.car)&&id.car[0x294]!=2&&(id.car[0x297]&0x40)&&
  !(game::EntityFlags(id.car)&((1u<<26)|(1u<<28)))&&std::isfinite(game::VehicleHealth(id.car))&&
  gMission&&gCutscene&&!gMission()&&!*gCutscene;
}
struct History {Identity id;firemodel::State state;uint8_t *fire=nullptr;uint64_t revision=0;uint32_t clock=0;bool seen=false;};
std::array<History,kSlots> gHistory;
History &Record(const Identity &id){auto &h=gHistory[size_t(id.ref>>8)];if(!Same(h.id,id)){h={};h.id=id;h.revision=Serial();}return h;}
struct Lease {Identity id;uint64_t revision=0;};
Lease Capture(uint8_t *v){Identity id;if(!Identify(v,id)||!Eligible(id))return {};auto &h=Record(id);return {id,h.revision};}
bool Current(const Lease &lease){Identity id;return lease.revision&&Identify(lease.id.car,id)&&Same(id,lease.id)&&Eligible(id)&&gHistory[size_t(id.ref>>8)].revision==lease.revision;}
bool FireAddress(const uint8_t *f){const uintptr_t a=reinterpret_cast<uintptr_t>(f),b=reinterpret_cast<uintptr_t>(gManager)+8;return gManager&&a>=b&&(a-b)%kFireStride==0&&(a-b)/kFireStride<kFireCount;}
bool FireFor(const uint8_t *f,const Identity &id){return FireAddress(f)&&f[0]==1&&f[1]==0&&*reinterpret_cast<uint8_t *const *>(f+0x10)==id.car&&*reinterpret_cast<uint8_t *const *>(id.car+0x280)==f;}
struct Scope;thread_local Scope *gScope;
void Sync();
struct Scope {
 Scope *previous=gScope;Lease lease;unsigned mask=0;bool control=false,valid=true;
 Scope(uint8_t *v,unsigned bits,bool controller=false):lease(Capture(v)),mask(bits),control(controller){gScope=this;Sync();}
 ~Scope(){gScope=previous;Sync();}
};
void Sync(){const unsigned mask=gScope&&gScope->valid&&Current(gScope->lease)?gScope->mask:0;for(int i=0;i<3;i++)if(gGates[i])*gGates[i]=(mask>>i)&1;}
void Invalidate(uint8_t *v){for(auto *s=gScope;s;s=s->previous)if(!v||s->lease.id.car==v)s->valid=false;Sync();}
bool ControllerOwns(uint8_t *v){for(auto *s=gScope;s;s=s->previous)if(s->control)return s->valid&&s->lease.id.car==v&&Current(s->lease);return false;}
void MarkDamage(const Lease &lease,float before,int weapon){if(weapon==31||!Current(lease))return;auto &h=gHistory[size_t(lease.id.ref>>8)];if(game::VehicleHealth(lease.id.car)<before)firemodel::Rearm(h.state);}
bool Ordinary(int type){return (type>=17&&type<=29)||type==31||type==32||type==33||type==35||type==42;}
void Damage(uint8_t *v,float amount,uint16_t piece){Scope scope(v,amount==0&&ControllerOwns(v)?1u<<Rollover:0);const float before=Current(scope.lease)?game::VehicleHealth(v):0;gDamage(v,amount,piece);MarkDamage(scope.lease,before,-1);}
void Inflict(uint8_t *v,uint8_t *by,int type,float amount,CVector point){Scope scope(v,Ordinary(type)?1u<<Lethal:0);const float before=Current(scope.lease)?game::VehicleHealth(v):0;gInflict(v,by,type,amount,point);MarkDamage(scope.lease,before,type);}
void Extinguish(uint8_t *f)
{
 Identity id;uint64_t revision=0;
 if(FireAddress(f)&&f[0]==1&&f[1]==0&&Identify(*reinterpret_cast<uint8_t **>(f+0x10),id)){
  const auto &h=gHistory[size_t(id.ref>>8)];if(Same(h.id,id)&&h.fire==f)revision=h.revision;
 }
 gExtinguish(f);Identity now;
 if(revision&&Identify(id.car,now)&&Same(id,now)){auto &h=gHistory[size_t(id.ref>>8)];if(h.revision==revision&&h.fire==f){h.fire=nullptr;firemodel::Extinguish(h.state);Log("car fire: model %d extinguished",id.model);}}
}
bool Create(const Lease &lease)
{
 if(!Current(lease))return false;auto *v=lease.id.car;
 const unsigned status=unsigned(gEngine(v+0x34c)),loan=std::min(status,224u);
 if(loan!=status)gSetEngine(v+0x34c,loan);
 struct Restore {Lease lease;unsigned status,loan;~Restore(){auto *v=lease.id.car;if(status!=loan&&Current(lease)&&unsigned(gEngine(v+0x34c))==loan)gSetEngine(v+0x34c,status);}} restore{lease,status,loan};
 auto *fire=gStart(gManager,v,nullptr,.3f,0);
 if(!Current(lease)||!FireFor(fire,lease.id))return false;
 auto &h=gHistory[size_t(lease.id.ref>>8)];h.fire=fire;Log("car fire: model %d ignited; native fire registered",lease.id.model);return true;
}
void Burnout(const Lease &lease)
{
 if(!Current(lease))return;auto &h=gHistory[size_t(lease.id.ref>>8)];auto *v=lease.id.car,*fire=h.fire;h.fire=nullptr;
 if(FireFor(fire,lease.id))gExtinguish(fire);
 if(!Current(lease))return;gSetEngine(v+0x34c,250);if(!Current(lease))return;
 *reinterpret_cast<float *>(v+0x2a0)=0;v[0x295]&=uint8_t(~0x10);v[0x60]=(v[0x60]&7)|0x28;Invalidate(v);
 Log("car fire: model %d burned out without blast",lease.id.model);
}
void Advance(const Lease &lease)
{
 if(!Current(lease)||!game::api.timeMs)return;auto &h=gHistory[size_t(lease.id.ref>>8)];const uint32_t now=*game::api.timeMs;const float dt=h.seen&&now>=h.clock?(now-h.clock)*.001f:0;h.clock=now;h.seen=true;
 auto *bound=*reinterpret_cast<uint8_t **>(lease.id.car+0x280);
 if(FireFor(bound,lease.id)&&bound!=h.fire){h.fire=bound;firemodel::Ignite(h.state);Log("car fire: model %d adopted native external fire",lease.id.model);}
 if(h.fire&&!FireFor(h.fire,lease.id)){h.fire=nullptr;firemodel::Extinguish(h.state);}
 auto input=carmechanics::FireInput(lease.id.car);input.health=game::VehicleHealth(lease.id.car);
 const bool burning=h.state.lit&&FireFor(h.fire,lease.id);firemodel::Tick(h.state,input,dt);
 if(!Current(lease))return;
 if(burning&&dt>0&&dt<=1)carmechanics::Burn(lease.id.car,dt);
 if(!Current(lease))return;
 if(h.state.spent){Burnout(lease);return;}
 if(h.state.lit&&!h.fire&&!Create(lease)){
  if(Current(lease)){h.state.lit=false;h.state.age=0;h.state.exposure=2.5;}return;
 }
 if(Current(lease)&&FireFor(h.fire,lease.id)){
  *reinterpret_cast<uint32_t *>(h.fire+0x20)=now+2000;*reinterpret_cast<float *>(h.fire+0x2c)=firemodel::Intensity(h.state);
 }
}
void Control(uint8_t *v){Scope scope(v,1u<<Countdown,true);gControl(v);if(scope.valid)Advance(scope.lease);}
void Process(uint8_t *f)
{
 uint8_t *v=FireAddress(f)&&f[0]==1&&f[1]==0?*reinterpret_cast<uint8_t **>(f+0x10):nullptr;
 Scope scope(v,0);const bool managed=Current(scope.lease)&&FireFor(f,scope.lease.id);if(managed)Advance(scope.lease);
 // The original may extinguish/recycle its record or trigger nested calls.
 if(!managed||(FireAddress(f)&&f[0]==1))gProcess(f);
}
void Forget(uint8_t *v,bool repair){Invalidate(v);Identity id;if(!Identify(v,id))return;auto &h=gHistory[size_t(id.ref>>8)];auto *fire=Same(h.id,id)?h.fire:nullptr;
 if(repair){h={};h.id=id;h.revision=Serial();if(FireFor(fire,id))gExtinguish(fire);}else if(Same(h.id,id))h.revision=Serial();}
void Fix(uint8_t *v){Forget(v,true);gFix(v);}void Delete(uint8_t *v){Forget(v,false);gDelete(v);}
void Restart(){Invalidate(nullptr);gHistory={};gRestart();}
struct DispatchScope;thread_local DispatchScope *gDispatchScope;
struct DispatchScope {DispatchScope *previous=gDispatchScope;bool allowed=false;DispatchScope(){
 if(gReady&&gConfig.carFire&&gCoords){const auto p=gCoords();for(const auto &h:gHistory){Lease lease{h.id,h.revision};if(!h.state.lit||!Current(lease)||!FireFor(h.fire,h.id))continue;
  const auto at=game::EntityPos(h.id.car);const float dx=p.x-at.x,dy=p.y-at.y,dz=p.z-at.z;if(std::isfinite(dx)&&std::isfinite(dy)&&std::isfinite(dz)&&dx*dx+dy*dy+dz*dz<120*120){allowed=true;break;}}
 }gDispatchScope=this;}~DispatchScope(){gDispatchScope=previous;}};
bool NativeCall(const void *address,uint32_t offset){return hooks::ImageBase()&&reinterpret_cast<uintptr_t>(address)-reinterpret_cast<uintptr_t>(hooks::ImageBase())==offset;}
int Total(const void *manager){const int count=gTotal(manager);return manager==gManager&&gDispatchScope&&gDispatchScope->allowed&&NativeCall(__builtin_return_address(0),0x53030)?std::max(3,count):count;}
uint8_t *Nearest(void *manager,CVector at,float *radius){
 if(manager==gManager&&radius&&*radius==30&&gDispatchScope&&gDispatchScope->allowed&&NativeCall(__builtin_return_address(0),0x53140)){float expanded=120;auto *f=gNearest(manager,at,&expanded);*radius=expanded;return f;}
 return gNearest(manager,at,radius);
}
void Dispatch(){DispatchScope scope;const int before=gDuty?*gDuty:0;gDispatch();if(scope.allowed&&gDuty&&*gDuty>before)Log("car fire: native firetruck dispatch accepted (%d on duty)",*gDuty);}
bool ReadBurning(const uint8_t *v){return Burning(v,!game::Wrecked(v)&&game::VehicleHealth(v)<game::kVehicleOnFire);}
} // namespace
bool Burning(const uint8_t *v,bool fallback){Identity id;if(!gReady||!Identify(const_cast<uint8_t *>(v),id))return fallback;if(game::Wrecked(v))return false;if(!Eligible(id))return fallback;const auto &h=gHistory[size_t(id.ref>>8)];return Same(h.id,id)?h.state.lit&&FireFor(h.fire,id):fallback;}
bool Install()
{
 if(gReady||!gConfig.carFire)return true;
#define B(v,s) v=reinterpret_cast<decltype(v)>(dlsym(RTLD_MAIN_ONLY,s));if(!v)return false
 B(gPool,"_ZN6CPools15ms_pVehiclePoolE");B(gManager,"gFireManager");B(gMission,"_ZN11CTheScripts18IsPlayerOnAMissionEv");B(gCutscene,"_ZN12CCutsceneMgr10ms_runningE");
 B(gCoords,"_Z15FindPlayerCoorsv");B(gDuty,"_ZN8CCarCtrl19NumFiretrucksOnDutyE");B(gStart,"_ZN12CFireManager9StartFireEP7CEntityS1_fh");B(gEngine,"_ZN14CDamageManager15GetEngineStatusEv");B(gSetEngine,"_ZN14CDamageManager15SetEngineStatusEj");
#undef B
 gGates[Countdown]=hooks::Flag("skip:car_fire_countdown");gGates[Rollover]=hooks::Flag("skip:car_rollover_drain");gGates[Lethal]=hooks::Flag("skip:car_ordinary_lethal");
 if(!gGates[0]||!gGates[1]||!gGates[2]||!game::api.PoolsGetVehicle||!game::api.timeMs||!hooks::IsCallTo(0x53030,"_ZNK12CFireManager19GetTotalActiveFiresEv")||!hooks::IsCallTo(0x53140,"_ZN12CFireManager15FindNearestFireE7CVectorPf"))return false;
#define C(v,s,f) if(!v)v=reinterpret_cast<decltype(v)>(hooks::Chain(s,reinterpret_cast<void *>(f)))
 C(gControl,"_ZN11CAutomobile14ProcessControlEv",Control);C(gDamage,"_ZN11CAutomobile13VehicleDamageEft",Damage);C(gInflict,"_ZN8CVehicle13InflictDamageEP7CEntity11eWeaponTypef7CVector",Inflict);
 C(gProcess,"_ZN5CFire11ProcessFireEv",Process);C(gExtinguish,"_ZN5CFire10ExtinguishEv",Extinguish);C(gFix,"_ZN11CAutomobile3FixEv",Fix);C(gDelete,"_ZN7CEntity14DeleteRwObjectEv",Delete);C(gRestart,"_ZN6CWorld15ClearForRestartEv",Restart);
 C(gDispatch,"_ZN8CCarCtrl28GenerateEmergencyServicesCarEv",Dispatch);C(gTotal,"_ZNK12CFireManager19GetTotalActiveFiresEv",Total);C(gNearest,"_ZN12CFireManager15FindNearestFireE7CVectorPf",Nearest);
#undef C
 gReady=gControl&&gDamage&&gInflict&&gProcess&&gExtinguish&&gFix&&gDelete&&gRestart&&gDispatch&&gTotal&&gNearest;
 if(gReady){game::api.VehicleBurning=ReadBurning;Log("car fire: progressive native fires and single-fire emergency response enabled");}return gReady;
}
}

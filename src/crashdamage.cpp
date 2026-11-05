#include "crashdamage.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "falldamage.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <dlfcn.h>
namespace crashdamage {namespace {
void **vehicles,**peds;bool ready;
void (*world)();bool (*hurt)(uint8_t *,uint8_t *,int,float,int,uint8_t);
int Ref(const uint8_t *v,void **binding,size_t stride){
 if(!v||!binding||!*binding)return -1;auto pool=static_cast<const uint8_t *>(*binding);auto entries=*reinterpret_cast<uint8_t *const *>(pool),flags=*reinterpret_cast<uint8_t *const *>(pool+8);int count=*reinterpret_cast<const int *>(pool+16);
 auto a=reinterpret_cast<uintptr_t>(v),b=reinterpret_cast<uintptr_t>(entries);if(!entries||!flags||count<1||count>100000||a<b||(a-b)%stride)return -1;auto i=(a-b)/stride;
 return i<size_t(count)&&!(flags[i]&128)?int(i*256)|flags[i]:-1;
}
struct Actor {uint8_t *v=nullptr;int ref=-1,model=-1,kind=-1;void *table=nullptr,*clump=nullptr;};
Actor Read(uint8_t *v){int r=Ref(v,vehicles,0x6f0);if(r<0||!game::api.PoolsGetVehicle||game::api.PoolsGetVehicle(r)!=v||(game::EntityFlags(v)&7)!=2)return {};return {v,r,game::EntityModel(v),*reinterpret_cast<int *>(v+0x348),*reinterpret_cast<void **>(v),game::EntityClump(v)};}
bool Same(const Actor &a){const auto b=Read(a.v);return a.v&&a.ref==b.ref&&a.model==b.model&&a.kind==b.kind&&a.table==b.table&&a.clump==b.clump;}
bool Enabled(uint8_t *v){return game::IsAircraft(v)?((*reinterpret_cast<int *>(v+0x348)==0||*reinterpret_cast<int *>(v+0x348)==3)?gConfig.heliDamage:gConfig.planes):gConfig.carMechanics;}
struct Seat {uint8_t *ped=nullptr;int ref=-1;void *clump=nullptr,*table=nullptr;};
struct Record {Actor actor;uint32_t last=0,queued=0;float peak=0,pending=0;bool seen=false;std::array<Seat,9> seats{};};
std::array<Record,256> records;uint32_t clock;bool seen;
bool Seated(const Seat &s,uint8_t *v,int seat){return s.ped&&Ref(s.ped,peds,0x8d0)==s.ref&&game::api.PoolsGetPed(s.ref)==s.ped&&(game::EntityFlags(s.ped)&7)==3&&game::PedInVehicle(s.ped)&&game::PedVehicle(s.ped)==v&&*reinterpret_cast<uint8_t **>(v+0x218+seat*8)==s.ped&&game::EntityClump(s.ped)==s.clump&&*reinterpret_cast<void **>(s.ped)==s.table;}
void World(){world();if(!ready||!game::api.timeMs)return;const auto now=*game::api.timeMs;
 for(auto &r:records){if(r.pending<=0)continue;const auto copy=r;r.pending=0;
  if(now<copy.queued||now-copy.queued>250||!Same(copy.actor)||!Enabled(copy.actor.v)||(game::EntityFlags(copy.actor.v)&game::kEntityCollisionProof))continue;
  for(int i=0;i<9;i++){if(!Same(copy.actor))break;const auto &s=copy.seats[i];if(Seated(s,copy.actor.v,i)&&std::isfinite(game::Health(s.ped))&&game::Health(s.ped)>0)hurt(s.ped,copy.actor.v,44,copy.pending,0,0);}
 }
}
}
void Contact(uint8_t *v,float speed){
 if(!ready||!game::api.timeMs||!game::api.PoolsGetPed||!std::isfinite(speed)||speed<=0)return;
 const auto actor=Read(v);if(!actor.v||actor.ref>>8>=int(records.size())||!Enabled(v)||(game::EntityFlags(v)&game::kEntityCollisionProof)||!(v[0x297]&64))return;
 const auto now=*game::api.timeMs;if(seen&&now<clock)records={};clock=now;seen=true;auto &r=records[size_t(actor.ref>>8)];
 if(!Same(r.actor)||r.actor.ref!=actor.ref||r.actor.model!=actor.model||now<r.last||now-r.last>350)r={actor};
 // Seats/crumple travel absorb some delta-v. Use the existing impact injury
 // curve for the occupant's remaining shock, independent of vehicle hull HP.
 const float damage=falldamage::Damage(std::min(speed,100.f)*.85f,0,false);r.last=now;r.seen=true;if(damage<=r.peak)return;
 const float extra=damage-r.peak;r.peak=damage;
 if(r.pending==0){r.queued=now;for(int i=0;i<9;i++){auto ped=*reinterpret_cast<uint8_t **>(v+0x218+i*8);const int ref=Ref(ped,peds,0x8d0);r.seats[i]={};if(ref>=0&&game::api.PoolsGetPed(ref)==ped)r.seats[i]={ped,ref,game::EntityClump(ped),*reinterpret_cast<void **>(ped)};}}
 r.pending+=extra;
}
bool Install(){if(ready)return true;
 vehicles=reinterpret_cast<void **>(dlsym(RTLD_MAIN_ONLY,"_ZN6CPools15ms_pVehiclePoolE"));peds=reinterpret_cast<void **>(dlsym(RTLD_MAIN_ONLY,"_ZN6CPools11ms_pPedPoolE"));hurt=reinterpret_cast<decltype(hurt)>(dlsym(RTLD_MAIN_ONLY,"_ZN4CPed13InflictDamageEP7CEntity11eWeaponTypef14ePedPieceTypesh"));
 if(!vehicles||!peds||!hurt||!game::api.PoolsGetVehicle||!game::api.PoolsGetPed)return false;
 world=reinterpret_cast<decltype(world)>(hooks::Chain("_ZN6CWorld7ProcessEv",reinterpret_cast<void *>(World)));return ready=world!=nullptr;
}
}

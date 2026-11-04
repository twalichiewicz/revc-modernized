#include "carimpact.h"
#include "carmechanics.h"
#include "carglass.h"
#include "partsmodel.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
namespace carimpact {namespace {
struct Point {CVector p;uint32_t padA;CVector n;uint32_t padB;uint8_t surfaceA,pieceA,surfaceB,pieceB;float depth;};
static_assert(sizeof(Point)==0x28);
constexpr size_t kStride=0x6f0,kSlots=256;
void **gPool;bool gReady;
void (*gControl)(uint8_t *),(*gFix)(uint8_t *),(*gDelete)(uint8_t *),(*gDamage)(uint8_t *,float,uint16_t);
bool (*gPair)(uint8_t *,uint8_t *,Point &,float &,float &),(*gSingle)(uint8_t *,Point &,float &);
bool (*gAlt)(uint8_t *,uint8_t *,Point &,float &,CVector &,CVector &);
struct Identity {uint8_t *v=nullptr;const void *base=nullptr,*flags=nullptr;int ref=-1,model=-1;void *clump=nullptr;};
bool Same(const Identity &a,const Identity &b){return a.v==b.v&&a.base==b.base&&a.flags==b.flags&&a.ref==b.ref&&a.model==b.model&&a.clump==b.clump;}
bool Identify(uint8_t *v,Identity &id)
{
 if(!v||!gPool||!*gPool||!game::api.PoolsGetVehicle)return false;
 const auto pool=static_cast<const uint8_t *>(*gPool);const auto base=*reinterpret_cast<uint8_t *const *>(pool),flags=*reinterpret_cast<uint8_t *const *>(pool+8);
 const int n=*reinterpret_cast<const int *>(pool+16);const uintptr_t a=reinterpret_cast<uintptr_t>(v),b=reinterpret_cast<uintptr_t>(base);
 if(!base||!flags||n<=0||n>100000||a<b||(a-b)%kStride)return false;
 const size_t index=(a-b)/kStride;if(index>=size_t(n)||index>=kSlots||(flags[index]&0x80))return false;
 const int ref=int(index*256)|flags[index];if(game::api.PoolsGetVehicle(ref)!=v||(game::EntityFlags(v)&7)!=2)return false;
 id={v,base,flags,ref,game::EntityModel(v),game::EntityClump(v)};return true;
}
bool Current(const Identity &id){Identity now;return Identify(id.v,now)&&Same(id,now);}
bool Eligible(const Identity &id){return id.clump&&id.model>=0&&id.model!=162&&id.model!=171&&
 *reinterpret_cast<const int *>(id.v+0x348)==0&&!game::IsAircraft(id.v)&&!game::Wrecked(id.v)&&
 (id.v[0x297]&0x40)&&!(game::EntityFlags(id.v)&(1u<<27));}
struct Contact {bool valid=false,knownOther=true;uint8_t *other=nullptr;uint8_t piece=0;Vec3 point{},inward{};CVector normal{};float force=0;uint32_t when=0;};
struct History {Identity id;Contact hit;std::array<partsmodel::Episode,5> regions{};};
std::array<History,kSlots> gHistory;
History &Record(const Identity &id){auto &h=gHistory[size_t(id.ref>>8)];if(!Same(h.id,id)){h={};h.id=id;}return h;}
struct Capture {Identity id;Contact hit;};
bool Enabled(){return gConfig.carDeformation||gConfig.carMechanics;}
Capture Preview(uint8_t *v,uint8_t *other,const Point &p,bool secondary=false,bool knownOther=true)
{
 Capture c;if(!gReady||!Enabled()||!game::api.timeMs||!Identify(v,c.id)||!Eligible(c.id))return c;
 const auto piece=secondary?p.pieceB:p.pieceA;if(piece>17)return c;
 const auto matrix=reinterpret_cast<const float *>(v+8);const float sign=secondary?-1.0f:1.0f;
 Vec3 point,inward;if(!dentmodel::Local(matrix,{p.p.x,p.p.y,p.p.z},false,point)||
  !dentmodel::Local(matrix,{sign*p.n.x,sign*p.n.y,sign*p.n.z},true,inward))return c;
 const float len=std::sqrt(inward.x*inward.x+inward.y*inward.y+inward.z*inward.z);
 if(!std::isfinite(len)||len<.5f||len>2||inward.z/len>.7f)return c; // wheel/road support remains rigid
 inward={inward.x/len,inward.y/len,inward.z/len};
 c.hit={true,knownOther,other,piece,point,inward,p.n,0,*game::api.timeMs};return c;
}
void Save(Capture c,float force)
{
 if(!c.hit.valid||!std::isfinite(force)||force<=0||!Current(c.id)||!Eligible(c.id))return;
 auto &h=Record(c.id);const auto now=c.hit.when;
 // Native sector collision retains its first strongest record when forces tie.
 if(!h.hit.valid||now<h.hit.when||now-h.hit.when>200||force>h.hit.force){c.hit.force=force;h.hit=c.hit;}
}
bool Pair(uint8_t *v,uint8_t *other,Point &p,float &a,float &b)
{
 const auto first=Preview(v,other,p),second=Preview(other,v,p,true);
 const bool result=gPair(v,other,p,a,b);if(result){Save(first,a);Save(second,b);}return result;
}
bool Single(uint8_t *v,Point &p,float &a)
{
 const auto c=Preview(v,nullptr,p,false,false);const bool result=gSingle(v,p,a);if(result)Save(c,a);return result;
}
bool Alt(uint8_t *v,uint8_t *other,Point &p,float &a,CVector &b,CVector &c)
{
 const auto hit=Preview(v,other,p);const bool result=gAlt(v,other,p,a,b,c);if(result)Save(hit,a);return result;
}
struct Controller;thread_local Controller *gController;
struct Controller {Controller *previous=gController;Identity id;bool owned=false;
 explicit Controller(uint8_t *v){owned=gReady&&Enabled()&&Identify(v,id)&&Eligible(id);gController=this;}
 ~Controller(){gController=previous;}
};
struct DamageScope;thread_local DamageScope *gDamaging;
struct DamageScope {DamageScope *previous=gDamaging;Identity id;bool valid;
 DamageScope(const Identity &i,bool owned):id(i),valid(owned){gDamaging=this;}
 ~DamageScope(){gDamaging=previous;}
};
void Invalidate(uint8_t *v){for(auto c=gDamaging;c;c=c->previous)if(c->id.v==v)c->valid=false;}
void Control(uint8_t *v){const Controller scope(v);gControl(v);}
bool Matches(uint8_t *v,const Contact &hit,uint32_t now)
{
 if(!hit.valid||now<hit.when||now-hit.when>200)return false;
 const float force=*reinterpret_cast<const float *>(v+0x13c);const auto normal=reinterpret_cast<const CVector *>(v+0x148);
 return std::isfinite(force)&&std::fabs(force-hit.force)<=std::max(.01f,hit.force*.0001f)&&
  (!hit.knownOther||*reinterpret_cast<uint8_t *const *>(v+0x140)==hit.other)&&
  *reinterpret_cast<const uint16_t *>(v+0x154)==hit.piece&&
  std::fabs(normal->x-hit.normal.x)<1e-4f&&std::fabs(normal->y-hit.normal.y)<1e-4f&&std::fabs(normal->z-hit.normal.z)<1e-4f;
}
void Damage(uint8_t *v,float amount,uint16_t component)
{
 Identity id;const bool owned=gReady&&Enabled()&&amount==0&&game::api.timeMs&&gController&&gController->owned&&
  Identify(v,id)&&Same(id,gController->id)&&Eligible(id);
 if(!owned)Invalidate(v);
 DamageScope scope(id,owned);Contact hit;History *history=nullptr;bool contact=false,matched=false;float severity=0;dentmodel::Impact impact;
 const float health=owned?*reinterpret_cast<const float *>(v+0x2a0):0;std::array<uint8_t,24> before{};
 if(owned){
  history=&Record(id);hit=history->hit;history->hit.valid=false;memcpy(before.data(),v+0x34c,before.size());
  const uint32_t now=*game::api.timeMs;
  if(Matches(v,hit,now)){
   const float mass=*reinterpret_cast<const float *>(v+0xcc);
   matched=std::isfinite(mass)&&mass>=100&&mass<=1000000;
   if(matched){severity=hit.force/mass*50;matched=std::isfinite(severity);}
   // Collision pieces13..16 are tyres; ApplyDamage components1..4 are a
   // separate numbering scheme. Body collision pieces1..4 remain deformable.
   if(gConfig.carDeformation&&!(hit.piece>=13&&hit.piece<=16))
    contact=partsmodel::Contact(history->regions[dentmodel::Region(hit.point,hit.inward)],now)&&matched&&dentmodel::Make(hit.point,hit.inward,severity,impact);
  }
 }
 gDamage(v,amount,component);
 if(!matched||!scope.valid||!Current(id)||!Eligible(id)||!Enabled())return;
 const float after=*reinterpret_cast<const float *>(v+0x2a0);
 if(!(std::isfinite(health)&&std::isfinite(after)&&after<health)&&!std::memcmp(before.data(),v+0x34c,before.size()))return;
 if(gConfig.carMechanics)carmechanics::Collision(v,{hit.point,hit.inward,severity,hit.piece});
 if(contact&&gConfig.carDeformation&&Current(id)&&carglass::Dent(v,impact))partsmodel::Commit(history->regions[dentmodel::Region(hit.point,hit.inward)]);
}
void Forget(uint8_t *v){Invalidate(v);Identity id;if(Identify(v,id))gHistory[size_t(id.ref>>8)]={};}
void Fix(uint8_t *v){Forget(v);gFix(v);}
void Delete(uint8_t *v){Forget(v);gDelete(v);}
} // namespace
bool Install()
{
 if(gReady||!Enabled())return true;
 gPool=reinterpret_cast<void **>(dlsym(RTLD_MAIN_ONLY,"_ZN6CPools15ms_pVehiclePoolE"));
 if(!gPool||!game::api.PoolsGetVehicle||!game::api.timeMs||(gConfig.carDeformation&&!carglass::Install()))return false;
#define CHAIN(v,s,f) if(!v)v=reinterpret_cast<decltype(v)>(hooks::Chain(s,reinterpret_cast<void *>(f)))
 CHAIN(gPair,"_ZN9CPhysical14ApplyCollisionEPS_R9CColPointRfS3_",Pair);
 CHAIN(gSingle,"_ZN9CPhysical14ApplyCollisionER9CColPointRf",Single);
 CHAIN(gAlt,"_ZN9CPhysical17ApplyCollisionAltEP7CEntityR9CColPointRfR7CVectorS6_",Alt);
 CHAIN(gControl,"_ZN11CAutomobile14ProcessControlEv",Control);CHAIN(gDamage,"_ZN11CAutomobile13VehicleDamageEft",Damage);
 CHAIN(gFix,"_ZN11CAutomobile3FixEv",Fix);CHAIN(gDelete,"_ZN7CEntity14DeleteRwObjectEv",Delete);
#undef CHAIN
 gReady=gPair&&gSingle&&gAlt&&gControl&&gDamage&&gFix&&gDelete;
 if(gReady){Log("car impacts: certified native collision damage enabled");if(gConfig.carDeformation)Log("car deformation: localized native collision dents enabled; 16 cars, 25 cm cap");}
 return gReady;
}
}

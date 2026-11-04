#include "carparts.h"
#include "partsmodel.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <dlfcn.h>

namespace carparts {namespace {
constexpr size_t kStride=0x6f0,kDamage=0x34c,kSlots=256;
void **gPool;
int (*gDoorStatus)(void *,int),(*gPanelStatus)(void *,int);
void (*gControl)(uint8_t *),(*gVehicleDamage)(uint8_t *,float,uint16_t),(*gFix)(uint8_t *);
bool (*gApply)(uint8_t *,int,float,float);
bool gReady;
struct Identity {
 uint8_t *vehicle=nullptr;const uint8_t *entries=nullptr,*flags=nullptr;
 int ref=-1,model=-1;void *clump=nullptr;
};
bool Same(const Identity &a,const Identity &b)
{
 return a.vehicle==b.vehicle&&a.entries==b.entries&&a.flags==b.flags&&a.ref==b.ref&&a.model==b.model&&a.clump==b.clump;
}
bool Identify(uint8_t *v,Identity &id)
{
 if(!v||!gPool||!*gPool||!game::api.PoolsGetVehicle)return false;
 auto p=static_cast<const uint8_t *>(*gPool);auto base=*reinterpret_cast<uint8_t *const *>(p);
 auto flags=*reinterpret_cast<uint8_t *const *>(p+8);int capacity=*reinterpret_cast<const int *>(p+16);
 const uintptr_t a=reinterpret_cast<uintptr_t>(v),b=reinterpret_cast<uintptr_t>(base);
 if(!base||!flags||capacity<=0||capacity>100000||a<b||(a-b)%kStride)return false;
 const size_t index=(a-b)/kStride;if(index>=size_t(capacity)||index>=kSlots||(flags[index]&0x80))return false;
 const int ref=int(index*256)|flags[index];if(game::api.PoolsGetVehicle(ref)!=v||(game::EntityFlags(v)&7)!=2)return false;
 id={v,base,flags,ref,game::EntityModel(v),game::EntityClump(v)};return true;
}
bool Current(const Identity &id){Identity now;return Identify(id.vehicle,now)&&Same(id,now);}
bool Eligible(const Identity &id)
{
 uint8_t *v=id.vehicle;
 return id.clump&&id.model>=0&&id.model!=171&&id.model!=162&&
        *reinterpret_cast<const int *>(v+0x348)==0&&!game::IsAircraft(v)&&!game::Wrecked(v)&&
        (v[0x297]&0x40)&&!(game::EntityFlags(v)&(1u<<27));
}
struct History {Identity id;std::array<partsmodel::Episode,partsmodel::kParts> parts{};};
std::array<History,kSlots> gHistory;
History &Record(const Identity &id)
{
 auto &h=gHistory[size_t(id.ref>>8)];if(!Same(h.id,id)){h={};h.id=id;}return h;
}
int State(uint8_t *v,int slot)
{
 const auto &part=partsmodel::Describe(slot);return part.panel?gPanelStatus(v+kDamage,part.index):gDoorStatus(v+kDamage,part.index);
}
struct ControlContext;
thread_local ControlContext *gController;
struct ControlContext {
 ControlContext *previous=gController;Identity id;bool owned=false;
 explicit ControlContext(uint8_t *v){owned=gReady&&gConfig.carParts&&game::api.timeMs&&Identify(v,id)&&Eligible(id);gController=this;}
 ~ControlContext(){gController=previous;}
};
struct DamageContext;
thread_local DamageContext *gDamage;
struct DamageContext {
 DamageContext *previous=gDamage;Identity id;History *history=nullptr;uint32_t now=0;uint8_t progressed=0;
 std::array<int,partsmodel::kParts> before{};
 explicit DamageContext(uint8_t *v,bool owned,const Identity &identity):id(identity){
  if(owned){history=&Record(id);now=*game::api.timeMs;for(int s=0;s<partsmodel::kParts;s++)before[s]=State(v,s);}
  gDamage=this;
 }
 ~DamageContext(){gDamage=previous;}
};
const char *StateName(int s,bool panel)
{
 static const char *names[]={"intact","damaged","swinging","detached"};
 return s==2&&panel?"damaged2":s>=0&&s<=3?names[s]:"unknown";
}
void Control(uint8_t *v){const ControlContext context(v);gControl(v);}
void VehicleDamage(uint8_t *v,float amount,uint16_t component)
{
 Identity id;
 const bool owned=gReady&&gConfig.carParts&&amount==0&&game::api.timeMs&&gController&&gController->owned&&
                  Identify(v,id)&&Same(id,gController->id)&&Eligible(id);
 // Reentrant collision damage for the same car shares its one-impact mask.
 // Other actors and scripted calls get a new (possibly unowned) context.
 if(owned&&gDamage&&gDamage->history&&Same(gDamage->id,id)){gVehicleDamage(v,amount,component);return;}
 // An unowned nested call can change the same parts after a physical hit.
 // Preserve any surviving physical progression, then rebase changed parts so
 // the outer collision cannot claim the script's state or consume its episode.
 DamageContext *parent=nullptr;std::array<int,partsmodel::kParts> scriptBefore{};
 if(!owned)for(auto c=gDamage;c;c=c->previous)
  if(c->history&&c->id.vehicle==v&&Current(c->id)){parent=c;break;}
 const Identity scriptId=parent?parent->id:Identity{};
 if(parent){
  for(int s=0;s<partsmodel::kParts;s++)scriptBefore[s]=State(v,s);
  for(auto c=parent;c;c=c->previous)if(c->history&&Same(c->id,scriptId))
   for(int s=0;s<partsmodel::kParts;s++)
    if((c->progressed&(1u<<s))&&scriptBefore[s]>c->before[s]&&scriptBefore[s]<=3)
     partsmodel::Commit(c->history->parts[s]);
 }
 DamageContext context(v,owned,id);gVehicleDamage(v,amount,component);
 if(parent&&Current(scriptId))
  for(auto c=parent;c;c=c->previous)if(c->history&&Same(c->id,scriptId))
   for(int s=0;s<partsmodel::kParts;s++){
    const int after=State(v,s);if(after==scriptBefore[s])continue;
    c->before[s]=after;c->progressed&=uint8_t(~(1u<<s));
   }
 if(!context.history||!Current(id))return;
 for(int s=0;s<partsmodel::kParts;s++){
  if(!(context.progressed&(1u<<s)))continue;
  const int after=State(v,s),before=context.before[s];
  if(before<0||before>3||after<=before||after>3)continue;
  partsmodel::Commit(context.history->parts[s]);
  const auto &part=partsmodel::Describe(s);
  Log("car parts: model %d %s %s -> %s",id.model,part.name,StateName(before,part.panel),StateName(after,part.panel));
 }
}
bool ApplyDamage(uint8_t *manager,int component,float force,float multiplier)
{
 auto c=gDamage;const int slot=partsmodel::Slot(component);
 if(!gReady||!gConfig.carParts||!c||!c->history||!gController||!gController->owned||
    !Same(c->id,gController->id)||manager!=c->id.vehicle+kDamage||slot<0||
    !Current(c->id)||!Eligible(c->id)||!std::isfinite(force)||force<=0)
  return gApply(manager,component,force,multiplier);
 const int before=State(c->id.vehicle,slot);if(before<0||before>3)return gApply(manager,component,force,multiplier);
 if(before==3)return false;
 const bool contact=partsmodel::Contact(c->history->parts[slot],c->now);
 const uint8_t bit=uint8_t(1u<<slot);if(!contact||(c->progressed&bit))return false;
 const float scaled=force*partsmodel::Scale(before);
 const bool result=gApply(manager,component,scaled,multiplier);
 if(result&&Current(c->id)&&State(c->id.vehicle,slot)>before)c->progressed|=bit;
 return result;
}
void Fix(uint8_t *v)
{
 Identity id;if(Identify(v,id)){
  // Repair also retires collision attribution already on the call stack.
  for(auto c=gDamage;c;c=c->previous)if(c->history&&Same(c->id,id))c->history=nullptr;
  gHistory[size_t(id.ref>>8)]={};
 }
 gFix(v);
}
} // namespace
bool Install()
{
 if(gReady||!gConfig.carParts)return true;
#define BIND(v,s) v=reinterpret_cast<decltype(v)>(dlsym(RTLD_MAIN_ONLY,s));if(!v)return false
 BIND(gPool,"_ZN6CPools15ms_pVehiclePoolE");
 BIND(gDoorStatus,"_ZN14CDamageManager13GetDoorStatusEi");BIND(gPanelStatus,"_ZN14CDamageManager14GetPanelStatusEi");
#undef BIND
 if(!game::api.PoolsGetVehicle||!game::api.timeMs)return false;
#define CHAIN(v,s,f) if(!v)v=reinterpret_cast<decltype(v)>(hooks::Chain(s,reinterpret_cast<void *>(f)))
 CHAIN(gControl,"_ZN11CAutomobile14ProcessControlEv",Control);
 CHAIN(gVehicleDamage,"_ZN11CAutomobile13VehicleDamageEft",VehicleDamage);
 CHAIN(gApply,"_ZN14CDamageManager11ApplyDamageE10tComponentff",ApplyDamage);
 CHAIN(gFix,"_ZN11CAutomobile3FixEv",Fix);
#undef CHAIN
 gReady=gControl&&gVehicleDamage&&gApply&&gFix;
 if(gReady)Log("car parts: native damage, swinging and detachment; 350 ms contact gap, stronger later hits");
 else Log("car parts: native hook missing; re-run install.sh");
 return gReady;
}
} // namespace carparts

#include "carparts.h"
#include "partsmodel.h"
#include "panelphysics.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "aircraftbodyclient.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <dlfcn.h>
#include <cstring>
#include <algorithm>

namespace carparts {namespace {
constexpr size_t kStride=0x6f0,kDamage=0x34c,kSlots=256;
void **gPool;
int (*gDoorStatus)(void *,int),(*gPanelStatus)(void *,int);
void (*gControl)(uint8_t *),(*gVehicleDamage)(uint8_t *,float,uint16_t),(*gFix)(uint8_t *);
bool (*gApply)(uint8_t *,int,float,float);
void (*gPreRender)(uint8_t *);
void *(*gAtomics)(void *,void *(*)(void *,void *),void *),*(*gFrame)(const void *),*(*gParent)(const void *),*(*gSetFlags)(void *,unsigned);
unsigned (*gFlags)(const void *);const char *(*gName)(void *);
bool gReady;uint64_t gSerial;
void (*gSetDoor)(uint8_t *,int,int,bool),(*gSetBumper)(uint8_t *,int,int,bool);
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
bool BaseEligible(const Identity &id)
{
 uint8_t *v=id.vehicle;
 return !aircraftbodyclient::Owned(v)&&id.clump&&id.model>=0&&id.model!=171&&id.model!=162&&
        *reinterpret_cast<const int *>(v+0x348)==0&&!game::IsAircraft(v)&&!game::Wrecked(v)&&
        (v[0x297]&0x40);
}
bool Eligible(const Identity &id){return BaseEligible(id)&&!(game::EntityFlags(id.vehicle)&game::kEntityCollisionProof);}
bool AutoHeli(const Identity &id)
{
 if(*reinterpret_cast<const int *>(id.vehicle+0x348)!=0)return false;
 switch(id.model){case 155:case 177:case 199:case 217:case 218:case 227:return true;default:return false;}
}
bool BallisticEligible(const Identity &id)
{
 return !aircraftbodyclient::Owned(id.vehicle)&&(BaseEligible(id)||(gConfig.heliDamage&&AutoHeli(id)&&id.clump&&!game::Wrecked(id.vehicle)&&(id.vehicle[0x297]&0x40)));
}
struct History {Identity id;std::array<partsmodel::Episode,partsmodel::kParts> parts{};std::array<float,partsmodel::kParts> strain{};std::array<int,partsmodel::kParts> stages{};uint64_t revision=0;};
std::array<History,kSlots> gHistory;
int State(uint8_t *,int);
History &Record(const Identity &id)
{
 auto &h=gHistory[size_t(id.ref>>8)];if(!Same(h.id,id)){h={};h.id=id;if(++gSerial==0)++gSerial;h.revision=gSerial;}
 for(int i=0;i<partsmodel::kParts;i++){const int state=State(id.vehicle,i);if(h.stages[i]!=state){h.stages[i]=state;h.strain[i]=0;}}return h;
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
 // Bullet cuts reduce the remaining fastening section. The next physical
 // load acts on that same connection instead of a separate collision counter.
 const float remaining=std::max(.1f,1-c->history->strain[slot]/180);
 const float scaled=force*partsmodel::Scale(before)/remaining;
 const bool result=gApply(manager,component,scaled,multiplier);
 if(result&&Current(c->id)&&State(c->id.vehicle,slot)>before){c->progressed|=bit;c->history->strain[slot]=0;}
 return result;
}
int RotorFrame(void *frame)
{
 for(int depth=0;frame&&depth<16;depth++,frame=gParent(frame)){
  const char *n=gName(frame);if(!n)continue;
  if(!std::strcmp(n,"toprotor")||!std::strcmp(n,"bonnet_hi_ok")||!std::strcmp(n,"bonnet_hi_dam"))return 1;
  if(!std::strcmp(n,"rearrotor")||!std::strcmp(n,"backrotor")||!std::strcmp(n,"boot_hi_ok")||!std::strcmp(n,"boot_hi_dam"))return 2;
 }return 0;
}
void *HideRotor(void *atomic,void *data)
{
 if((*static_cast<const unsigned *>(data))&RotorFrame(gFrame(atomic)))gSetFlags(atomic,gFlags(atomic)&~4u);return atomic;
}
void PreRender(uint8_t *v)
{
 gPreRender(v);Identity id;if(aircraftbodyclient::Owned(v)||!gReady||!gConfig.carParts||!gConfig.heliDamage||!Identify(v,id)||!AutoHeli(id)||!id.clump)return;
 const unsigned mask=(gDoorStatus(v+kDamage,0)==3?1u:0u)|(gDoorStatus(v+kDamage,1)==3?2u:0u);if(mask)gAtomics(id.clump,HideRotor,const_cast<unsigned *>(&mask));
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
BulletToken BeginBullet(uint8_t *v){Identity id;if(!gReady||!gConfig.carParts||!Identify(v,id)||!BallisticEligible(id))return {};return {Record(id).revision};}
bool CurrentBullet(uint8_t *v,const BulletToken &token){Identity id;if(!token.revision||!gReady||!gConfig.carParts||!Identify(v,id)||!BallisticEligible(id)||(game::EntityFlags(v)&game::kEntityBulletProof))return false;const auto &h=gHistory[size_t(id.ref>>8)];return Same(h.id,id)&&h.revision==token.revision;}
void Projectile(uint8_t *v,const BulletToken &token,int component,float budget,Vec3 point,Vec3 inward)
{
 Identity id;const int slot=partsmodel::Slot(component);
 if(!token.revision||!gReady||!gConfig.carParts||slot<0||!std::isfinite(budget)||budget<=0||!Identify(v,id)||!BallisticEligible(id)||(AutoHeli(id)&&(component<7||component>10))||!(v[0x297]&0x40)||(game::EntityFlags(v)&game::kEntityBulletProof))return;
 auto &h=Record(id);if(h.revision!=token.revision)return;const int before=State(v,slot);if(before<0||before>=3)return;
 const auto &p=partsmodel::Describe(slot);const int nodes[]={17,18,15,11,16,12,7,8};
 panelphysics::Apply({v,token,nodes[slot],p.panel?-1:p.index,point,inward,budget},[&](){
 // Repeated rounds load the struck fastenings. Each accepted shot can advance
 // one native stage; the next stage needs a new independent localized round.
 auto &strain=h.strain[slot];strain+=std::min(budget,1000.f);if(strain<180)return;strain=0;
 if(!gApply(v+kDamage,component,200,1)||!Current(id)||Record(id).revision!=token.revision)return;
 if(p.panel)gSetBumper(v,nodes[slot],p.index,false);else gSetDoor(v,nodes[slot],p.index,false);
 if(Current(id)&&Record(id).revision==token.revision&&State(v,slot)>before)Log("car parts: ref %d model %d %s ballistic %s -> %s",id.ref,id.model,p.name,StateName(before,p.panel),StateName(State(v,slot),p.panel));
 });
}
bool SeverRotor(uint8_t *v,bool main)
{
 Identity id;if(!gReady||!gConfig.carParts||!Identify(v,id)||!AutoHeli(id)||!BallisticEligible(id))return false;
 const int component=main?5:6,node=main?17:18,door=main?0:1;
 if(!*reinterpret_cast<void **>(v+0x440+node*8))return false;
 auto &history=Record(id);const auto revision=history.revision;const bool alreadyMissing=State(v,component-5)==3;
 for(int i=0;i<3&&State(v,component-5)<3;i++){
  if(!gApply(v+kDamage,component,200,1)||!Current(id)||Record(id).revision!=revision)return false;
 }
 if(State(v,component-5)!=3)return false;
 gSetDoor(v,node,door,alreadyMissing);
 return Current(id)&&Record(id).revision==revision&&State(v,component-5)==3;
}
bool Enabled(){return gReady&&gConfig.carParts;}
bool Install()
{
 if(gReady||!gConfig.carParts)return true;
#define BIND(v,s) v=reinterpret_cast<decltype(v)>(dlsym(RTLD_MAIN_ONLY,s));if(!v)return false
 BIND(gSetDoor,"_ZN11CAutomobile13SetDoorDamageEi6eDoorsb");BIND(gSetBumper,"_ZN11CAutomobile15SetBumperDamageEi7ePanelsb");
 BIND(gAtomics,"_Z20RpClumpForAllAtomicsPN2rw5ClumpEPFPNS_6AtomicES3_PvES4_");BIND(gFrame,"_Z16RpAtomicGetFramePKN2rw6AtomicE");BIND(gParent,"_Z16RwFrameGetParentPKN2rw5FrameE");
 BIND(gName,"_Z16GetFrameNodeNamePN2rw5FrameE");BIND(gFlags,"_Z16RpAtomicGetFlagsPKN2rw6AtomicE");BIND(gSetFlags,"_Z16RpAtomicSetFlagsPN2rw6AtomicEj");
 BIND(gPool,"_ZN6CPools15ms_pVehiclePoolE");
 BIND(gDoorStatus,"_ZN14CDamageManager13GetDoorStatusEi");BIND(gPanelStatus,"_ZN14CDamageManager14GetPanelStatusEi");
#undef BIND
 if(!game::api.PoolsGetVehicle||!game::api.timeMs)return false;
#define CHAIN(v,s,f) if(!v)v=reinterpret_cast<decltype(v)>(hooks::Chain(s,reinterpret_cast<void *>(f)))
 CHAIN(gPreRender,"_ZN11CAutomobile9PreRenderEv",PreRender);
 CHAIN(gControl,"_ZN11CAutomobile14ProcessControlEv",Control);
 CHAIN(gVehicleDamage,"_ZN11CAutomobile13VehicleDamageEft",VehicleDamage);
 CHAIN(gApply,"_ZN14CDamageManager11ApplyDamageE10tComponentff",ApplyDamage);
 CHAIN(gFix,"_ZN11CAutomobile3FixEv",Fix);
#undef CHAIN
 gReady=gPreRender&&gControl&&gVehicleDamage&&gApply&&gFix;
 if(gReady)Log("car parts: native damage, swinging and detachment; 350 ms contact gap, stronger later hits");
 else Log("car parts: native hook missing; re-run install.sh");
 return gReady;
}
} // namespace carparts

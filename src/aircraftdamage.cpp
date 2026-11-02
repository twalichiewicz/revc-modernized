#include "aircraftdamage.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include <array>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <vector>

using namespace game;
namespace aircraftdamage {namespace {
constexpr size_t kHandlingSize=0xdc,kVehicleStride=0x6f0;
void (*gAutomobile)(uint8_t *),(*gBoat)(uint8_t *);
void (*gVehicleDamage)(uint8_t *,float,uint16_t);
bool (*gMission)();bool *gCutscene;uint8_t **gPool;
bool gInstalled,gClockSeen;uint32_t gClock;
struct Identity {int ref=-1,model=-1,kind=-1;void *vtable=nullptr;};
struct Lease {int ref=-1,model=-1,kind=-1;bool admitted=false;};
struct ControlContext {uint8_t *vehicle;Identity identity;bool admitted;ControlContext *previous;};
ControlContext *gControlContext;
std::vector<Lease> gLeases;const uint8_t *gEntries,*gFlags;
bool Vehicle(uint8_t *v,Identity &id)
{
 if(!v||!gPool||!*gPool||!api.PoolsGetVehicle)return false;
 const uint8_t *pool=*gPool,*entries=*reinterpret_cast<uint8_t *const *>(pool),*flags=*reinterpret_cast<uint8_t *const *>(pool+8);
 const int capacity=*reinterpret_cast<const int *>(pool+0x10);
 const uintptr_t address=reinterpret_cast<uintptr_t>(v),base=reinterpret_cast<uintptr_t>(entries);
 if(!entries||!flags||capacity<=0||capacity>100000||address<base||(address-base)%kVehicleStride)return false;
 const size_t index=(address-base)/kVehicleStride;if(index>=size_t(capacity)||(flags[index]&0x80))return false;
 const int ref=int(index*256)|flags[index];if(api.PoolsGetVehicle(ref)!=v||(EntityFlags(v)&7)!=2)return false;
 if(gEntries!=entries||gFlags!=flags||gLeases.size()!=size_t(capacity)){gLeases.assign(capacity,{});gEntries=entries;gFlags=flags;}
 id={ref,EntityModel(v),*reinterpret_cast<const int32_t *>(v+0x348),*reinterpret_cast<void *const *>(v)};return true;
}
bool Admitted(uint8_t *v,Identity &id)
{
 if(api.timeMs){const uint32_t now=*api.timeMs;if(gClockSeen&&now<gClock){gLeases.clear();gEntries=gFlags=nullptr;}gClock=now;gClockSeen=true;}
 if(!Vehicle(v,id))return false;
 Lease &lease=gLeases[size_t(id.ref>>8)];
 if(lease.ref!=id.ref||lease.model!=id.model||lease.kind!=id.kind)lease={id.ref,id.model,id.kind,false};
 const bool heli=id.kind==0&&(id.model==155||id.model==177||id.model==199||id.model==217||id.model==218||id.model==227);
 const bool skimmer=id.kind==1&&id.model==190;
 if((!heli&&!skimmer)||(heli&&!gConfig.heliDamage)||(skimmer&&!gConfig.planes)||Wrecked(v)||
    !gMission||!gCutscene||*gCutscene||gMission()){lease.admitted=false;return false;}
 uint8_t *player=api.FindPlayerPed?api.FindPlayerPed():nullptr,*driver=*reinterpret_cast<uint8_t **>(v+0x218);
 const int status=v[0x60]>>3;
 const bool owns=player&&status==0&&driver==player&&PedInVehicle(player)&&PedVehicle(player)==v;
 const bool abandoned=lease.admitted&&status==4&&!driver&&v[0x19e]==0;
 if(!owns&&!abandoned){lease.admitted=false;return false;}
 lease.admitted=true;return true;
}
struct HandlingScope {
 uint8_t *vehicle,*original=nullptr;Identity identity;alignas(16) std::array<uint8_t,kHandlingSize> copy{};
 explicit HandlingScope(uint8_t *v,bool allowed=true):vehicle(v)
 {
  if(!allowed||!Admitted(v,identity))return;
  original=*reinterpret_cast<uint8_t **>(v+0x160);if(!original)return;
  float mass=0,multiplier=0;std::memcpy(&mass,original+4,sizeof mass);std::memcpy(&multiplier,original+0xc8,sizeof multiplier);
  // tHandlingData0xdc, LoadHandlingData token24 ->+c8. Native converts this
  // field by2000/handlingMass. Hunter's raw.50 is the nonzero native baseline;
  // preserve every existing custom/nonzero coefficient, including the Hunter.
  if(multiplier!=0||!std::isfinite(mass)||mass<=0)return;
  const float baseline=.50f*2000/mass;if(!std::isfinite(baseline))return;
  std::memcpy(copy.data(),original,copy.size());std::memcpy(copy.data()+0xc8,&baseline,sizeof baseline);
  *reinterpret_cast<uint8_t **>(v+0x160)=copy.data();
 }
 ~HandlingScope()
 {
  if(!original)return;Identity current;
  // The native configuration is read-only. Never mutate the shared handling
  // table or overwrite a replacement actor/pointer installed by a callback.
  if(Vehicle(vehicle,current)&&current.ref==identity.ref&&current.model==identity.model&&current.kind==identity.kind&&current.vtable==identity.vtable&&
     *reinterpret_cast<uint8_t **>(vehicle+0x160)==copy.data())*reinterpret_cast<uint8_t **>(vehicle+0x160)=original;
 }
};
void Automobile(uint8_t *v)
{
 Identity identity;const bool admitted=Admitted(v,identity);
 ControlContext context{v,identity,admitted,gControlContext};gControlContext=&context;
 struct RestoreContext {ControlContext *previous;~RestoreContext(){gControlContext=previous;}} restore{context.previous};
 // The native transmission writes handling+90 and retains a gear pointer.
 // Keep its real allocation through the whole controller; only VehicleDamage
 // below borrows a configuration copy for its synchronous collision reads.
 gAutomobile(v);
}
void VehicleDamage(uint8_t *v,float damage,uint16_t component)
{
 Identity current;const bool owned=gControlContext&&gControlContext->admitted&&gControlContext->vehicle==v&&damage==0&&Vehicle(v,current)&&
     current.ref==gControlContext->identity.ref&&current.model==gControlContext->identity.model&&current.kind==0&&current.vtable==gControlContext->identity.vtable;
 const HandlingScope scope(v,owned);gVehicleDamage(v,damage,component);
}
void Boat(uint8_t *v){const HandlingScope scope(v);gBoat(v);}
template<class T> bool Bind(T &out,const char *name){out=reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY,name));return out!=nullptr;}
}
bool Install()
{
 if(gInstalled)return true;
 if(!Bind(gMission,"_ZN11CTheScripts18IsPlayerOnAMissionEv")||!Bind(gCutscene,"_ZN12CCutsceneMgr10ms_runningE")||!Bind(gPool,"_ZN6CPools15ms_pVehiclePoolE"))return false;
 if(!gAutomobile)gAutomobile=reinterpret_cast<decltype(gAutomobile)>(hooks::Chain("_ZN11CAutomobile14ProcessControlEv",reinterpret_cast<void *>(Automobile)));
 if(!gBoat)gBoat=reinterpret_cast<decltype(gBoat)>(hooks::Chain("_ZN5CBoat14ProcessControlEv",reinterpret_cast<void *>(Boat)));
 if(!gVehicleDamage)gVehicleDamage=reinterpret_cast<decltype(gVehicleDamage)>(hooks::Chain("_ZN11CAutomobile13VehicleDamageEft",reinterpret_cast<void *>(VehicleDamage)));
 return gInstalled=gAutomobile&&gBoat&&gVehicleDamage;
}
}

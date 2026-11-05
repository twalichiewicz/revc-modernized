#include "airportheli.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <cstdlib>
namespace airportheli {namespace {
constexpr int kModels[]={155,177,199,217,218,227};
struct Pool {uint8_t *entries=nullptr,*flags=nullptr;int count=0;};
void **gBuildings,**gVehicles;uint8_t *gStreaming;int *gArea;bool *gCutscene;bool gReady;
void Destroy(uint8_t *v){auto table=*reinterpret_cast<void (***)(uint8_t *)>(v);table[1](v);}
void *(*gAllocate)(size_t);void (*gConstruct)(uint8_t *,int,uint8_t),(*gDestroy)(uint8_t *)=Destroy,(*gAdd)(uint8_t *),(*gRemove)(uint8_t *),(*gMatrix)(void *),(*gFrame)(uint8_t *),(*gDeletable)(int);
bool (*gMission)();uint32_t (*gRandom)()=+[](){return arc4random();};void (*gRestart)(),(*gShutdown)();
struct Slot {uint8_t *v=nullptr;void *base=nullptr,*flags=nullptr,*vtable=nullptr;int ref=-1,model=-1;};Slot gSlot;
int gChoice=-1;bool gRequested=false;uint32_t gClock=0,gAttempt=0;bool gSeen=false;
Pool Get(void **binding,int maximum){Pool p;if(!binding||!*binding)return p;auto b=static_cast<uint8_t *>(*binding);p={*reinterpret_cast<uint8_t **>(b),*reinterpret_cast<uint8_t **>(b+8),*reinterpret_cast<int *>(b+16)};if(!p.entries||!p.flags||p.count<1||p.count>maximum)return {};return p;}
int Ref(uint8_t *v,const Pool &p){auto a=reinterpret_cast<uintptr_t>(v),b=reinterpret_cast<uintptr_t>(p.entries);if(!v||!p.entries||a<b||(a-b)%0x6f0)return -1;size_t i=(a-b)/0x6f0;return i<size_t(p.count)&&!(p.flags[i]&128)?int(i*256)|p.flags[i]:-1;}
uint8_t *Live(){const auto p=Get(gVehicles,10000);return gSlot.v&&p.entries==gSlot.base&&p.flags==gSlot.flags&&Ref(gSlot.v,p)==gSlot.ref&&game::api.PoolsGetVehicle(gSlot.ref)==gSlot.v&&*reinterpret_cast<void **>(gSlot.v)==gSlot.vtable&&game::EntityModel(gSlot.v)==gSlot.model&&*reinterpret_cast<int *>(gSlot.v+0x348)==0&&gSlot.v[0x294]==1?gSlot.v:nullptr;}
bool Occupied(const uint8_t *v){for(size_t o=0x218;o<0x260;o+=8)if(*reinterpret_cast<void *const *>(v+o))return true;return false;}
float Distance(Vec3 a,Vec3 b){const float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return std::sqrt(x*x+y*y+z*z);}
Vec3 Position(const uint8_t *v){return *reinterpret_cast<const Vec3 *>(v+0x38);}
bool Finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
Vec3 Transform(const float *m,Vec3 p){return {m[12]+m[0]*p.x+m[4]*p.y+m[8]*p.z,m[13]+m[1]*p.x+m[5]*p.y+m[9]*p.z,m[14]+m[2]*p.x+m[6]*p.y+m[10]*p.z};}
void Release(){if(gRequested&&gChoice>=0)gDeletable(gChoice);gRequested=false;gChoice=-1;}
void Reset(){Release();gSlot={};gSeen=false;gAttempt=0;}
void Restart(){Reset();gRestart();}void Shutdown(){Reset();gShutdown();}
struct Pad {uint8_t *v=nullptr;Pool pool;int index=-1;uint8_t generation=0;std::array<float,16> pose{};Vec3 centre{};};
bool Current(const Pad &pad){const auto p=Get(gBuildings,50000);return p.entries==pad.pool.entries&&p.flags==pad.pool.flags&&pad.index>=0&&pad.index<p.count&&p.flags[pad.index]==pad.generation&&game::EntityModel(pad.v)==876&&(game::EntityFlags(pad.v)&7)==1&&!std::memcmp(pad.pose.data(),pad.v+8,sizeof pad.pose);}
bool FindPad(Pad &pad)
{
 const auto p=Get(gBuildings,50000);
 for(int i=0;i<p.count;i++){
  if(p.flags[i]&128)continue;auto v=p.entries+size_t(i)*0x78;
  if(game::EntityModel(v)!=876||(game::EntityFlags(v)&7)!=1)continue;
  pad={v,p,i,p.flags[i]};std::memcpy(pad.pose.data(),v+8,sizeof pad.pose);
  // Centre of the two measured yellow landing-mark triangles in stock Helipad0.
  pad.centre=Transform(pad.pose.data(),{10.02f,-2.03f,1.29f});return Finite(pad.centre);
 }
 return false;
}

bool Spawn(const Pad &pad){
 auto info=game::api.modelInfoPtrs[gChoice];auto col=info?*reinterpret_cast<uint8_t **>(info+0x20):nullptr;if(!info||info[0x1d]!=6||!col||!Current(pad))return false;
 const Vec3 lo=*reinterpret_cast<const Vec3 *>(col+0x10);if(!Finite(lo)||lo.z>=0||lo.z< -10)return false;
 alignas(16) uint8_t point[0x30]{};uint8_t *floorEntity=nullptr;const CVector from{pad.centre.x,pad.centre.y,pad.centre.z+12};
 if(!game::api.ProcessVerticalLine(from,pad.centre.z-12,point,floorEntity,true,false,false,false,false,false,nullptr)||!Current(pad))return false;
 const Vec3 floor=*reinterpret_cast<Vec3 *>(point),normal=*reinterpret_cast<Vec3 *>(point+0x10);if(!Finite(floor)||normal.z<.7f)return false;
 std::array<float,16> pose=pad.pose;pose[12]=pad.centre.x;pose[13]=pad.centre.y;pose[14]=floor.z-lo.z+.06f;
 const int count=*reinterpret_cast<int16_t *>(col+0x28);auto spheres=*reinterpret_cast<uint8_t **>(col+0x38);if(count<1||count>128||!spheres)return false;
 for(int i=0;i<count;i++){auto s=spheres+size_t(i)*20;const auto p=Transform(pose.data(),*reinterpret_cast<Vec3 *>(s));const float radius=*reinterpret_cast<float *>(s+12);if(!Finite(p)||!std::isfinite(radius)||radius<=0||radius>10||game::api.TestSphereAgainstWorld({p.x,p.y,p.z},radius,nullptr,true,true,true,true,false,false)||!Current(pad))return false;}
 const auto pool=Get(gVehicles,10000);int free=0;for(int i=0;i<pool.count;i++)free+=(pool.flags[i]&128)!=0;if(free<5)return false;
 auto v=static_cast<uint8_t *>(gAllocate(0x6f0));if(!v)return false;const int ref=Ref(v,pool);if(ref<0)return false;gConstruct(v,gChoice,1);
 if(game::api.PoolsGetVehicle(ref)!=v)return false;if(!game::EntityClump(v)||!Current(pad)){gDestroy(v);return false;}
 std::memcpy(v+8,pose.data(),sizeof pose);*reinterpret_cast<unsigned *>(v+0x60)=(game::EntityFlags(v)&~0xf8u)|(4u<<3)|game::kEntityUsesCollision;v[0x295]&=uint8_t(~16);
 gMatrix(v+8);gFrame(v);gSlot={v,pool.entries,pool.flags,*reinterpret_cast<void **>(v),ref,gChoice};gAdd(v);Log("airport helipad: model %d ref %d at %.1f %.1f %.1f",gChoice,ref,pose[12],pose[13],pose[14]);Release();return true;
}
}
bool Owns(const uint8_t *v){return gReady&&v&&Live()==v;}
void Frame(uint32_t now){
 if(!gReady)return;if(gSeen&&now<gClock){Release();gAttempt=0;}gClock=now;gSeen=true;
 auto player=game::api.FindPlayerPed();auto v=Live();if(gSlot.v&&!v)gSlot={};
 if(v&&!Occupied(v)&&(!gConfig.planes||!player||*gArea||Distance(Position(v),Position(player))>450)){gRemove(v);gDestroy(v);gSlot={};v=nullptr;}
 if(v)return;
 if(!gConfig.planes||!player||*gArea||gMission()||*gCutscene||game::Health(player)<=0){Release();return;}
 Pad pad;if(!FindPad(pad)||Distance(pad.centre,Position(player))>220){Release();return;}
 if(gChoice<0)gChoice=kModels[gRandom()%6];auto info=game::api.modelInfoPtrs[gChoice];if(!info||info[0x1d]!=6)return;
 if(gStreaming[size_t(gChoice)*32+0x10]!=1||!*reinterpret_cast<void **>(info+0x30)){game::api.RequestModel(gChoice,2);gRequested=true;return;}
 if(gAttempt&&now>=gAttempt&&now-gAttempt<500)return;gAttempt=now;Spawn(pad);
}
bool Install(){if(gReady)return true;
#define B(v,s) v=reinterpret_cast<decltype(v)>(dlsym(RTLD_MAIN_ONLY,s));if(!v)return false
 B(gBuildings,"_ZN6CPools16ms_pBuildingPoolE");B(gVehicles,"_ZN6CPools15ms_pVehiclePoolE");B(gStreaming,"_ZN10CStreaming16ms_aInfoForModelE");B(gArea,"_ZN5CGame8currAreaE");B(gCutscene,"_ZN12CCutsceneMgr10ms_runningE");
 B(gAllocate,"_ZN8CVehiclenwEm");B(gConstruct,"_ZN11CAutomobileC1Eih");B(gAdd,"_ZN6CWorld3AddEP7CEntity");B(gRemove,"_ZN6CWorld6RemoveEP7CEntity");B(gMatrix,"_ZN7CMatrix8UpdateRWEv");B(gFrame,"_ZN7CEntity13UpdateRwFrameEv");B(gDeletable,"_ZN10CStreaming19SetModelIsDeletableEi");B(gMission,"_ZN11CTheScripts18IsPlayerOnAMissionEv");
#undef B
 if(!game::api.FindPlayerPed||!game::api.PoolsGetVehicle||!game::api.modelInfoPtrs||!game::api.RequestModel||!game::api.ProcessVerticalLine||!game::api.TestSphereAgainstWorld)return false;
 if(!gRestart)gRestart=reinterpret_cast<decltype(gRestart)>(hooks::Chain("_ZN6CWorld15ClearForRestartEv",reinterpret_cast<void *>(Restart)));if(!gShutdown)gShutdown=reinterpret_cast<decltype(gShutdown)>(hooks::Chain("_ZN6CWorld8ShutDownEv",reinterpret_cast<void *>(Shutdown)));
 return gReady=gRestart&&gShutdown;
}
}

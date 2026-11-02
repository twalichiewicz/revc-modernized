#include "towercollision.h"
#include "towercollisiondata.h"
#include "airworld.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <type_traits>
namespace towercollision {namespace {
using namespace game;
using Models=int (*)(const uint8_t *,uint8_t *,const uint8_t *,uint8_t *,uint8_t *,uint8_t *,float *);
int (*gPhysical)(uint8_t *,uint8_t *,uint8_t *),(*gAutomobile)(uint8_t *,uint8_t *,uint8_t *);Models gModels;
void (*gRestart)(),(*gShutdown)(),(*gRemovePlanes)(uint8_t *);
uint8_t **gVehiclePool,**gBuildingPool;
bool gReady,gDataReady;uint32_t gEpoch;unsigned gReports;
static_assert(sizeof(CVector)==12&&sizeof(towercollisiondata::Triangle)==8,"native collision vertex/triangle layout");
struct NativeCol {alignas(16) std::array<uint8_t,0x68> bytes{};};
std::array<NativeCol,std::size(towercollisiondata::kModels)> gColumns;
struct Identity {uint8_t *entity=nullptr,*entries=nullptr,*flags=nullptr,*info=nullptr,*col=nullptr;void *vtable=nullptr;size_t index=0;uint8_t generation=0;int model=-1,kind=-1;CVector matrix[4]{};};
struct Context {Identity actor,building;const towercollisiondata::Model *data=nullptr;uint8_t *points=nullptr;uint32_t epoch=0;bool admitted=false,automobile=false;Context *previous=nullptr;};
Context *gContext;
bool Finite(CVector p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool Snapshot(uint8_t *entity,bool vehicle,Identity &out){
 const uint8_t *pool=vehicle?(gVehiclePool?*gVehiclePool:nullptr):(gBuildingPool?*gBuildingPool:nullptr);if(!entity||!pool||!api.modelInfoPtrs)return false;
 auto *entries=*reinterpret_cast<uint8_t *const *>(pool),*flags=*reinterpret_cast<uint8_t *const *>(pool+8);const int count=*reinterpret_cast<const int *>(pool+16);
 const size_t stride=vehicle?0x6f0:0x78;const uintptr_t address=reinterpret_cast<uintptr_t>(entity),base=reinterpret_cast<uintptr_t>(entries);
 if(!entries||!flags||count<=0||count>(vehicle?10000:50000)||address<base||(address-base)%stride)return false;
 const size_t index=(address-base)/stride;if(index>=size_t(count)||(flags[index]&128)||(EntityFlags(entity)&7)!=(vehicle?2:1)||!(EntityFlags(entity)&kEntityUsesCollision))return false;
 if(vehicle&&(!api.PoolsGetVehicle||api.PoolsGetVehicle(int(index*256)|flags[index])!=entity))return false;
 const int model=EntityModel(entity);if(model<0||model>=6500)return false;auto *info=api.modelInfoPtrs[model];if(!info||info[0x1d]!=(vehicle?6:1))return false;
 auto *col=*reinterpret_cast<uint8_t **>(info+0x20);if(!col)return false;
 out={};out.entity=entity;out.entries=entries;out.flags=flags;out.info=info;out.col=col;out.vtable=*reinterpret_cast<void **>(entity);out.index=index;out.generation=flags[index];out.model=model;
 out.kind=vehicle?*reinterpret_cast<const int32_t *>(entity+0x348):-1;if(!out.vtable)return false;
 for(int i=0;i<4;++i){out.matrix[i]=*reinterpret_cast<const CVector *>(entity+8+16*i);if(!Finite(out.matrix[i]))return false;}
 return true;
}
bool Same(const Identity &old,bool vehicle){Identity current;if(!Snapshot(old.entity,vehicle,current))return false;
 return current.entries==old.entries&&current.flags==old.flags&&current.index==old.index&&current.generation==old.generation&&current.model==old.model&&current.kind==old.kind&&current.info==old.info&&current.col==old.col&&current.vtable==old.vtable&&!std::memcmp(current.matrix,old.matrix,sizeof old.matrix);
}
bool Actor(uint8_t *v,Identity &out){return Snapshot(v,true,out)&&((out.kind==4)||(out.kind==1&&out.model==190))&&!Wrecked(v)&&std::isfinite(VehicleHealth(v))&&VehicleHealth(v)>0&&airworld::OwnsFlightView(v);}
const towercollisiondata::Model *Data(const Identity &building){for(const auto &data:towercollisiondata::kModels)if(building.model==data.id&&!std::strcmp(reinterpret_cast<const char *>(building.info+8),data.name))return &data;return nullptr;}
bool Current(const Context &context){return gReady&&context.admitted&&context.epoch==gEpoch&&Same(context.actor,true)&&Same(context.building,false)&&!Wrecked(context.actor.entity)&&std::isfinite(VehicleHealth(context.actor.entity))&&VehicleHealth(context.actor.entity)>0&&airworld::OwnsFlightView(context.actor.entity)&&Data(context.building)==context.data;}
int EntityCollision(uint8_t *v,uint8_t *building,uint8_t *points,bool automobile){
 Context context;context.previous=gContext;context.epoch=gEpoch;context.points=points;context.automobile=automobile;
 context.admitted=gReady&&Actor(v,context.actor)&&(!automobile||(context.actor.model==190&&context.actor.kind==1))&&Snapshot(building,false,context.building)&&(context.data=Data(context.building));
 gContext=&context;struct Restore {Context *previous;~Restore(){gContext=previous;}} restore{context.previous};return (automobile?gAutomobile:gPhysical)(v,building,points);
}
int Physical(uint8_t *v,uint8_t *building,uint8_t *points){return EntityCollision(v,building,points,false);}
int Automobile(uint8_t *v,uint8_t *building,uint8_t *points){return EntityCollision(v,building,points,true);}
int ColModels(const uint8_t *ma,uint8_t *a,const uint8_t *mb,uint8_t *b,uint8_t *points,uint8_t *lines,float *distances){
 // Preserve the complete original primitive evaluation and its positive
 // output. A missing upper surface is the native zero-contact branch only.
 const int original=gModels(ma,a,mb,b,points,lines,distances);
 const Context *context=gContext;if(original!=0||!points||!context||points!=context->points||!Current(*context)||ma!=context->actor.entity+8||mb!=context->building.entity+8||a!=context->actor.col||b!=context->building.col)return original;
 const unsigned lineCount=a[0x2e];if(context->automobile){if(lineCount!=4||lines!=context->actor.entity+0x4e0||distances!=reinterpret_cast<float *>(context->actor.entity+0x580))return original;}else if(lineCount||lines||distances)return original;
 // Native body output has one contact per A sphere; CPhysical's array is64
 // CColPoints (40 bytes each). Supported aircraft are below that same bound.
 const int spheres=*reinterpret_cast<const int16_t *>(a+0x28);if(spheres<1||spheres>64)return original;
 const size_t index=size_t(context->data-towercollisiondata::kModels);auto *upper=gColumns[index].bytes.data();
 // Skimmer's native automobile caller also evaluates four wheel lines. Keep
 // the actual A COL identity and give that native loop bounded scratch outputs;
 // only original wheel contacts/fractions may reach its controller afterward.
 alignas(16) uint8_t wheelPoints[4*40]{};float wheelDistances[4]{1,1,1,1};
 const int result=gModels(ma,a,mb,upper,points,context->automobile?wheelPoints:nullptr,context->automobile?wheelDistances:nullptr);
 if(!Current(*context))return original;
 if(result>0&&gReports<4){++gReports;const auto p=EntityPos(context->actor.entity);Log("tower collision: native upper model%d contacts%d aircraft%d at %.2f %.2f %.2f",context->building.model,result,context->actor.model,p.x,p.y,p.z);}
 return result;
}
bool Prepare(){
 if(gDataReady)return true;
 for(size_t i=0;i<gColumns.size();++i){const auto &data=towercollisiondata::kModels[i];if(!data.vertexCount||!data.triangleCount||data.triangleCount>=600)return false;
  CVector lo=data.vertices[0],hi=lo;for(int j=0;j<data.vertexCount;++j){const CVector p=data.vertices[j];if(!Finite(p))return false;lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
  for(int j=0;j<data.triangleCount;++j)for(int k=0;k<3;++k)if(data.triangles[j].v[k]>=data.vertexCount)return false;
  auto *col=gColumns[i].bytes.data();const CVector centre{(lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f,(lo.z+hi.z)*.5f};*reinterpret_cast<CVector *>(col)=centre;
  const CVector diagonal{hi.x-lo.x,hi.y-lo.y,hi.z-lo.z};*reinterpret_cast<float *>(col+12)=std::sqrt(diagonal.x*diagonal.x+diagonal.y*diagonal.y+diagonal.z*diagonal.z)*.5f;
  *reinterpret_cast<CVector *>(col+0x10)=lo;*reinterpret_cast<CVector *>(col+0x1c)=hi;*reinterpret_cast<int16_t *>(col+0x2c)=int16_t(data.triangleCount);
  *reinterpret_cast<const CVector **>(col+0x50)=data.vertices;*reinterpret_cast<const towercollisiondata::Triangle **>(col+0x58)=data.triangles;
 }
 return gDataReady=true;
}
void Clear(){++gEpoch;gReports=0;for(auto &col:gColumns)if(*reinterpret_cast<void **>(col.bytes.data()+0x60))gRemovePlanes(col.bytes.data());}
void Restart(){Clear();gRestart();}void Shutdown(){Clear();gShutdown();}
template<class T>bool Bind(T &out,const char *name){out=reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY,name));return out!=nullptr;}
}
bool Install(){
 if(gReady)return true;
 if(!Bind(gVehiclePool,"_ZN6CPools15ms_pVehiclePoolE")||!Bind(gBuildingPool,"_ZN6CPools16ms_pBuildingPoolE")||!Bind(gRemovePlanes,"_ZN10CCollision20RemoveTrianglePlanesEP9CColModel")||!Prepare())return false;
 auto chain=[](const char *name,auto hook,auto &original){if(original)return true;original=reinterpret_cast<std::decay_t<decltype(original)>>(hooks::Chain(name,reinterpret_cast<void *>(hook)));return original!=nullptr;};
 return gReady=chain("_ZN9CPhysical22ProcessEntityCollisionEP7CEntityP9CColPoint",Physical,gPhysical)&&chain("_ZN11CAutomobile22ProcessEntityCollisionEP7CEntityP9CColPoint",Automobile,gAutomobile)&&chain("_ZN10CCollision16ProcessColModelsERK7CMatrixR9CColModelS2_S4_P9CColPointS6_Pf",ColModels,gModels)&&chain("_ZN6CWorld15ClearForRestartEv",Restart,gRestart)&&chain("_ZN10CModelInfo8ShutDownEv",Shutdown,gShutdown);
}
}

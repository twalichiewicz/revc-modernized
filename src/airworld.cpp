#include "airworld.h"
#include "config.h"
#include "draw.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "planeflight.h"
#include "shaders.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <dlfcn.h>

using namespace game;

namespace airworld {
// Native CPtrList/CPtrNode, macVC1.0.5217 (ScanBigBuildingList walks +0x10).
struct PtrNode {void *entity;PtrNode *previous,*next;};
struct PtrList {PtrNode *first;};
static_assert(sizeof(PtrList)==8&&sizeof(PtrNode)==24);
namespace {
constexpr int kMaxNodes=2048,kMaxPending=32,kRequestsPerFrame=8;
constexpr uint64_t kPendingBytes=16*1024*1024;
constexpr float kCoverage=3000,kMaxClip=8000;
constexpr uint32_t kKeepMs=8000;
constexpr const char *kFarSymbol="_Z23RwCameraSetFarClipPlanePN2rw6CameraEf";
constexpr const char *kFogSymbol="_Z22RwCameraSetFogDistancePN2rw6CameraEf";

void (*orig_Construct)();
int (*orig_BigVisibility)(uint8_t *);
int (*orig_EntityVisibility)(uint8_t *);
void *(*orig_Atomic)(uint8_t *,float);
void *(*orig_DetailAtomic)(uint8_t *,float);
void *(*orig_SetFar)(void *,float),*(*orig_SetFog)(void *,float);
void (*orig_Horizon)();
void *(*orig_FadingAtomic)(void *,float);
void (*orig_RenderBuilding)(uint8_t *,float);
uint8_t *(*gAtomicModel)(void *);
void (*gScanBig)(PtrList &);
void (*gDeletable)(int);
float (*gLargest)(const uint8_t *);
float (*gGetFar)(const void *),(*gGetFog)(const void *);
bool (*gMission)(),(*gOutside)();
bool (*gTime)(uint8_t,uint8_t);
int32_t *gArea,*gLevel;
bool *gCutscene;
PtrList *gBigLists;
PtrList *gSectors;
CVector *gRendererCamera;
uint8_t *gStreaming,**gVehiclePool,**gBuildingPool;
uint32_t *gFrameCounter;
float *gNativeFar,*gNativeFog,*gDrawFar;
uintptr_t gFarReturn,gFogReturn;
bool gReady,gActive,gWasActive;
float gBlend,gFar,gFog;
uint32_t gPrevious;
CVector gPosition;
// A sorted fade list stores the original physical distance, but its draw
// consumer reselects geometry later. Carry this frame's exact instance policy
// through that boundary without changing transparent sorting or shared models.
struct Admission {
 const uint8_t *entity=nullptr,*model=nullptr,*pool=nullptr,*entries=nullptr,*flags=nullptr;
 uint8_t **table=nullptr,*stream=nullptr;
 void *atomic=nullptr,*sourceAtomic=nullptr,*vtable=nullptr;
 std::array<uint8_t,21> name{};
 int id=-1,txd=-1,index=-1;
 uint32_t frame=0;
 uint8_t generation=0;
 float offset=0,nativeDistance=0;
 bool queried=false,selected=false,detail=false,opaque=false;
};
std::array<Admission,kMaxNodes> gAdmissions;
size_t gAdmissionCount;
Admission *gBigAdmission;
Admission *gDetailAdmission;
std::array<uint32_t,50000> gWarmScans{};
uint32_t gWarmEpoch;

struct Pending {
 int id=-1,txd=-1;
 uint8_t *model=nullptr,*stream=nullptr,**table=nullptr;
 std::array<uint8_t,21> name{};
 uint32_t since=0;
 bool modelKeep=false,txdKeep=false,detail=false;
};
std::array<Pending,kMaxPending> gPending;
std::array<uint32_t,6500> gRetry{};

bool Finite(CVector p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
void *MainCamera(){return api.theCamera?*reinterpret_cast<void **>(api.theCamera+0x858):nullptr;}
uint8_t *Info(int id){return gStreaming+size_t(id)*0x20;}
bool Same(const Pending &p)
{
 return p.id>=0&&p.id<6500&&p.stream==gStreaming&&p.table==api.modelInfoPtrs&&api.modelInfoPtrs&&
        api.modelInfoPtrs[p.id]==p.model&&p.model&&
        !std::memcmp(p.name.data(),p.model+8,p.name.size())&&
        *reinterpret_cast<const int16_t *>(p.model+0x2e)==p.txd;
}
void Release(size_t index)
{
 const Pending p=gPending[index];gPending[index]={};
 if(!Same(p))return; // a reload/replaced metadata cannot inherit a stale pin
 if(p.modelKeep&&(Info(p.id)[0x11]&1))gDeletable(p.id);
 if(!p.txdKeep)return;
 // A shared TXD keep bit belongs to the remaining requests until the last
 // requesting model completes; the first completion must not drop it.
 for(Pending &other:gPending)if(other.id>=0&&other.txd==p.txd&&Same(other)){
  other.txdKeep=true;return;
 }
 if(Info(6500+p.txd)[0x11]&1)gDeletable(6500+p.txd);
}
void ReleaseAll(){for(size_t i=0;i<gPending.size();++i)Release(i);}
uint64_t Bytes(int id){return uint64_t(*reinterpret_cast<const uint32_t *>(Info(id)+0x18))*2048;}

bool PlayerAircraft(const uint8_t *ped,const uint8_t *vehicle)
{
 if(!vehicle)return false;
 // CELL's ringing/talking routine sets the broad OnAMission flag while the
 // same independently owned aircraft remains under its player's controls.
 // That flag does not transfer an owned actor or its ordinary player camera.
 if(planeflight::Owns(vehicle))return IsVehicle(vehicle)&&(vehicle[0x60]>>3)==0&&
     *reinterpret_cast<uint8_t *const *>(vehicle+0x218)==ped;
 if(gMission())return false; // native/SCM aircraft still require freeplay
 // A stock SCM-created Skimmer is native CAutomobile model190/class1. Its
 // current pool generation and player driver own this freeplay view; neither
 // an ambient CPlane nor a passenger acquires presentation ownership.
 const uint8_t *pool=gVehiclePool?*gVehiclePool:nullptr;
 if(!pool||!api.PoolsGetVehicle)return false;
 const auto *entries=*reinterpret_cast<uint8_t *const *>(pool);
 const auto *flags=*reinterpret_cast<uint8_t *const *>(pool+8);
 const int count=*reinterpret_cast<const int *>(pool+16);
 if(!entries||!flags||count<=0||count>10000)return false;
 const uintptr_t address=reinterpret_cast<uintptr_t>(vehicle),base=reinterpret_cast<uintptr_t>(entries);
 if(address<base)return false;
 const uintptr_t offset=address-base;
 if(offset%0x6f0||offset/0x6f0>=size_t(count))return false;
 const size_t index=offset/0x6f0;
 if(flags[index]&128||api.PoolsGetVehicle(int(index*256)|flags[index])!=vehicle)return false;
 return IsVehicle(vehicle)&&EntityModel(vehicle)==190&&*reinterpret_cast<const int32_t *>(vehicle+0x348)==1&&
        *reinterpret_cast<uint8_t *const *>(vehicle+0x218)==ped&&(vehicle[0x60]>>3)==0;
}
bool Eligible(CVector &position,const uint8_t *vehicle=nullptr)
{
 if(!gConfig.planes||!api.theCamera||!api.FindPlayerPed||!api.modelInfoPtrs||!api.timeMs||
    !gArea||*gArea||!gLevel||*gLevel<0||*gLevel>2||!gCutscene||*gCutscene||
    api.theCamera[0x75]||!api.theCamera[0x63]||!gOutside()||!MainCamera())return false;
 const uint8_t *ped=api.FindPlayerPed();
 if(!ped||!PedInVehicle(ped)||!std::isfinite(Health(ped))||Health(ped)<=0||
    (vehicle&&PedVehicle(ped)!=vehicle)||!PlayerAircraft(ped,PedVehicle(ped)))return false;
 position=*reinterpret_cast<const CVector *>(api.theCamera+0x30);
 return Finite(position)&&position.z>120&&position.z<7000;
}
uint8_t *BigModel(const uint8_t *entity)
{
 if(!entity||!api.modelInfoPtrs||!(*reinterpret_cast<const uint32_t *>(entity+0x60)&(1u<<22)))return nullptr;
 const int id=EntityModel(entity);
 if(id<0||id>=6500)return nullptr;
 const int interior=int8_t(entity[0x6d]);
 if(interior!=0&&interior!=13)return nullptr;
 uint8_t *model=api.modelInfoPtrs[id];
 if(!model||(model[0x1d]!=1&&model[0x1d]!=3))return nullptr;
 return model;
}
uint8_t *DetailModel(const uint8_t *entity)
{
 if(!entity||!api.modelInfoPtrs)return nullptr;
 const uint32_t flags=*reinterpret_cast<const uint32_t *>(entity+0x60);
 const int id=EntityModel(entity),interior=int8_t(entity[0x6d]);
 if((flags&7)!=1||(flags&((1u<<22)|(1u<<30)))||!(flags&(1u<<18))||
    (interior!=0&&interior!=13)||(*reinterpret_cast<const uint16_t *>(entity+0x64)&4096)||id<0||id>=6500)return nullptr;
 auto *model=api.modelInfoPtrs[id];
 return model&&(model[0x1d]==1||model[0x1d]==3)?model:nullptr;
}
// Native COL bounds remain present when its streamed primitive arrays are
// absent. They describe the actual original mesh's full tower/rooftop extent;
// a distance to its ground-level IPL origin cannot describe a roof approach.
bool DetailBounds(const uint8_t *entity,const uint8_t *model,CVector &lo,CVector &hi)
{
 const auto *col=*reinterpret_cast<uint8_t *const *>(model+0x20);if(!col)return false;
 const CVector a=*reinterpret_cast<const CVector *>(col+0x10),b=*reinterpret_cast<const CVector *>(col+0x1c);
 if(!Finite(a)||!Finite(b)||a.x>b.x||a.y>b.y||a.z>b.z||
    b.x-a.x>2000||b.y-a.y>2000||b.z-a.z>2000)return false;
 const CVector p=EntityPos(entity),r=*reinterpret_cast<const CVector *>(entity+8),
  f=*reinterpret_cast<const CVector *>(entity+0x18),u=*reinterpret_cast<const CVector *>(entity+0x28);
 if(!Finite(p)||!Finite(r)||!Finite(f)||!Finite(u))return false;
 lo={INFINITY,INFINITY,INFINITY};hi={-INFINITY,-INFINITY,-INFINITY};
 for(int i=0;i<8;++i){const float x=i&1?b.x:a.x,y=i&2?b.y:a.y,z=i&4?b.z:a.z;
  const CVector v{p.x+r.x*x+f.x*y+u.x*z,p.y+r.y*x+f.y*y+u.y*z,p.z+r.z*x+f.z*y+u.z*z};
  lo={std::min(lo.x,v.x),std::min(lo.y,v.y),std::min(lo.z,v.z)};
  hi={std::max(hi.x,v.x),std::max(hi.y,v.y),std::max(hi.z,v.z)};
 }
 return Finite(lo)&&Finite(hi)&&hi.z>100;
}
float BoxDistance(CVector p,CVector lo,CVector hi)
{
 const float x=p.x-std::clamp(p.x,lo.x,hi.x),y=p.y-std::clamp(p.y,lo.y,hi.y),z=p.z-std::clamp(p.z,lo.z,hi.z);
 return std::sqrt(x*x+y*y+z*z);
}
bool Corridor(CVector lo,CVector hi,CVector end)
{
 float near=0,far=1;
 const float a[3]={gPosition.x,gPosition.y,gPosition.z},d[3]={end.x-a[0],end.y-a[1],end.z-a[2]},
  low[3]={lo.x-180,lo.y-180,lo.z-180},high[3]={hi.x+180,hi.y+180,hi.z+180};
 for(int i=0;i<3;++i){
  if(std::fabs(d[i])<.00001f){if(a[i]<low[i]||a[i]>high[i])return false;continue;}
  float x=(low[i]-a[i])/d[i],y=(high[i]-a[i])/d[i];if(x>y)std::swap(x,y);
  near=std::max(near,x);far=std::min(far,y);if(near>far)return false;
 }
 return true;
}
float DetailOffset(const uint8_t *entity,const uint8_t *model)
{
 CVector lo,hi;if(!DetailBounds(entity,model,lo,hi)||BoxDistance(gPosition,lo,hi)>180)return 0;
 const CVector p=EntityPos(entity);const float x=gPosition.x-p.x,y=gPosition.y-p.y,z=gPosition.z-p.z;
 return std::max(0.0f,std::sqrt(x*x+y*y+z*z)-BoxDistance(gPosition,lo,hi));
}
// Keep the native near-model handoff and actual frustum/occlusion unchanged.
// Only the BIG's far radial budget gains the vertical leg and a bounded
// horizontal radius. Changing a global LOD multiplier would stream street
// details and make the near-model handoff hide terrain at altitude.
float Offset(const uint8_t *entity,const uint8_t *model)
{
 const float lod=gLargest(model);
 const CVector at=EntityPos(entity);
 if(!Finite(at)||!std::isfinite(lod)||lod<300)return 0;
 const float reach=std::hypot(std::max(lod,kCoverage),gPosition.z-at.z);
 return gBlend*(reach-lod);
}
float Distance(const uint8_t *entity,const uint8_t *model,float nativeDistance)
{
 if(!std::isfinite(nativeDistance))return nativeDistance;
 // An additive reach offset preserves native D-20 fallback selection and
 // its 20m alpha ramp exactly. Scaling D would give Setup and Fade different
 // positions within that band because Fade subtracts20 after its call seam.
 return nativeDistance-Offset(entity,model);
}
bool Capture(Admission &a,uint8_t *entity,bool detail=false)
{
 const uint8_t *model=detail?DetailModel(entity):BigModel(entity),*pool=gBuildingPool?*gBuildingPool:nullptr;
 if(!model||!pool||!gFrameCounter||model[0x54]!=1||((model[0x56]&8)&&(model[0x56]&3))||
    (*reinterpret_cast<const uint32_t *>(entity+0x60)&7)!=1)return false;
 const auto *entries=*reinterpret_cast<uint8_t *const *>(pool);
 const auto *flags=*reinterpret_cast<uint8_t *const *>(pool+8);
 const int count=*reinterpret_cast<const int *>(pool+16);
 if(!entries||!flags||count<=0||count>50000)return false;
 const uintptr_t address=reinterpret_cast<uintptr_t>(entity),base=reinterpret_cast<uintptr_t>(entries);
 if(address<base)return false;
 const uintptr_t offset=address-base;
 if(offset%0x78||offset/0x78>=size_t(count))return false;
 const size_t index=offset/0x78;
 if(flags[index]&128)return false;
 const float correction=detail?DetailOffset(entity,model):Offset(entity,model);
 if(!std::isfinite(correction)||correction<=0)return false;
 a.entity=entity;a.model=model;a.id=EntityModel(entity);a.pool=pool;a.entries=entries;a.flags=flags;
 a.index=int(index);a.generation=flags[index];a.frame=*gFrameCounter;a.table=api.modelInfoPtrs;a.stream=gStreaming;
 a.detail=detail;a.offset=correction;a.sourceAtomic=*reinterpret_cast<void *const *>(model+0x30);
 a.vtable=*reinterpret_cast<void *const *>(entity);a.txd=*reinterpret_cast<const int16_t *>(model+0x2e);
 std::memcpy(a.name.data(),model+8,a.name.size());
 return a.sourceAtomic&&Info(a.id)[0x10]==1;
}
bool SameAdmission(const Admission &a,void *atomic,float distance)
{
 if(!a.selected||a.atomic!=atomic||distance!=a.nativeDistance||!gFrameCounter||a.frame!=*gFrameCounter||
    !gBuildingPool||*gBuildingPool!=a.pool||a.table!=api.modelInfoPtrs||a.stream!=gStreaming)return false;
 const auto *entries=*reinterpret_cast<uint8_t *const *>(a.pool);
 const auto *flags=*reinterpret_cast<uint8_t *const *>(a.pool+8);
 const int count=*reinterpret_cast<const int *>(a.pool+16);
 if(entries!=a.entries||flags!=a.flags||a.index<0||a.index>=count||count>50000||flags[a.index]!=a.generation)return false;
 if(api.modelInfoPtrs[a.id]!=a.model||Info(a.id)[0x10]!=1||EntityModel(a.entity)!=a.id||
    *reinterpret_cast<void *const *>(a.entity)!=a.vtable||*reinterpret_cast<void *const *>(a.entity+0x58)!=atomic||
    *reinterpret_cast<void *const *>(a.model+0x30)!=a.sourceAtomic||a.model[0x54]!=1||
    *reinterpret_cast<const int16_t *>(a.model+0x2e)!=a.txd||std::memcmp(a.name.data(),a.model+8,a.name.size()))return false;
 return gAtomicModel(atomic)==a.model;
}
int Hook_BigVisibility(uint8_t *entity)
{
 // Native handoff compares the BIG's origin against300m. A tall original
 // admitted near its actual roof can be farther than300m from that origin.
 // Suppress only the corresponding, actually opaque original instance.
 if(gActive&&BigModel(entity)){
  const void *related=*reinterpret_cast<void *const *>(BigModel(entity)+0x40);
  for(size_t i=0;i<gAdmissionCount;++i){const Admission &a=gAdmissions[i];
   if(!a.detail||!a.opaque||a.model!=related||!SameAdmission(a,a.atomic,a.nativeDistance))continue;
   const CVector x=EntityPos(entity),y=EntityPos(a.entity);
   if(std::fabs(x.x-y.x)>.1f||std::fabs(x.y-y.y)>.1f||std::fabs(x.z-y.z)>.1f)continue;
   bool same=true;for(int row=0;row<3;++row)if(std::memcmp(entity+8+size_t(row)*16,a.entity+8+size_t(row)*16,sizeof(CVector)))same=false;
   if(same)return 0;
  }
 }
 Admission candidate;
 Admission *saved=gBigAdmission;
 gBigAdmission=nullptr;
 if(gActive&&gAdmissionCount<gAdmissions.size()&&Capture(candidate,entity)){
  // Reserve before calling native visibility: a nested visibility evaluation
  // cannot consume or overwrite this instance's slot.
  gAdmissions[gAdmissionCount]=candidate;gBigAdmission=&gAdmissions[gAdmissionCount++];
 }
 Admission *current=gBigAdmission;
 const int result=orig_BigVisibility(entity);
 if(current&&current->selected)current->atomic=*reinterpret_cast<void **>(entity+0x58);
 gBigAdmission=saved;
 return result;
}
int Hook_EntityVisibility(uint8_t *entity)
{
 Admission candidate;Admission *saved=gDetailAdmission;gDetailAdmission=nullptr;
 if(gActive&&gAdmissionCount<gAdmissions.size()&&Capture(candidate,entity,true)){
  gAdmissions[gAdmissionCount]=candidate;gDetailAdmission=&gAdmissions[gAdmissionCount++];
 }
 Admission *current=gDetailAdmission;const int result=orig_EntityVisibility(entity);
 if(current&&current->selected){current->atomic=*reinterpret_cast<void **>(entity+0x58);current->opaque=result==1&&current->model[0x55]==255;}
 gDetailAdmission=saved;return result;
}
void *Hook_DetailAtomic(uint8_t *model,float distance)
{
 Admission *a=gDetailAdmission;
 if(a&&a->model==model){
  if(!a->queried){a->queried=true;a->nativeDistance=distance;}
  void *atomic=orig_DetailAtomic(model,distance-a->offset);a->selected|=atomic!=nullptr;return atomic;
 }
 return orig_DetailAtomic(model,distance);
}
void *Hook_Atomic(uint8_t *model,float distance)
{
 Admission *a=gBigAdmission;
 if(a&&a->model==model){
  if(!a->queried){a->queried=true;a->nativeDistance=distance;}
  void *atomic=orig_Atomic(model,distance-a->offset);
  a->selected|=atomic!=nullptr;
  return atomic;
 }
 return orig_Atomic(model,distance);
}
float AdmittedDistance(void *atomic,float distance)
{
 for(size_t i=0;i<gAdmissionCount;++i)if(SameAdmission(gAdmissions[i],atomic,distance)){
  // The original native queue distance is still the sort key. Only this
  // admitted atomic's draw/alpha selection receives its captured correction;
  // owner/config changes later this frame cannot split admission from draw.
  return distance-gAdmissions[i].offset;
 }
 return distance;
}
void *Hook_FadingAtomic(void *atomic,float distance)
{
 Admission *saved=gDetailAdmission;gDetailAdmission=nullptr;
 void *result=orig_FadingAtomic(atomic,AdmittedDistance(atomic,distance));gDetailAdmission=saved;return result;
}
void Hook_RenderBuilding(uint8_t *entity,float distance)
{
 Admission *saved=gDetailAdmission;gDetailAdmission=nullptr;
 if(entity&&(*reinterpret_cast<const uint16_t *>(entity+0x64)&128))
  distance=AdmittedDistance(*reinterpret_cast<void **>(entity+0x58),distance);
 orig_RenderBuilding(entity,distance);gDetailAdmission=saved;
}

void WarmTerrain(uint32_t now)
{
 uint64_t pendingBytes=0;
 for(size_t i=0;i<gPending.size();++i){
  Pending &p=gPending[i];if(p.id<0)continue;
  if(!Same(p)||now<p.since||(!p.detail&&Info(p.id)[0x10]==1&&Info(6500+p.txd)[0x10]==1)||now-p.since>=kKeepMs){
   const int id=p.id;Release(i);if(id>=0&&id<6500)gRetry[size_t(id)]=now;continue;
  }
  pendingBytes+=Bytes(p.id)+Bytes(6500+p.txd);
 }
 struct Candidate {const uint8_t *entity=nullptr;float distance=0;};
 std::array<Candidate,kMaxNodes> candidates{};size_t count=0;int visited=0;
 // Stream only ordinary original buildings intersecting the current
 // altitude and a three-second, at-most400m flight corridor. Walk native
 // building/overlap sectors, not every street model in the world. Both this
 // walk and BIG terrain share the same hard node/request/byte budgets.
 const auto *pool=gBuildingPool?*gBuildingPool:nullptr;
 const auto *entries=pool?*reinterpret_cast<uint8_t *const *>(pool):nullptr;
 const auto *flags=pool?*reinterpret_cast<uint8_t *const *>(pool+8):nullptr;
 const int capacity=pool?*reinterpret_cast<const int *>(pool+16):0;
 if(gSectors&&entries&&flags&&capacity>0&&capacity<=50000){
  if(!++gWarmEpoch){gWarmScans.fill(0);++gWarmEpoch;}
  CVector step=EntitySpeed(PedVehicle(api.FindPlayerPed()));
  if(!Finite(step))step={0,0,0};
  step={step.x*150,step.y*150,step.z*150};const float length=std::sqrt(step.x*step.x+step.y*step.y+step.z*step.z);
  if(length>400){step.x*=400/length;step.y*=400/length;step.z*=400/length;}
  const CVector end{gPosition.x+step.x,gPosition.y+step.y,gPosition.z+step.z};
  const auto sector=[](float value,float origin){return std::clamp(int(std::floor((value+origin)/50)),0,79);};
  const int x0=sector(std::min(gPosition.x,end.x)-180,2400),x1=sector(std::max(gPosition.x,end.x)+180,2400),
   y0=sector(std::min(gPosition.y,end.y)-180,2000),y1=sector(std::max(gPosition.y,end.y)+180,2000);
  struct Cell{int index;float distance;};std::array<Cell,400> cells{};size_t cellsCount=0;
  for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
   const float dx=float(x*50-2375)-gPosition.x,dy=float(y*50-1975)-gPosition.y;
   if(cellsCount<cells.size())cells[cellsCount++]={y*80+x,dx*dx+dy*dy};
  }
  std::sort(cells.begin(),cells.begin()+cellsCount,[](const Cell &a,const Cell &b){return a.distance<b.distance;});
  for(size_t c=0;c<cellsCount&&visited<kMaxNodes;++c)for(int list=0;list<2&&visited<kMaxNodes;++list)
   for(PtrNode *node=gSectors[size_t(cells[c].index)*10+size_t(list)].first;node&&visited<kMaxNodes;node=node->next,++visited){
    const auto *entity=static_cast<const uint8_t *>(node->entity);const uintptr_t address=reinterpret_cast<uintptr_t>(entity),base=reinterpret_cast<uintptr_t>(entries);
    if(address<base||(address-base)%0x78||(address-base)/0x78>=size_t(capacity))continue;
    const size_t index=(address-base)/0x78;if((flags[index]&128)||gWarmScans[index]==gWarmEpoch)continue;gWarmScans[index]=gWarmEpoch;
    auto *model=DetailModel(entity);if(!model||
      (model[0x1d]==3&&(!gTime||!gTime(model[0x58],model[0x5c]))))continue;
    CVector lo,hi;if(!DetailBounds(entity,model,lo,hi)||!Corridor(lo,hi,end))continue;
    const int id=EntityModel(entity);
    if(Info(id)[0x10]==1){
     // Keep a ready original through native least-used-model eviction until
     // it has left this corridor for8s. Its lease still counts against the
     // same32-model/16MiB cap and is released immediately on owner handoff.
     for(Pending &p:gPending)if(p.id==id&&p.detail&&Same(p)&&Info(6500+p.txd)[0x10]==1)p.since=now;
     continue;
    }
    candidates[count++]={entity,BoxDistance(gPosition,lo,hi)};
   }
 }
 for(int list=0;list<3&&visited<kMaxNodes;++list)for(PtrNode *node=gBigLists[list].first;node&&visited<kMaxNodes;node=node->next,++visited){
  const auto *entity=static_cast<const uint8_t *>(node->entity);uint8_t *model=BigModel(entity);if(!model)continue;
  const int id=EntityModel(entity);if(Info(id)[0x10]==1)continue;
  const CVector at=EntityPos(entity);if(!Finite(at))continue;
  const float distance=std::sqrt((gPosition.x-at.x)*(gPosition.x-at.x)+(gPosition.y-at.y)*(gPosition.y-at.y)+(gPosition.z-at.z)*(gPosition.z-at.z));
  const float lod=gLargest(model);
  if(!std::isfinite(lod)||lod<300||Distance(entity,model,distance)>=lod+20)continue;
  candidates[count++]={entity,distance};
 }
 std::sort(candidates.begin(),candidates.begin()+count,[](const Candidate &a,const Candidate &b){return a.distance<b.distance;});
 int admitted=0;
 for(size_t i=0;i<count&&admitted<kRequestsPerFrame;++i){
  const auto *entity=candidates[i].entity;const int id=EntityModel(entity);uint8_t *model=BigModel(entity);if(!model)model=DetailModel(entity);if(!model)continue;
  const int txd=*reinterpret_cast<const int16_t *>(model+0x2e);
  if(txd<0||txd>=1385||!Bytes(id)||!Bytes(6500+txd)||
     (gRetry[size_t(id)]&&now>=gRetry[size_t(id)]&&now-gRetry[size_t(id)]<1000))continue;
  bool exists=false;for(const Pending &p:gPending)if(p.id==id){exists=true;break;}if(exists)continue;
  auto free=std::find_if(gPending.begin(),gPending.end(),[](const Pending &p){return p.id<0;});if(free==gPending.end())break;
  const uint64_t bytes=Bytes(id)+Bytes(6500+txd);if(bytes>kPendingBytes||pendingBytes>kPendingBytes-bytes)continue;
  const uint8_t modelFlags=Info(id)[0x11],txdFlags=Info(6500+txd)[0x11];
  Pending p;p.id=id;p.txd=txd;p.model=model;p.stream=gStreaming;p.table=api.modelInfoPtrs;p.since=now;
  p.detail=DetailModel(entity)!=nullptr;
  std::memcpy(p.name.data(),model+8,p.name.size());
  // TXDs and DFFs are often separate IMG reads. Preserve their native flags,
  // retain both through Update's unflagged-request pruning, then return only
  // the keep bits we acquired. There is no synchronous disk load here.
  api.RequestModel(6500+txd,txdFlags|1);api.RequestModel(id,modelFlags|1);
  if(!Same(p))continue;
  p.modelKeep=!(modelFlags&1)&&(Info(id)[0x11]&1);p.txdKeep=!(txdFlags&1)&&(Info(6500+txd)[0x11]&1);
  *free=p;pendingBytes+=bytes;++admitted;
 }
}

float FarFor(void *camera,float value,uintptr_t caller)
{
 return gActive&&camera==MainCamera()&&caller==gFarReturn?std::max(value,gFar):value;
}
float FogFor(void *camera,float value,uintptr_t caller)
{
 return gActive&&camera==MainCamera()&&caller==gFogReturn?gFog:value;
}
__attribute__((noinline)) void *Hook_SetFar(void *camera,float value)
{
 return orig_SetFar(camera,FarFor(camera,value,reinterpret_cast<uintptr_t>(__builtin_return_address(0))));
}
__attribute__((noinline)) void *Hook_SetFog(void *camera,float value)
{
 return orig_SetFog(camera,FogFor(camera,value,reinterpret_cast<uintptr_t>(__builtin_return_address(0))));
}
void Hook_Construct()
{
 gAdmissionCount=0;gBigAdmission=gDetailAdmission=nullptr;
 const uint32_t now=api.timeMs?*api.timeMs:0;
 if(now<gPrevious){ReleaseAll();gRetry.fill(0);}gPrevious=now;
 gActive=gReady&&Eligible(gPosition);
 if(gActive){
  const float rise=std::clamp((gPosition.z-120)/380,0.0f,1.0f);gBlend=rise*rise*(3-2*rise);
  const float nativeFar=std::isfinite(*gNativeFar)&&*gNativeFar>0?*gNativeFar:1000;
  const float wanted=std::min(kMaxClip,std::hypot(gPosition.z,3500.0f)+300);
  gFar=nativeFar+gBlend*(std::max(nativeFar,wanted)-nativeFar);
  const float nativeFog=std::isfinite(*gNativeFog)&&*gNativeFog>=0?*gNativeFog:500;
  gFog=std::clamp(nativeFog+gBlend*(std::max(nativeFog,gPosition.z*.6f+600)-nativeFog),0.0f,gFar-1);
  // Both native consumers matter: ScanWorld reads RW far; IsSphereVisible
  // reads CDraw far. Idle later sets RW far/fog again via verified callers.
  orig_SetFar(MainCamera(),gFar);*gDrawFar=gFar;
  WarmTerrain(now);
 }else{
  ReleaseAll();gBlend=0;
  if(gWasActive&&MainCamera()){
   if(std::isfinite(*gNativeFar)&&*gNativeFar>0){
    if(gGetFar(MainCamera())==gFar)orig_SetFar(MainCamera(),*gNativeFar);
    if(*gDrawFar==gFar)*gDrawFar=*gNativeFar;
   }
   if(std::isfinite(*gNativeFog)&&*gNativeFog>=0&&gGetFog(MainCamera())==gFog)orig_SetFog(MainCamera(),*gNativeFog);
  }
 }
 orig_Construct();
 if(gActive&&(*gLevel==1||*gLevel==2)&&(!api.frontEndMenuManager||api.frontEndMenuManager[0x72d]!=2)){
  // Native ScanWorld already scans current+shared lists. The opposite
  // island uses the same native visibility/occlusion/materialization path.
  // Refuse a malformed/cyclic list before handing it to the native walker.
  PtrList &other=gBigLists[*gLevel==1?2:1];int count=0;
  for(PtrNode *node=other.first;node&&count<=kMaxNodes;node=node->next)++count;
  if(count<=kMaxNodes)gScanBig(other);
 }
 gWasActive=gActive;
}

const char *const kHorizonVertex=R"(
uniform vec4 u_xform;
VSIN(ATTRIB_POS) vec4 in_pos;
VSOUT vec3 v_airNear;
VSOUT vec3 v_airFar;
void main(){
 gl_Position=in_pos;gl_Position.w=1.0;
 gl_Position.xy=gl_Position.xy*u_xform.xy+u_xform.zw;
 mat4 back=inverse(u_proj*u_view);
 vec4 a=back*vec4(gl_Position.xy,-1.0,1.0),b=back*vec4(gl_Position.xy,1.0,1.0);
 v_airNear=a.xyz/a.w;v_airFar=b.xyz/b.w;
}
)";
const char *const kHorizonFragment=R"(
uniform vec4 u_airHorizon; // native horizon RGB, transition strength
uniform vec4 u_airWater;   // native ocean RGB, coverage radius
uniform vec4 u_airCamera;
FSIN vec3 v_airNear;
FSIN vec3 v_airFar;
void main(){
 vec3 ray=normalize(v_airFar-v_airNear);
 if(ray.z>=-0.00001)discard;
 float t=max(0.0,-v_airNear.z/ray.z);
 vec2 at=v_airNear.xy+ray.xy*t;
 float distance=length(at-u_airCamera.xy);
 float haze=smoothstep(600.0,u_airWater.w,distance);
 vec3 colour=mix(u_airWater.rgb,u_airHorizon.rgb,haze);
 FRAGCOLOR(vec4(colour,u_airHorizon.a));
}
)";
void *gHorizonShader;
int gHorizonUniform,gWaterUniform,gCameraUniform;
float *gWaterColour;
bool gHorizonTried;
Im2DCanvas gCanvas;
void Hook_Horizon()
{
 if(!gActive){orig_Horizon();return;}
 if(!gHorizonTried&&shaders::Ready()){
  gHorizonTried=true;
  gHorizonUniform=shaders::Uniform("u_airHorizon");gWaterUniform=shaders::Uniform("u_airWater");gCameraUniform=shaders::Uniform("u_airCamera");
  gHorizonShader=shaders::Create("flight ocean horizon",kHorizonVertex,kHorizonFragment);
 }
 if(!gHorizonShader){orig_Horizon();return;}
 if(gBlend<.999f)orig_Horizon();
 const shaders::Sky sky=shaders::CurrentSky();
 shaders::Set(gHorizonUniform,sky.bottom.x,sky.bottom.y,sky.bottom.z,gBlend);
 shaders::Set(gWaterUniform,std::clamp(gWaterColour[0]/255,0.0f,1.0f),std::clamp(gWaterColour[1]/255,0.0f,1.0f),std::clamp(gWaterColour[2]/255,0.0f,1.0f),kCoverage);
 shaders::Set(gCameraUniform,gPosition.x,gPosition.y,gPosition.z);
 const shaders::SavedStates saved;SetHudRenderStates();
 shaders::OverrideIm2D(gHorizonShader);gCanvas.SetTexture(nullptr);
 const float w=float(ScreenWidth()),h=float(ScreenHeight());
 gCanvas.Quad({0,0},{w,0},{w,h},{0,h},{255,255,255,255});gCanvas.Flush();shaders::OverrideIm2D(nullptr);
}

template<class T>bool Bind(T &out,const char *name)
{
 out=reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY,name));if(!out)Log("airworld: missing %s",name);return out!=nullptr;
}
}
bool OwnsFlightView(const uint8_t *vehicle)
{
 CVector position;return vehicle&&gReady&&Eligible(position,vehicle);
}
bool Install()
{
 const auto base=reinterpret_cast<uintptr_t>(hooks::ImageBase());
 const bool bound=base&&hooks::IsCallTo(0x141fd0,kFarSymbol)&&hooks::IsCallTo(0x141fe4,kFogSymbol)&&
  Bind(gArea,"_ZN5CGame8currAreaE")&&Bind(gLevel,"_ZN5CGame9currLevelE")&&Bind(gCutscene,"_ZN12CCutsceneMgr10ms_runningE")&&
  Bind(gMission,"_ZN11CTheScripts18IsPlayerOnAMissionEv")&&Bind(gOutside,"_ZN5CGame25CanSeeOutSideFromCurrAreaEv")&&
  Bind(gTime,"_ZN6CClock16GetIsTimeInRangeEhh")&&Bind(gSectors,"_ZN6CWorld11ms_aSectorsE")&&
  Bind(gBuildingPool,"_ZN6CPools16ms_pBuildingPoolE")&&Bind(gFrameCounter,"_ZN6CTimer14m_FrameCounterE")&&
  Bind(gAtomicModel,"_ZN18CVisibilityPlugins18GetAtomicModelInfoEPN2rw6AtomicE")&&Bind(gVehiclePool,"_ZN6CPools15ms_pVehiclePoolE")&&Bind(gBigLists,"_ZN6CWorld19ms_bigBuildingsListE")&&Bind(gRendererCamera,"_ZN9CRenderer20ms_vecCameraPositionE")&&
  Bind(gScanBig,"_ZN9CRenderer19ScanBigBuildingListER8CPtrList")&&Bind(gStreaming,"_ZN10CStreaming16ms_aInfoForModelE")&&
  Bind(gDeletable,"_ZN10CStreaming19SetModelIsDeletableEi")&&Bind(gLargest,"_ZN16CSimpleModelInfo21GetLargestLodDistanceEv")&&
  Bind(gNativeFar,"_ZN10CTimeCycle17m_fCurrentFarClipE")&&Bind(gNativeFog,"_ZN10CTimeCycle18m_fCurrentFogStartE")&&
  Bind(gDrawFar,"_ZN5CDraw12ms_fFarClipZE")&&Bind(gGetFar,"_Z23RwCameraGetFarClipPlanePKN2rw6CameraE")&&
  Bind(gGetFog,"_Z22RwCameraGetFogDistancePKN2rw6CameraE")&&Bind(gWaterColour,"_ZN10CTimeCycle18m_fCurrentWaterRedE")&&
  api.theCamera&&api.FindPlayerPed&&api.timeMs&&api.modelInfoPtrs&&api.RequestModel;
 if(!bound){Log("airworld: native bindings/callers unavailable; flight terrain remains native");return false;}
 gFarReturn=reinterpret_cast<uintptr_t>(hooks::ReturnAddress(0x141fd0));gFogReturn=reinterpret_cast<uintptr_t>(hooks::ReturnAddress(0x141fe4));
 orig_EntityVisibility=reinterpret_cast<decltype(orig_EntityVisibility)>(hooks::Chain("_ZN9CRenderer21SetupEntityVisibilityEP7CEntity",reinterpret_cast<void *>(Hook_EntityVisibility)));
 gReady=orig_EntityVisibility&&hooks::Install("_ZN9CRenderer19ConstructRenderListEv",Hook_Construct,orig_Construct)&&
  hooks::Install("_ZN9CRenderer26SetupBigBuildingVisibilityEP7CEntity",Hook_BigVisibility,orig_BigVisibility)&&
  hooks::Install("_ZN16CSimpleModelInfo21GetAtomicFromDistanceEf",Hook_DetailAtomic,orig_DetailAtomic)&&
  hooks::Install("_ZN16CSimpleModelInfo26GetFirstAtomicFromDistanceEf",Hook_Atomic,orig_Atomic)&&
  hooks::Install(kFarSymbol,Hook_SetFar,orig_SetFar)&&hooks::Install(kFogSymbol,Hook_SetFog,orig_SetFog)&&
  hooks::Install("_ZN7CClouds13RenderHorizonEv",Hook_Horizon,orig_Horizon)&&
  hooks::Install("_ZN18CVisibilityPlugins18RenderFadingAtomicEPN2rw6AtomicEf",Hook_FadingAtomic,orig_FadingAtomic)&&
  hooks::Install("_ZN9CRenderer17RenderOneBuildingEP7CEntityf",Hook_RenderBuilding,orig_RenderBuilding);
 if(!gReady)Log("airworld: hooks unavailable; flight terrain remains native");
 return gReady;
}
}

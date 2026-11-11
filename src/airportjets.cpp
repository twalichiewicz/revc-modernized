#include "airportjets.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "planeflight.h"
#include "airportjetmodel.h"
#include "airportjetcollision.h"
#include "jetboarding.h"
#include "flightinput.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <limits>
#include <strings.h>
#include <type_traits>
#include <vector>

using namespace game;
namespace airportjets {
namespace {
constexpr int kSource=636,kCount=7,kMaxSpheres=32;
constexpr size_t kVehicleStride=0x6f0,kBuildingStride=0x78;
// Original mesh has no cabin dummies. These are measured inside its +Y
// cockpit, ahead of the wings; native pilot fit still requires visual QA.
constexpr CVector kSeat{-.32f,5.0f,-.65f},kDoor{-1.6f,3.7f,-.65f};
struct Triangle {uint16_t v[3],material;};
struct Native {
 void *(*allocate)(size_t);void (*construct)(uint8_t *,int,uint8_t),(*destroy)(uint8_t *);
 void (*add)(uint8_t *),(*remove)(uint8_t *),(*physical)(uint8_t *);
 void *(*addModel)(int);void (*addRef)(uint8_t *),(*removeRef)(uint8_t *);
 bool (*cd)(uint8_t *,uint32_t &,uint32_t &);void (*setCd)(uint8_t *,uint32_t,uint32_t),(*deletable)(int);
 void (*warp)(uint8_t *,uint8_t *),(*out)(void *,void *),(*teleport)(uint8_t *,CVector);
 void (*matrix)(void *),(*frame)(void *);bool (*mission)(),(*exit)(uint8_t *);int (*pedRef)(uint8_t *);
 void (*damage)(uint8_t *,uint8_t *,int,float,CVector),(*kill)(uint8_t *);
 bool (*explosion)(uint8_t *,uint8_t *,int,const CVector &,uint32_t,bool);
 void *(*each)(void *,void *(*)(void *,void *),void *),*(*geometry)(const void *),*(*morph)(const void *,int);
 int (*verticesCount)(const void *),(*triangleCount)(const void *);const CVector *(*vertices)(const void *);const Triangle *(*triangles)(const void *);
 void *(*atomicFrame)(const void *);const float *(*ltm)(void *);const char *(*name)(void *);
 int16_t (*roll)(uint8_t *),(*pitch)(uint8_t *),(*gunPitch)(uint8_t *),(*gunYaw)(uint8_t *);bool (*left)(uint8_t *),(*right)(uint8_t *),(*horn)(uint8_t *);
 void (*animTime)(uint8_t *,float);
} n{};
uint8_t **gBuildings,**gVehicles,*gStreaming,*gStore;int32_t *gArea;bool *gCutscene;
bool gReady;int gModel=-1,gTxd=-1;uint8_t *gInfo,*gSourceInfo;bool gSourcePinned,gShapeReady;
uint8_t *gCockpitInfo;int gCockpitTxd=-1;bool gCockpitPinned;
uint32_t gPrevious,gDiscoveryAt;bool gDiscoveryScanned;
struct Pin {int id=-1;bool owned=false;};Pin gModelPin,gTxdPin,gCockpitModelPin,gCockpitTxdPin;
alignas(16) uint8_t gCollision[0x68]{},gSpheres[kMaxSpheres*20]{},gBoxes[2*28]{};
void (*originalControl)(uint8_t *),(*originalSeat)(uint8_t *),(*originalShutdown)(),(*originalRestart)();
void (*originalWorldShutdown)();
void (*originalPreRender)(uint8_t *);
void (*originalPlayerControl)(uint8_t *);
void (*originalEnter)(uint8_t *,uint8_t *,uint32_t),(*originalExit)(uint8_t *,uint8_t *,uint32_t);
void (*originalDamage)(uint8_t *,uint8_t *,int,float,CVector);
struct Anchor {
 uint8_t *sourceEntity=nullptr,*sourceEntries=nullptr,*sourceFlags=nullptr;
 int source=-1;uint8_t generation=0;CVector basis[4]{};bool hidden=false;
};
struct Jet {
 uint8_t *entity=nullptr,*vtable=nullptr;int ref=-1;
 uint32_t boarded=0,burning=0,doorHold=0;float doorOpen=0,gearDown=1;
 bool gearTargetDown=true,manualGear=false,hornHeld=false;int gearDriverRef=-1;
 uint8_t *doorPed=nullptr;int doorPedRef=-1;int32_t doorState=0,doorObjective=0;
 enum class Door {None,Board,Leave} door=Door::None;
 enum class Boarding {Approach,Reach,Retreat,Lower,Step,Crouch,Walk,Sit} boarding=Boarding::Approach;
 enum class Failure {None,Owner,Context,Animation,Clock,ClockReset,ClockJump,ClockStalled,Timeout,PositionStep,Obstruction,NativeMove,Seat,Pose,Button} failure=Failure::None;
 uint64_t boardingSerial=0;uint32_t boardingBegan=0;
 uint32_t clockAdvanced=0;float stageTime=0,walkDistance=0,lastStep=0,lastBlend=0,lastDuration=0,lastSpeed=0,lastDelta=0;int lastCallback=0;uint16_t lastFlags=0;bool abortLogged=false;
 uint8_t *boardingClump=nullptr,*boardingAssoc=nullptr,*boardingHierarchy=nullptr;
 int boardingAnim=-1,boardingSavedObjective=0;float boardingTime=0;bool boardingCollision=false,boardingSetObjective=false;
 CVector boardingAt{},boardingOutside{},boardingHandle{},boardingBodyAt{};
};
std::array<Jet,kCount> gJets{};
std::array<Anchor,kCount> gAnchors{};
uint64_t gBoardingSerial=0;
const char *BoardPhase(Jet::Boarding phase){switch(phase){case Jet::Boarding::Approach:return "approach";case Jet::Boarding::Reach:return "reach";case Jet::Boarding::Retreat:return "retreat";case Jet::Boarding::Lower:return "door";case Jet::Boarding::Step:return "steps";case Jet::Boarding::Crouch:return "crouch";case Jet::Boarding::Walk:return "walk";case Jet::Boarding::Sit:return "sit";}return "unknown";}
const char *BoardFailure(Jet::Failure why){switch(why){case Jet::Failure::None:return "cancelled";case Jet::Failure::Owner:return "owner";case Jet::Failure::Context:return "eligibility";case Jet::Failure::Animation:return "association";case Jet::Failure::Clock:return "clock-invalid";case Jet::Failure::ClockReset:return "clock-reset";case Jet::Failure::ClockJump:return "clock-jump";case Jet::Failure::ClockStalled:return "clock-stalled";case Jet::Failure::Timeout:return "timeout";case Jet::Failure::PositionStep:return "position-step";case Jet::Failure::Obstruction:return "obstruction";case Jet::Failure::NativeMove:return "native-move";case Jet::Failure::Seat:return "seat-binding";case Jet::Failure::Pose:return "pose-ownership";case Jet::Failure::Button:return "entry-button";}return "unknown";}
bool BoardFail(Jet &jet,uint64_t serial,Jet::Failure why){if(jet.boardingSerial==serial&&jet.failure==Jet::Failure::None)jet.failure=why;return false;}
void BoardStage(Jet &jet,Jet::Boarding stage,uint32_t now){
 jet.boarding=stage;jet.stageTime=0;jet.clockAdvanced=now;
 Log("airport jets: boarding %llu phase %s vehicle%d elapsed%u",static_cast<unsigned long long>(jet.boardingSerial),BoardPhase(stage),jet.ref,now-jet.boardingBegan);
}
CVector Add(CVector a,CVector b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
CVector Sub(CVector a,CVector b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
CVector Mul(CVector a,float s){return {a.x*s,a.y*s,a.z*s};}
float Dot(CVector a,CVector b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float Length(CVector a){return std::sqrt(Dot(a,a));}
bool Finite(CVector v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
CVector &Vector(uint8_t *p,size_t at){return *reinterpret_cast<CVector *>(p+at);}
const CVector &Vector(const uint8_t *p,size_t at){return *reinterpret_cast<const CVector *>(p+at);}
template<class T> bool Bind(T &value,const char *name){value=reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY,name));return value!=nullptr;}
struct Pool {uint8_t *entries=nullptr,*flags=nullptr;int count=0;};
Pool GetPool(uint8_t **binding,int max){
 const uint8_t *p=binding?*binding:nullptr;if(!p)return {};
 Pool pool{*reinterpret_cast<uint8_t *const *>(p),*reinterpret_cast<uint8_t *const *>(p+8),*reinterpret_cast<const int *>(p+16)};
 return pool.entries&&pool.flags&&pool.count>0&&pool.count<=max?pool:Pool{};
}
uint8_t *Stream(int id){return gStreaming+size_t(id)*32;}
bool ModelValid(){return gModel>=240&&gModel<=249&&api.modelInfoPtrs&&api.modelInfoPtrs[gModel]==gInfo&&gInfo&&gInfo[0x1d]==6&&
    *reinterpret_cast<void **>(gInfo+0x20)==gCollision&&*reinterpret_cast<int32_t *>(gInfo+0x4c)==4&&
    *reinterpret_cast<int16_t *>(gInfo+0x2e)==gTxd&&!std::memcmp(gInfo+8,"airportjet",11);}
bool CockpitValid(){return api.modelInfoPtrs&&gCockpitInfo&&api.modelInfoPtrs[227]==gCockpitInfo&&gCockpitInfo[0x1d]==6&&
    !std::memcmp(gCockpitInfo+8,"polmav",7)&&*reinterpret_cast<int16_t *>(gCockpitInfo+0x2e)==gCockpitTxd;}
uint8_t *Live(const Jet &jet){
 uint8_t *v=jet.ref>=0&&api.PoolsGetVehicle?api.PoolsGetVehicle(jet.ref):nullptr;
 return ModelValid()&&v&&v==jet.entity&&*reinterpret_cast<uint8_t *const *>(v)==jet.vtable&&EntityModel(v)==gModel&&
     *reinterpret_cast<const int32_t *>(v+0x348)==4&&v[0x294]==1?v:nullptr;
}
Jet *Find(const uint8_t *v){for(auto &jet:gJets)if(v&&Live(jet)==v)return &jet;return nullptr;}
uint8_t *Source(const Anchor &jet){
 const Pool pool=GetPool(gBuildings,50000);if(jet.source<0||jet.source>=pool.count||pool.entries!=jet.sourceEntries||pool.flags!=jet.sourceFlags||(pool.flags[jet.source]&128)||pool.flags[jet.source]!=jet.generation)return nullptr;
 uint8_t *source=pool.entries+size_t(jet.source)*kBuildingStride;
 if(source!=jet.sourceEntity||EntityModel(source)!=kSource||(EntityFlags(source)&7)!=1)return nullptr;
 for(int i=0;i<4;++i)if(std::memcmp(&Vector(source,8+size_t(i)*16),jet.basis+i,sizeof(CVector)))return nullptr;
 return source;
}
int Reference(uint8_t *v){
 const Pool pool=GetPool(gVehicles,10000);if(!pool.entries)return -1;
 const uintptr_t offset=reinterpret_cast<uintptr_t>(v)-reinterpret_cast<uintptr_t>(pool.entries);
 if(offset%kVehicleStride||offset/kVehicleStride>=size_t(pool.count))return -1;
 const size_t i=offset/kVehicleStride;if(pool.flags[i]&128)return -1;
 const int ref=int(i*256)|pool.flags[i];return api.PoolsGetVehicle(ref)==v?ref:-1;
}
void ReleasePins(){
 if(ModelValid())for(Pin *pin:{&gModelPin,&gTxdPin})if(pin->owned&&pin->id>=0)n.deletable(pin->id);
 if(CockpitValid())for(Pin *pin:{&gCockpitModelPin,&gCockpitTxdPin})if(pin->owned&&pin->id>=0)n.deletable(pin->id);
 gModelPin={};gTxdPin={};gCockpitModelPin={};gCockpitTxdPin={};
}
void *CockpitClump(){
 if(!gCockpitInfo){auto *info=api.modelInfoPtrs[227];if(!info||info[0x1d]!=6||std::memcmp(info+8,"polmav",7))return nullptr;const int txd=*reinterpret_cast<int16_t *>(info+0x2e);if(txd<0||txd>=1385)return nullptr;gCockpitInfo=info;gCockpitTxd=txd;}
 if(!CockpitValid())return nullptr;
 void *clump=*reinterpret_cast<void **>(gCockpitInfo+0x30);
 if(Stream(227)[0x10]==1&&clump){
  // Native CBaseModelInfo::AddRef also retains its TXD. Keep this source
  // until model shutdown, after every owned actor and prepared template use.
  if(!gCockpitPinned){n.addRef(gCockpitInfo);gCockpitPinned=true;}
  for(Pin *pin:{&gCockpitModelPin,&gCockpitTxdPin}){if(pin->owned)n.deletable(pin->id);*pin={};}return clump;
 }
 const int txdId=6500+gCockpitTxd;const bool oldModel=(Stream(227)[0x11]&1)!=0,oldTxd=(Stream(txdId)[0x11]&1)!=0;
 api.RequestModel(txdId,Stream(txdId)[0x11]|1);api.RequestModel(227,Stream(227)[0x11]|1);
 if(!oldModel&&(Stream(227)[0x11]&1))gCockpitModelPin={227,true};if(!oldTxd&&(Stream(txdId)[0x11]&1))gCockpitTxdPin={txdId,true};return nullptr;
}
void Request(){
 if(!ModelValid())return;
 const int txd=*reinterpret_cast<int16_t *>(gInfo+0x2e);if(txd<0||txd>=1385)return;
 const bool oldModel=(Stream(gModel)[0x11]&1)!=0,oldTxd=(Stream(6500+txd)[0x11]&1)!=0;
 // Native state0 RequestModel assigns flags, including recursively requested
 // TXDs. Admit the dependency first and preserve its existing ownership bits.
 api.RequestModel(6500+txd,Stream(6500+txd)[0x11]|1);
 api.RequestModel(gModel,Stream(gModel)[0x11]|1);
 if(!oldModel&&(Stream(gModel)[0x11]&1))gModelPin={gModel,true};
 if(!oldTxd&&(Stream(6500+txd)[0x11]&1))gTxdPin={6500+txd,true};
}
bool RegisterModel(){
 if(ModelValid())return true;
 if(gInfo)return false; // replaced registry belongs to its current owner
 auto *source=api.modelInfoPtrs[kSource],*dodo=api.modelInfoPtrs[181];
 if(!source||source[0x1d]!=1||!dodo||dodo[0x1d]!=6||!(*reinterpret_cast<void **>(source+0x20)))return false;
 const int txd=*reinterpret_cast<int16_t *>(source+0x2e),handling=*reinterpret_cast<int16_t *>(dodo+0x56);
 if(txd<0||txd>=1385||handling<0||handling>=110||*reinterpret_cast<int32_t *>(dodo+0x4c)!=4)return false;
 const int count=*reinterpret_cast<int32_t *>(gStore);if(count<0||count>=110)return false;
 uint32_t position=0,size=0;if(!n.cd(Stream(kSource),position,size)||!size)return false;
 int model=-1;for(int i=240;i<=249;++i)if(!api.modelInfoPtrs[i]&&Stream(i)[0x10]==0&&!*reinterpret_cast<uint64_t *>(Stream(i))&&
     !*reinterpret_cast<uint64_t *>(Stream(i)+8)&&!*reinterpret_cast<uint32_t *>(Stream(i)+0x18)){model=i;break;}
 if(model<0)return false;
 auto *info=static_cast<uint8_t *>(n.addModel(model));if(!info||info[0x1d]!=6||api.modelInfoPtrs[model]!=info)return false;
 std::memset(info+8,0,21);std::memcpy(info+8,"airportjet",10);
 std::memset(info+0x42,0,10);std::memcpy(info+0x42,"VCJET",5);*reinterpret_cast<int32_t *>(info+0x4c)=4;
 *reinterpret_cast<float *>(info+0x50)=1;
 *reinterpret_cast<int16_t *>(info+0x54)=-1; // no separate ambient LOD model
 *reinterpret_cast<int16_t *>(info+0x56)=*reinterpret_cast<int16_t *>(dodo+0x56);
 info[0x59]=0xff;info[0x5a]=0; // not an automobile traffic class
 std::memset(info+0x60,0,5*sizeof(CVector));
 *reinterpret_cast<int16_t *>(info+0x2e)=*reinterpret_cast<int16_t *>(source+0x2e);
 *reinterpret_cast<void **>(info+0x20)=gCollision;info[0x1f]=0;
 n.setCd(Stream(model),position,size); // native loader reads the original Learjet DFF independently
 *reinterpret_cast<int16_t *>(Stream(model)+0x12)=-1; // alias has no consecutive archive-model chain
 *reinterpret_cast<int16_t *>(info+0x5c)=0;
 gModel=model;gTxd=txd;gInfo=info;gSourceInfo=source;n.addRef(source);gSourcePinned=true;
 Log("airport jets: registered native vehicle model%d from original Learjet636",model);return true;
}
bool Shape(){
 if(gShapeReady)return true;
 const auto *col=*reinterpret_cast<uint8_t **>(gSourceInfo+0x20);
 airportjetcollision::Input input{n.each,n.geometry,n.morph,n.verticesCount,n.triangleCount,n.vertices,
  reinterpret_cast<const airportjetcollision::Triangle *(*)(const void *)>(n.triangles),n.atomicFrame,n.ltm,n.name};
 std::vector<CVector> points;
 if(!airportjetcollision::Samples(*reinterpret_cast<void **>(gInfo+0x30),input,points)||
    !airportjetcollision::Build(points,col,gCollision,gSpheres,gBoxes))return false;
 gShapeReady=true;Log("airport jets: actual Learjet structural collider ready (%d spheres)",kMaxSpheres);return true;
}
CVector At(const uint8_t *v,CVector p){return Add(Vector(v,0x38),Add(Add(Mul(Vector(v,8),p.x),Mul(Vector(v,0x18),p.y)),Mul(Vector(v,0x28),p.z)));}
void Position(uint8_t *v,CVector at){Vector(v,0x38)=at;n.matrix(v+8);n.frame(v);}
bool Ground(CVector at,float &z,const uint8_t *self=nullptr){alignas(16) uint8_t point[48]{};at.z+=5;const float to=at.z-25;
 // Native ProcessVerticalLineSectorList0x12e38c does not use pIgnoreEntity.
 // Keep querying below only this scenery's returned surfaces; foreign roofs
 // and other obstructions still participate in every native nearest-hit ray.
 for(int hit=0;hit<16;++hit){uint8_t *entity=nullptr;
  if(!api.ProcessVerticalLine(at,to,point,entity,true,false,false,false,false,false,nullptr)||!Finite(Vector(point,0)))return false;
  const CVector contact=Vector(point,0);if(contact.z>at.z+.001f||contact.z<to)return false;
  if(self&&entity==self){at.z=std::nextafter(std::min(at.z,contact.z),-std::numeric_limits<float>::infinity());if(at.z<=to)return false;continue;}
  if(!std::isfinite(Vector(point,0x10).z)||Vector(point,0x10).z<.7f)return false;
  z=contact.z;return true;
 }return false;
}
struct Ignore {uint8_t *saved;explicit Ignore(uint8_t *v):saved(*api.ignoreEntity){*api.ignoreEntity=v;}~Ignore(){*api.ignoreEntity=saved;}};
bool Parked(uint8_t *v){float floor=0;return Vector(v,0x28).z>.7f&&Length(Vector(v,0x84))*50<3&&Ground(EntityPos(v),floor)&&std::fabs(EntityPos(v).z-floor+Vector(gCollision,0x10).z)<.5f;}
bool Clear(uint8_t *v,CVector from,CVector to){Ignore ignore(v);return api.GetIsLineOfSightClear(from,to,true,true,false,true,false,false,false);}
bool Occupied(uint8_t *v){for(int i=0;i<9;++i)if(*reinterpret_cast<uint8_t **>(v+0x218+size_t(i)*8))return true;return false;}
void Seat(uint8_t *ped){
 const int ref=n.pedRef(ped);originalSeat(ped);
 if(ref<0||api.PoolsGetPed(ref)!=ped)return;
 uint8_t *v=PedInVehicle(ped)?PedVehicle(ped):nullptr;
 if(ped==api.FindPlayerPed()&&Find(v)&&*reinterpret_cast<uint8_t **>(v+0x218)==ped){
  Vector(ped,0x38)=At(v,kSeat);n.matrix(ped+8);
  if(api.PoolsGetPed(ref)==ped&&ped==api.FindPlayerPed()&&Find(v)&&PedInVehicle(ped)&&PedVehicle(ped)==v)n.frame(ped);
 }
}
bool CanUseDoor(uint8_t *ped,uint8_t *v,bool requireEnabled=true){uint8_t *pad=api.GetPad(0);
 return ped&&ped==api.FindPlayerPed()&&api.PoolsGetPed(n.pedRef(ped))==ped&&pad&&!PadControlsDisabled(pad)&&(!requireEnabled||gConfig.planes)&&*gArea==0&&Health(ped)>0&&!Wrecked(v)&&!n.mission()&&!*gCutscene;
}
bool CanBoard(uint8_t *ped,uint8_t *v,uint32_t now){Jet *jet=Find(v);
 return jet&&CanUseDoor(ped,v)&&now-jet->boarded>=300&&!PedInVehicle(ped)&&
     (PedState(ped)==kPedIdle||PedObjective(ped)==18)&&!*reinterpret_cast<uint8_t **>(v+0x218)&&Parked(v)&&Length(Sub(EntityPos(ped),At(v,kDoor)))<=4&&Clear(v,EntityPos(ped),At(v,kSeat));
}
bool ExitPoint(uint8_t *ped,uint8_t *v,CVector &out){
 if(!Find(v)||!CanUseDoor(ped,v)||!PedInVehicle(ped)||PedVehicle(ped)!=v||*reinterpret_cast<uint8_t **>(v+0x218)!=ped||!Parked(v))return false;
 for(float side:{-1.6f}){CVector at=At(v,{side,kDoor.y,kDoor.z});float floor=0;if(!Ground(at,floor))continue;at.z=floor+1.04f;Ignore ignore(v);
  if(api.TestSphereAgainstWorld(at,.4f,v,true,true,true,true,false,false)||!Clear(v,At(v,kSeat),at))continue;
  out=at;return true;
 }return false;
}
bool Board(uint8_t *ped,uint8_t *v,uint32_t now){
 Jet *jet=Find(v);if(!jet||!CanBoard(ped,v,now)||(jet->door!=Jet::Door::None&&(jet->door!=Jet::Door::Board||jet->doorPed!=ped)))return false;
 if(jet->door==Jet::Door::None){
  jet->doorPedRef=n.pedRef(ped);jet->doorState=PedState(ped);jet->doorObjective=PedObjective(ped);
  jet->boardingSerial=++gBoardingSerial;jet->boardingBegan=now;jet->boarding=Jet::Boarding::Approach;
  jet->failure=Jet::Failure::None;jet->stageTime=jet->walkDistance=jet->lastStep=jet->lastBlend=0;jet->abortLogged=false;jet->clockAdvanced=now;
  jet->boardingSavedObjective=jet->doorObjective;jet->boardingSetObjective=false;
  jet->boardingClump=EntityClump(ped);jet->boardingAt=jet->boardingBodyAt=EntityPos(ped);
  jet->boardingCollision=(EntityFlags(ped)&kEntityUsesCollision)!=0;
  jet->boardingOutside=At(v,{-2.25f,3.7f,0});jet->boardingHandle=At(v,{-1.30f,3.58f,0});float ground=0;
  if(!jet->boardingClump||!Ground(jet->boardingOutside,ground)||Find(v)!=jet||api.PoolsGetPed(jet->doorPedRef)!=ped)return false;
  jet->boardingOutside.z=jet->boardingHandle.z=ground+1.04f;
  EntityFlags(ped)&=~kEntityUsesCollision;EntitySpeed(ped)={};
 }
 jet->door=Jet::Door::Board;jet->doorPed=ped;jet->doorHold=0;
 if(jet->boardingBegan==now)BoardStage(*jet,Jet::Boarding::Approach,now);return true;
}
bool Leave(uint8_t *ped,uint8_t *v){
 Jet *jet=Find(v);CVector at{};if(!jet||!ExitPoint(ped,v,at)||(jet->door!=Jet::Door::None&&(jet->door!=Jet::Door::Leave||jet->doorPed!=ped)))return false;
 if(jet->door==Jet::Door::None){jet->doorPedRef=n.pedRef(ped);jet->doorState=PedState(ped);jet->doorObjective=PedObjective(ped);}
 jet->door=Jet::Door::Leave;jet->doorPed=ped;jet->doorHold=0;return true;
}
int Driver(uint8_t *v){
 uint8_t *player=api.FindPlayerPed();if(!player)return -1;const int ref=n.pedRef(player);
 return ref>=0&&api.PoolsGetPed(ref)==player&&Health(player)>0&&!Wrecked(v)&&
     PedInVehicle(player)&&PedVehicle(player)==v&&*reinterpret_cast<uint8_t **>(v+0x218)==player&&(v[0x60]>>3)==0?ref:-1;
}
bool Pilot(uint8_t *v,uint8_t *&pad){
 uint8_t *player=api.FindPlayerPed();pad=api.GetPad(0);
 return player&&pad&&!PadControlsDisabled(pad)&&gConfig.planes&&*gArea==0&&!n.mission()&&!*gCutscene&&Health(player)>0&&!Wrecked(v)&&
     PedInVehicle(player)&&PedVehicle(player)==v&&*reinterpret_cast<uint8_t **>(v+0x218)==player&&(v[0x60]>>3)==0;
}
bool GearClearance(uint8_t *v,float &height){
 const CVector at=EntityPos(v);if(!Finite(at))return false;
 alignas(16) uint8_t point[48]{};uint8_t *entity=nullptr;float floor=0;bool found=false;
 if(api.ProcessVerticalLine(at,at.z-2000,point,entity,true,false,false,false,false,false,nullptr)&&Finite(Vector(point,0))){floor=Vector(point,0).z;found=floor<=at.z;}
 float water=0;if(api.GetWaterLevel&&api.GetWaterLevel(at.x,at.y,at.z,&water,true)&&std::isfinite(water)&&water<=at.z){floor=found?std::max(floor,water):water;found=true;}
 height=at.z+jetwheels::kTireBottom-floor;return found&&std::isfinite(height);
}
void UpdateGear(Jet &jet,uint8_t *v,float dt){
 // Disabled input and a configuration toggle retain an occupied owned actor
 // for landing, just as the force adapter does. They suppress manual input,
 // rather than freezing automatic approach safety or the gear mechanism.
 const int driver=Driver(v);
 if(driver<0){jet.manualGear=false;jet.hornHeld=true;jet.gearDriverRef=-1;return;}
 if(jet.gearDriverRef!=driver){jet.gearDriverRef=driver;jet.manualGear=false;jet.hornHeld=true;}
 uint8_t *pad=api.GetPad(0);
 const bool automatic=*gArea==0&&!n.mission()&&!*gCutscene;
 const bool manual=automatic&&gConfig.planes&&pad&&!PadControlsDisabled(pad);
 const bool horn=manual&&n.horn(pad);
 // Native GetHorn itself returnsfalse while disabled. Require a release
 // after every invalid input context before accepting another manual edge.
 if(!manual){jet.manualGear=false;jet.hornHeld=true;}
 if(automatic){
  float clearance=0;const bool known=GearClearance(v,clearance);if(Find(v)!=&jet)return;
  const CVector velocity=Vector(v,0x84),forward=Vector(v,0x18),up=Vector(v,0x28);
  if(Finite(velocity)&&Finite(forward)&&Finite(up)){
   const bool safe=known&&clearance>2&&up.z>.5f;
   if(known&&clearance<=2){jet.gearTargetDown=true;jet.manualGear=false;}
   else if(manual&&horn&&!jet.hornHeld&&safe){jet.gearTargetDown=!jet.gearTargetDown;jet.manualGear=true;}
   if(!jet.manualGear){
    // Account for remaining travel at the actual closing speed. Hysteresis
    // keeps leveling a low approach from immediately retracting again.
    const float sink=std::max(0.0f,-velocity.z*50),remaining=(1-jet.gearDown)*2.4f;
    const float approach=std::clamp(sink*remaining+std::max(8.0f,.75f*sink),18.0f,180.0f);
    if(known&&clearance<approach&&velocity.z<=.04f)jet.gearTargetDown=true;
    else if(safe&&Dot(velocity,forward)*50>22&&((clearance>30&&velocity.z>=.04f)||(clearance>60&&velocity.z>=-.02f)))jet.gearTargetDown=false;
   }
  }
 }
 if(manual)jet.hornHeld=horn;
 // Mission/cutscene takeover cannot issue new commands, but a mechanism
 // already traveling to its commanded stop can finish while ownership holds.
 const float target=jet.gearTargetDown?1.0f:0.0f;
 jet.gearDown+=std::clamp(target-jet.gearDown,-dt/2.4f,dt/2.4f);
}
// Native associations are 0x58 bytes in macVC1.0.5217. Poll their clocks:
// a finish callback also runs on deletion, and must never retain a CPed/Jet.
bool BoardingPed(const Jet &jet){return jet.doorPed&&jet.doorPedRef>=0&&api.PoolsGetPed(jet.doorPedRef)==jet.doorPed;}
void ReleaseBoardingAnim(Jet &jet,float fade=8){
 const auto serial=jet.boardingSerial;auto *a=jet.boardingAssoc;auto *hierarchy=jet.boardingHierarchy;
 if(BoardingPed(jet)&&EntityClump(jet.doorPed)==jet.boardingClump&&a&&
    api.GetAssociation(jet.boardingClump,uint32_t(jet.boardingAnim))==a&&jet.boardingSerial==serial&&BoardingPed(jet)){
  if(*reinterpret_cast<uint8_t **>(a+0x20)==hierarchy&&!*reinterpret_cast<int32_t *>(a+0x40)){
   *reinterpret_cast<uint16_t *>(a+0x3e)|=4;
   if(fade==0)*reinterpret_cast<float *>(a+0x28)=0;
   *reinterpret_cast<float *>(a+0x2c)=fade==0?-8:-fade;
  }
 }
 if(jet.boardingSerial==serial){jet.boardingAssoc=jet.boardingHierarchy=nullptr;jet.boardingAnim=-1;}
}
void CancelDoor(Jet &jet,uint64_t expected=0){
 const auto serial=jet.boardingSerial;if(expected&&expected!=serial)return;
 auto *ped=jet.doorPed;const int ref=jet.doorPedRef,savedObjective=jet.boardingSavedObjective;
 const bool restoreObjective=jet.door==Jet::Door::Board&&jet.boardingSetObjective&&BoardingPed(jet)&&!PedInVehicle(ped)&&PedObjective(ped)==18;
 ReleaseBoardingAnim(jet);
 if(jet.boardingSerial!=serial)return;
 if(jet.door==Jet::Door::Board&&BoardingPed(jet)&&!PedInVehicle(jet.doorPed)){
  // Only bits/state borrowed by this generation are restored. No pad/global
  // input flags or game animation callbacks are ever installed or retained.
  if(jet.boardingCollision)EntityFlags(jet.doorPed)|=kEntityUsesCollision;
 }
 jet.boardingSerial=0;jet.door=Jet::Door::None;jet.doorPed=nullptr;jet.doorHold=0;
 if(restoreObjective&&api.PoolsGetPed(ref)==ped&&PedObjective(ped)==18)api.SetObjective(ped,savedObjective,nullptr);
}
bool BoardingOwned(const Jet &jet,uint8_t *v,uint64_t serial){
 return serial&&jet.boardingSerial==serial&&jet.door==Jet::Door::Board&&Live(jet)==v&&BoardingPed(jet)&&
  jet.doorPed==api.FindPlayerPed()&&EntityClump(jet.doorPed)==jet.boardingClump;
}
bool BoardingAllowed(const Jet &jet,uint8_t *v,uint64_t serial,bool recovery=false){
 if(!BoardingOwned(jet,v,serial))return false;auto *ped=jet.doorPed;
 if(PedState(ped)!=jet.doorState||PedObjective(ped)!=jet.doorObjective||PedInVehicle(ped)||
    Length(Sub(EntityPos(ped),jet.boardingAt))>.35f||!CanUseDoor(ped,v,!recovery)||
    *reinterpret_cast<uint8_t **>(v+0x218)||!Parked(v))return false;
 return BoardingOwned(jet,v,serial);
}
bool BoardingAnimation(Jet &jet,uint8_t *v,int group,int anim,const char *name,float speed=1){
 const auto serial=jet.boardingSerial;ReleaseBoardingAnim(jet,anim==102?0:8);
 if(!BoardingAllowed(jet,v,serial))return false;
 auto *assoc=api.BlendAnimation(jet.boardingClump,group,anim,anim==102?4:8);
 if(!BoardingAllowed(jet,v,serial)||!assoc||api.GetAssociation(jet.boardingClump,uint32_t(anim))!=assoc)return false;
 if(!BoardingAllowed(jet,v,serial))return false;
 auto *hierarchy=*reinterpret_cast<uint8_t **>(assoc+0x20);
 if(!hierarchy||strncasecmp(reinterpret_cast<char *>(hierarchy),name,24)||
    *reinterpret_cast<int32_t *>(assoc+0x40))return false;
 const float duration=*reinterpret_cast<float *>(hierarchy+0x24);
 if(!std::isfinite(duration)||duration<=0||duration>3)return false;
 jet.boardingAssoc=assoc;jet.boardingHierarchy=hierarchy;jet.boardingAnim=anim;jet.boardingTime=0;
 // Keep the finite animation at its last frame until the route consumes it.
 // WALK_player is the only loop; no native car-entry callback is attached.
 auto &flags=*reinterpret_cast<uint16_t *>(assoc+0x3e);flags=uint16_t((flags&~uint16_t(14))|1|(anim==0?2:0));
 *reinterpret_cast<float *>(assoc+0x34)=speed;n.animTime(assoc,0);
 if(!BoardingAllowed(jet,v,serial))return false;
 if(anim==102){*reinterpret_cast<float *>(assoc+0x28)=1;*reinterpret_cast<float *>(assoc+0x2c)=0;}
 return BoardingAllowed(jet,v,serial);
}
bool BoardingClock(Jet &jet,uint8_t *v,float &elapsed,float &progress){
 const auto serial=jet.boardingSerial;
 auto *a=api.GetAssociation(jet.boardingClump,uint32_t(jet.boardingAnim));
 if(!BoardingAllowed(jet,v,serial)||!a||a!=jet.boardingAssoc||
    *reinterpret_cast<uint8_t **>(a+0x20)!=jet.boardingHierarchy)return BoardFail(jet,serial,Jet::Failure::Animation);
 jet.lastCallback=*reinterpret_cast<int32_t *>(a+0x40);jet.lastFlags=*reinterpret_cast<uint16_t *>(a+0x3e);jet.lastSpeed=*reinterpret_cast<float *>(a+0x34);jet.lastDelta=*reinterpret_cast<float *>(a+0x2c);
 if(jet.lastCallback||jet.lastDelta<0)return BoardFail(jet,serial,Jet::Failure::Animation);
 const float duration=*reinterpret_cast<float *>(jet.boardingHierarchy+0x24),time=*reinterpret_cast<float *>(a+0x30);
 jet.lastDuration=duration;
 if(!std::isfinite(duration)||duration<=0||duration>3||!std::isfinite(time)||time<0||time>duration+.001f)return BoardFail(jet,serial,Jet::Failure::Clock);
 elapsed=time-jet.boardingTime;if(elapsed<0){if(jet.boardingAnim!=0)return BoardFail(jet,serial,Jet::Failure::ClockReset);elapsed+=duration;}
 if(elapsed>.21f)return BoardFail(jet,serial,Jet::Failure::ClockJump);jet.boardingTime=time;jet.lastBlend=*reinterpret_cast<float *>(a+0x28);progress=std::clamp(time/duration,0.0f,1.0f);return true;
}
// Body roots on the actual integrated treads, then through the low aperture
// and to the cockpit. Timing is native CAR_getin_LHS progress, not a door
// timer. The late lean keeps the head under the 1.22m opening.
CVector BoardingRoute(float progress,CVector outside){return jetboarding::Route(progress,outside);}

bool MoveBoarding(Jet &jet,uint8_t *v,CVector at,CVector forward,CVector animationOffset={}){
 const auto serial=jet.boardingSerial;auto *ped=jet.doorPed;
 jet.lastStep=Length(Sub(at,jet.boardingAt));
 if(!Finite(at)||jet.lastStep>.31f)return BoardFail(jet,serial,Jet::Failure::PositionStep);
 // Test the swept body against foreign geometry; our CPlane's structural
 // spheres deliberately cover its hull, so ignore only that owned actor.
 const CVector from=jet.boardingBodyAt,body=Add(at,animationOffset);
 for(float t:{.5f,1.0f})for(float height:{-.35f,.1f,.55f}){const CVector offset{0,0,height},p=Add(Add(from,Mul(Sub(body,from),t)),offset);
  if(!Clear(v,Add(from,offset),p))return BoardFail(jet,serial,Jet::Failure::Obstruction);
  if(!BoardingAllowed(jet,v,serial))return BoardFail(jet,serial,Jet::Failure::Owner);
  Ignore ignore(v);if(api.TestSphereAgainstWorld(p,.23f,v,true,true,true,true,false,false))return BoardFail(jet,serial,Jet::Failure::Obstruction);
  if(!BoardingAllowed(jet,v,serial))return BoardFail(jet,serial,Jet::Failure::Owner);
 }
 // CPed::Teleport0x1a73d4 refreshes CWorld sector membership around every
 // bounded step. Physics/collision is leased only for this player's route.
 jet.boardingAt=at;jet.boardingBodyAt=body;
 n.teleport(ped,at);if(!BoardingAllowed(jet,v,serial))return BoardFail(jet,serial,Jet::Failure::NativeMove);EntitySpeed(ped)={};
 const float flat=std::hypot(forward.x,forward.y);if(flat<.01f)return BoardFail(jet,serial,Jet::Failure::NativeMove);
 forward=Mul(forward,1/flat);forward.z=0;Vector(ped,8)={forward.y,-forward.x,0};Vector(ped,0x18)=forward;Vector(ped,0x28)={0,0,1};
 *reinterpret_cast<float *>(ped+0x46c)=*reinterpret_cast<float *>(ped+0x470)=std::atan2(-forward.x,forward.y);
 n.matrix(ped+8);if(!BoardingAllowed(jet,v,serial))return BoardFail(jet,serial,Jet::Failure::NativeMove);n.frame(ped);return BoardingAllowed(jet,v,serial)||BoardFail(jet,serial,Jet::Failure::NativeMove);
}
bool UpdateBoarding(Jet &jet,uint8_t *v,uint32_t now,float dt){
 const auto serial=jet.boardingSerial;auto fail=[&](Jet::Failure why){return BoardFail(jet,serial,why);};
 if(jet.failure!=Jet::Failure::None)return false;
 if(!BoardingOwned(jet,v,serial))return fail(Jet::Failure::Owner);
 if(!BoardingAllowed(jet,v,serial))return fail(Jet::Failure::Context);
 if(now<jet.boardingBegan||now-jet.boardingBegan>20000)return fail(Jet::Failure::Timeout);
 if(!jet.boardingAssoc&&!BoardingAnimation(jet,v,30,0,"walk_player"))return fail(Jet::Failure::Animation);
 float elapsed=0,progress=0;if(!BoardingClock(jet,v,elapsed,progress))return false;
 if(elapsed>0)jet.clockAdvanced=now;
 if(jet.boarding<=Jet::Boarding::Step&&now-jet.clockAdvanced>2500)return fail(Jet::Failure::ClockStalled);
 auto animate=[&](Jet::Boarding stage,int group,int anim,const char *name,float speed=1){BoardStage(jet,stage,now);return BoardingAnimation(jet,v,group,anim,name,speed)||fail(Jet::Failure::Animation);};
 auto moveLocal=[&](CVector at,CVector key,float yaw){
  const auto root=jetboarding::Rotate(key,yaw);const auto offset=Add(Add(Mul(Vector(v,8),root.x),Mul(Vector(v,0x18),root.y)),Mul(Vector(v,0x28),root.z));
  const auto forward=Add(Mul(Vector(v,8),std::sin(yaw)),Mul(Vector(v,0x18),std::cos(yaw)));
  return MoveBoarding(jet,v,At(v,at),forward,offset);
 };
 if(jet.boarding==Jet::Boarding::Approach){
  const CVector delta=Sub(jet.boardingHandle,jet.boardingAt);const float distance=Length(delta),step=.785835f*elapsed;
  if(distance>.015f&&!MoveBoarding(jet,v,Add(jet.boardingAt,Mul(delta,std::min(step,distance)/distance)),delta))return false;
  if(distance<=step+.015f)return animate(Jet::Boarding::Reach,0,76,"CAR_open_LHS");
 }else if(jet.boarding==Jet::Boarding::Reach){
  if(!MoveBoarding(jet,v,jet.boardingAt,Vector(v,0x18)))return false;
  if(progress>=1)return animate(Jet::Boarding::Retreat,52,0,"walk_back");
 }else if(jet.boarding==Jet::Boarding::Retreat){
  const CVector delta=Sub(jet.boardingOutside,jet.boardingAt);const float distance=Length(delta),step=1.624076f*elapsed;
  if(distance>.015f&&!MoveBoarding(jet,v,Add(jet.boardingAt,Mul(delta,std::min(step,distance)/distance)),Mul(delta,-1)))return false;
  if(distance<=step+.015f)return animate(Jet::Boarding::Lower,30,3,"IDLE_stance");
 }else if(jet.boarding==Jet::Boarding::Lower){
  if(!MoveBoarding(jet,v,jet.boardingAt,Vector(v,0x18)))return false;
  jet.doorOpen=std::clamp(jet.doorOpen+elapsed/.8f,0.0f,1.0f);
  if(jet.doorOpen>=1)return animate(Jet::Boarding::Step,0,80,"CAR_getin_LHS",.16f);
 }else if(jet.boarding==Jet::Boarding::Step){
  const float entry=std::min(progress,jetboarding::kEntryEnd);const auto outside=Sub(jet.boardingOutside,Vector(v,0x38));
  const CVector local{Dot(outside,Vector(v,8)),Dot(outside,Vector(v,0x18)),Dot(outside,Vector(v,0x28))};
  if(!moveLocal(BoardingRoute(entry,local),jetboarding::Root(entry),jetboarding::Yaw(entry)))return false;
  if(progress>=jetboarding::kEntryEnd){
   const float duration=*reinterpret_cast<float *>(jet.boardingHierarchy+0x24);n.animTime(jet.boardingAssoc,duration*jetboarding::kEntryEnd);
   if(!BoardingAllowed(jet,v,serial))return fail(Jet::Failure::Owner);
   *reinterpret_cast<uint16_t *>(jet.boardingAssoc+0x3e)&=~uint16_t(1);jet.boardingTime=duration*jetboarding::kEntryEnd;BoardStage(jet,Jet::Boarding::Crouch,now);
  }
 }else if(jet.boarding==Jet::Boarding::Crouch){
  jet.stageTime=std::min(jet.stageTime+dt,jetboarding::kCrouchSeconds);const float blend=jetboarding::Smooth(jet.stageTime/jetboarding::kCrouchSeconds);
  if(!moveLocal(jetboarding::CrouchActor(blend),jetboarding::CrouchKey(blend),jetboarding::CrouchYaw(blend)))return false;
  if(blend>=1)BoardStage(jet,Jet::Boarding::Walk,now);
 }else if(jet.boarding==Jet::Boarding::Walk){
  jet.walkDistance=std::min(jet.walkDistance+jetboarding::kWalkSpeed*dt,jetboarding::WalkLength());
  if(!moveLocal(jetboarding::WalkRoot(jet.walkDistance),jetboarding::CrouchRoot(),jetboarding::WalkYaw()))return false;
  if(jet.walkDistance>=jetboarding::WalkLength())return animate(Jet::Boarding::Sit,0,102,"CAR_sit");
 }else{
  jet.stageTime=std::min(jet.stageTime+dt,jetboarding::kSitSeconds);const float blend=jetboarding::Smooth(jet.stageTime/jetboarding::kSitSeconds);
  if(!moveLocal(jetboarding::SitActor(blend),jetboarding::SitKey(blend),jetboarding::WalkYaw()*(1-blend)))return false;
  if(blend>=1&&progress>=1){
   auto *ped=jet.doorPed;jet.doorObjective=18;jet.boardingSetObjective=true;api.SetObjective(ped,18,v);
   if(!BoardingAllowed(jet,v,serial))return fail(Jet::Failure::Owner);
   n.warp(ped,v);if(!BoardingOwned(jet,v,serial)||!PedInVehicle(ped)||PedVehicle(ped)!=v||*reinterpret_cast<uint8_t **>(v+0x218)!=ped)return fail(Jet::Failure::Seat);
   Seat(ped);if(!BoardingOwned(jet,v,serial)||api.PoolsGetPed(jet.doorPedRef)!=ped)return fail(Jet::Failure::Owner);
   Log("airport jets: boarding %llu complete driver%d vehicle%d elapsed%u",static_cast<unsigned long long>(serial),jet.doorPedRef,jet.ref,now-jet.boardingBegan);
   jet.boardingAssoc=jet.boardingHierarchy=nullptr;jet.boardingAnim=-1;CancelDoor(jet);jet.boarded=jet.doorHold=now;return true;
  }
 }
 return true;
}

void AbortBoarding(Jet &jet,uint64_t expected){
 if(!expected||jet.boardingSerial!=expected)return;
 if(!jet.abortLogged){jet.abortLogged=true;const uint32_t now=api.timeMs?*api.timeMs:gPrevious;
  Log("airport jets: boarding %llu abort phase%s reason%s vehicle%d anim%d clock%.4f blend%.3f step%.3f stage%.3f distance%.3f duration%.4f speed%.3f delta%.3f flags%x callback%d elapsed%u",static_cast<unsigned long long>(expected),BoardPhase(jet.boarding),BoardFailure(jet.failure),jet.ref,jet.boardingAnim,jet.boardingTime,jet.lastBlend,jet.lastStep,jet.stageTime,jet.walkDistance,jet.lastDuration,jet.lastSpeed,jet.lastDelta,unsigned(jet.lastFlags),jet.lastCallback,now-jet.boardingBegan);
 }
 auto *ped=jet.doorPed,*v=jet.entity,*clump=jet.boardingClump;
 const auto serial=jet.boardingSerial,epoch=gBoardingSerial;
 const int ref=jet.doorPedRef,state=jet.doorState,objective=jet.boardingSavedObjective,anim=jet.boardingAnim;
 const auto outside=jet.boardingOutside,at=jet.boardingAt;
 auto *association=jet.boardingAssoc,*hierarchy=jet.boardingHierarchy;
 auto allowed=[&](){return BoardingAllowed(jet,v,serial,true);};
 auto animationOwned=[&](auto valid){
  if(anim<0)return valid();auto *current=api.GetAssociation(clump,uint32_t(anim));
  return valid()&&(!current||(current==association&&*reinterpret_cast<uint8_t **>(current+0x20)==hierarchy&&!*reinterpret_cast<int32_t *>(current+0x40)));
 };
 auto clearOutside=[&](){
  if(!allowed())return false;const auto from=EntityPos(ped);const float distance=Length(Sub(outside,from));
  if(!Finite(from)||!Finite(outside)||!std::isfinite(distance)||distance>12)return false;
  const int steps=std::max(1,int(std::ceil(distance/.3f)));
  // Check the full Ped1 shape while our collision lease is still held, so
  // world queries include foreign pedestrians without hitting Tommy himself.
  for(float height:{-.25f,.15f,.55f}){
   const CVector offset{0,0,height};
   if(!Clear(v,Add(from,offset),Add(outside,offset))||!allowed())return false;
   for(int step=1;step<=steps;++step){
    const auto point=Add(Add(from,Mul(Sub(outside,from),float(step)/steps)),offset);
    Ignore ignore(v);
    if(api.TestSphereAgainstWorld(point,.35f,v,true,true,true,true,false,false)||!allowed())return false;
   }
  }return true;
 };
 const bool rollback=allowed()&&animationOwned(allowed)&&clearOutside()&&allowed()&&animationOwned(allowed)&&allowed();
 CancelDoor(jet,serial);
 // Only the same free-roam owner can return to its ground station. Present
 // foreign animation owners and mission/state changes retain their position.
 auto live=[&](bool returned=false){return epoch==gBoardingSerial&&!jet.boardingSerial&&Live(jet)==v&&api.PoolsGetPed(ref)==ped&&ped==api.FindPlayerPed()&&EntityClump(ped)==clump&&!PedInVehicle(ped)&&PedState(ped)==state&&PedObjective(ped)==objective&&Length(Sub(EntityPos(ped),returned?outside:at))<=.35f&&Health(ped)>0&&!n.mission()&&!*gCutscene;};
 if(rollback&&live()&&animationOwned(live)&&live()){
  n.teleport(ped,outside);if(live(true)){n.matrix(ped+8);if(live(true))n.frame(ped);}
 }
}
void PlayerControl(uint8_t *ped){
 for(auto &jet:gJets)if(jet.door==Jet::Door::Board&&jet.doorPed==ped){
  const auto serial=jet.boardingSerial;if(BoardingAllowed(jet,jet.entity,serial))return;
  BoardFail(jet,serial,Jet::Failure::Context);AbortBoarding(jet,serial);
  if(jet.door==Jet::Door::Board&&jet.doorPed==ped&&jet.boardingSerial!=serial)return;
 }
 originalPlayerControl(ped);
}
bool CancelBoardingButton(uint8_t *ped,uint32_t now){
 for(auto &jet:gJets)if(jet.door==Jet::Door::Board&&jet.doorPed==ped){
  // The native SetEnterCar hook and the frame input consumer can observe
  // the same initial edge. A later deliberate entry/exit edge aborts it.
  if(now>=jet.boardingBegan&&now-jet.boardingBegan>300){
   BoardFail(jet,jet.boardingSerial,Jet::Failure::Button);AbortBoarding(jet,jet.boardingSerial);
  }
  return true;
 }
 return false;
}
void UpdateDoor(Jet &jet,uint8_t *v,uint32_t now,float dt){
 if(jet.door==Jet::Door::Board){const auto serial=jet.boardingSerial;if(!UpdateBoarding(jet,v,now,dt))AbortBoarding(jet,serial);return;}
 CVector at{};uint8_t *ped=jet.doorPed;
 if(jet.door!=Jet::Door::None&&(!ped||ped!=api.FindPlayerPed()||jet.doorPedRef<0||api.PoolsGetPed(jet.doorPedRef)!=ped||PedState(ped)!=jet.doorState||PedObjective(ped)!=jet.doorObjective||!ExitPoint(ped,v,at)))CancelDoor(jet);
 const bool opening=jet.door!=Jet::Door::None;const bool hold=jet.doorHold&&now>=jet.doorHold&&now-jet.doorHold<150;
 const float target=opening||hold?1.0f:0.0f;jet.doorOpen+=std::clamp(target-jet.doorOpen,-dt/.8f,dt/.8f);
 if(!opening||jet.doorOpen<1||Find(v)!=&jet||ped!=api.FindPlayerPed()||api.PoolsGetPed(jet.doorPedRef)!=ped)return;
 // Eligibility was checked this frame, after the whole opening animation.
 // Native callbacks may change ownership, so never retain a Jet reference
 // across them without validating the same generation again.
 n.out(nullptr,ped);if(!Find(v)||PedInVehicle(ped)||ped!=api.FindPlayerPed())return;n.teleport(ped,at);
 if(Find(v)!=&jet)return;jet.boarded=now;jet.doorHold=now;jet.door=Jet::Door::None;jet.doorPed=nullptr;
}
void Enter(uint8_t *ped,uint8_t *v,uint32_t door){if(Find(v)){Board(ped,v,api.timeMs?*api.timeMs:gPrevious);return;}originalEnter(ped,v,door);}
void Exit(uint8_t *ped,uint8_t *v,uint32_t door){if(Find(v)){Leave(ped,v);return;}originalExit(ped,v,door);}
void Crash(uint8_t *v,uint8_t *by){if(Wrecked(v))return;*reinterpret_cast<float *>(v+0x2a0)=0;v[0x295]&=~uint8_t(16);uint32_t &flags=*reinterpret_cast<uint32_t *>(v+0x60);flags=(flags&~0xf8u)|(5<<3)|(1<<20);n.kill(v);n.explosion(v,by,6,EntityPos(v),0,false);}
void Damage(uint8_t *v,uint8_t *by,int weapon,float amount,CVector at){originalDamage(v,by,weapon,amount,at);if(Find(v)&&VehicleHealth(v)<=0&&!(weapon==31&&api.VehicleFireManaged&&api.VehicleFireManaged(v)))Crash(v,by);}
void Control(uint8_t *v){Jet *jet=Find(v);if(!jet)return originalControl(v);
 const float mass=*reinterpret_cast<float *>(v+0xcc),impulse=*reinterpret_cast<float *>(v+0x13c);const float impact=mass>0?impulse*50/mass:0;
 const bool materialManaged=api.AirframeContactsManaged&&api.AirframeContactsManaged(v);
 if(!materialManaged&&!Wrecked(v)&&std::isfinite(impact)&&impact>4)n.damage(v,nullptr,39,std::min(1500.0f,(impact-4)*(impact-4)*12),EntityPos(v));
 const uint32_t now=api.timeMs?*api.timeMs:gPrevious;
 if(!Wrecked(v)&&VehicleHealth(v)<250&&!(api.VehicleFireManaged&&api.VehicleFireManaged(v))){if(!jet->burning)jet->burning=now;else if(now-jet->burning>5000)Crash(v,nullptr);}else jet->burning=0;
 float water=0;if(!Wrecked(v)&&api.GetWaterLevel&&api.GetWaterLevel(EntityPos(v).x,EntityPos(v).y,EntityPos(v).z,&water,true)&&EntityPos(v).z<water-.5f)Crash(v,nullptr);
 if(api.AirframeFlightUsed)api.AirframeFlightUsed(v);
 n.physical(v);if(!Wrecked(v))planeflight::Apply(v,5);
}
void PreRender(uint8_t *v){originalPreRender(v);Jet *jet=Find(v);if(!jet)return;
 airportjetmodel::Pose pose;jetwheels::Snapshot(v,pose.wheels);pose.gearDown=jet->gearDown;pose.doorOpen=jet->doorOpen;
 uint8_t *pad=nullptr;
 if(Pilot(v,pad)){const auto axes=flightinput::Read(pad,n.pitch(pad),float(n.right(pad))-float(n.left(pad)),n.gunPitch(pad),n.gunYaw(pad));const float roll=std::clamp(n.roll(pad)/128.0f,-1.0f,1.0f);
  pose.leftFlap=.25f*axes.pitch+.3f*roll;pose.rightFlap=.25f*axes.pitch-.3f*roll;pose.elevator=.3f*axes.pitch;pose.rudder=.35f*axes.yaw;
 }
 airportjetmodel::Animate(EntityClump(v),pose);
}
// An anchor owns only the saved source generation and transform. A source
// replaced by native world code belongs to its current owner; keep the old
// identity reserved until world reset so it cannot be claimed a second time.
void DiscoverAnchors(uint32_t now){
 if(gInfo&&!ModelValid())return;
 bool missing=false;for(const auto &anchor:gAnchors)missing|=anchor.source<0;
 if(missing&&(!gDiscoveryScanned||now<gDiscoveryAt||now-gDiscoveryAt>=750)){
 const Pool pool=GetPool(gBuildings,50000);
 if(pool.entries){gDiscoveryAt=now;gDiscoveryScanned=true;}
 for(int index=0;index<pool.count;++index){
  if(pool.flags[index]&128)continue;uint8_t *source=pool.entries+size_t(index)*kBuildingStride;
  if(EntityModel(source)!=kSource||(EntityFlags(source)&7)!=1)continue;
  bool known=false;for(const auto &anchor:gAnchors)known|=anchor.source==index&&anchor.sourceEntries==pool.entries&&anchor.sourceFlags==pool.flags;
  if(known)continue;
  Anchor pending;pending.source=index;pending.generation=pool.flags[index];pending.sourceEntity=source;pending.sourceEntries=pool.entries;pending.sourceFlags=pool.flags;
  bool finite=true;for(int i=0;i<4;++i){pending.basis[i]=Vector(source,8+size_t(i)*16);finite&=Finite(pending.basis[i]);}if(!finite)continue;
  for(auto &anchor:gAnchors)if(anchor.source<0){anchor=pending;break;}
 }
 }
 // Do this before model registration, asynchronous loading, donor preparation
 // or player eligibility. The static RT consumer retains captured scenery.
 for(auto &anchor:gAnchors)if(!anchor.hidden)if(uint8_t *source=Source(anchor)){anchor.hidden=true;n.remove(source);}
}
void RestoreAnchor(Anchor &anchor){if(anchor.hidden)if(uint8_t *source=Source(anchor)){anchor.hidden=false;n.add(source);}}
void Retire(Jet &jet,bool destroy){CancelDoor(jet);uint8_t *v=Live(jet);if(v&&destroy){n.remove(v);n.destroy(v);}jet={};}
void Restart(){
 for(auto &jet:gJets)if(jet.entity)Retire(jet,true);
 for(auto &anchor:gAnchors){RestoreAnchor(anchor);anchor={};}
 ReleasePins();gPrevious=gDiscoveryAt=0;gDiscoveryScanned=false;originalRestart();
}
void WorldShutdown(){
 for(auto &jet:gJets)CancelDoor(jet);
 // Native world shutdown visits its lists, then model shutdown precedes raw
 // pool-array disposal. Hidden originals must rejoin that traversal so their
 // RW objects and model references receive the normal native destructors.
 for(auto &anchor:gAnchors)RestoreAnchor(anchor);
 originalWorldShutdown();gAnchors={};
}
void Shutdown(){
 // CGame::ShutDown has already destroyed world entities. Native pool arrays
 // remain until later disposal; never inspect saved source pointers here.
 ReleasePins();for(auto &jet:gJets){CancelDoor(jet);jet={};}gAnchors={};
 if(gSourcePinned&&api.modelInfoPtrs[kSource]==gSourceInfo&&gSourceInfo[0x1d]==1&&*reinterpret_cast<int16_t *>(gSourceInfo+0x2e)==gTxd)n.removeRef(gSourceInfo);
 if(gCockpitPinned&&CockpitValid())n.removeRef(gCockpitInfo);gCockpitPinned=false;gCockpitInfo=nullptr;gCockpitTxd=-1;
 gSourcePinned=false;gSourceInfo=gInfo=nullptr;gModel=gTxd=-1;gShapeReady=false;gPrevious=gDiscoveryAt=0;gDiscoveryScanned=false;originalShutdown();
}
bool Spawn(Jet &slot,const Anchor &anchor){
 uint8_t *source=Source(anchor);if(!source||!anchor.hidden)return false;
 CVector at=anchor.basis[3];float floor=0;if(!Ground(at,floor,source)||Source(anchor)!=source)return false;
 at.z=floor-Vector(gCollision,0x10).z+.08f;
 const Pool vehicles=GetPool(gVehicles,10000);int free=0;for(int i=0;i<vehicles.count;++i)free+=(vehicles.flags[i]&128)!=0;if(free<5)return false;
 {Ignore ignore(source);for(int i=0;i<kMaxSpheres;++i){const uint8_t *sphere=gSpheres+i*20;CVector centre=At(source,Vector(sphere,0));centre.z+=at.z-EntityPos(source).z;
  if(api.TestSphereAgainstWorld(centre,*reinterpret_cast<const float *>(sphere+12),source,true,true,true,true,false,false))return false;}}
 if(Source(anchor)!=source)return false;
 auto *v=static_cast<uint8_t *>(n.allocate(0x388));if(!v)return false;const int ref=Reference(v);if(ref<0)return false;
 n.construct(v,gModel,1);if(api.PoolsGetVehicle(ref)!=v)return false;
 Jet pending;pending.entity=v;pending.vtable=*reinterpret_cast<uint8_t **>(v);pending.ref=ref;
 if(!Live(pending))return false;
 if(!EntityClump(v)||Source(anchor)!=source){n.destroy(v);return false;}
 uint32_t &flags=*reinterpret_cast<uint32_t *>(v+0x60);flags=(flags&~(0x400000u|0xf8u))|kEntityUsesCollision|(4<<3);
 *reinterpret_cast<float *>(v+0xcc)=5000;*reinterpret_cast<float *>(v+0xd0)=20000;*reinterpret_cast<float *>(v+0xd8)=.00016f;v[0x295]=(v[0x295]|8)&~uint8_t(16);
 for(int i=0;i<3;++i)Vector(v,8+size_t(i)*16)=anchor.basis[i];Position(v,at);
 if(!Live(pending))return false;if(Source(anchor)!=source){n.destroy(v);return false;}
 slot=pending;n.add(v);ReleasePins();return true;
}
}
bool Owns(const uint8_t *vehicle){return gReady&&Find(vehicle);}
float GearDown(const uint8_t *vehicle){const Jet *jet=gReady?Find(vehicle):nullptr;return jet?jet->gearDown:0;}
bool UsesRegisteredModel(const uint8_t *v){return v&&ModelValid()&&Reference(const_cast<uint8_t *>(v))>=0&&EntityModel(v)==gModel&&*reinterpret_cast<const int32_t *>(v+0x348)==4;}
bool ReachDoor(uint8_t *ped,CVector &target,float &weight,int &ref,uint64_t &serial){
 for(auto &jet:gJets)if(jet.door==Jet::Door::Board&&jet.boarding==Jet::Boarding::Reach&&jet.doorPed==ped){
  const auto token=jet.boardingSerial;auto *v=jet.entity;if(!BoardingAllowed(jet,v,token))return false;
  auto *a=api.GetAssociation(jet.boardingClump,76);
  if(!BoardingAllowed(jet,v,token)||!a||a!=jet.boardingAssoc||*reinterpret_cast<uint8_t **>(a+0x20)!=jet.boardingHierarchy||*reinterpret_cast<int32_t *>(a+0x40))return false;
  const float time=*reinterpret_cast<float *>(a+0x30),duration=*reinterpret_cast<float *>(jet.boardingHierarchy+0x24);
  if(!std::isfinite(time)||!std::isfinite(duration)||duration<=0||time<0||time>duration)return false;
  const float progress=time/duration;auto smooth=[](float x){x=std::clamp(x,0.0f,1.0f);return x*x*(3-2*x);};
  weight=smooth(progress/.15f)*smooth((.85f-progress)/.25f);
  // Exact original Learjet face319 at Y3.7/Z-.78, retained by the private
  // door cut (barycentric u.125651357/v.663221276).
  const auto delta=Sub(At(v,{-.833174114f,3.7f,-.78f}),EntityPos(ped));
  target={Dot(delta,Vector(ped,8)),Dot(delta,Vector(ped,0x18)),Dot(delta,Vector(ped,0x28))};ref=jet.doorPedRef;serial=token;return weight>0;
 }
 return false;
}
bool CabinPose(uint8_t *ped,jetboarding::CabinPose &pose){
 for(auto &jet:gJets)if(jet.door==Jet::Door::Board&&jet.doorPed==ped&&jet.boarding>=Jet::Boarding::Crouch){
  const auto token=jet.boardingSerial;auto *v=jet.entity;if(!BoardingAllowed(jet,v,token))return false;
  auto *a=api.GetAssociation(jet.boardingClump,uint32_t(jet.boardingAnim));
  if(!BoardingAllowed(jet,v,token)||!a||a!=jet.boardingAssoc||*reinterpret_cast<uint8_t **>(a+0x20)!=jet.boardingHierarchy||*reinterpret_cast<int32_t *>(a+0x40))return false;
  CVector feet[2]{};pose.pedRef=jet.doorPedRef;pose.serial=token;pose.distance=jet.walkDistance;
  if(jet.boarding==Jet::Boarding::Crouch){pose.mode=jetboarding::CabinPose::Mode::Crouch;pose.blend=jetboarding::Smooth(jet.stageTime/jetboarding::kCrouchSeconds);pose.root=jetboarding::CrouchKey(pose.blend);for(int leg=0;leg<2;++leg)feet[leg]=jetboarding::CrouchFoot(pose.blend,leg);}
  else if(jet.boarding==Jet::Boarding::Walk){pose.mode=jetboarding::CabinPose::Mode::Walk;pose.blend=1;pose.root=jetboarding::CrouchRoot();for(int leg=0;leg<2;++leg)feet[leg]=jetboarding::WalkFoot(pose.distance,leg).at;}
  else{pose.mode=jetboarding::CabinPose::Mode::Sit;pose.blend=jetboarding::Smooth(jet.stageTime/jetboarding::kSitSeconds);pose.root=jetboarding::SitKey(pose.blend);for(int leg=0;leg<2;++leg)feet[leg]=jetboarding::SitFoot(pose.blend,leg);}
  for(int leg=0;leg<2;++leg){const auto delta=Sub(At(v,feet[leg]),EntityPos(ped));pose.feet[leg]={Dot(delta,Vector(ped,8)),Dot(delta,Vector(ped,0x18)),Dot(delta,Vector(ped,0x28))};}
  return true;
 }
 return false;
}
void RejectBoardingPose(uint8_t *ped,int ref,uint64_t serial){
 for(auto &jet:gJets)if(jet.doorPed==ped&&jet.doorPedRef==ref&&jet.boardingSerial==serial)BoardFail(jet,serial,Jet::Failure::Pose);
}

bool Install(){
 const bool bound=Bind(n.allocate,"_ZN8CVehiclenwEm")&&Bind(n.construct,"_ZN6CPlaneC1Eih")&&Bind(n.destroy,"_ZN6CPlaneD0Ev")&&
     Bind(n.add,"_ZN6CWorld3AddEP7CEntity")&&Bind(n.remove,"_ZN6CWorld6RemoveEP7CEntity")&&Bind(n.physical,"_ZN9CPhysical14ProcessControlEv")&&
     Bind(n.addModel,"_ZN10CModelInfo15AddVehicleModelEi")&&Bind(n.addRef,"_ZN14CBaseModelInfo6AddRefEv")&&Bind(n.removeRef,"_ZN14CBaseModelInfo9RemoveRefEv")&&
     Bind(n.cd,"_ZN14CStreamingInfo16GetCdPosnAndSizeERjS0_")&&Bind(n.setCd,"_ZN14CStreamingInfo16SetCdPosnAndSizeEjj")&&Bind(n.deletable,"_ZN10CStreaming19SetModelIsDeletableEi")&&
     Bind(n.warp,"_ZN4CPed14WarpPedIntoCarEP8CVehicle")&&Bind(n.out,"_ZN4CPed14PedSetOutCarCBEP21CAnimBlendAssociationPv")&&Bind(n.teleport,"_ZN4CPed8TeleportE7CVector")&&
     Bind(n.matrix,"_ZN7CMatrix8UpdateRWEv")&&Bind(n.frame,"_ZN7CEntity13UpdateRwFrameEv")&&Bind(n.mission,"_ZN11CTheScripts18IsPlayerOnAMissionEv")&&Bind(n.exit,"_ZN4CPad19ExitVehicleJustDownEv")&&Bind(n.pedRef,"_ZN6CPools9GetPedRefEP4CPed")&&
     Bind(n.damage,"_ZN8CVehicle13InflictDamageEP7CEntity11eWeaponTypef7CVector")&&Bind(n.kill,"_ZN8CVehicle17KillPedsInVehicleEv")&&Bind(n.explosion,"_ZN10CExplosion12AddExplosionEP7CEntityS1_14eExplosionTypeRK7CVectorjb")&&
     Bind(n.each,"_Z20RpClumpForAllAtomicsPN2rw5ClumpEPFPNS_6AtomicES3_PvES4_")&&Bind(n.geometry,"_Z19RpAtomicGetGeometryPKN2rw6AtomicE")&&Bind(n.verticesCount,"_Z24RpGeometryGetNumVerticesPKN2rw8GeometryE")&&
     Bind(n.morph,"_Z24RpGeometryGetMorphTargetPKN2rw8GeometryEi")&&Bind(n.vertices,"_Z24RpMorphTargetGetVerticesPKN2rw11MorphTargetE")&&
     Bind(n.triangleCount,"_Z25RpGeometryGetNumTrianglesPKN2rw8GeometryE")&&Bind(n.triangles,"_Z22RpGeometryGetTrianglesPKN2rw8GeometryE")&&
     Bind(n.atomicFrame,"_Z16RpAtomicGetFramePKN2rw6AtomicE")&&Bind(n.ltm,"_Z13RwFrameGetLTMPN2rw5FrameE")&&Bind(n.name,"_Z16GetFrameNodeNamePN2rw5FrameE")&&
     Bind(n.roll,"_ZN4CPad20GetSteeringLeftRightEv")&&Bind(n.pitch,"_ZN4CPad17GetSteeringUpDownEv")&&Bind(n.gunPitch,"_ZN4CPad15GetCarGunUpDownEv")&&Bind(n.gunYaw,"_ZN4CPad18GetCarGunLeftRightEv")&&
     Bind(n.left,"_ZN4CPad11GetLookLeftEv")&&Bind(n.right,"_ZN4CPad12GetLookRightEv")&&Bind(n.horn,"_ZN4CPad7GetHornEv")&&
     Bind(n.animTime,"_ZN21CAnimBlendAssociation14SetCurrentTimeEf")&&api.BlendAnimation&&api.GetAssociation&&
     Bind(gBuildings,"_ZN6CPools16ms_pBuildingPoolE")&&Bind(gVehicles,"_ZN6CPools15ms_pVehiclePoolE")&&Bind(gStreaming,"_ZN10CStreaming16ms_aInfoForModelE")&&Bind(gStore,"_ZN10CModelInfo20ms_vehicleModelStoreE")&&
     Bind(gArea,"_ZN5CGame8currAreaE")&&Bind(gCutscene,"_ZN12CCutsceneMgr10ms_runningE")&&api.modelInfoPtrs&&api.FindPlayerPed&&api.GetPad&&api.PoolsGetVehicle&&api.PoolsGetPed&&api.RequestModel&&api.ProcessVerticalLine&&api.TestSphereAgainstWorld&&api.GetIsLineOfSightClear&&api.SetObjective&&api.ignoreEntity;
 if(!bound||!airportjetmodel::Install())return false;
 auto chain=[](const char *name,auto hook,auto &original){if(original)return true;original=reinterpret_cast<std::decay_t<decltype(original)>>(hooks::Chain(name,reinterpret_cast<void *>(hook)));return original!=nullptr;};
 gReady=chain("_ZN6CPlane14ProcessControlEv",Control,originalControl)&&chain("_ZN4CPed19SetPedPositionInCarEv",Seat,originalSeat)&&chain("_ZN10CPlayerPed14ProcessControlEv",PlayerControl,originalPlayerControl)&&
     chain("_ZN4CPed11SetEnterCarEP8CVehiclej",Enter,originalEnter)&&chain("_ZN4CPed10SetExitCarEP8CVehiclej",Exit,originalExit)&&chain("_ZN8CVehicle13InflictDamageEP7CEntity11eWeaponTypef7CVector",Damage,originalDamage)&&
     chain("_ZN10CModelInfo8ShutDownEv",Shutdown,originalShutdown)&&chain("_ZN6CWorld8ShutDownEv",WorldShutdown,originalWorldShutdown)&&
     chain("_ZN6CWorld15ClearForRestartEv",Restart,originalRestart)&&chain("_ZN6CPlane9PreRenderEv",PreRender,originalPreRender);
 if(gReady)planeflight::SetAirportOwner(Owns);return gReady;
}
void Frame(uint32_t now){
 if(!gReady)return;
 const float dt=gPrevious&&now>=gPrevious?std::min(now-gPrevious,uint32_t(100))*.001f:0;
 if(now<gPrevious){ReleasePins();gDiscoveryScanned=false;gDiscoveryAt=0;for(auto &jet:gJets){CancelDoor(jet);jet.burning=jet.boarded=jet.doorHold=0;jet.doorOpen=0;jet.gearDown=1;jet.gearTargetDown=true;jet.manualGear=false;jet.hornHeld=true;jet.gearDriverRef=-1;}}gPrevious=now;
 if(gConfig.planes)DiscoverAnchors(now);
 uint8_t *player=api.FindPlayerPed();
 for(auto &jet:gJets)if(uint8_t *v=Live(jet)){UpdateGear(jet,v,dt);if(Live(jet)==v)UpdateDoor(jet,v,now,dt);}
 for(size_t i=0;i<gJets.size();++i){auto &jet=gJets[i];
  if(jet.entity){uint8_t *v=Live(jet);if(!v)Retire(jet,false);
   else if(!Occupied(v)&&(!gConfig.planes||!Source(gAnchors[i])||*gArea!=0||(player&&Length(Sub(EntityPos(v),EntityPos(player)))>450)))Retire(jet,true);}
  if(!gConfig.planes&&!jet.entity)RestoreAnchor(gAnchors[i]);
 }
 if(!player){ReleasePins();return;}
 uint8_t *pad=api.GetPad(0);if(pad&&!PadControlsDisabled(pad)&&n.exit(pad)){if(PedInVehicle(player)){uint8_t *v=PedVehicle(player);if(Jet *jet=Find(v))if(now-jet->boarded>300)Leave(player,v);}else if(!CancelBoardingButton(player,now))for(auto &jet:gJets)if(uint8_t *v=Live(jet))if(Board(player,v,now))break;}
 if(!gConfig.planes||*gArea!=0||n.mission()||*gCutscene||Health(player)<=0){ReleasePins();return;}
 bool near=false;for(size_t i=0;i<gAnchors.size();++i)if(!gJets[i].entity&&Source(gAnchors[i])&&Length(Sub(gAnchors[i].basis[3],EntityPos(player)))<120){near=true;break;}
 if(!near){ReleasePins();return;}if(!RegisterModel())return;
 if(Stream(gModel)[0x10]!=1||!*reinterpret_cast<void **>(gInfo+0x30)){Request();return;}
 void *cockpit=CockpitClump();if(!cockpit)return;
 if(!airportjetmodel::Prepare(*reinterpret_cast<void **>(gInfo+0x30),cockpit)||!Shape()){ReleasePins();return;}
 for(size_t i=0;i<gAnchors.size();++i)if(!gJets[i].entity&&Source(gAnchors[i])&&Length(Sub(gAnchors[i].basis[3],EntityPos(player)))<120)Spawn(gJets[i],gAnchors[i]);
}
}

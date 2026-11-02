// Native BIG visibility + asynchronous streaming consumers. The asset/ABI
// companion checks these native contracts against the installed arm64 image.
#include "game.h"
#include "config.h"
#include "hooks.h"
#include "shaders.h"
#include "draw.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static void *Symbol(void *,const char *);
int AirworldActualCameraVisibility(uint8_t *,uint8_t *,int (*)(uint8_t *));
#define dlsym Symbol
#include "../src/airworld.cpp"
#undef dlsym

namespace game { Api api; }
Config gConfig;
void Log(const char *,...) {}
namespace {
int failures=0;
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);++failures;}}while(0)
using namespace game;
std::map<std::string,void *> originals,installed;
alignas(16) uint8_t camera[0x1000]{},ped[0x800]{},plane[0x400]{};
alignas(16) uint8_t models[48][0x80]{};
uint8_t *modelTable[6500]{};
std::array<uint8_t, 0x20*8000> stream{};
CVector rendererCamera{};
int area=0,level=1;bool cutscene=false,mission=false,outside=true,owned=true,timeActive=true;
uint32_t now=1000;
float nativeFar=900,nativeFog=500,drawFar=900;float water[4]={30,90,110,255};
struct RwCamera {float far=900,fog=500;} rw;
std::vector<int> visible,requested,released;int horizonCalls=0;
airworld::PtrList lists[3]{};
std::array<std::array<airworld::PtrList,10>,6400> sectors{};
alignas(16) uint8_t bounds[48][0x68]{};
std::array<airworld::PtrNode,48> nodes{};
std::array<std::array<uint8_t,0x78>,48> entities{};
struct VehiclePool {uint8_t *entries,*flags;int count;};uint8_t vehicleFlags[1]={7};VehiclePool pool{plane,vehicleFlags,1};uint8_t *poolPointer=reinterpret_cast<uint8_t *>(&pool);
uint8_t *PoolGet(int ref){return ref==7&&!(vehicleFlags[0]&128)?plane:nullptr;}
struct BuildingPool {uint8_t *entries,*flags;int count;};std::array<uint8_t,48> buildingFlags{};
BuildingPool buildings{entities[0].data(),buildingFlags.data(),48};uint8_t *buildingPointer=reinterpret_cast<uint8_t *>(&buildings);
struct AtomicInstance {uint8_t *model;uint8_t *entity;};std::array<AtomicInstance,48> atomics{};
struct FadeItem {uint8_t *entity;float distance;};std::vector<FadeItem> fadeQueue;
struct FadeResult {void *atomic,*selected;float distance;unsigned alpha;};std::vector<FadeResult> fadeResults;
uint32_t nativeFrame=0;int nullGeometry=0;bool nestedFade=false;
int (*cameraVisibilityNext)(uint8_t *);int nativeVehicleVisibilityCalls=0;
int CameraVisibility(uint8_t *entity){
 return AirworldActualCameraVisibility(entity,plane,cameraVisibilityNext);
}
int entityCount=0;
bool shadersReady=false;int draws=0;void *shaderOverride=nullptr;
template<class Fn> Fn Function(const char *name) {
 auto it=installed.find(name);return reinterpret_cast<Fn>(it==installed.end()?originals.at(name):it->second);
}
uint8_t *Player(){return ped;}
bool Mission(){return mission;}
bool Outside(){return outside;}
bool Time(uint8_t,uint8_t){return timeActive;}
float GetFar(const void *p){return static_cast<const RwCamera *>(p)->far;}
void *SetFar(void *p,float v){static_cast<RwCamera *>(p)->far=v;return p;}
float GetFog(const void *p){return static_cast<const RwCamera *>(p)->fog;}
void *SetFog(void *p,float v){static_cast<RwCamera *>(p)->fog=v;return p;}
float Largest(const uint8_t *model){return *reinterpret_cast<const float *>(model+0x48)**reinterpret_cast<const float *>(camera+0xf8);}
void *Atomic(uint8_t *model,float distance){return distance<Largest(model)?*reinterpret_cast<void **>(model+0x30):nullptr;}
void *AtomicForFade(uint8_t *model,float distance){
 const unsigned flags=*reinterpret_cast<const uint16_t *>(model+0x56);
 const unsigned first=(flags&8)?flags&3:0;
 return first<model[0x54]?Atomic(model,distance):nullptr;
}
void *AtomicModel(void *atomic){for(auto &a:atomics)if(&a==atomic)return a.model;return nullptr;}
void *Fade(void *atomic,float distance){
 // Native RenderFadingAtomic subtracts20 before DIFFERENT GetAtomicFromDistance,
 // computes alpha from that same distance, then dereferences selected geometry.
 auto *model=static_cast<uint8_t *>(AtomicModel(atomic));CHECK(model);if(!model)return atomic;
 if(nestedFade){nestedFade=false;Function<void *(*)(void *,float)>("_ZN18CVisibilityPlugins18RenderFadingAtomicEPN2rw6AtomicEf")(&atomics[1],100);}
 auto *selected=Function<void *(*)(uint8_t *,float)>("_ZN16CSimpleModelInfo21GetAtomicFromDistanceEf")(model,distance-20);
 const float amount=std::min(1.0f,(Largest(model)-(distance-20))/20);
 const unsigned alpha=unsigned(std::max(0.0f,amount*model[0x55]));
 fadeResults.push_back({atomic,selected,distance,alpha});
 if(alpha<255&&!selected)++nullGeometry;
 return atomic;
}
void BuildingDraw(uint8_t *entity,float distance){
 if(*reinterpret_cast<const uint16_t *>(entity+0x64)&128)Fade(*reinterpret_cast<void **>(entity+0x58),distance);
}
void FlushWorldFades(){for(const auto &item:fadeQueue)Function<void(*)(uint8_t *,float)>("_ZN9CRenderer17RenderOneBuildingEP7CEntityf")(item.entity,item.distance);}
void FlushFades(){for(const auto &item:fadeQueue)Function<void *(*)(void *,float)>("_ZN18CVisibilityPlugins18RenderFadingAtomicEPN2rw6AtomicEf")(*reinterpret_cast<void **>(item.entity+0x58),item.distance);}
void Request(int id,int flags){requested.push_back(id);auto *s=stream.data()+size_t(id)*0x20;if(!s[0x10])s[0x10]=2;s[0x11]|=flags;}
void Deletable(int id){released.push_back(id);stream[size_t(id)*0x20+0x11]&=~1;}
int BigVisibility(uint8_t *e){
 const auto p=EntityPos(e);const float dx=rendererCamera.x-p.x,dy=rendererCamera.y-p.y,dz=rendererCamera.z-p.z;
 const float distance=std::sqrt(dx*dx+dy*dy+dz*dz);auto *model=modelTable[EntityModel(e)];
 // The native near-model handoff is decided BEFORE GetFirstAtomic: a
 // loaded high-detail model replaces this BIG only inside its original300m.
 if(distance<300&&e[0x70])return 0;
 auto atomic=Function<void *(*)(uint8_t *,float)>("_ZN16CSimpleModelInfo26GetFirstAtomicFromDistanceEf")(model,distance);
 const bool fringe=!atomic;
 if(!atomic)atomic=Function<void *(*)(uint8_t *,float)>("_ZN16CSimpleModelInfo26GetFirstAtomicFromDistanceEf")(model,distance-20);
 if(!atomic)return 3;
 // CCamera::IsSphereVisible uses CDraw::ms_fFarClipZ independently of RW.
 if(std::sqrt(dx*dx+dy*dy+dz*dz)>drawFar)return 0;
 if(fringe||model[0x55]<255){fadeQueue.push_back({e,distance});*reinterpret_cast<uint16_t *>(e+0x64)|=128;return 0;}
 return 1;
}
void Scan(airworld::PtrList &list){for(auto *n=list.first;n;n=n->next){
 auto *e=static_cast<uint8_t *>(n->entity);const int result=Function<int(*)(uint8_t *)>("_ZN9CRenderer26SetupBigBuildingVisibilityEP7CEntity")(e);
 if(result==1)visible.push_back(EntityModel(e));
}}
int DetailVisibility(uint8_t *e){
 if(e==plane){++nativeVehicleVisibilityCalls;return CamMode(ActiveCam())==16?0:1;}
 const CVector p=EntityPos(e);const float dx=rendererCamera.x-p.x,dy=rendererCamera.y-p.y,dz=rendererCamera.z-p.z;
 const float d=std::sqrt(dx*dx+dy*dy+dz*dz);auto *m=modelTable[EntityModel(e)];
 if(e[0x6d]||!(EntityFlags(e)&(1u<<18)))return 0;
 if(m[0x1d]==3&&!Time(m[0x58],m[0x5c]))return 0;
 auto *a=Function<void *(*)(uint8_t *,float)>("_ZN16CSimpleModelInfo21GetAtomicFromDistanceEf")(m,d);
 const bool fringe=!a;if(!a)a=Function<void *(*)(uint8_t *,float)>("_ZN16CSimpleModelInfo21GetAtomicFromDistanceEf")(m,d-20);
 if(!a)return 3;
 if(fringe||m[0x55]<255){fadeQueue.push_back({e,d});*reinterpret_cast<uint16_t *>(e+0x64)|=128;return 0;}
 return 1;
}
void Construct(){fadeQueue.clear();rendererCamera=*reinterpret_cast<const CVector *>(camera+0x30);
 for(int i=0;i<entityCount;++i){auto *e=entities[size_t(i)].data();if(EntityFlags(e)&(1u<<22))continue;
  if(Function<int(*)(uint8_t *)>("_ZN9CRenderer21SetupEntityVisibilityEP7CEntity")(e)==1)visible.push_back(EntityModel(e));
 }
 Scan(lists[level]);Scan(lists[0]);}
void Horizon(){++horizonCalls;}
void Add(int model,int list,CVector p,float lod=1700,bool loaded=true,bool highDetail=true){
 const int index=entityCount++;auto *e=entities[size_t(index)].data();modelTable[model]=models[index];
 *reinterpret_cast<int16_t *>(e+0x6a)=int16_t(model);*reinterpret_cast<CVector *>(e+0x38)=p;
 *reinterpret_cast<uint32_t *>(e+0x60)=1u|(1u<<22)|(1u<<23);e[0x6d]=0;e[0x70]=highDetail;
 models[index][0x1d]=1;models[index][0x54]=1;models[index][0x55]=255;atomics[size_t(index)]={models[index],e};*reinterpret_cast<void **>(e+0x58)=&atomics[size_t(index)];*reinterpret_cast<float *>(models[index]+0x48)=lod;
 *reinterpret_cast<int16_t *>(models[index]+0x2e)=int16_t(index);std::snprintf(reinterpret_cast<char *>(models[index]+8),21,"LODtest%d",index);
 *reinterpret_cast<void **>(models[index]+0x30)=loaded?e:nullptr;
 stream[size_t(model)*0x20+0x10]=loaded?1:0;stream[size_t(6500+index)*0x20+0x10]=loaded?1:0;
 *reinterpret_cast<uint32_t *>(stream.data()+size_t(model)*0x20+0x18)=16;
 *reinterpret_cast<uint32_t *>(stream.data()+size_t(6500+index)*0x20+0x18)=32;
 nodes[size_t(index)]={e,nullptr,lists[list].first};lists[list].first=&nodes[size_t(index)];
}
void AddDetail(int model,CVector p,CVector lo,CVector hi,float lod,bool loaded){
 Add(model,1,p,lod,loaded,false);const size_t i=size_t(entityCount-1);auto *e=entities[i].data();
 lists[1].first=nodes[i].next;*reinterpret_cast<uint32_t *>(e+0x60)=1u|(1u<<8)|(1u<<18);
 *reinterpret_cast<CVector *>(e+8)={1,0,0};*reinterpret_cast<CVector *>(e+0x18)={0,1,0};*reinterpret_cast<CVector *>(e+0x28)={0,0,1};
 *reinterpret_cast<CVector *>(bounds[i]+0x10)=lo;*reinterpret_cast<CVector *>(bounds[i]+0x1c)=hi;
 *reinterpret_cast<void **>(models[i]+0x20)=bounds[i];std::snprintf(reinterpret_cast<char *>(models[i]+8),21,"doontoon%d",model);
 const int x=std::clamp(int((p.x+2400)/50),0,79),y=std::clamp(int((p.y+2000)/50),0,79);
 auto &list=sectors[size_t(y*80+x)][0];nodes[i]={e,nullptr,list.first};list.first=&nodes[i];
}
void NativeUpdate(){
 // Native Update consumes one async asset, then drops unflagged pending
 // requests. A separate TXD must remain until its DFF is converted.
 bool consumed=false;
 for(int id=6500;id<6548;++id)if(stream[size_t(id)*0x20+0x10]==2&&!consumed){stream[size_t(id)*0x20+0x10]=1;consumed=true;}
 for(int id=0;id<6500;++id){auto *m=modelTable[id];if(!m)continue;auto *s=stream.data()+size_t(id)*0x20;
  const int txd=*reinterpret_cast<int16_t *>(m+0x2e);
  if(!consumed&&s[0x10]==2&&stream[size_t(6500+txd)*0x20+0x10]==1){s[0x10]=1;*reinterpret_cast<void **>(m+0x30)=entities[0].data();consumed=true;}
 }
 for(int id=0;id<8000;++id){auto *s=stream.data()+size_t(id)*0x20;if(s[0x10]==2&&!(s[0x11]&15))s[0x10]=0;}
}
void Frame(){++nativeFrame;visible.clear();Function<void(*)()>("_ZN9CRenderer19ConstructRenderListEv")();}
}
namespace planeflight {bool Owns(const uint8_t *v){return owned&&v==plane;}}
namespace hooks {
void *Install(const char *name,void *replacement){auto it=originals.find(name);if(it==originals.end())return nullptr;installed[name]=replacement;return it->second;}
void *Chain(const char *name,void *replacement){auto it=originals.find(name);if(it==originals.end()||!replacement)return nullptr;auto current=installed.find(name);void *next=current==installed.end()?it->second:current->second;if(next==replacement)return nullptr;installed[name]=replacement;return next;}
const void *ImageBase(){return reinterpret_cast<const void *>(0x100000000ull);}
bool IsCallTo(uint32_t,const char *){return true;}
}
// No GL context is created. The terrain consumer above exercises the real
// production hook path; native shader compilation/appearance remains live QA.
namespace shaders {
bool Ready(){return shadersReady;}int Uniform(const char *){return 0;}void *Create(const char *,const char *,const char *){return reinterpret_cast<void *>(1);}
void Set(int,float,float,float,float){}void OverrideIm2D(void *p){shaderOverride=p;}Sky CurrentSky(){return {};}
SavedStates::SavedStates(){std::memset(values_,0,sizeof values_);}SavedStates::~SavedStates(){}
}
void SetHudRenderStates(){}
void Im2DCanvas::SetTexture(void *){}void Im2DCanvas::SetDepthMode(DepthMode){}
void Im2DCanvas::Tri(const Vtx &,const Vtx &,const Vtx &,CRGBA,CRGBA,CRGBA){++draws;}void Im2DCanvas::Flush(){}
static void *Symbol(void *,const char *name){
 const std::map<std::string,void *> symbols={
 {"_ZN6CPools16ms_pBuildingPoolE",&buildingPointer},{"_ZN6CTimer14m_FrameCounterE",&nativeFrame},{"_ZN18CVisibilityPlugins18GetAtomicModelInfoEPN2rw6AtomicE",reinterpret_cast<void *>(AtomicModel)},{"_ZN6CPools15ms_pVehiclePoolE",&poolPointer},{"_ZN5CGame8currAreaE",&area},{"_ZN5CGame9currLevelE",&level},{"_ZN12CCutsceneMgr10ms_runningE",&cutscene},
 {"_ZN11CTheScripts18IsPlayerOnAMissionEv",reinterpret_cast<void *>(Mission)},
 {"_ZN5CGame25CanSeeOutSideFromCurrAreaEv",reinterpret_cast<void *>(Outside)},
 {"_ZN6CWorld19ms_bigBuildingsListE",lists},{"_ZN9CRenderer20ms_vecCameraPositionE",&rendererCamera},
 {"_ZN6CWorld11ms_aSectorsE",sectors.data()},
 {"_ZN6CClock16GetIsTimeInRangeEhh",reinterpret_cast<void *>(Time)},
 {"_ZN10CStreaming16ms_aInfoForModelE",stream.data()},{"_ZN10CStreaming19SetModelIsDeletableEi",reinterpret_cast<void *>(Deletable)},
 {"_ZN9CRenderer19ScanBigBuildingListER8CPtrList",reinterpret_cast<void *>(Scan)},
 {"_ZN10CTimeCycle17m_fCurrentFarClipE",&nativeFar},{"_ZN10CTimeCycle18m_fCurrentFogStartE",&nativeFog},
 {"_ZN10CTimeCycle18m_fCurrentWaterRedE",water},{"_ZN5CDraw12ms_fFarClipZE",&drawFar},{"_Z23RwCameraGetFarClipPlanePKN2rw6CameraE",reinterpret_cast<void *>(GetFar)},
 {"_Z22RwCameraGetFogDistancePKN2rw6CameraE",reinterpret_cast<void *>(GetFog)},
 {"_ZN16CSimpleModelInfo21GetLargestLodDistanceEv",reinterpret_cast<void *>(Largest)}
 };
 auto it=symbols.find(name);return it==symbols.end()?nullptr:it->second;
}
int main(){
 int32_t screen[4]={0,0,1280,720};api.rsGlobal=reinterpret_cast<uint8_t *>(screen);api.theCamera=camera;api.PoolsGetVehicle=PoolGet;api.FindPlayerPed=Player;api.timeMs=&now;api.modelInfoPtrs=modelTable;api.RequestModel=Request;
 *reinterpret_cast<void **>(camera+0x858)=&rw;*reinterpret_cast<float *>(camera+0xf8)=1;
 camera[0x63]=1;ped[0x4c0]=1;*reinterpret_cast<uint8_t **>(ped+0x4b8)=plane;*reinterpret_cast<float *>(ped+0x44c)=100;
 *reinterpret_cast<uint8_t **>(plane+0x218)=ped;*reinterpret_cast<uint32_t *>(plane+0x60)=2;gConfig.planes=true;
 originals={{"_ZN9CRenderer21SetupEntityVisibilityEP7CEntity",reinterpret_cast<void *>(DetailVisibility)},{"_ZN9CRenderer17RenderOneBuildingEP7CEntityf",reinterpret_cast<void *>(BuildingDraw)},{"_ZN18CVisibilityPlugins18RenderFadingAtomicEPN2rw6AtomicEf",reinterpret_cast<void *>(Fade)},{"_ZN16CSimpleModelInfo21GetAtomicFromDistanceEf",reinterpret_cast<void *>(AtomicForFade)},{"_ZN9CRenderer19ConstructRenderListEv",reinterpret_cast<void *>(Construct)},
 {"_ZN9CRenderer26SetupBigBuildingVisibilityEP7CEntity",reinterpret_cast<void *>(BigVisibility)},
 {"_ZN16CSimpleModelInfo26GetFirstAtomicFromDistanceEf",reinterpret_cast<void *>(Atomic)},
 {"_Z23RwCameraSetFarClipPlanePN2rw6CameraEf",reinterpret_cast<void *>(SetFar)},
 {"_Z22RwCameraSetFogDistancePN2rw6CameraEf",reinterpret_cast<void *>(SetFog)},
 {"_ZN7CClouds13RenderHorizonEv",reinterpret_cast<void *>(Horizon)}};
 cameraVisibilityNext=DetailVisibility;installed["_ZN9CRenderer21SetupEntityVisibilityEP7CEntity"]=reinterpret_cast<void *>(CameraVisibility);
 *reinterpret_cast<uint16_t *>(ActiveCam()+0xc)=16;
 CHECK(airworld::Install());
 CHECK(Function<int(*)(uint8_t *)>("_ZN9CRenderer21SetupEntityVisibilityEP7CEntity")(plane)==1);
 CHECK(CamMode(ActiveCam())==16&&nativeVehicleVisibilityCalls==1);
 Add(0,1,{0,0,0});Add(1,2,{1000,0,0});
 *reinterpret_cast<CVector *>(camera+0x30)={0,0,2000};
 Frame();CHECK(visible.size()==2);CHECK(drawFar>3000&&drawFar<=8000);CHECK(rw.far==drawFar);
 if(failures)return 1;
 // Crash2811 regression: native Setup queues original distance; the later
 // fade consumer reselects geometry through GetAtomicFromDistance. Admission
 // must survive through that real queue, rather than only selecting a BIG.
 modelTable[0][0x55]=64;Frame();CHECK(fadeQueue.size()==1);
 CHECK(fadeQueue[0].distance==2000); // native transparent sorting retains depth
 FlushFades();CHECK(nullGeometry==0);CHECK(fadeResults.size()==1);
 CHECK(fadeResults[0].selected!=nullptr&&fadeResults[0].alpha==64);
 // The alternate WorldRender path has its own copy of the fade consumer;
 // it must receive the same instance policy while retaining its sort key.
 fadeResults.clear();FlushWorldFades();CHECK(nullGeometry==0&&fadeResults.size()==1);
 CHECK(fadeResults[0].selected&&fadeResults[0].alpha==64);
 // GetFirst ignores damaged-atomic start flags while the fade selector
 // honors them. Such a model cannot acquire new altitude-only admission.
 *reinterpret_cast<uint16_t *>(modelTable[0]+0x56)=9;Frame();CHECK(fadeQueue.empty());
 *reinterpret_cast<uint16_t *>(modelTable[0]+0x56)=0;
 // Same model metadata at different world heights must retain two instance
 // corrections; a model-wide cache would apply the wrong draw distance.
 auto *second=entities[1].data();const CVector secondPosition=EntityPos(second);
 *reinterpret_cast<int16_t *>(second+0x6a)=0;atomics[1].model=modelTable[0];
 *reinterpret_cast<CVector *>(second+0x38)={1000,0,500};
 fadeResults.clear();Frame();CHECK(fadeQueue.size()==2);FlushFades();CHECK(nullGeometry==0&&fadeResults.size()==2);
 CHECK(std::fabs(fadeResults[0].distance-94.4487f)<.002f);
 CHECK(std::fabs(fadeResults[1].distance-148.6736f)<.002f);
 fadeResults.clear();FlushWorldFades();CHECK(nullGeometry==0&&fadeResults.size()==2);
 CHECK(std::fabs(fadeResults[0].distance-94.4487f)<.002f&&std::fabs(fadeResults[1].distance-148.6736f)<.002f);
 *reinterpret_cast<int16_t *>(second+0x6a)=1;atomics[1].model=modelTable[1];*reinterpret_cast<CVector *>(second+0x38)=secondPosition;
 // Setup's outer D-20 fallback and Fade's D-20 selector must describe the
 // SAME 20m band: ten metres beyond admission gives half native opacity.
 auto *first=entities[0].data();
 *reinterpret_cast<CVector *>(first+0x38)={3012.01f,0,0};fadeResults.clear();Frame();
 CHECK(fadeQueue.size()==1);FlushFades();CHECK(nullGeometry==0&&fadeResults.size()==1);
 CHECK(fadeResults[0].alpha>=31&&fadeResults[0].alpha<=33&&fadeResults[0].selected);
 fadeResults.clear();FlushWorldFades();CHECK(nullGeometry==0&&fadeResults.size()==1);
 CHECK(fadeResults[0].alpha>=31&&fadeResults[0].alpha<=33&&fadeResults[0].selected);
 *reinterpret_cast<CVector *>(first+0x38)={3025.3f,0,0};Frame();CHECK(fadeQueue.empty());
 *reinterpret_cast<CVector *>(first+0x38)={0,0,0};
 // Admission remains valid to this frame's consumer if controls/config change
 // between queue construction and drawing. Nested ordinary fades remain native.
 fadeResults.clear();Frame();owned=false;gConfig.planes=false;nestedFade=true;FlushFades();
 CHECK(nullGeometry==0&&fadeResults.size()==2);CHECK(fadeResults[0].atomic==&atomics[1]&&fadeResults[0].distance==100);
 CHECK(fadeResults[1].atomic==&atomics[0]&&fadeResults[1].alpha==64);owned=true;gConfig.planes=true;
 // Invalid frame, generation, current instance atomic, stream state and model
 // identity cannot inherit an earlier admission. Use an ordinary in-range
 // distance here so each native negative is a valid render on its own.
 *reinterpret_cast<CVector *>(camera+0x30)={0,0,600};
 for(int invalid=0;invalid<5;++invalid){
  Frame();CHECK(fadeQueue.size()==1);fadeResults.clear();
  void *originalObject=*reinterpret_cast<void **>(first+0x58);
  if(invalid==0)++nativeFrame;
  if(invalid==1)++buildingFlags[0];
  if(invalid==2)*reinterpret_cast<void **>(first+0x58)=&atomics[47];
  if(invalid==3)stream[0x10]=0;
  std::array<uint8_t,0x80> replacementModel{};
  if(invalid==4){std::memcpy(replacementModel.data(),modelTable[0],replacementModel.size());modelTable[0]=replacementModel.data();}
  Function<void *(*)(void *,float)>("_ZN18CVisibilityPlugins18RenderFadingAtomicEPN2rw6AtomicEf")(&atomics[0],600);
  CHECK(fadeResults.size()==1&&fadeResults[0].distance==600&&nullGeometry==0);
  if(invalid==1)--buildingFlags[0];
  *reinterpret_cast<void **>(first+0x58)=originalObject;stream[0x10]=1;modelTable[0]=models[0];
 }
 // Ordinary non-BIG and multi-atomic model fades retain native distance and
 // opacity, including the established20m high-detail falloff.
 *reinterpret_cast<uint32_t *>(first+0x60)&=~(1u<<22);
 *reinterpret_cast<CVector *>(first+0x38)={1595.94f,0,0};fadeResults.clear();Frame();FlushFades();
 CHECK(fadeResults.size()==1&&fadeResults[0].alpha>=47&&fadeResults[0].alpha<=49);
 CHECK(std::fabs(fadeResults[0].distance-1705)<.02f&&nullGeometry==0);
 *reinterpret_cast<uint32_t *>(first+0x60)|=(1u<<22);modelTable[0][0x54]=2;
 *reinterpret_cast<CVector *>(first+0x38)={0,0,0};fadeResults.clear();Frame();FlushFades();
 CHECK(fadeResults.size()==1&&fadeResults[0].distance==600&&fadeResults[0].alpha==64);
 modelTable[0][0x54]=1;modelTable[0][0x55]=255;
 *reinterpret_cast<CVector *>(camera+0x30)={0,0,2000};Frame();fadeResults.clear();
 auto fade=Function<void *(*)(void *,float)>("_ZN18CVisibilityPlugins18RenderFadingAtomicEPN2rw6AtomicEf");
 CHECK(fade(&atomics[0],2000)==&atomics[0]);CHECK(fadeResults.size()==1&&fadeResults[0].alpha==255&&fadeResults[0].selected);
 // A same-MI atomic that never traversed BIG admission keeps the native path.
 atomics[1].model=modelTable[0];fadeResults.clear();CHECK(fade(&atomics[1],1000)==&atomics[1]);
 CHECK(fadeResults.size()==1&&fadeResults[0].distance==1000&&fadeResults[0].alpha==255);atomics[1].model=modelTable[1];
 // The opaque WorldRender list passes distance0, rather than queued depth.
 fadeResults.clear();Function<void(*)(uint8_t *,float)>("_ZN9CRenderer17RenderOneBuildingEP7CEntityf")(first,0);
 CHECK(fadeResults.size()==1&&fadeResults[0].distance==0&&fadeResults[0].alpha==255&&nullGeometry==0);
 fadeResults.clear();
 if(failures)return 1;
 // Idle overwrites RW clipping after Construct; only its two verified main
 // camera call sites may consume the correction. Shadow cameras keep theirs.
 const uintptr_t base=0x100000000ull;
 CHECK(airworld::FarFor(&rw,900,base+0x141fd0)==drawFar);
 CHECK(airworld::FogFor(&rw,500,base+0x141fe4)>500);
 CHECK(airworld::FarFor(&rw,900,base+0x20ebe4)==900);
 RwCamera shadow;CHECK(airworld::FarFor(&shadow,900,base+0x141fd0)==900);
 // The actual horizon hook emits an ocean background and restores the shader
 // override; without GPU readiness it retains the native renderer.
 auto horizon=Function<void(*)()>("_ZN7CClouds13RenderHorizonEv");
 horizon();CHECK(horizonCalls==1&&draws==0);
 shadersReady=true;horizon();CHECK(horizonCalls==1&&draws==2&&!shaderOverride);
 // Actual passive flight samples43→44→53→89→90: CELL rings while the same
 // freeplay Learjet/ref1282 remains the player-driven, ordinary camera actor.
 // Its broad mission flag stays true for23,461ms (about23.46s), without a
 // camera/driver/autopilot takeover. Coverage must continue throughout.
 const uint32_t beforeCallTime=now;entities[0][0x70]=0;
 now=40894590;*reinterpret_cast<CVector *>(camera+0x30)={0,0,217.704041f};Frame();CHECK(visible.size()==2);
 auto *otherSource=*reinterpret_cast<void **>(modelTable[1]+0x30);
 stream[1*0x20+0x10]=0;stream[(6500+1)*0x20+0x10]=0;*reinterpret_cast<void **>(modelTable[1]+0x30)=nullptr;
 mission=true;now=40895134;*reinterpret_cast<CVector *>(camera+0x30)={0,0,229.475372f};requested.clear();Frame();
 CHECK(drawFar>nativeFar&&rw.far==drawFar);CHECK(!requested.empty());
 CHECK((stream[1*0x20+0x11]&1)&&(stream[(6500+1)*0x20+0x11]&1));
 NativeUpdate();now=40895678;Frame();NativeUpdate();
 for(const auto &sample:std::array<std::pair<uint32_t,float>,2>{{{40899827,258.263336f},{40918595,363.704193f}}}){
  now=sample.first;*reinterpret_cast<CVector *>(camera+0x30)={0,0,sample.second};Frame();
  CHECK(visible.size()==2&&drawFar>nativeFar);const int beforeDraw=draws;horizon();CHECK(draws==beforeDraw+2);
 }
 CHECK(!(stream[1*0x20+0x11]&1)&&!(stream[(6500+1)*0x20+0x11]&1));
 // A phone flag grants no exception to real actor/camera ownership loss.
 // Each transition restores native clipping and stops extra-island requests.
 for(int veto=0;veto<9;++veto){
  *reinterpret_cast<CVector *>(camera+0x30)={0,0,258.263336f};Frame();CHECK(drawFar>nativeFar);
  if(veto==0)*reinterpret_cast<uint8_t **>(plane+0x218)=nullptr;
  if(veto==1)owned=false;
  if(veto==2)*reinterpret_cast<uint32_t *>(plane+0x60)=2|(1u<<3);
  if(veto==3)camera[0x63]=0;
  if(veto==4)camera[0x75]=1;
  if(veto==5)cutscene=true;
  if(veto==6)area=1;
  if(veto==7)*reinterpret_cast<float *>(ped+0x44c)=0;
  if(veto==8)gConfig.planes=false;
  requested.clear();const int beforeDraw=draws;Frame();horizon();
  CHECK(drawFar==nativeFar&&rw.far==nativeFar&&requested.empty()&&draws==beforeDraw);
  *reinterpret_cast<uint8_t **>(plane+0x218)=ped;owned=true;*reinterpret_cast<uint32_t *>(plane+0x60)=2;
  camera[0x63]=1;camera[0x75]=0;cutscene=false;area=0;*reinterpret_cast<float *>(ped+0x44c)=100;gConfig.planes=true;
 }
 mission=false;now=40919139;*reinterpret_cast<CVector *>(camera+0x30)={0,0,353.560333f};Frame();CHECK(visible.size()==2&&drawFar>nativeFar);
 *reinterpret_cast<void **>(modelTable[1]+0x30)=otherSource;entities[0][0x70]=1;now=beforeCallTime;
 *reinterpret_cast<CVector *>(camera+0x30)={0,0,2000};Frame();
 // Loaded high-detail metadata must not hide BIG ground at high slant range.
 *reinterpret_cast<CVector *>(camera+0x30)={0,0,10};Frame();CHECK(visible.empty());CHECK(drawFar==nativeFar);
 // Lost eligibility restores the native camera and cannot warm other islands.
 for(int condition=0;condition<6;++condition){
  *reinterpret_cast<CVector *>(camera+0x30)={0,0,2000};Frame();
  if(condition==0){mission=true;owned=false;}if(condition==1)area=1;if(condition==2)outside=false;
  if(condition==3)owned=false;if(condition==4)gConfig.planes=false;if(condition==5)camera[0x75]=1;
  requested.clear();Frame();CHECK(visible.empty());CHECK(requested.empty());CHECK(drawFar==nativeFar&&rw.far==nativeFar);
  mission=false;area=0;outside=true;owned=true;gConfig.planes=true;camera[0x75]=0;
 }
 // A scripted camera may replace the far/fog values before ownership is
 // lost; restoration must not overwrite values written by that new owner.
 *reinterpret_cast<CVector *>(camera+0x30)={0,0,2000};Frame();
 rw.far=4321;drawFar=4321;rw.fog=1234;mission=true;camera[0x63]=0;Frame();
 CHECK(rw.far==4321&&drawFar==4321&&rw.fog==1234);mission=false;camera[0x63]=1;
 // Real production requests must survive native per-frame pruning until
 // separate TXD/DFF disk completions produce visible terrain.
 Add(2,1,{100,0,0},1700,false);requested.clear();Frame();
 CHECK(!requested.empty());CHECK(stream[2*0x20+0x11]&1);CHECK(stream[(6500+2)*0x20+0x11]&1);
 NativeUpdate();++now;Frame();NativeUpdate();++now;Frame();
 CHECK(std::find(visible.begin(),visible.end(),2)!=visible.end());CHECK(!(stream[2*0x20+0x11]&1));CHECK(!(stream[(6500+2)*0x20+0x11]&1));
 // Pending assets are released on an interior/config/mission/owner transition.
 Add(3,1,{200,0,0},1700,false);Frame();gConfig.planes=false;Frame();
 CHECK(!(stream[3*0x20+0x11]&1)&&!(stream[(6500+3)*0x20+0x11]&1));gConfig.planes=true;
 // Per-frame request work has a fixed admission limit even over dense city.
 for(int i=4;i<40;++i)Add(i,1,{float(i*10),0,0},1000,false);
 requested.clear();Frame();CHECK(requested.size()<=16);
 *reinterpret_cast<CVector *>(camera+0x30)={NAN,0,2000};requested.clear();Frame();CHECK(requested.empty());CHECK(drawFar==nativeFar);
 // Model metadata replacement invalidates old leases without clearing a
 // foreign owner's flags. Pending assets exceeding the disk budget are ignored.
 *reinterpret_cast<CVector *>(camera+0x30)={0,0,2000};
 gConfig.planes=false;Frame();gConfig.planes=true;
 std::array<uint8_t,0x80> replacement{};auto *prior=modelTable[4];
 Frame();std::memcpy(replacement.data(),prior,replacement.size());modelTable[4]=replacement.data();
 stream[4*0x20+0x11]=3;stream[(6500+4)*0x20+0x11]=3;released.clear();
 gConfig.planes=false;Frame();CHECK(stream[4*0x20+0x11]==3&&stream[(6500+4)*0x20+0x11]==3);gConfig.planes=true;modelTable[4]=prior;
 // A current native Skimmer driver receives the same presentation. Pool
 // generation and driver ownership reject stale pointers and passengers.
 *reinterpret_cast<CVector *>(camera+0x30)={0,0,2000};owned=false;
 *reinterpret_cast<int16_t *>(plane+0x6a)=190;*reinterpret_cast<int32_t *>(plane+0x348)=1;
 *reinterpret_cast<uint32_t *>(plane+0x60)=2;Frame();CHECK(!visible.empty());
 mission=true;Frame();CHECK(visible.empty()&&drawFar==nativeFar);mission=false;Frame();CHECK(!visible.empty());
 vehicleFlags[0]=135;Frame();CHECK(visible.empty());vehicleFlags[0]=7;
 *reinterpret_cast<uint8_t **>(plane+0x218)=nullptr;Frame();CHECK(visible.empty());
 *reinterpret_cast<uint8_t **>(plane+0x218)=ped;
 *reinterpret_cast<int16_t *>(plane+0x6a)=180;Frame();CHECK(visible.empty());owned=true;
 // Shared texture retention transfers to the second in-flight model.
 for(int i=0;i<40;++i)stream[size_t(i)*0x20+0x10]=1;
 Add(40,1,{0,100,0},1700,false);Add(41,1,{0,200,0},1700,false);
 *reinterpret_cast<int16_t *>(modelTable[41]+0x2e)=40;
 Frame();CHECK(stream[(6500+40)*0x20+0x11]&1);
 stream[40*0x20+0x10]=1;stream[(6500+40)*0x20+0x10]=1;++now;Frame();
 CHECK(stream[(6500+40)*0x20+0x11]&1);
 stream[41*0x20+0x10]=1;++now;Frame();CHECK(!(stream[(6500+40)*0x20+0x11]&1));
 Add(42,1,{0,300,0},1700,false);
 *reinterpret_cast<uint32_t *>(stream.data()+42*0x20+0x18)=9000;
 requested.clear();Frame();CHECK(std::find(requested.begin(),requested.end(),42)==requested.end());
 // Clock rewind releases matching persistent native keep bits.
 Add(43,1,{0,400,0},1700,false);Frame();CHECK(stream[43*0x20+0x11]&1);
 now=1;owned=false;Frame();CHECK(!(stream[43*0x20+0x11]&1));
 // Actual doontoon39 native bounds reach467m while its origin is58m and
 // original IDE LOD150m. The native-shaped ordinary consumer must stream
 // and select its ORIGINAL geometry at the rooftop, then survive its fade.
 owned=true;now=10000;*reinterpret_cast<CVector *>(camera+0x30)={-400.44f,1298.79f,350};
 *reinterpret_cast<CVector *>(plane+0x38)={-400.44f,1298.79f,350};
 AddDetail(1882,{-354.4389f,1298.7897f,58.115f},{-46.826f,-43.169f,-47.824f},{46.246f,42.571f,409.0893f},150,false);
 requested.clear();Frame();CHECK(std::find(requested.begin(),requested.end(),1882)!=requested.end());
 for(int i=0;i<100&&stream[1882*0x20+0x10]!=1;++i){NativeUpdate();++now;Frame();}
 CHECK(std::find(visible.begin(),visible.end(),1882)!=visible.end());
 CHECK(stream[1882*0x20+0x11]&1); // retain ready original through native memory pressure
 modelTable[1882][0x55]=64;fadeResults.clear();Frame();FlushFades();
 CHECK(nullGeometry==0);bool towerFaded=false;for(const auto &f:fadeResults)if(f.atomic==&atomics[size_t(entityCount-1)])towerFaded=f.selected&&f.alpha==64;CHECK(towerFaded);
 modelTable[1882][0x55]=255;
 CHECK(airworld::OwnsFlightView(plane));const CVector lastRender=airworld::gPosition;
 mission=true;CHECK(airworld::OwnsFlightView(plane));mission=false; // same independent phone-call actor
 for(int veto=0;veto<5;++veto){
  if(veto==0)camera[0x75]=1;if(veto==1)camera[0x63]=0;if(veto==2)area=1;if(veto==3)owned=false;if(veto==4)*reinterpret_cast<uint8_t **>(plane+0x218)=nullptr;
  CHECK(!airworld::OwnsFlightView(plane));
  camera[0x75]=0;camera[0x63]=1;area=0;owned=true;*reinterpret_cast<uint8_t **>(plane+0x218)=ped;
 }
 CHECK(!airworld::OwnsFlightView(ped)&&!airworld::OwnsFlightView(nullptr));
 CHECK(!std::memcmp(&lastRender,&airworld::gPosition,sizeof lastRender));
 // Same metadata at a distant instance cannot inherit the nearby tower's
 // correction. The whole Setup→queue→fade consumer remains instance-scoped.
 AddDetail(1882,{700,1298.7897f,58.115f},{-46.826f,-43.169f,-47.824f},{46.246f,42.571f,409.0893f},150,true);
 atomics[size_t(entityCount-2)].model=modelTable[1882];
 Frame();CHECK(std::count(visible.begin(),visible.end(),1882)==1);
 camera[0x75]=1;requested.clear();Frame();CHECK(std::find(visible.begin(),visible.end(),1882)==visible.end()&&requested.empty());camera[0x75]=0;
 // The original radio-mast roof lies409m above its IPL origin; an opaque
 // original must replace only its own related proxy, even above300m slant.
 *reinterpret_cast<CVector *>(camera+0x30)={-400.44f,1298.79f,450};
 Add(1898,1,{-354.4389f,1298.7897f,58.115f},2000,true);
 auto *proxy=entities[size_t(entityCount-1)].data();for(int row=0;row<3;++row)std::memcpy(proxy+8+size_t(row)*16,entities[44].data()+8+size_t(row)*16,sizeof(CVector));
 *reinterpret_cast<void **>(modelTable[1898]+0x40)=modelTable[1882];
 Frame();CHECK(std::count(visible.begin(),visible.end(),1882)==1&&std::count(visible.begin(),visible.end(),1898)==0);
 modelTable[1882][0x55]=64;Frame();CHECK(std::count(visible.begin(),visible.end(),1898)==1);modelTable[1882][0x55]=255;
 modelTable[1882][0x1d]=3;timeActive=false;Frame();CHECK(std::count(visible.begin(),visible.end(),1882)==0&&std::count(visible.begin(),visible.end(),1898)==1);timeActive=true;modelTable[1882][0x1d]=1;
 // Preserve the native20m alpha band using distance from the real bounds,
 // while the queue's sort key still contains the physical origin distance.
 *reinterpret_cast<CVector *>(camera+0x30)={-148.1929f,1298.79f,400};fadeResults.clear();Frame();FlushFades();
 bool roofFringe=false;for(const auto &f:fadeResults)if(f.atomic==&atomics[44])roofFringe=f.selected&&std::fabs(f.distance-160)<.01f&&f.alpha>=126&&f.alpha<=128;CHECK(roofFringe&&nullGeometry==0);
 // Warm ahead in the real aircraft velocity direction without expanding
 // its present detail draw radius. Stationary flight cannot request this
 // actual tower55; a110m/s approach receives the same bounded async path.
 *reinterpret_cast<CVector *>(camera+0x30)={-250,1298.79f,450};
 AddDetail(1889,{-554.4275f,1173.8754f,54.1643f},{-62.7406f,-60.9051f,-52.7801f},{62.7405f,62.212f,218.5135f},250,false);
 requested.clear();Frame();CHECK(std::find(requested.begin(),requested.end(),1889)==requested.end());
 *reinterpret_cast<CVector *>(plane+0x84)={-2.2f,0,0};requested.clear();Frame();CHECK(std::find(requested.begin(),requested.end(),1889)!=requested.end());
 CHECK(requested.size()<=16);*reinterpret_cast<CVector *>(plane+0x84)={0,0,0};
 // Reverse installation order: the actual camera callback wraps airworld's
 // native-selector scope; both still see the same original consumer once.
 installed["_ZN9CRenderer21SetupEntityVisibilityEP7CEntity"]=reinterpret_cast<void *>(airworld::Hook_EntityVisibility);
 airworld::orig_EntityVisibility=DetailVisibility;
 cameraVisibilityNext=reinterpret_cast<int(*)(uint8_t *)>(hooks::Chain("_ZN9CRenderer21SetupEntityVisibilityEP7CEntity",reinterpret_cast<void *>(CameraVisibility)));
 nativeVehicleVisibilityCalls=0;CHECK(Function<int(*)(uint8_t *)>("_ZN9CRenderer21SetupEntityVisibilityEP7CEntity")(plane)==1);
 CHECK(CamMode(ActiveCam())==16&&nativeVehicleVisibilityCalls==1);
 Frame();CHECK(std::count(visible.begin(),visible.end(),1882)==1);
 // Source/model/frame identities still guard ordinary fade admission.
 modelTable[1882][0x55]=64;fadeResults.clear();Frame();CHECK(!fadeQueue.empty());
 ++buildingFlags[44];FlushFades();bool stale=false;for(const auto &f:fadeResults)if(f.atomic==&atomics[44])stale=!f.selected;CHECK(stale);--buildingFlags[44];modelTable[1882][0x55]=255;
 // The scoped policy cannot turn an ordinary ground view into an aircraft
 // detail view, and releases its acquired stream pins immediately.
 *reinterpret_cast<CVector *>(camera+0x30)={-400.44f,1298.79f,50};requested.clear();Frame();
 CHECK(!airworld::OwnsFlightView(plane)&&requested.empty()&&!(stream[1889*0x20+0x11]&1));
 // A malformed native streaming overlap list has a hard walk bound; it
 // cannot consume unlimited plugin work or admit unlimited asynchronous I/O.
 *reinterpret_cast<CVector *>(camera+0x30)={-400.44f,1298.79f,450};
 auto *savedNext=nodes[44].next;nodes[44].next=&nodes[44];requested.clear();Frame();CHECK(requested.size()<=16);nodes[44].next=savedNext;
 if(!failures)std::puts("airworld native consumers: all passed");return failures?1:0;
}

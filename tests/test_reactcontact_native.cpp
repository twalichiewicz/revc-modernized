// Exercise Install and the native refinement ABI, without a game or Body solver.
#include "reactworld.h"
#include <array>
#include <dlfcn.h>
#include <limits>
namespace {
using namespace reactfixture;
alignas(16) uint8_t entities[18][0x350], infos[18][0x30], cols[18][0x68];
uint8_t *modelInfos[6500];
unsigned entityCount, nativeCalls, modelCalls;
bool malformed, unknown, omitModels;
int modelMode;
bool nestedRunning;
unsigned nestedCalls;
float queryThreshold;
bool returnIgnoredActor;
void *Symbol(void *, const char *);
uint8_t *Sphere(CVector c,float r,uint8_t *ignore,bool b,bool v,bool p,bool o,bool d,bool i)
{
 assert(b&&v&&!p&&o&&!d&&!i);
 ++nativeCalls;
 if(c.x+r<=queryThreshold)return nullptr;
 if(returnIgnoredActor)return ignore;
 for(unsigned j=0;j<entityCount;j++) {
  auto *e=entities[j];
  if(e==ignore || !(*reinterpret_cast<uint32_t *>(e+0x60)&0x100))continue;
  // Vehicles may return only their broad envelope. The model ABI below
  // must independently distinguish a real primitive from that envelope.
  if(objects[j].ghostCar)return e;
  Vec3 center{c.x,c.y,c.z}; float distance=r; Vec3 closest{};bool hit=false;
  for(const auto &t:objects[j].triangles){auto q=Closest(center,t);float a=Length(Sub(center,q));if(a<distance){distance=a;closest=q;hit=true;}}
  if(hit){nativePoint.point=closest;nativePoint.normal=Unit(Sub(center,closest));nativePoint.depth=r-distance;return e;}
 }
 return nullptr;
}
int Models(const uint8_t *ma,uint8_t *a,const uint8_t *mb,uint8_t *b,uint8_t *out,uint8_t *lines,float *distances)
{
 ++modelCalls;
 assert(!lines&&!distances);
 assert(reinterpret_cast<uintptr_t>(ma)%16==0 && reinterpret_cast<uintptr_t>(a)%16==0);
 assert(*reinterpret_cast<const int16_t *>(a+0x28)==1 && a[0x2e]==0);
 auto *sphere=*reinterpret_cast<const uint8_t *const *>(a+0x38);
 assert(sphere && *reinterpret_cast<const float *>(sphere)==0 && *reinterpret_cast<const float *>(sphere+4)==0);
 const Vec3 center=*reinterpret_cast<const Vec3 *>(ma+0x30);
 const float radius=*reinterpret_cast<const float *>(sphere+0xc);
 assert(*reinterpret_cast<const float *>(ma)==1 && *reinterpret_cast<const float *>(ma+0x14)==1 && *reinterpret_cast<const float *>(ma+0x28)==1);
 unsigned j=0;while(j<entityCount&&mb!=entities[j]+8)++j;assert(j<entityCount&&b==cols[j]);
 if(unknown)return -1;
 if(modelMode==9){*reinterpret_cast<uint8_t **>(infos[j]+0x20)=nullptr;return 0;}
 if(modelMode==10)return 2;
 if(modelMode==11){auto &p=*reinterpret_cast<reactcontact::NativePoint *>(out);p.depth=std::numeric_limits<float>::quiet_NaN();return 1;}
 if(modelMode==13){*reinterpret_cast<float *>(entities[j]+0x38)=.2f;return 0;}
 if(modelMode==14){*reinterpret_cast<uint8_t **>(cols[j]+0x58)=nullptr;return 0;}
 if(modelMode==20){*reinterpret_cast<float *>(cols[j]+0x10)=.25f;return 0;}
 if(modelMode==12 && j==1 && !nestedRunning) {
  nestedRunning=true;
  uint32_t before[18];for(unsigned k=0;k<entityCount;k++)before[k]=*reinterpret_cast<uint32_t *>(entities[k]+0x60);
  const auto saved=nativePoint;
  const auto inner=reactcontact::Sweep({0,0,2},{.1f,0,2},.18f,nullptr);
  assert(!inner.hit && inner.position.x>.099f);
  for(unsigned k=0;k<entityCount;k++)assert(before[k]==*reinterpret_cast<uint32_t *>(entities[k]+0x60));
  assert(!std::memcmp(&saved,&nativePoint,sizeof saved));
  ++nestedCalls;nestedRunning=false;
 }
 if(modelMode==15 && j==1 && !nestedRunning) {
  nestedRunning=true;
  uint32_t before[18];for(unsigned k=0;k<entityCount;k++)before[k]=*reinterpret_cast<uint32_t *>(entities[k]+0x60);
  const auto saved=nativePoint;
  const auto inner=reactcontact::Sweep({0,0,1.5f},{0,0,2.5f},.18f,nullptr);
  assert(inner.hit && inner.position.z<=1.821f && inner.position.z>1.7f);
  for(unsigned k=0;k<entityCount;k++)assert(before[k]==*reinterpret_cast<uint32_t *>(entities[k]+0x60));
  assert(!std::memcmp(&saved,&nativePoint,sizeof saved));
  ++nestedCalls;nestedRunning=false;
 }
 if(malformed){std::memset(out,0,sizeof(reactcontact::NativePoint));return 1;}
 // The actual native solver rejects against B's bounds before evaluating
 // its primitives. Invalid or stale bounds must never certify a clear car.
 const Vec3 lo=*reinterpret_cast<const Vec3 *>(b+0x10),hi=*reinterpret_cast<const Vec3 *>(b+0x1c);
 if(center.x+radius<lo.x||center.y+radius<lo.y||center.z+radius<lo.z||
    center.x-radius>hi.x||center.y-radius>hi.y||center.z-radius>hi.z)return 0;
 float distance=radius;Vec3 closest{};bool hit=false;
 for(const auto &t:objects[j].triangles){auto q=Closest(center,t);float d=Length(Sub(center,q));if(d<distance){distance=d;closest=q;hit=true;}}
 if(!hit)return 0;
 auto &point=*reinterpret_cast<reactcontact::NativePoint *>(out);
 point.point=closest;point.normal=Unit(Sub(center,closest));point.depth=radius-distance;return 1;
}
void Prepare(unsigned ghosts)
{
 Reset();std::memset(entities,0,sizeof entities);std::memset(infos,0,sizeof infos);std::memset(cols,0,sizeof cols);std::memset(modelInfos,0,sizeof modelInfos);
 entityCount=ghosts;nativeCalls=modelCalls=0;malformed=unknown=omitModels=false;modelMode=-1;nestedRunning=false;nestedCalls=0;queryThreshold=-std::numeric_limits<float>::infinity();returnIgnoredActor=false;
 for(unsigned j=0;j<ghosts;j++){
  objects.push_back({{},true,true});
  *reinterpret_cast<uint32_t *>(entities[j]+0x60)=0x12340902;
  *reinterpret_cast<int16_t *>(entities[j]+0x6a)=int16_t(j);
  modelInfos[j]=infos[j];*reinterpret_cast<uint8_t **>(infos[j]+0x20)=cols[j];
  *reinterpret_cast<float *>(cols[j]+0xc)=52;
  *reinterpret_cast<Vec3 *>(cols[j]+0x10)={-30,-30,-30};
  *reinterpret_cast<Vec3 *>(cols[j]+0x1c)={30,30,30};
  *reinterpret_cast<int16_t *>(cols[j]+0x2c)=1;
  *reinterpret_cast<uint8_t **>(cols[j]+0x50)=cols[j];
  *reinterpret_cast<uint8_t **>(cols[j]+0x58)=cols[j];
  *reinterpret_cast<float *>(entities[j]+8)=1;
  *reinterpret_cast<float *>(entities[j]+8+0x14)=1;
  *reinterpret_cast<float *>(entities[j]+8+0x28)=1;
 }
 nativePoint={{12,13,14},0,{0,1,0},0,1,2,3,4,.09f};
}
void Restored(const reactcontact::NativePoint &saved)
{
 assert(!std::memcmp(&saved,&nativePoint,sizeof saved));
 for(unsigned j=0;j<entityCount;j++)assert(*reinterpret_cast<uint32_t *>(entities[j]+0x60)==0x12340902);
}
}
#define dlsym Symbol
#include "../src/reactcontact.cpp"
#undef dlsym
namespace {
void *Symbol(void *,const char *name)
{
 if(std::strstr(name,"TestSphereAgainstWorld"))return reinterpret_cast<void *>(Sphere);
 if(!std::strcmp(name,"gaTempSphereColPoints"))return &nativePoint;
 if(std::strstr(name,"ProcessColModels"))return omitModels?nullptr:reinterpret_cast<void *>(Models);
 if(std::strstr(name,"ms_modelInfoPtrs"))return modelInfos;
 return nullptr;
}
void EnvelopeAndBehind()
{
 Prepare(2);assert(reactcontact::Install());const auto saved=nativePoint;
 auto clear=reactcontact::Sweep({0,0,1},{1,0,1},.18f,nullptr);
 assert(!clear.hit && clear.resolved && clear.position.x>.999f && modelCalls>0);Restored(saved);
 // The third object lies behind both proven-clear vehicle envelopes.
 Plane({1,0,0},.6f);objects.back().vehicle=false;objects.back().ghostCar=false;
 unsigned j=entityCount++;*reinterpret_cast<uint32_t *>(entities[j]+0x60)=0x12340902;
 auto wall=reactcontact::Sweep({0,0,1},{1,0,1},.18f,nullptr);
 assert(wall.hit && wall.position.x<=.421f);Restored(saved);
}
void RealCarAndMalformed()
{
 Prepare(1);Plane({1,0,0},.6f);objects[0].triangles=objects.back().triangles;objects.pop_back();
 assert(reactcontact::Install());const auto saved=nativePoint;
 auto car=reactcontact::Sweep({0,0,1},{1,0,1},.18f,nullptr);
 assert(car.hit&&car.position.x<=.421f&&car.position.x>.3f);Restored(saved);
 auto ignored=reactcontact::Sweep({0,0,1},{1,0,1},.18f,entities[0]);assert(!ignored.hit&&ignored.position.x>.999f);Restored(saved);
 for(int mode=0;mode<21;mode++) {
  if(mode==12||mode==15)continue;
  modelMode=mode;
  malformed=mode==0;unknown=mode==1;omitModels=mode==2;
  *reinterpret_cast<uint8_t **>(infos[0]+0x20)=mode==3?nullptr:cols[0];
  *reinterpret_cast<int16_t *>(entities[0]+0x6a)=mode==4?-1:0;
  *reinterpret_cast<int16_t *>(cols[0]+0x2c)=mode==6?-1:mode==7?0:1;
  *reinterpret_cast<uint8_t **>(cols[0]+0x50)=mode==5?nullptr:cols[0];
  *reinterpret_cast<uint8_t **>(cols[0]+0x58)=cols[0];
  *reinterpret_cast<Vec3 *>(cols[0]+0x10)=mode==16?Vec3{std::numeric_limits<float>::quiet_NaN(),-30,-30}:mode==17?Vec3{40,40,40}:Vec3{-30,-30,-30};
  *reinterpret_cast<float *>(cols[0]+0xc)=mode==18?-1:mode==19?std::numeric_limits<float>::quiet_NaN():52;
  *reinterpret_cast<float *>(entities[0]+0x38)=0;
  *reinterpret_cast<float *>(entities[0]+8)=mode==8?std::numeric_limits<float>::quiet_NaN():1;
  assert(reactcontact::Install());
  auto blocked=reactcontact::Sweep({0,0,1},{1,0,1},.18f,nullptr);
  assert(blocked.hit&&!blocked.resolved&&blocked.position.x<.001f);Restored(saved);
 }
}
void NestedAndUnknownRestoration()
{
 Prepare(2);assert(reactcontact::Install());const auto saved=nativePoint;
 modelMode=12;
 const auto outer=reactcontact::Sweep({0,0,1},{.1f,0,1},.18f,nullptr);
 assert(!outer.hit && nestedCalls>0);Restored(saved);
 modelMode=-1;*reinterpret_cast<uint8_t **>(infos[1]+0x20)=nullptr;
 const auto unknownResult=reactcontact::Sweep({0,0,1},{1,0,1},.18f,nullptr);
 assert(unknownResult.hit&&!unknownResult.resolved&&unknownResult.position.x<.001f);Restored(saved);
 const auto stationary=reactcontact::Sweep({0,0,1},{0,0,1},.18f,nullptr);
 assert(stationary.hit&&!stationary.resolved);Restored(saved);
}
void NestedQuerySeesOuterFilteredGeometry()
{
 Prepare(2);Plane({0,0,1},2);
 objects[0].triangles=objects.back().triangles;objects.pop_back();
 assert(reactcontact::Install());const auto saved=nativePoint;
 // car0 is clear at the outer location but obstructs the inner sweep.
 // The outer native callback runs while car0's broad envelope is filtered.
 modelMode=15;
 const auto outer=reactcontact::Sweep({0,0,1},{.1f,0,1},.18f,nullptr);
 assert(!outer.hit && outer.position.x>.099f && nestedCalls>0);Restored(saved);
}
void BudgetRestoration()
{
 Prepare(16);Plane({1,0,0},.6f);unsigned j=entityCount++;
 *reinterpret_cast<uint32_t *>(entities[j]+0x60)=0x12340902;
 assert(reactcontact::Install());const auto saved=nativePoint;
 reactcontact::NativeBindings bindings{Models,modelInfos};
 reactcontact::World world{Sphere,&nativePoint,&bindings};reactcontact::Stats stats;
 const auto result=reactcontact::Sweep(world,{0,0,1},{1,0,1},.18f,nullptr,&stats);
 assert(result.hit&&!result.resolved&&result.position.x<=.421f&&stats.exhausted&&stats.queries==reactcontact::kMaxQueries);
 assert(stats.queries==nativeCalls+modelCalls);Restored(saved);
}
void MovingMalformedStatus()
{
 for(int mode:{0,11}) {
  Prepare(1);queryThreshold=.6f;modelMode=mode;malformed=mode==0;
  assert(reactcontact::Install());const auto saved=nativePoint;
  reactcontact::NativeBindings bindings{Models,modelInfos};
  reactcontact::World world{Sphere,&nativePoint,&bindings};reactcontact::Stats stats;
  const auto result=reactcontact::Sweep(world,{0,0,1},{1,0,1},.18f,nullptr,&stats);
  assert(result.hit&&!result.resolved&&stats.unresolved&&!stats.exhausted&&result.position.x<=.421f);
  Restored(saved);
 }
}
void MovingReturnedActorStatus()
{
 Prepare(1);queryThreshold=.6f;returnIgnoredActor=true;
 assert(reactcontact::Install());const auto saved=nativePoint;
 reactcontact::NativeBindings bindings{Models,modelInfos};
 reactcontact::World world{Sphere,&nativePoint,&bindings};reactcontact::Stats stats;
 const auto result=reactcontact::Sweep(world,{0,0,1},{1,0,1},.18f,entities[0],&stats);
 assert(result.hit&&!result.resolved&&stats.unresolved&&!stats.exhausted&&result.position.x<=.421f);
 Restored(saved);
}
void CandidateBound()
{
 Prepare(18);assert(reactcontact::Install());const auto saved=nativePoint;
 auto result=reactcontact::Sweep({0,0,1},{1,0,1},.18f,nullptr);
 assert(result.hit&&result.position.x<.001f&&nativeCalls<=reactcontact::kMaxQueries);Restored(saved);
}
}
int main(int argc,char **){if(argc>1){MovingReturnedActorStatus();return 0;}MovingMalformedStatus();MovingReturnedActorStatus();EnvelopeAndBehind();RealCarAndMalformed();NestedAndUnknownRestoration();NestedQuerySeesOuterFilteredGeometry();BudgetRestoration();CandidateBound();puts("all native contact refinement ABI tests passed");}

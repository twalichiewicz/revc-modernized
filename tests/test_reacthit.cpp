// Exercise the production common LOS hook against pristine arm64 collision
// instructions, including pool validation, native masks and nested callers.
#include "game.h"
#include "reactrig.h"
#include "nativejetshadowarm.h"
#include <limits>
#include <dlfcn.h>
namespace game { Api api{}; }
namespace fixture {
void *Lookup(void *,const char *);
void *Chain(const char *,void *);
bool Pose(uint8_t *,reactrig::Matrices &);
}
namespace reactions { bool CopyHitPose(uint8_t *ped,reactrig::Matrices &pose){return fixture::Pose(ped,pose);} }
namespace hooks { void *Chain(const char *name,void *replacement){return fixture::Chain(name,replacement);} }
#define dlsym fixture::Lookup
#include "../src/reacthit.cpp"
#undef dlsym
namespace fixture {
using nativejetshadowtest::Store;
using nativeweavetest::Require;
using namespace reacthit;
alignas(16) std::array<uint8_t,0x8d0*2> actors{};
uint8_t generations[2]{17,33};
struct Pool {uint8_t *entries,*flags;int count;};
Pool pool{actors.data(),generations,2};Pool *poolPtr=&pool;
std::array<uint8_t,0x80> info{};void *models[6500]{};
Model nativeModel{};Sphere nativeSpheres[10]{};
reactrig::Matrices poses[2];bool available[2]{true,true},stale=false;
Narrow native,entry;
void *seenMatrix,*seenModel;bool seenSee,seenShoot,inspectOnly=false,nested=false,inNested=false;
int calls,failures;
void Check(bool good,const char *message){if(!good){std::printf("FAIL: %s\n",message);++failures;}}
uint8_t *Ped(int ref){for(int i=0;i<2;i++)if(ref==(i*256|generations[i]))return stale?nullptr:actors.data()+size_t(i)*0x8d0;return nullptr;}
bool Pose(uint8_t *ped,reactrig::Matrices &pose){for(int i=0;i<2;i++)if(ped==actors.data()+size_t(i)*0x8d0&&available[i]){pose=poses[i];return true;}return false;}
void *Lookup(void *,const char *name){if(!std::strcmp(name,"_ZN6CPools11ms_pPedPoolE"))return &poolPtr;if(!std::strcmp(name,"_ZN10CModelInfo16ms_modelInfoPtrsE"))return models;return nullptr;}
bool Next(const Line &line,const void *matrix,void *source,Point &point,float &closest,bool see,bool shoot){
 ++calls;seenMatrix=const_cast<void *>(matrix);seenModel=source;seenSee=see;seenShoot=shoot;
 if(inspectOnly)return false;
 if(nested&&!inNested){
  const auto saved=*static_cast<Model *>(source);std::array<Sphere,10> spheres;std::memcpy(spheres.data(),saved.sphere,sizeof spheres);
  inNested=true;Point inner{};float fraction=1;const Line ray{{-1.2f,-1,.25f},0,{-1.2f,1,.25f},0};
  Check(entry(ray,actors.data()+0x8d0+8,&nativeModel,inner,fraction,false,false)&&inner.pieceB==6,"nested actor sharing model info keeps its own pose and piece");inNested=false;
  Check(!std::memcmp(&saved,source,sizeof saved)&&!std::memcmp(spheres.data(),saved.sphere,sizeof spheres),"nested native query leaves outer stack COL and sphere positions unchanged");
 }
 return native(line,matrix,source,point,closest,see,shoot);
}
void *Chain(const char *name,void *replacement){Check(!std::strcmp(name,"_ZN10CCollision18ProcessLineOfSightERK8CColLineRK7CMatrixR9CColModelR9CColPointRfbb"),"only common native narrow LOS is hooked");entry=reinterpret_cast<Narrow>(replacement);return reinterpret_cast<void *>(Next);}
struct Native {
 nativejetshadowtest::Code code;
 Native(){
  code.Add(0x10004a440,0x10004a60c);code.Add(0x10004bc00,0x10004c028);code.Add(0x100049530,0x1000497e8);code.Add(0x10004ad0c,0x10004ae34);
  code.Add(0x1001723fc,0x1001724d0);code.Add(0x100172e50,0x100172e94);code.Add(0x100172ec0,0x100172fb0);
  *code.Page(0x1007b7c08)=1;code.Finish();native=reinterpret_cast<Narrow>(code.Address(0x10004bc00));
 }
};
void Setup(){
 actors={};info={};stale=false;inspectOnly=nested=inNested=false;available[0]=available[1]=true;generations[0]=17;generations[1]=33;
 pool={actors.data(),generations,2};models[0]=info.data();Store(info.data()+0x50,static_cast<void *>(&nativeModel));
 nativeModel={};nativeModel.spheres=10;nativeModel.sphere=nativeSpheres;nativeModel.radius=2;nativeModel.minimum={-.5f,-.5f,-1.2f};nativeModel.maximum={.5f,.5f,1.2f};
 constexpr uint8_t pieces[10]={6,0,0,1,2,3,4,5,4,5};
 for(int i=0;i<10;i++)nativeSpheres[i]={{0,0,0},i==0?.15f:.2f,17,pieces[i],0};
 for(int i=0;i<2;i++){
  auto *ped=actors.data()+size_t(i)*0x8d0;auto *m=reinterpret_cast<float *>(ped+8);m[0]=m[5]=m[10]=1;Store(ped+0x60,uint32_t(3|game::kEntityUsesCollision));
  for(auto &bone:poses[i]){bone={};bone[0]=bone[5]=bone[10]=1;bone[14]=.2f;}poses[i][5][12]=i?-1.2f:1.2f;
 }
 game::api.PoolsGetPed=Ped;calls=0;
}
Line Head(){return {{1.2f,-1,.25f},0,{1.2f,1,.25f},0};}
void Hits(){
 Setup();const Model saved=nativeModel;std::array<Sphere,10> spheres;std::memcpy(spheres.data(),nativeSpheres,sizeof spheres);const auto rig=poses[0];
 Point point{};float closest=1;Check(entry(Head(),actors.data()+8,&nativeModel,point,closest,true,true),"native LOS hits visible head outside upright model bounds");
 Check(point.pieceB==6&&point.surfaceB==17&&std::fabs(point.p.x-1.2f)<1e-5f&&std::fabs(point.p.y+.15f)<1e-4f&&std::fabs(closest-.425f)<1e-4f,"native piece material point normal and nearest fraction survive projection");
 Check(seenSee&&seenShoot,"both native material filter flags reach collision predecessor");
 Check(!std::memcmp(&saved,&nativeModel,sizeof saved)&&!std::memcmp(spheres.data(),nativeSpheres,sizeof spheres)&&poses[0]==rig,"query never mutates shared native COL, spheres or HAnim pose");
 closest=.3f;point={};point.pieceB=99;Check(!entry(Head(),actors.data()+8,&nativeModel,point,closest,false,false)&&closest==.3f&&point.pieceB==99,"nearer world obstruction bounds the ped test without fabricating an impact");
 nativeSpheres[0].surface=7;closest=1;Check(!entry(Head(),actors.data()+8,&nativeModel,point,closest,true,false),"native see-through surface masks remain effective");
 closest=1;Check(entry(Head(),actors.data()+8,&nativeModel,point,closest,false,false)&&point.surfaceB==7,"same native surface hits when its filter is disabled");
 Setup();nested=true;closest=1;Check(entry(Head(),actors.data()+8,&nativeModel,point,closest,false,false)&&point.pieceB==6,"outer native caller retains actor after nested query");
 float left=-1.25f,top=1.25f,right=1.25f,bottom=-1.25f;reacthit::ExpandBounds(actors.data(),left,top,right,bottom);Check(left==-1.25f&&top==1.25f&&bottom==-1.25f&&right>=1.349f,"sector bounds merge covers posed extremity beyond native root sphere");
 available[0]=false;const float before=right;reacthit::ExpandBounds(actors.data(),left,top,right,bottom);Check(right==before,"untracked actor leaves membership bounds unchanged");
 Setup();Store(actors.data()+0x38,48.6f);for(auto &bone:poses[0])bone[12]+=48.6f;poses[0][5][12]=50.0f;
 const auto edgePose=poses[0];const Model edgeModel=nativeModel;std::array<Sphere,10> edgeSpheres;std::memcpy(edgeSpheres.data(),nativeSpheres,sizeof edgeSpheres);
 left=48.6f-1.25f;right=48.6f+1.25f;top=1.25f;bottom=-1.25f;const int oldSector=int(std::floor((right+2400)/50));reacthit::ExpandBounds(actors.data(),left,top,right,bottom);
 Check(right>=50.149f&&int(std::floor((right+2400)/50))>oldSector,"extremity across sector edge expands membership beyond native root rectangle");
 Check(poses[0]==edgePose&&!std::memcmp(&edgeModel,&nativeModel,sizeof edgeModel)&&!std::memcmp(edgeSpheres.data(),nativeSpheres,sizeof edgeSpheres),"sector extent query leaves pose and shared native model unchanged");
}
void Fallback(){
 Setup();inspectOnly=true;const Line line=Head();Point point{};float fraction=.7f;
 auto unchanged=[&](const void *matrix,void *model,const char *why){const int before=calls;const bool hit=entry(line,matrix,model,point,fraction,true,false);Check(!hit&&calls==before+1&&seenMatrix==matrix&&seenModel==model&&seenSee&&!seenShoot&&fraction==.7f,why);};
 const size_t page=size_t(getpagesize());void *guard=mmap(nullptr,page,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);Require(guard!=MAP_FAILED,"guard page for arbitrary non-ped matrix");unchanged(guard,&nativeModel,"unreadable non-ped matrix falls through before any ped-field read");munmap(guard,page);
 unchanged(reinterpret_cast<void *>(1),&nativeModel,"underflowing arbitrary matrix is rejected before ped arithmetic");
 unchanged(actors.data()+9,&nativeModel,"misaligned matrix within ped pool is rejected");
 unchanged(actors.data()+actors.size()+8,&nativeModel,"matrix beyond ped pool allocation is rejected");
 generations[0]|=0x80;unchanged(actors.data()+8,&nativeModel,"unallocated pool slot cannot supply a reaction pose");generations[0]&=0x7f;
 stale=true;unchanged(actors.data()+8,&nativeModel,"pool lookup generation mismatch retains native query");stale=false;
 Store(actors.data()+0x60,uint32_t(2));unchanged(actors.data()+8,&nativeModel,"vehicle-type record cannot be interpreted as ped");Store(actors.data()+0x60,uint32_t(3));
 nativeModel.spheres=9;unchanged(actors.data()+8,&nativeModel,"unsupported native COL shape falls through unchanged");nativeModel.spheres=10;
 nativeSpheres[0].radius=NAN;unchanged(actors.data()+8,&nativeModel,"nonfinite native sphere fails closed without partial local result");nativeSpheres[0].radius=.15f;
 Model other=nativeModel;unchanged(actors.data()+8,&other,"unrelated COL at same ped matrix remains native");
 available[0]=false;unchanged(actors.data()+8,&nativeModel,"inactive or disabled reaction retains original model");available[0]=true;
 poses[0][5][12]=NAN;unchanged(actors.data()+8,&nativeModel,"invalid reaction matrix retains original model");
}
}
int main(){fixture::Native native;fixture::Setup();fixture::Require(reacthit::Install(),"install common native LOS hook");fixture::Hits();fixture::Fallback();if(!fixture::failures)std::puts("reacthit: native pose hits, masks, nearest impact, shared model isolation, nested queries and bounded pool validation passed");return fixture::failures?1:0;}

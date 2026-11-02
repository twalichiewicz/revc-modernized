// Actual reaction publication and pristine native arm64 ped LOS/damage replay.
// No game is launched; only allocation/animation data bindings are substituted.
#define main reaction_fixture_existing_main
#include "test_reactionhook.cpp"
#undef main
#include "nativejetshadowarm.h"
using nativejetshadowtest::Store;
using nativeweavetest::Require;
namespace reacthitfixture {
void *Lookup(void *,const char *);
inline void *next=nullptr,*replacement=nullptr;
inline uint8_t poolHeader[32]{};
inline void *pool=poolHeader;
inline void *models[6500]{};
}
namespace hooks {void *Chain(const char *,void *replacement){reacthitfixture::replacement=replacement;return reacthitfixture::next;}}
#define dlsym reacthitfixture::Lookup
#include "../src/reacthit.cpp"
#undef dlsym
namespace nh {
using Code=nativejetshadowtest::Code;
static Code *currentCode;
static void Missing(){auto *pc=static_cast<uint8_t *>(__builtin_return_address(0));for(auto r:currentCode->ranges)if(pc>=currentCode->code+r.offset&&pc<currentCode->code+r.offset+r.last-r.first)std::fprintf(stderr,"missing binding from native pc %llx\n",r.first+uint64_t(pc-currentCode->code-r.offset)-4);std::exit(2);}
struct Line { CVector a; uint32_t pa; CVector b; uint32_t pb; };
struct Sphere { CVector c;float r;uint8_t surface,piece;uint16_t pad; };
static_assert(sizeof(Sphere)==20);
using Narrow=bool (*)(const Line &,const void *,void *,reactcontact::NativePoint &,float &,bool,bool);
using Sector=bool (*)(nativeweavetest::Node **,const Line &,reactcontact::NativePoint &,float &,uint8_t *&,bool,bool,bool);
static float *Create(){auto *m=new float[16]{};m[0]=m[5]=m[10]=1;return m;}
static void Destroy(float *m){delete[] m;}
static void *Hierarchy(void *){return gContactClump;}
static float *Matrices(void *){return gContactRig;}
static int Index(void *,int tag){return ContactBone(nullptr,tag);}
static float *Frame(void *){return reinterpret_cast<float *>(gPed+8);}
static int Type(void *){return 2;}
static int Random(){return 0;}
static void *NoAssociation(void *,int){return nullptr;}
static uint8_t *NativePlayer(){return gPlayer;}
static void EmptyModel(uint8_t *m){std::memset(m,0,0x68);}
static void NoopModel(void *){}
static void BadAssert(const char *,const char *,unsigned,const char *){Require(false,"native assertion");}
struct Probe {
 Code c;alignas(16) uint8_t model[0x80]{},col[0x68]{};Sphere spheres[10]{};uint64_t guard=0;nativeweavetest::Node node{gPed};Narrow narrow;Sector sector;void *(*animate)(void *,void *);bool (*sphere)(const Line &,const Sphere &,reactcontact::NativePoint &,float &);
 Probe(){
  // Pin the native paths that share the replayed world/ped consumer. These
  // calls have no player-only wrapper requirement; deferred sniper bullets
  // enter the same CWorld path when CBulletInfo updates.
  auto branch=[&](uint64_t pc,uint64_t target){const uint32_t w=c.image.Word(pc);Require((w&0xfc000000)==0x94000000&&uint64_t(int64_t(pc)+nativeweavetest::Signed(w&0x3ffffff,26)*4)==target,"native firearm caller still reaches common LOS consumer");};
  branch(0x100288318,0x10012c554);branch(0x100288670,0x10012c554);branch(0x1002887bc,0x10012c554);branch(0x100288cec,0x10012c554);branch(0x100289264,0x10012c554);
  branch(0x100286a40,0x10012c554);branch(0x100287658,0x10012c554);branch(0x10028cbac,0x10012c554);branch(0x100291050,0x10012c554);
  branch(0x100288068,0x100280cb8);branch(0x100280f2c,0x10012c554);
  c.Add(0x1001c7898,0x1001c872c);c.Add(0x10018e5e0,0x10018e620);c.Add(0x100191624,0x100191634);
  c.Add(0x10012d794,0x10012dba4);c.Add(0x10017a2ec,0x10017a430);
  c.Add(0x10004a440,0x10004a60c);c.Add(0x10004bc00,0x10004c028);c.Add(0x100049530,0x1000497e8);c.Add(0x10004ad0c,0x10004ae34);
  c.Add(0x1001723fc,0x1001724d0);c.Add(0x100172e50,0x100172e94);c.Add(0x100172ec0,0x100172fb0);
  c.Add(0x100007d68,0x100007db8);c.Add(0x100293ddc,0x100293f28);c.Add(0x100294700,0x100294984);c.Add(0x10029562c,0x10029588c);
  c.bindings={{0x100143f28,reinterpret_cast<void *>(Random)},{0x10010c0f4,reinterpret_cast<void *>(NativePlayer)},{0x10000bb90,reinterpret_cast<void *>(NoAssociation)},{0x10016f740,reinterpret_cast<void *>(Create)},{0x10016f728,reinterpret_cast<void *>(Destroy)},
   {0x100232d64,reinterpret_cast<void *>(Hierarchy)},{0x1001711a8,reinterpret_cast<void *>(Index)},
   {0x100171218,reinterpret_cast<void *>(Matrices)},{0x100170c64,reinterpret_cast<void *>(Hierarchy)},
   {0x10016f9c0,reinterpret_cast<void *>(Frame)},{0x10016f780,c.Address(0x100294700)},
   {0x10016f75c,c.Address(0x10029562c)},{0x10016f704,c.Address(0x100293ddc)},
   {0x10016f5a0,reinterpret_cast<void *>(Type)},{0x100046ca0,reinterpret_cast<void *>(EmptyModel)},
   {0x100046da8,reinterpret_cast<void *>(NoopModel)},
   {0x10014a6b4,reinterpret_cast<void *>(BadAssert)}};
  Store(c.Page(0x100344610),&guard);Store(c.Page(0x10060b358),uint16_t(1));Store(c.Page(0x10060b508),static_cast<void *>(nullptr));
  *c.Page(0x1007b7c08)=1;Store(c.Page(0x1006b2050),static_cast<void *>(model));
  Store(model+0x50,static_cast<void *>(col));Store(col+0x38,static_cast<void *>(spheres));Store(col+0x28,uint16_t(10));
  const float bounds[6]={-.5f,-.5f,-1.2f,.5f,.5f,1.2f};std::memcpy(col+0x10,bounds,sizeof bounds);
  for(int i=0;i<10;i++){auto *data=c.Page(0x10036d8e0+size_t(i)*32);std::memcpy(&spheres[i].r,data+16,4);spheres[i].surface=17;spheres[i].piece=uint8_t(*reinterpret_cast<int *>(data+4));}
  currentCode=&c;for(auto r:c.ranges)for(uint64_t pc=r.first;pc<r.last;pc+=4){auto w=c.image.Word(pc);if((w&0x7c000000)==0x14000000){uint64_t dest=uint64_t(int64_t(pc)+nativeweavetest::Signed(w&0x3ffffff,26)*4);if(!c.Address(dest)&&!c.bindings.count(dest))c.bindings[dest]=reinterpret_cast<void *>(Missing);}}
  c.Finish();narrow=reinterpret_cast<Narrow>(c.Address(0x10004bc00));sector=reinterpret_cast<Sector>(c.Address(0x10012d794));animate=reinterpret_cast<decltype(animate)>(c.Address(0x10017a2ec));sphere=reinterpret_cast<decltype(sphere)>(c.Address(0x10004ad0c));
 }
 void Wide(){const float bounds[6]={-4,-4,-4,4,4,4};std::memcpy(col+0x10,bounds,sizeof bounds);}
 bool Hit(Line ray,reactcontact::NativePoint &point){Store(c.Page(0x10060b358),uint16_t(*reinterpret_cast<uint16_t *>(c.Page(0x10060b358))+1));float f=1;uint8_t *victim=nullptr;auto *head=&node;const bool hit=sector(&head,ray,point,f,victim,false,false,false);return hit&&victim==gPed;}
};
}
namespace reacthitfixture {
void *Lookup(void *,const char *name){if(!std::strcmp(name,"_ZN6CPools11ms_pPedPoolE"))return &pool;if(!std::strcmp(name,"_ZN10CModelInfo16ms_modelInfoPtrsE"))return models;return nullptr;}
}
int main(){
 gNow=1000;gConfig.npcReactions=true;reactions::gInstalled=true;game::api.timeMs=&gNow;game::api.FindPlayerPed=Player;game::api.PoolsGetPed=Lookup;game::api.GetAnimHierarchyFromSkinClump=ContactHierarchy;game::api.HAnimIDGetIndex=ContactBone;game::api.HAnimMatrixArray=ContactMatrices;game::api.UpdateRpHAnim=CapturedIdleMatrices;game::api.ProcessVerticalLine=NativeGround;
 uint8_t *ignored=nullptr;game::api.ignoreEntity=&ignored;reactions::gRef=Ref;reactions::gUpdateMatrix=Noop;reactions::gUpdateFrame=Noop;reactions::gReindex=Noop;reactions::gSetFall=Fall;reactions::gGetUp=GetUp;
 std::memset(gPed,0,sizeof gPed);gLive=gPed;*reinterpret_cast<uint32_t *>(gPed+0x60)=3|game::kEntityUsesCollision;*reinterpret_cast<float *>(gPed+0x44c)=100;*reinterpret_cast<uint8_t **>(gPed+0x58)=gContactClump;*reinterpret_cast<CVector *>(gPed+0x38)={0,0,1.04f};auto *matrix=reinterpret_cast<float *>(gPed+8);matrix[0]=matrix[5]=matrix[10]=1;
 auto *p=reactions::Track(gPed);Require(p,"actual reaction tracking");reactphysics::Pose standing=p->physical.Positions();p->physical.Impulse(reactphysics::Chest,{6,0,0});
 for(int i=0;i<90;i++)p->physical.Advance(1.0f/60,standing,Ground,nullptr);
 reactions::Place(*p,gPed);CapturedIdleMatrices(gPed);reactions::AfterPreRender(gPed);
 std::printf("mode=%d weight=%g root=(%g,%g,%g) head=(%g,%g,%g) health=%g usesCol=%d\n",int(p->physical.State()),p->physical.Weight(),game::EntityPos(gPed).x,game::EntityPos(gPed).y,game::EntityPos(gPed).z,p->physical.Positions()[reactphysics::Head].x,p->physical.Positions()[reactphysics::Head].y,p->physical.Positions()[reactphysics::Head].z,game::Health(gPed),bool(game::EntityFlags(gPed)&game::kEntityUsesCollision));
 nh::Probe probe;
 uint8_t generation=17;Store(reacthitfixture::poolHeader,gPed);Store(reacthitfixture::poolHeader+8,&generation);Store(reacthitfixture::poolHeader+16,int(1));reacthitfixture::models[0]=probe.model;reacthitfixture::next=probe.c.Address(0x10004bc00);Require(reacthit::Install(),"install common native ped LOS hook");
 pthread_jit_write_protect_np(0);const auto callsite=static_cast<uint8_t *>(probe.c.Address(0x10012d9fc))-probe.c.code;const auto thunk=probe.c.Thunk(reacthitfixture::replacement);Store(probe.c.code+callsite,uint32_t(0x94000000|(uint32_t((int64_t(thunk)-int64_t(callsite))/4)&0x3ffffff)));sys_icache_invalidate(probe.c.code,0x10000);pthread_jit_write_protect_np(1);
 alignas(16) uint8_t stats[0x40]{},headNode[0x20]{};Store(headNode+0x18,int(5));Store(gPed+0x210,headNode);Store(stats+0x2c,1.0f);Store(gPed+0x500,stats);Store(gPed+0x4f8,int(6));
 auto shootable=reinterpret_cast<bool (*)(uint8_t *)>(probe.c.Address(0x100191624));auto damage=reinterpret_cast<bool (*)(uint8_t *,uint8_t *,int,float,int,uint8_t)>(probe.c.Address(0x1001c7898));
 for(int state:{game::kPedFall,game::kPedGetUp}){Store(gPed+0x2fc,state);Store(gPed+0x44c,100.0f);const bool allowed=shootable(gPed);damage(gPed,gOther,17,10.0f,1,0);std::printf("native damage state=%d shootable=%d healthAfter=%g\n",state,allowed,game::Health(gPed));Require(allowed&&game::Health(gPed)==90,"native fall/get-up accept bullet damage");}
 Store(gPed+0x44c,100.0f);Store(gPed+0x2fc,game::kPedFall);probe.animate(probe.model,gContactClump);int excluded=0;
 // Compare every projected volume with the independently executed native
 // model builder while it sees the actual rendered HAnim matrices.
 reacthit::Projection projected;Require(reacthit::Project(gPed,probe.col,projected),"project actual tracked physical pose");
 for(int i=0;i<10;i++){
  const auto &a=probe.spheres[i];const auto &b=projected.spheres[size_t(i)];
  Require(std::fabs(a.c.x-b.center.x)<1e-6f&&std::fabs(a.c.y-b.center.y)<1e-6f&&std::fabs(a.c.z-b.center.z)<1e-6f&&a.r==b.radius&&a.surface==b.surface&&a.piece==b.piece,"all physical hit spheres retain exact native bone offsets, radii, surfaces and pieces");
 }
 std::array<float,body::kBones*16> savedRig;std::memcpy(savedRig.data(),gContactRig,sizeof gContactRig);
 for(int i=0;i<10;i++){auto s=probe.spheres[i];s.c.x+=game::EntityPos(gPed).x;s.c.y+=game::EntityPos(gPed).y;s.c.z+=game::EntityPos(gPed).z;nh::Line ray{{s.c.x,s.c.y,s.c.z+1},0,{s.c.x,s.c.y,s.c.z-1},0};reactcontact::NativePoint point{};float f=1;const bool direct=probe.sphere(ray,s,point,f);const bool world=probe.Hit(ray,point);std::printf("render sphere %d piece=%d at=(%.3f,%.3f,%.3f) direct=%d nativeWorld=%d\n",i,s.piece,s.c.x,s.c.y,s.c.z,direct,world);excluded+=direct&&!world;}
 Require(!std::memcmp(savedRig.data(),gContactRig,sizeof gContactRig),"native LOS queries leave the real HAnim skeleton unchanged");
 probe.Wide();probe.animate(probe.model,gContactClump);const auto s=probe.spheres[0];const CVector root=game::EntityPos(gPed);nh::Line ray{{root.x+s.c.x-1,root.y+s.c.y,root.z+s.c.z},0,{root.x+s.c.x+1,root.y+s.c.y,root.z+s.c.z},0};reactcontact::NativePoint point{};const bool rendered=probe.Hit(ray,point);gNow+=16;reactions::Advance(*p,gPed);const bool controlled=probe.Hit(ray,point);reactions::AfterPreRender(gPed);const bool rerendered=probe.Hit(ray,point);
 std::printf("render-visible head ray with enlarged diagnostic bounds: afterPreRender=%d afterAdvance=%d afterNextPreRender=%d; excludedNativeBounds=%d\n",rendered,controlled,rerendered,excluded);
 if(excluded||!controlled){std::puts("FAIL: living ragdoll rendered hit volumes are absent from the common native ped LOS path");return 1;}std::puts("reacthit native: current rendered pose hits before and after control; native fall/get-up damage preserved");return 0;
}

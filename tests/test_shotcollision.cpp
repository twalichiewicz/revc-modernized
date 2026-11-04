#include "game.h"
#include "carmechanics.h"
#include "carparts.h"
#include "nativebullet.h"
#include "hooks.h"
#include "../tools/bake/visualmodel.h"
#include <array>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>
#include <dlfcn.h>
#include <cstdarg>
#include <chrono>
#include <cstdlib>
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif
namespace game { Api api; }
void Log(const char *,...){}
namespace carglass {
int calls=0,face=-1;const void *part=nullptr;uint8_t *car=nullptr;Vec3 point{};
void Bullet(uint8_t *v,const void *a,int t,Vec3 p,Vec3){++calls;car=v;part=a;face=t;point=p;}
}
namespace carmechanics {
static bool enabled=false;static int deliveries=0;static uint64_t revision=1;
static projectilemodel::Ray lastRay;static std::array<projectilemodel::Layer,64> lastLayers;
bool Enabled(){return enabled;}
BulletToken BeginBullet(uint8_t *){BulletToken t;if(enabled)t.revision=revision;return t;}
void Projectile(uint8_t *,const BulletToken &t,const projectilemodel::Ray &r){if(!enabled||t.revision!=revision)return;++deliveries;lastRay=r;std::copy(r.layers,r.layers+r.count,lastLayers.begin());lastRay.layers=lastLayers.data();}
}
namespace carparts {bool enabled=false;int hits=0,component=0;bool Enabled(){return enabled;}BulletToken BeginBullet(uint8_t *){return {enabled?1u:0u};}void Projectile(uint8_t *,const BulletToken &t,int c,float){if(t.revision){++hits;component=c;}}}
namespace fixture { void *Lookup(void *,const char *); void *Native(const char *); std::map<std::string,void *> entry; }
namespace hooks {void *Chain(const char *s,void *p){fixture::entry[s]=p;return fixture::Native(s);}}
#define dlsym fixture::Lookup
#ifndef SHOTCOLLISION_IMPLEMENTATION
#define SHOTCOLLISION_IMPLEMENTATION "../src/shotcollision.cpp"
#endif
#include SHOTCOLLISION_IMPLEMENTATION
#undef dlsym
namespace fixture {
using namespace shotcollision;
struct V {float x,y,z;};
struct Tri {uint16_t v[3],material;};
struct Frame { std::array<float,16> matrix{},world{}; std::string name;Frame *parent=nullptr; };
struct Material {std::array<uint8_t,4> colour{255,255,255,255};};
struct Geometry {uint8_t prefix[0x88]{};int materials=1; std::vector<V> verts;std::vector<Tri> tris;std::vector<Material> mats{Material{}};};
struct Atomic {Frame frame;Geometry geo;unsigned flags=4,id=1;};
struct Clump {Frame root;std::vector<Atomic> atomics;};
struct Sphere {V c;float r;uint8_t piece=0;};
struct Model {std::vector<Sphere> spheres;};
struct Pool {uint8_t *entries;uint8_t *flags;int capacity;};
alignas(16) std::array<uint8_t,0x6f0*2> cars;
alignas(16) std::array<uint8_t,0x8d0*3> peds;
uint8_t carFlags[2]{1,2},pedFlags[3]{3,4,5};Pool carPool{cars.data(),carFlags,2},pedPool{peds.data(),pedFlags,3};Pool *cp=&carPool,*pp=&pedPool;
Clump clump;Model carModel,pedModel,passengerModel,standerModel,wheelModel;std::array<uint8_t,0x80> modelInfo{},modelInfoB{};void *modelPtrs[6500]{};
bool tyres=true;int fail,narrowCalls,worldCalls,fireCalls,occupantCalls,updates;bool wall=false,nested=false,throwFire=false,throwWorld=false,stale=false,stander=false,noPeds=false,staleLTM=false,windowMode=false,ignoreSee=false;int windowStatus=0,windowSounds=0,afterTrace=0,clumpType=2;int fireType=1,rounds=0,roundDamage=0;float nestedX=0;float wallT=.1f;uint8_t *ignore=nullptr;
alignas(8) std::array<uint8_t,nativebullet::kCount*nativebullet::kStride> deferredBullets{};
std::array<uint8_t,100> weaponInfo{};uint32_t roundBudget=20;
bool nestCover=false,nestedCoverPreserved=false,repairBeforeImpact=false,nativeGlassGate=false;bool doDamage=false,acceptDamage=true,doubleDamage=false,coverWall=false,throwCover=false;int damageMutation=0;
Point result{};uint8_t *victim;CVector from{-3,0,.5f},to{3,0,.5f};int weaponType=17;
void Check(bool yes,const char *msg){if(!yes){printf("FAIL: %s\n",msg);++fail;}}
void Identity(float *m){std::fill(m,m+16,0);m[0]=m[5]=m[10]=m[15]=1;}
uint8_t *Car(int ref){if(stale)return nullptr;for(int i=0;i<2;i++)if(ref==(i<<8|carFlags[i]))return cars.data()+i*0x6f0;return nullptr;}
uint8_t *Ped(int ref){for(int i=0;i<3;i++)if(ref==(i<<8|pedFlags[i]))return peds.data()+i*0x8d0;return nullptr;}
void *Atomics(void *c,void *(*cb)(void *,void *),void *u){for(auto &a:static_cast<Clump *>(c)->atomics)if(!cb(&a,u))break;if(afterTrace==1)++carFlags[0];if(afterTrace==2)*reinterpret_cast<void **>(cars.data()+0x58)=reinterpret_cast<void *>(0x1234);if(afterTrace==3)*reinterpret_cast<int16_t *>(cars.data()+0x6a)=8;if(afterTrace==4)reinterpret_cast<float *>(cars.data()+8)[12]+=.1f;if(afterTrace==5)carFlags[0]|=0x80;if(afterTrace==6)clumpType=1;return c;}
int ObjectType(const void *){return clumpType;}
unsigned Flags(const void *a){return static_cast<const Atomic *>(a)->flags;}
void *AFrame(const void *a){return const_cast<Frame *>(&static_cast<const Atomic *>(a)->frame);}
void *AGeo(const void *a){return const_cast<Geometry *>(&static_cast<const Atomic *>(a)->geo);}
float *LTM(void *f){return staleLTM?static_cast<Frame *>(f)->world.data():static_cast<Frame *>(f)->matrix.data();}
float *FrameMatrix(void *f){return static_cast<Frame *>(f)->matrix.data();}
void *Root(const void *c){return const_cast<Frame *>(&static_cast<const Clump *>(c)->root); }
const char *Name(void *f){return static_cast<Frame *>(f)->name.c_str();}
void *Parent(const void *f){return static_cast<const Frame *>(f)->parent;}
int NV(const void *g){return int(static_cast<const Geometry *>(g)->verts.size());}
int NT(const void *g){return int(static_cast<const Geometry *>(g)->tris.size());}
const void *Tris(const void *g){return static_cast<const Geometry *>(g)->tris.data();}
void *Morph(const void *g,int){return const_cast<void *>(g);}
const void *Verts(const void *g){return static_cast<const Geometry *>(g)->verts.data();}
void *Mat(const void *g,int i){return const_cast<Material *>(&static_cast<const Geometry *>(g)->mats.at(i));}
const uint8_t *Colour(const void *m){return static_cast<const Material *>(m)->colour.data();}
void *Animate(void *self,void *){return self==modelInfoB.data()?&passengerModel:&pedModel;}
uint8_t *Info(int){std::memcpy(weaponInfo.data(),&fireType,4);uint32_t damage=roundBudget;std::memcpy(weaponInfo.data()+0x14,&damage,4);return weaponInfo.data(); }
V Sub(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}float Dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
bool NativeNarrow(const Line &l,const void *,void *m,Point &p,float &closest,bool,bool)
{
 ++narrowCalls;bool hit=false;V a{l.a.x,l.a.y,l.a.z},d{l.b.x-l.a.x,l.b.y-l.a.y,l.b.z-l.a.z};
 for(auto s:static_cast<Model *>(m)->spheres){V v=Sub(a,s.c);float A=Dot(d,d),B=Dot(v,d),C=Dot(v,v)-s.r*s.r,disc=B*B-A*C;if(disc<0||A<1e-9f)continue;float t=(-B-std::sqrt(disc))/A;if(t>=0&&t<closest){closest=t;hit=true;p={};p.p={a.x+d.x*t,a.y+d.y*t,a.z+d.z*t};p.n={(p.p.x-s.c.x)/s.r,(p.p.y-s.c.y)/s.r,(p.p.z-s.c.z)/s.r};p.surfaceB=6;p.pieceB=s.piece;}}
 return hit;
}
Narrow NarrowEntry(){return reinterpret_cast<Narrow>(entry["_ZN10CCollision18ProcessLineOfSightERK8CColLineRK7CMatrixR9CColModelR9CColPointRfbb"]);}
World WorldEntry();
bool NativeWorld(const CVector &a,const CVector &b,Point &p,uint8_t *&e,bool,bool vehicles,bool pedsFlag,bool,bool,bool see,bool,bool shoot)
{
 if(nestCover&&ignore==cars.data()){
  nestCover=false;const auto saved=gShot->last;Point other{};uint8_t *hit=nullptr;WorldEntry()(from,to,other,hit,true,true,true,true,true,false,false,true);
  nestedCoverPreserved=gShot->last.car==saved.car&&!std::memcmp(&gShot->last.point,&saved.point,sizeof saved.point);
 }
 ++worldCalls;if(throwCover&&ignore==cars.data())throw 9;if(throwWorld)throw 8;float t=1;e=nullptr;Line line{a,0,b,0};if(vehicles && ignore!=cars.data() && NarrowEntry()(line,cars.data()+8,&carModel,p,t,see,shoot))e=cars.data();
 if(stander&&pedsFlag&&NarrowEntry()(line,peds.data()+0x8d0+8,&standerModel,p,t,false,false))e=peds.data()+0x8d0;
 if(vehicles && ignore!=cars.data() && tyres){float wheelClosest=1;Point wheel{};if(NarrowEntry()(line,cars.data()+8,&wheelModel,wheel,wheelClosest,see,shoot)&&wheelClosest<=t){p=wheel;t=wheelClosest;e=cars.data();}}
 // Same native closest-fraction competition after the vehicle list.
 if(coverWall&&std::fabs(b.x-a.x)>1e-6f){const float at=-a.x/(b.x-a.x);if(at>=0&&at<t){t=at;p={};p.p={0,a.y+(b.y-a.y)*at,a.z+(b.z-a.z)*at};e=peds.data()+0x8d0;}}
 if(wall && wallT<t){p={};p.p={a.x+(b.x-a.x)*wallT,a.y+(b.y-a.y)*wallT,a.z+(b.z-a.z)*wallT};e=reinterpret_cast<uint8_t *>(0x1234);}
 return e!=nullptr;
}
World WorldEntry(){return reinterpret_cast<World>(entry["_ZN6CWorld18ProcessLineOfSightERK7CVectorS2_R9CColPointRP7CEntitybbbbbbbb"]);}
void NativeOccupant(uint8_t **e,Point *p,int,const CVector &,const CVector &){++occupantCalls;
 // Native CheckForShootingVehicleOccupant -> ProgressPanelDamage(4), then
 // SetPanelDamage(node19,panel4,true) -> SetVehicleAtomicVisibilityCB:
 // atomicId&3 must equal selector2 (cracked) or0 (missing) to retain render4.
 if(*e==cars.data()&&windowMode&&windowStatus<3){++windowStatus;if(windowStatus==2)++windowStatus;unsigned selector=windowStatus==1?2:0;for(auto &a:clump.atomics)if(a.frame.name.find("windscreen")!=std::string::npos)a.flags=(a.id&3)==selector?4:0;++windowSounds;}
if(*e==cars.data() && *reinterpret_cast<void **>(cars.data()+0x218)){*e=peds.data();p->p={0,0,.5f};p->pieceB=6;}}
Occupant OccupantEntry(){return reinterpret_cast<Occupant>(entry["_ZN7CWeapon31CheckForShootingVehicleOccupantEPP7CEntityP9CColPoint11eWeaponTypeRK7CVectorS8_"]);}
void NativeImpact(uint8_t *,uint8_t *,uint8_t *,CVector *,CVector *,Point *,Vec2);
void NativeBurst(uint8_t *v,uint8_t piece,bool){if(v[0x299]&2)return;const int map[]={0,2,1,3};const int w=piece>=13&&piece<=16?map[piece-13]:piece;if(w<4)v[0x351+w]=1;}
bool NativeFire(uint8_t *weapon,uint8_t *by,CVector *)
{
 ++fireCalls;if(throwFire)throw 7;
 if(nested){nested=false;uint8_t nonbullet[32]{};int type=30;std::memcpy(nonbullet,&type,4);reinterpret_cast<Fire>(entry["_ZN7CWeapon4FireEP7CEntityP7CVector"])(nonbullet,peds.data()+0x8d0,&from);nestedX=result.p.x;}
 WorldEntry()(from,to,result,victim,true,true,!noPeds,true,true,ignoreSee,false,true);
 if(victim&&(!nativeGlassGate||result.pieceB==17))OccupantEntry()(&victim,&result,weaponType,from,to);
 if(repairBeforeImpact)++carmechanics::revision;
 if(doDamage&&victim==cars.data()){
  using Impact=void (*)(uint8_t *,uint8_t *,uint8_t *,CVector *,CVector *,Point *,Vec2);
  const auto found=entry.find("_ZN7CWeapon14DoBulletImpactEP7CEntityS1_P7CVectorS3_P9CColPoint9CVector2D");
  if(found!=entry.end())reinterpret_cast<Impact>(found->second)(weapon,by,victim,&from,&to,&result,{});
  else NativeImpact(weapon,by,victim,&from,&to,&result,{});
 }
 if(doDamage&&victim==peds.data())reinterpret_cast<bool (*)(uint8_t *,uint8_t *,int,float,int,uint8_t)>(entry["_ZN4CPed13InflictDamageEP7CEntity11eWeaponTypef14ePedPieceTypesh"])(victim,by,weaponType,20,result.pieceB,0);
 return true;
}
void NativeVehicleDamage(uint8_t *v,uint8_t *,int,float damage,CVector){
 if(acceptDamage)*reinterpret_cast<float *>(v+0x2a0)-=damage;
 if(damageMutation==1)++carFlags[0];if(damageMutation==2)*reinterpret_cast<void **>(v+0x58)=reinterpret_cast<void *>(0x1234);
 if(damageMutation==3)reinterpret_cast<float *>(v+8)[12]+=.1f;if(damageMutation==4)++carmechanics::revision;
}
void NativeImpact(uint8_t *,uint8_t *by,uint8_t *v,CVector *,CVector *,Point *point,Vec2){
 if(point->pieceB>=13&&point->pieceB<=16){reinterpret_cast<void (*)(uint8_t *,uint8_t,bool)>(entry["_ZN11CAutomobile9BurstTyreEhb"])(v,point->pieceB,true);return;}
 auto inflict=reinterpret_cast<void (*)(uint8_t *,uint8_t *,int,float,CVector)>(entry["_ZN8CVehicle13InflictDamageEP7CEntity11eWeaponTypef7CVector"]);
 inflict(v,by,weaponType,float(roundBudget),{});if(doubleDamage)inflict(v,by,weaponType,float(roundBudget),{});
}
float lastPedDamage=0;bool NativePedDamage(uint8_t *,uint8_t *,int,float damage,int,uint8_t){lastPedDamage=damage;return false;}
void DirectDamage(){
 if(victim==cars.data()&&entry["_ZN8CVehicle13InflictDamageEP7CEntity11eWeaponTypef7CVector"])reinterpret_cast<void (*)(uint8_t *,uint8_t *,int,float,CVector)>(entry["_ZN8CVehicle13InflictDamageEP7CEntity11eWeaponTypef7CVector"])(victim,peds.data()+0x8d0*2,weaponType,20,CVector{0,0,0});
 if(victim==peds.data()&&entry["_ZN4CPed13InflictDamageEP7CEntity11eWeaponTypef14ePedPieceTypesh"])reinterpret_cast<bool (*)(uint8_t *,uint8_t *,int,float,int,uint8_t)>(entry["_ZN4CPed13InflictDamageEP7CEntity11eWeaponTypef14ePedPieceTypesh"])(victim,peds.data()+0x8d0*2,weaponType,20,result.pieceB,0);
}
void BodyDamage(uint8_t *by,int type,float amount){if(victim==cars.data())reinterpret_cast<void (*)(uint8_t *,uint8_t *,int,float,CVector)>(entry["_ZN8CVehicle13InflictDamageEP7CEntity11eWeaponTypef7CVector"])(victim,by,type,amount,{});else DirectDamage();}
bool NativeFireCar(uint8_t *,uint8_t *,bool,bool){WorldEntry()(from,to,result,victim,true,true,true,true,true,false,false,true);BodyDamage(peds.data()+0x8d0*2,42,20);return true;}
void NativeUpdate(){++updates;WorldEntry()(from,to,result,victim,true,true,!noPeds,true,true,false,false,true);if(doDamage){auto r=nativebullet::Read(deferredBullets.data(),0);BodyDamage(r.creator,r.type,r.damage);}}
void NativeRound(CVector *a,CVector *b,int amount){++rounds;roundDamage=amount;Check(a==&from&&b==&to,"native cannon keeps original ray argument pointers");WorldEntry()(*a,*b,result,victim,true,true,true,true,true,false,false,true);BodyDamage(nullptr,23,float(amount));}
void *Native(const char *s){
 if(strstr(s,"DoBulletImpact"))return reinterpret_cast<void *>(NativeImpact);
 if(strstr(s,"BurstTyre"))return reinterpret_cast<void *>(NativeBurst);
 if(strstr(s,"CVehicle13InflictDamage"))return reinterpret_cast<void *>(NativeVehicleDamage);
 if(strstr(s,"CPed13InflictDamage"))return reinterpret_cast<void *>(NativePedDamage);
 if(strstr(s,"FireOneInstantHitRound"))return reinterpret_cast<void *>(NativeRound);
 if(strstr(s,"CheckForShooting"))return reinterpret_cast<void *>(NativeOccupant);
 if(strstr(s,"CCollision"))return reinterpret_cast<void *>(NativeNarrow);
 if(strstr(s,"CWorld"))return reinterpret_cast<void *>(NativeWorld);
 if(strstr(s,"CBulletInfo"))return reinterpret_cast<void *>(NativeUpdate);
 if(strstr(s,"FromCar"))return reinterpret_cast<void *>(NativeFireCar);
 return reinterpret_cast<void *>(NativeFire);
}
void *Lookup(void *,const char *s){
 if(!strcmp(s,"gaBulletInfo"))return deferredBullets.data();
 if(strstr(s,"GetWheelStatus"))return reinterpret_cast<void *>(+[](void *v,int w)->int{return static_cast<uint8_t *>(v)[5+w];});
#define BIND(n,v) if(!strcmp(s,n))return reinterpret_cast<void *>(v)
 BIND("_ZN6CPools15ms_pVehiclePoolE",&cp);BIND("_ZN6CPools11ms_pPedPoolE",&pp);BIND("_ZN10CModelInfo16ms_modelInfoPtrsE",modelPtrs);BIND("_ZN6CWorld16bIncludeCarTyresE",&tyres);
 BIND("_Z15RwObjectGetTypePKN2rw6ObjectE",ObjectType);BIND("_Z15RpClumpGetFramePKN2rw5ClumpE",Root);BIND("_Z16RwFrameGetMatrixPN2rw5FrameE",FrameMatrix);BIND("_Z20RpClumpForAllAtomicsPN2rw5ClumpEPFPNS_6AtomicES3_PvES4_",Atomics);BIND("_Z16RpAtomicGetFlagsPKN2rw6AtomicE",Flags);BIND("_Z16RpAtomicGetFramePKN2rw6AtomicE",AFrame);BIND("_Z19RpAtomicGetGeometryPKN2rw6AtomicE",AGeo);BIND("_Z13RwFrameGetLTMPN2rw5FrameE",LTM);BIND("_Z16GetFrameNodeNamePN2rw5FrameE",Name);BIND("_Z16RwFrameGetParentPKN2rw5FrameE",Parent);BIND("_Z24RpGeometryGetNumVerticesPKN2rw8GeometryE",NV);BIND("_Z25RpGeometryGetNumTrianglesPKN2rw8GeometryE",NT);BIND("_Z22RpGeometryGetTrianglesPKN2rw8GeometryE",Tris);BIND("_Z24RpGeometryGetMorphTargetPKN2rw8GeometryEi",Morph);BIND("_Z24RpMorphTargetGetVerticesPKN2rw11MorphTargetE",Verts);BIND("_Z21RpGeometryGetMaterialPKN2rw8GeometryEi",Mat);BIND("_Z18RpMaterialGetColorPKN2rw8MaterialE",Colour);BIND("_ZN13CPedModelInfo25AnimatePedColModelSkinnedEPN2rw5ClumpE",Animate);
#undef BIND
 return nullptr;
}
void Setup(){
 carparts::enabled=false;carparts::hits=0;roundBudget=20;nestCover=nestedCoverPreserved=repairBeforeImpact=nativeGlassGate=false;carmechanics::enabled=false;carmechanics::deliveries=0;++carmechanics::revision;deferredBullets={};doDamage=false;acceptDamage=true;doubleDamage=coverWall=throwCover=false;damageMutation=0;ignore=nullptr;
 carglass::calls=0;carglass::part=nullptr;carglass::face=-1;
 cars={};peds={};carFlags[0]=1;carFlags[1]=2;clumpType=2;clump.atomics.clear();Identity(clump.root.matrix.data());Identity(clump.root.world.data());staleLTM=windowMode=ignoreSee=false;windowStatus=windowSounds=afterTrace=0;carModel.spheres={{{0,0,.5f},1,0}};pedModel.spheres={{{0,0,.5f},.25f,3}};modelPtrs[7]=modelInfo.data();tyres=true;wall=false;stale=false;throwFire=throwWorld=nested=stander=noPeds=false;fireType=1;modelPtrs[8]=modelInfoB.data();passengerModel.spheres={{{-.5f,0,.5f},.2f,2}};standerModel.spheres={{{1,0,.5f},.25f,3}};
 for(int i=0;i<2;i++){auto c=cars.data()+i*0x6f0;*reinterpret_cast<float *>(c+0x2a0)=1000;Identity(reinterpret_cast<float *>(c+8));*reinterpret_cast<unsigned *>(c+0x60)=2|0x100;*reinterpret_cast<int16_t *>(c+0x6a)=7;*reinterpret_cast<void **>(c+0x58)=&clump;}
 for(int i=0;i<3;i++){auto p=peds.data()+i*0x8d0;Identity(reinterpret_cast<float *>(p+8));*reinterpret_cast<unsigned *>(p+0x60)=3;*reinterpret_cast<int16_t *>(p+0x6a)=7;*reinterpret_cast<void **>(p+0x58)=&clump;p[0x193]=4;p[0x4c0]=1;*reinterpret_cast<void **>(p+0x4b8)=cars.data();*reinterpret_cast<float *>(p+0x44c)=100;}
 *reinterpret_cast<void **>(cars.data()+0x218)=peds.data();game::api.GetWeaponInfo=Info;game::api.FindPlayerPed=+[](){return peds.data()+0x8d0*2;};game::api.PoolsGetVehicle=Car;game::api.PoolsGetPed=Ped;game::api.ignoreEntity=&ignore;from={-3,0,.5f};to={3,0,.5f};weaponType=17;victim=nullptr;result={};narrowCalls=worldCalls=fireCalls=occupantCalls=updates=0;
}
void Panel(float x,unsigned alpha=255,const char *name="door_lf_hi_ok"){
 Atomic a;Identity(a.frame.matrix.data());Identity(a.frame.world.data());a.frame.parent=&clump.root;a.frame.name=name;a.geo.verts={{x,-1,0},{x,1,0},{x,1,1},{x,-1,1}};a.geo.tris={{{0,1,2},0},{{0,2,3},0}};a.geo.mats[0].colour[3]=uint8_t(alpha);clump.atomics.push_back(std::move(a));
}

template<class T>T Get(const std::vector<uint8_t> &b,size_t p){T v;std::memcpy(&v,b.data()+p,sizeof v);return v;}
struct Chunk {unsigned type;size_t start,end;};
std::vector<Chunk> Chunks(const std::vector<uint8_t> &b,size_t a,size_t e){std::vector<Chunk> out;while(a+12<=e){unsigned n=Get<unsigned>(b,a+4);out.push_back({Get<unsigned>(b,a),a+12,a+12+n});a+=12+n;}return out;}
bool LoadCar(const char *name){
 const char *env=std::getenv("GAMEDATA");const std::string base=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";ImgArchive img;std::vector<uint8_t> b,col;
 if(!img.Open(base+"/models/gta3.img")||!img.Read(std::string(name)+".dff",b)||!ReadGameFile(base+"/models/coll/vehicles.col",col))return false;
 std::vector<std::string> names;
 for(const auto &c:Chunks(b,12,12+Get<unsigned>(b,4)))if(c.type==14){auto ch=Chunks(b,c.start,c.end);unsigned n=Get<unsigned>(b,ch[0].start);for(unsigned i=0;i<n;i++){std::string name;for(auto x:Chunks(b,ch[i+1].start,ch[i+1].end))if(x.type==0x253f2fe)name=std::string(reinterpret_cast<char *>(b.data()+x.start),x.end-x.start);names.push_back(name);}}
 // Native CVehicleModelInfo removes _lo, renders _vlo only at distance and
 // HideDamagedAtomicCB initially hides _dam. Keep precisely the intact set.
 for(const auto &c:Chunks(b,12,12+Get<unsigned>(b,4)))if(c.type==20){auto ch=Chunks(b,c.start,c.end);unsigned frame=Get<unsigned>(b,ch[0].start);auto n=names.at(frame);if(n.find("_dam")!=std::string::npos||n.find("_vlo")!=std::string::npos||n.find("_lo")!=std::string::npos){unsigned flags=0;std::memcpy(b.data()+ch[0].start+8,&flags,4);}}
 visualmodel::Mesh mesh;std::string why;if(!visualmodel::Read(b,mesh,why))return false;
 Atomic atom;Identity(atom.frame.matrix.data());Identity(atom.frame.world.data());atom.frame.parent=&clump.root;atom.frame.name="chassis_hi";atom.geo.mats.clear();for(const auto &m:mesh.materials){Material x;x.colour[3]=m.alpha;atom.geo.mats.push_back(x);}atom.geo.materials=int(atom.geo.mats.size());
 for(auto v:mesh.vertices)atom.geo.verts.push_back({v.x,v.y,v.z});for(auto f:mesh.faces){if(f.a>65535||f.b>65535||f.c>65535)return false;atom.geo.tris.push_back({{uint16_t(f.a),uint16_t(f.b),uint16_t(f.c)},uint16_t(f.material)});}clump.atomics.clear();clump.atomics.push_back(std::move(atom));
 for(size_t o=0;o+72<col.size();o+=8+Get<unsigned>(col,o+4))if(std::string(reinterpret_cast<const char *>(col.data()+o+8))==name){size_t p=o+72;unsigned ns=Get<unsigned>(col,p);p+=4;carModel.spheres.clear();for(unsigned i=0;i<ns;i++,p+=20)carModel.spheres.push_back({Get<V>(col,p+4),Get<float>(col,p),col[p+17]});return true;}
 return false;
}

bool LoadAircraftParts(const char *model){
 const char *env=std::getenv("GAMEDATA");const std::string base=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";ImgArchive img;std::vector<uint8_t> b;if(!img.Open(base+"/models/gta3.img")||!img.Read(std::string(model)+".dff",b))return false;
 std::vector<std::string> names;struct Part {size_t flags;std::string name;};std::vector<Part> parts;
 for(const auto &c:Chunks(b,12,12+Get<unsigned>(b,4)))if(c.type==14){auto ch=Chunks(b,c.start,c.end);unsigned n=Get<unsigned>(b,ch[0].start);for(unsigned i=0;i<n;i++){std::string name;for(auto x:Chunks(b,ch[i+1].start,ch[i+1].end))if(x.type==0x253f2fe)name=std::string(reinterpret_cast<char *>(b.data()+x.start),x.end-x.start).c_str();names.push_back(name);}}
 for(const auto &c:Chunks(b,12,12+Get<unsigned>(b,4)))if(c.type==20){auto ch=Chunks(b,c.start,c.end);unsigned frame=Get<unsigned>(b,ch[0].start);parts.push_back({ch[0].start+8,names.at(frame)});unsigned flags=0;std::memcpy(b.data()+parts.back().flags,&flags,4);}
 clump.atomics.clear();
 for(const Part &part:parts){
  if(part.name.find("_dam")!=std::string::npos||part.name.find("_vlo")!=std::string::npos||part.name.find("_lo")!=std::string::npos)continue;
  unsigned flags=4;std::memcpy(b.data()+part.flags,&flags,4);visualmodel::Mesh mesh;std::string why;if(!visualmodel::Read(b,mesh,why))return false;flags=0;std::memcpy(b.data()+part.flags,&flags,4);
  Atomic a;Identity(a.frame.matrix.data());Identity(a.frame.world.data());a.frame.parent=&clump.root;a.frame.name=part.name;a.geo.mats.clear();
  for(auto material:mesh.materials){Material m;m.colour[3]=material.alpha;a.geo.mats.push_back(m);}a.geo.materials=int(a.geo.mats.size());
  for(auto v:mesh.vertices)a.geo.verts.push_back({v.x,v.y,v.z});for(auto f:mesh.faces){if(f.a>65535||f.b>65535||f.c>65535)return false;a.geo.tris.push_back({{uint16_t(f.a),uint16_t(f.b),uint16_t(f.c)},uint16_t(f.material)});}clump.atomics.push_back(std::move(a));
 }

 return !clump.atomics.empty();
}
bool RotorPart(const std::string &name){return name.find("rotor")!=std::string::npos||name=="bonnet_hi_ok"||name=="bonnet_hi_dam";}
void ActualRotorTrace(){
 Setup();Check(LoadAircraftParts("hunter"),"actual installed Hunter clump supplies named body and rotor atomics");*reinterpret_cast<int16_t *>(cars.data()+0x6a)=155;
 MeshHit hit;Vec3 rayFrom{},rayTo{};bool found=false;
 for(auto &a:clump.atomics){if(!RotorPart(a.frame.name))continue;for(const auto &face:a.geo.tris){
  const V x=a.geo.verts[face.v[0]],y=a.geo.verts[face.v[1]],z=a.geo.verts[face.v[2]];
  const CVector center{(x.x+y.x+z.x)/3,(x.y+y.y+z.y)/3,(x.z+y.z+z.z)/3};CVector n=shotcollision::Cross({y.x-x.x,y.y-x.y,y.z-x.z},{z.x-x.x,z.y-x.y,z.z-x.z});float length=std::sqrt(shotcollision::Dot(n,n));if(length<1e-6f||std::hypot(center.x,center.y)<3)continue;n=shotcollision::Mul(n,1/length);
  const Vec3 from{center.x+n.x*.2f,center.y+n.y*.2f,center.z+n.z*.2f},to{center.x-n.x*.2f,center.y-n.y*.2f,center.z-n.z*.2f};
  if(TraceVehicleMesh(cars.data(),from,to,hit)&&RotorPart(static_cast<const Frame *>(hit.frame)->name)){rayFrom=from;rayTo=to;found=true;break;}
 }if(found)break;}
 Check(found,"actual stock Hunter rotor triangle is reached through public native-layout trace");
 if(found){printf("actual Hunter trace: nearest %s at (%.3f,%.3f,%.3f), fraction%.4f\n",static_cast<const Frame *>(hit.frame)->name.c_str(),hit.position.x,hit.position.y,hit.position.z,hit.fraction);for(auto &a:clump.atomics)if(RotorPart(a.frame.name))a.flags=0;const bool remaining=TraceVehicleMesh(cars.data(),rayFrom,rayTo,hit);Check(!remaining||!RotorPart(static_cast<const Frame *>(hit.frame)->name),"actual hidden solid and blur rotor atomics cannot catch a later trace");}
}
void FireShot(){uint8_t weapon[0x20]{};std::memcpy(weapon,&weaponType,4);auto f=reinterpret_cast<Fire>(entry["_ZN7CWeapon4FireEP7CEntityP7CVector"]);f(weapon,peds.data()+0x8d0*2,&from);}
}

void PlaneWheelPoison(bool enabled){
#if __has_feature(address_sanitizer)
 if(enabled)__asan_poison_memory_region(fixture::cars.data()+0x440,0x40);
 else __asan_unpoison_memory_region(fixture::cars.data()+0x440,0x40);
#else
 (void)enabled;
#endif
}
void TraceChecks(){using namespace fixture;using shotcollision::MeshHit;using shotcollision::TraceVehicleMesh;
 Setup();Panel(-.8f,255,"toprotor");Panel(1,255,"chassis_hi");*reinterpret_cast<int16_t *>(cars.data()+0x6a)=155;MeshHit hit;
 const auto vehicleBefore=cars;const unsigned rotorFlags=clump.atomics[0].flags;Check(!gShot&&!gQuery,"public mesh trace fixture begins outside a bullet scope");
 Check(TraceVehicleMesh(cars.data(),{-3,0,.5f},{3,0,.5f},hit)&&std::fabs(hit.fraction-2.2f/6)<1e-5f&&std::fabs(hit.position.x+.8f)<1e-5f&&std::fabs(hit.normal.x+1)<1e-5f&&hit.atomic==&clump.atomics[0]&&hit.frame==&clump.atomics[0].frame,"read-only public aircraft trace reports nearest actual rotor fraction, point, normal and identity");
 Check(!gShot&&!gQuery&&cars==vehicleBefore&&clump.atomics[0].flags==rotorFlags&&windowStatus==0&&occupantCalls==0&&narrowCalls==0,"public trace preserves native bytes, render flags, damage and shot ownership");
 clump.atomics[0].flags=0;Check(TraceVehicleMesh(cars.data(),{-3,0,.5f},{3,0,.5f},hit)&&hit.atomic==&clump.atomics[1],"hidden solid rotor leaves visible nearest body cover");
 clump.atomics[0].flags=4;clump.atomics[1].frame.matrix[12]=-2;Check(TraceVehicleMesh(cars.data(),{-3,0,.5f},{3,0,.5f},hit)&&hit.atomic==&clump.atomics[1]&&std::fabs(hit.position.x+1)<1e-5f,"nearer body geometry shields rotor regardless atomic iteration order");
 Setup();Panel(-.8f,100,"toprotor");Panel(-1.5f,100,"toprotor_blur");clump.atomics[1].flags=0;Check(TraceVehicleMesh(cars.data(),{-3,0,.5f},{3,0,.5f},hit)&&hit.atomic==&clump.atomics[0],"hidden blur geometry cannot catch a trace aimed at visible solid rotor");clump.atomics[1].flags=4;Check(TraceVehicleMesh(cars.data(),{-3,0,.5f},{3,0,.5f},hit)&&hit.atomic==&clump.atomics[1],"currently visible translucent blur triangle participates in pure mesh nearest hit");
 Setup();Panel(-.8f,89,"windscreen_hi_ok");Panel(1);Check(TraceVehicleMesh(cars.data(),{-3,0,.5f},{3,0,.5f},hit)&&hit.atomic==&clump.atomics[0],"pure mesh helper reports nearest visible glass rather than bullet occupant penetration");
 Setup();Panel(-.8f,255,"toprotor");*reinterpret_cast<int *>(cars.data()+0x348)=4;*reinterpret_cast<int16_t *>(cars.data()+0x6a)=190;std::memset(cars.data()+0x440,0xa5,0x40);PlaneWheelPoison(true);Check(TraceVehicleMesh(cars.data(),{-3,0,.5f},{3,0,.5f},hit)&&hit.atomic==&clump.atomics[0],"CPlane explicit trace uses mesh only without automobile wheel-node classification");PlaneWheelPoison(false);
 Setup();Panel(-.8f,255,"toprotor");clump.atomics[0].frame.matrix[12]=2;reinterpret_cast<float *>(cars.data()+8)[12]=3;staleLTM=true;clump.atomics[0].frame.world[12]=100;Check(TraceVehicleMesh(cars.data(),{0,0,.5f},{6,0,.5f},hit)&&std::fabs(hit.position.x-4.2f)<1e-5f,"public trace uses current local rotor plus native root despite stale rendered transform");
 for(int change:{1,2,3,4,5,6}){Setup();Panel(-.8f,255,"toprotor");afterTrace=change;Check(!TraceVehicleMesh(cars.data(),{-3,0,.5f},{3,0,.5f},hit)&&!hit.atomic&&!hit.frame,"generation, clump, model, transform or pool removal during trace invalidates and clears result");afterTrace=0;}
 Setup();Panel(-.8f);clumpType=1;Check(!TraceVehicleMesh(cars.data(),{-3,0,.5f},{3,0,.5f},hit)&&!hit.atomic,"vehicle with a non-clump RW object refuses trace before walking atomics");
 Setup();Panel(-.8f);hit.atomic=reinterpret_cast<void *>(0x1);Check(!TraceVehicleMesh(cars.data()+1,{-3,0,.5f},{3,0,.5f},hit)&&!hit.atomic,"non-pool-aligned vehicle pointer refuses read-only trace");
 Check(!TraceVehicleMesh(cars.data(),{NAN,0,0},{1,0,0},hit)&&!hit.atomic,"nonfinite segment refuses read-only trace");
 Setup();Panel(-.8f);Check(!TraceVehicleMesh(cars.data(),{-3,0,2},{3,0,2},hit)&&!hit.atomic,"public finite miss returns cleared result");
}
int main(int argc,char **argv){const bool assets=argc>1&&!std::strcmp(argv[1],"--assets");using namespace fixture;Setup();Check(shotcollision::Install(),"native ABI hooks install");TraceChecks();Setup();
 Panel(-.8f);FireShot();Check(victim==cars.data() && std::fabs(result.p.x+.8f)<1e-5f,"real metal blocks native head-only retarget and uses visible surface");
 Setup();Panel(-.8f,128,"windscreen_hi_ok");*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;FireShot();Check(victim==cars.data() && result.surfaceB==7,"glass retains native car impact when no seated body is behind it");
 Setup();Panel(-.8f,255,"door_lr_hi_ok");*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;nativeGlassGate=doDamage=carparts::enabled=true;FireShot();Check(carparts::hits==1&&carparts::component==9,"actual accepted opaque part impact reaches ballistic native part owner with mechanics disabled");
 Setup();Panel(-.8f,89,"door_lr_hi_ok");*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;nativeGlassGate=doDamage=carparts::enabled=true;FireShot();Check(carparts::hits==0,"a glazing hit cannot shoot the entire door off");
 for(int model:{157,158}){
  Setup();Panel(-.8f,89,"door_lf_hi_ok");*reinterpret_cast<int16_t *>(cars.data()+0x6a)=model;roundBudget=100;doDamage=true;FireShot();Check(lastPedDamage>0&&lastPedDamage<20,"penetrated armor glazing scales occupant damage by residual budget");
  Setup();Panel(-.8f,255,"door_lf_hi_ok");*reinterpret_cast<int16_t *>(cars.data()+0x6a)=model;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;nativeGlassGate=doDamage=true;FireShot();Check(*reinterpret_cast<float *>(cars.data()+0x2a0)==1000,"armored body stops ordinary rounds without whole-vehicle health loss");
  Setup();Panel(-.8f,89,"door_lf_hi_ok");*reinterpret_cast<int16_t *>(cars.data()+0x6a)=model;nativeGlassGate=doDamage=true;FireShot();Check(victim==cars.data()&&carglass::calls==0,"armored glazing stops occupant retarget and pane destruction");
  Setup();Panel(-.8f,255,"door_lf_hi_ok");*reinterpret_cast<int16_t *>(cars.data()+0x6a)=model;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;nativeGlassGate=doDamage=true;roundBudget=150;FireShot();Check(*reinterpret_cast<float *>(cars.data()+0x2a0)<1000&&*reinterpret_cast<float *>(cars.data()+0x2a0)>850,"powerful armor penetration preserves only residual hull damage");
 }
 Setup();Panel(-.8f,89,"door_lf_hi_ok");*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;FireShot();Check(carglass::calls==1&&carglass::car==cars.data()&&carglass::part==&clump.atomics[0]&&carglass::face>=0&&std::fabs(carglass::point.x+.8f)<1e-5f,"actual glass impact certifies exact car atomic triangle and point");
 Setup();Panel(-.8f,89,"door_lf_hi_ok");Panel(2);FireShot();Check(carglass::calls==1&&carglass::part==&clump.atomics[0]&&victim==peds.data()&&result.pieceB==3,"glass before occupant breaks without retargeting body hit");
 Setup();Panel(-.8f,89,"door_lf_hi_ok");wall=true;FireShot();Check(carglass::calls==0,"world obstruction prevents side-window effects");
 Setup();Panel(-.8f,89,"door_lf_hi_ok");*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;{uint8_t gun[32]{};int t=17;memcpy(gun,&t,4);Shot query(gun,peds.data()+0x8d0*2);WorldEntry()(from,to,result,victim,true,true,true,true,true,false,false,true);DirectDamage();}Check(carglass::calls==0,"zero damage point only accepted in native drive-by and mounted scopes");
 Setup();Panel(-.8f,89,"door_lf_hi_ok");*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;{uint8_t gun[32]{};int t=17;memcpy(gun,&t,4);reinterpret_cast<FireCar>(entry["_ZN7CWeapon21FireInstantHitFromCarEP8CVehiclebb"])(gun,cars.data()+0x6f0,true,false);}Check(carglass::calls==1,"native drive-by damage path consumes glass without occupant checker");
 Setup();Panel(-.8f,89,"door_lf_hi_ok");Panel(2);{uint8_t gun[32]{};int t=17;memcpy(gun,&t,4);reinterpret_cast<FireCar>(entry["_ZN7CWeapon21FireInstantHitFromCarEP8CVehiclebb"])(gun,cars.data()+0x6f0,true,false);}Check(carglass::calls==1&&victim==peds.data(),"direct seated-ped damage also consumes crossed side glass");
 Setup();Panel(2);FireShot();Check(victim==peds.data() && result.pieceB==3 && std::fabs(result.p.x+.25f)<1e-5f,"open window exposes actual animated body before far car panel");
 Setup();Panel(2);carModel.spheres={{{0,0,.5f},1,0}};*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;from.z=to.z=1.5f;FireShot();Check(victim==nullptr,"empty space around inflated car sphere has no bullet impact");
 Setup();Panel(2);wall=true;FireShot();Check(victim==reinterpret_cast<uint8_t *>(0x1234),"nearer native world obstruction wins over seated body");
 Setup();Panel(-.8f);clump.atomics[0].flags=0;Panel(2);FireShot();Check(victim==peds.data(),"hidden broken door no longer blocks body");
 Setup();Panel(-.8f);clump.atomics[0].frame.matrix[12]=4;FireShot();Check(victim==peds.data(),"current moved door frame replaces closed bind pose");
 Setup();Panel(-.8f);float t=1;Point p{};Line l{from,0,to,0};NarrowEntry()(l,cars.data()+8,&carModel,p,t,false,false);Check(std::fabs(p.p.x+1)<1e-5f,"ordinary nonbullet collision retains native body COL");
 Check(tyres,"world tyre include flag restored after shot");
 
 if(assets)for(const char *car:{"oceanic","sentinel","police"}){
  Setup();Check(LoadCar(car),"installed car DFF and COL load through native-layout RW adapter");*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;
  // Probe horizontal side rays through actual stock sphere caps. Exact ray
  // coordinates and selected empty-space counts are retained in test output.
  // Captured stock phantom rays are fixed coordinates, independent of the
  // implementation's triangle solver and current sweep selection.
  const bool oceanic=!strcmp(car,"oceanic"),sentinel=!strcmp(car,"sentinel");
  from={-4,oceanic||sentinel?-3.f:-2.9f,oceanic?.3f:sentinel?-.4f:-.2f};to={4,from.y,from.z};
  Line stock{from,0,to,0};Point native{};float limit=1;Check(NativeNarrow(stock,cars.data()+8,&carModel,native,limit,false,false),"captured actual stock COL sphere hits phantom ray");FireShot();Check(victim==nullptr,"captured actual DFF empty-space ray bypasses stock COL cap");
  from={-4,0,0};to={4,0,0};FireShot();Check(victim==cars.data() && result.p.x<-.98f && result.p.x>-1.1f,"actual closed car door remains real nearest metal cover");
  int removed=0;for(int yi=-30;yi<=30;yi++)for(int zi=-5;zi<=15;zi++){
   from={-4,yi*.1f,zi*.1f};to={4,from.y,from.z};Line l{from,0,to,0};Point native{};float t=1;if(!NativeNarrow(l,cars.data()+8,&carModel,native,t,false,false))continue;
   MeshQuery q{l,1,cars.data()};TestAtomic(&clump.atomics[0],&q);if(q.valid&&!q.hit&&!q.glass){FireShot();Check(victim==nullptr,"actual stock inflated COL ray misses rendered car");if(!removed)printf("  first phantom ray y=%.3f z=%.3f nativeHitX=%.4f\n",from.y,from.z,native.p.x);removed++;}
  }
  *reinterpret_cast<void **>(cars.data()+0x218)=peds.data();from={-4,0,.6f};to={4,0,.6f};pedModel.spheres={{{0,0,.6f},.25f,3}};FireShot();Check(victim==peds.data()&&result.pieceB==3,"installed closed side-window car mesh permits a real nearest seated body ray");
  printf("asset %s: %zu visible triangles, %d inflated sphere rays corrected\n",car,clump.atomics[0].geo.tris.size(),removed);Check(removed>0,"stock asset proves collision outside its current visible mesh");
 }


 Setup();Panel(2);*reinterpret_cast<void **>(cars.data()+0x220)=peds.data()+0x8d0;*reinterpret_cast<int16_t *>(peds.data()+0x8d0+0x6a)=8;FireShot();Check(victim==peds.data()+0x8d0&&std::fabs(result.p.x+.7f)<1e-5f,"nearest passenger body beats farther driver independently of slot iteration");
 Setup();Panel(2);stander=true;peds[0x8d0+0x4c0]=0;*reinterpret_cast<void **>(peds.data()+0x8d0+0x4b8)=nullptr;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;FireShot();Check(victim==peds.data()+0x8d0&&std::fabs(result.p.x-.75f)<1e-5f,"pedestrian behind an open car ray competes before the farther car mesh");
 Setup();Panel(-.8f);stander=true;peds[0x8d0+0x4c0]=0;*reinterpret_cast<void **>(peds.data()+0x8d0+0x4b8)=nullptr;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;FireShot();Check(victim==cars.data(),"real metal occludes pedestrian behind car");
 Setup();Panel(2);noPeds=true;FireShot();Check(victim==cars.data()&&std::fabs(result.p.x-2)<1e-5f,"native LOS peds=false excludes seated bodies too");
 Setup();Panel(2);*reinterpret_cast<void **>(peds.data()+0x4b8)=cars.data()+0x6f0;FireShot();Check(victim==cars.data()&&std::fabs(result.p.x-2)<1e-5f,"foreign seat ownership cannot become this car occupant target");
 Setup();Panel(-.8f);fireType=2;FireShot();Check(victim==peds.data()&&result.pieceB==6,"custom projectile in gun slot retains native collision");

 Setup();Panel(-.8f);reinterpret_cast<void (*)(CVector *,CVector *,int)>(entry["_Z22FireOneInstantHitRoundP7CVectorS0_i"])(&from,&to,15);Check(rounds==1&&roundDamage==15&&victim==cars.data()&&std::fabs(result.p.x+.8f)<1e-5f,"native Hunter cannon entry uses real nearest metal and forwards damage unchanged");
 Setup();Panel(2);reinterpret_cast<void (*)(CVector *,CVector *,int)>(entry["_Z22FireOneInstantHitRoundP7CVectorS0_i"])(&from,&to,15);Check(rounds==2&&victim==peds.data()&&result.pieceB==3,"native Hunter cannon can hit exposed seated body without a CWeapon Fire call");

 Setup();Panel(-.8f,89,"door_lf_hi_ok");*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;reinterpret_cast<void (*)(CVector *,CVector *,int)>(entry["_Z22FireOneInstantHitRoundP7CVectorS0_i"])(&from,&to,15);Check(carglass::calls==1,"native mounted round consumes zero-position vehicle damage");
 Setup();Panel(-.8f);staleLTM=true;clump.atomics[0].frame.world[12]=10;clump.root.world[12]=10;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;FireShot();Check(victim==cars.data()&&std::fabs(result.p.x+.8f)<1e-5f,"native control before RW update cannot leave whole-car shot box at old rendered position");
 Setup();Panel(-.8f);staleLTM=true;Identity(clump.atomics[0].frame.world.data());auto current=reinterpret_cast<float *>(cars.data()+8);current[0]=0;current[1]=1;current[4]=-1;current[5]=0;current[12]=5;from={5,-3,.5f};to={5,3,.5f};*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;FireShot();Check(victim==cars.data()&&std::fabs(result.p.x-5)<1e-5f&&std::fabs(result.p.y+.8f)<1e-5f,"current native root rotation and translation replace stale whole-car RW basis");
 Setup();Panel(-.8f);staleLTM=true;clump.atomics[0].frame.world[12]=20;clump.atomics[0].frame.matrix[12]=4;reinterpret_cast<float *>(cars.data()+8)[12]=5;from={6,0,.5f};to={10,0,.5f};*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;FireShot();Check(victim==cars.data()&&std::fabs(result.p.x-8.2f)<1e-5f,"current local moving door composes onto native root despite both cached LTMs lagging");

 Setup();Panel(-.8f);Frame hinge;Identity(hinge.matrix.data());hinge.parent=&clump.root;hinge.matrix[0]=0;hinge.matrix[1]=2;hinge.matrix[4]=-1;hinge.matrix[5]=0;hinge.matrix[12]=1;clump.atomics[0].frame.parent=&hinge;clump.atomics[0].frame.matrix[12]=.2f;reinterpret_cast<float *>(cars.data()+8)[12]=2;from={3,-3,.5f};to={3,3,.5f};*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;FireShot();Check(victim==cars.data()&&std::fabs(result.p.x-3)<1e-5f&&std::fabs(result.p.y+1.2f)<1e-5f&&std::fabs(result.n.y+1)<1e-5f,"nested moving door hierarchy keeps current local motion and correct scaled normal");


 Setup();Panel(-.8f,89,"windscreen_hi_ok");Panel(2);windowMode=true;{uint8_t gun[32]{};int type=17;std::memcpy(gun,&type,4);Shot context(gun,peds.data()+0x8d0*2);WorldEntry()(from,to,result,victim,true,true,true,true,true,false,false,true);Check(victim==peds.data()&&windowStatus==0&&windowSounds==0&&carglass::calls==0,"precision obstruction query remains read-only until native impact checker consumes glass");}
 for(int piece:{3,6}){
  Setup();Panel(-.8f,89,"door_lf_hi_ok");Panel(2);pedModel.spheres[0].piece=uint8_t(piece);FireShot();Check(victim==peds.data()&&result.pieceB==piece,"ordinary closed side-window glass permits native-style occupant body/head hit");FireShot();Check(victim==peds.data()&&result.pieceB==piece&&clump.atomics[0].flags==4&&clump.atomics[0].geo.mats[0].colour[3]==89,"embedded side glass cannot become a permanent shield when native never hides that door mesh");
  Setup();Panel(-.8f,89,"windscreen_hi_ok");Panel(-.8f,242,"windscreen_hi_dam");clump.atomics[1].id=2;clump.atomics[1].flags=0;Panel(2);windowMode=true;pedModel.spheres[0].piece=uint8_t(piece);FireShot();Check(victim==peds.data()&&result.pieceB==piece&&windowStatus==1&&windowSounds==1&&clump.atomics[0].flags==0&&clump.atomics[1].flags==4,"intact windscreen penetrates to body/head while native panel consumer selects cracked atomic");FireShot();Check(victim==peds.data()&&windowStatus==3&&windowSounds==2&&clump.atomics[1].flags==0,"second body/head shot retains native windscreen shatter and removes cracked atomic");
 }
 Setup();Panel(-.8f,89,"door_lf_hi_ok");Panel(-.5f);FireShot();Check(victim==cars.data(),"real opaque metal behind translucent glass still shields seated target");
 Setup();Panel(-.8f,89,"windscreen_hi_ok");Panel(2);ignoreSee=true;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;FireShot();Check(victim==cars.data()&&std::fabs(result.p.x-2)<1e-5f,"native ignoreSeeThrough excludes ordinary glass from precise LOS");
 Setup();Panel(-.8f);weaponType=35;auto instant=entry.find("_ZN7CWeapon14FireInstantHitEP7CEntityP7CVector");Check(instant!=entry.end(),"direct native InstantHit entry is scoped independently of Fire");if(instant!=entry.end()){uint8_t cannon[32]{};int type=35;std::memcpy(cannon,&type,4);reinterpret_cast<Fire>(instant->second)(cannon,peds.data()+0x8d0*2,&from);Check(victim==cars.data()&&std::fabs(result.p.x+.8f)<1e-5f,"credited Hunter weapon35 direct instant hit uses actual car mesh");}
 Setup();Panel(-.8f);nested=true;FireShot();Check(victim==cars.data() && std::fabs(result.p.x+.8f)<1e-5f && nestedX==0 && fireCalls==2,"nested nonbullet uses native path and restores outer bullet scope");
 Setup();Panel(-.8f);throwWorld=true;try{FireShot();}catch(int){}throwWorld=false;Check(!gShot&&!gQuery&&tyres,"exception restores both shot/world scopes and native flags");
 Setup();Panel(-.8f);auto carFire=reinterpret_cast<FireCar>(entry["_ZN7CWeapon21FireInstantHitFromCarEP8CVehiclebb"]);uint8_t w[32]{};int type=17;std::memcpy(w,&type,4);*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;carFire(w,cars.data(),true,false);Check(victim==cars.data()&&result.p.x<-.79f,"drive-by entry scopes the same native nearest collision");
 Setup();Panel(-.8f);reinterpret_cast<void (*)()>(entry["_ZN11CBulletInfo6UpdateEv"])();Check(updates==1&&victim==cars.data()&&result.p.x<-.79f,"deferred native sniper bullet update receives precision scope");
 Setup();Panel(-.8f);stale=true;FireShot();Check(victim==peds.data()&&result.pieceB==6,"invalid native vehicle generation falls back without precision certification");
 Setup();Panel(-.8f);clump.atomics[0].geo.tris[0].v[0]=65000;FireShot();Check(victim==peds.data()&&result.pieceB==6,"malformed native RW index conservatively preserves stock collision");
 Setup();Panel(-.8f);*reinterpret_cast<int16_t *>(cars.data()+0x6a)=155;FireShot();Check(victim==peds.data()&&result.pieceB==6,"native CAutomobile-based helicopters retain their own collision/damage path");
 Setup();Panel(-.8f);clump.atomics[0].frame.name="wheel";*reinterpret_cast<void **>(cars.data()+0x460)=&clump.atomics[0].frame;FireShot();Check(victim==cars.data()&&result.pieceB==13&&result.surfaceB==23,"actual current native wheel node preserves burst-tyre piece and surface");
 Setup();Panel(-.8f);weaponType=30;FireShot();Check(victim==peds.data()&&result.pieceB==6,"projectiles retain native collision outside bullet scope");
 Setup();Panel(-.8f);from={-1000,0,.5f};to={1500,0,.5f};FireShot();Check(victim==cars.data()&&std::fabs(result.p.x+.8f)<.0003f,"long finite shot still preserves nearest real metal cover");
 if(assets)ActualRotorTrace();
 if(assets){Setup();Check(LoadCar("police"),"benchmark loads actual stock police mesh");*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;from={-4,0,0};to={4,0,0};
 auto begin=std::chrono::steady_clock::now();for(int n=0;n<1000;n++)FireShot();auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();printf("actual stock police: %.4f ms per native-layout shot query (1000 samples)\n",elapsed/1000);}
 Setup();Panel(-.8f);noPeds=true;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;carmechanics::enabled=doDamage=nestCover=true;FireShot();
 Check(nestedCoverPreserved&&carmechanics::deliveries==1,"nested read-only cover query preserves its parent impact certificate");
 Setup();Panel(-.8f);carmechanics::enabled=doDamage=true;FireShot();
 Check(carmechanics::deliveries==1&&std::fabs(carmechanics::lastRay.limit-2.75f/6)<.0001f&&*reinterpret_cast<float *>(peds.data()+0x44c)==100&&carglass::calls==0,"seated occupant behind opaque panel clips internal damage without occupant/glass side effects");
 Setup();Panel(-.8f);noPeds=true;carmechanics::enabled=doDamage=true;FireShot();Check(carmechanics::deliveries==1&&carmechanics::lastRay.limit==1,"native peds=false excludes seated cover as well");
 Setup();Panel(-.8f);noPeds=true;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;carmechanics::enabled=true;
 {Shot shot(w,peds.data()+0x8d0*2);WorldEntry()(from,to,result,victim,true,true,true,true,true,false,false,true);Point unrelated=result;unrelated.p.x+=.1f;
  {ImpactScope other(w,peds.data()+0x8d0*2,victim,&from,&to,&unrelated);}
  Check(shot.pending.actor.car==cars.data(),"mismatched impact scope preserves the pending certified ray");
  Hook_Impact(w,peds.data()+0x8d0*2,victim,&from,&to,&result,{});
 }
 Check(carmechanics::deliveries==1,"real native impact still delivers after an unrelated impact scope");
 // Native vehicle damage is position-zero in DoBulletImpact; only its certified impact scope authorizes it.
 Setup();Panel(-.8f);noPeds=true;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;carmechanics::enabled=doDamage=true;FireShot();
 Check(carmechanics::deliveries==1&&carmechanics::lastRay.count==1&&carmechanics::lastRay.layers[0].material==projectilemodel::Material::Body,"accepted native body impact delivers one material path with coincident triangles merged");
 Setup();Panel(-.8f);noPeds=true;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;carmechanics::enabled=doDamage=true;acceptDamage=false;FireShot();Check(carmechanics::deliveries==0,"native rejected damage cannot injure modules");
 Setup();Panel(-.8f);noPeds=true;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;carmechanics::enabled=doDamage=doubleDamage=true;FireShot();Check(carmechanics::deliveries==1,"native duplicate damage call cannot deliver the same pending ray twice");
 for(int mutation:{1,2,3,4}){Setup();Panel(-.8f);noPeds=true;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;carmechanics::enabled=doDamage=true;damageMutation=mutation;FireShot();Check(carmechanics::deliveries==0,"native callback recycling/clump/motion/repair retires pending projectile");}
 Setup();Panel(-.8f);noPeds=true;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;carmechanics::enabled=doDamage=repairBeforeImpact=true;FireShot();Check(carmechanics::deliveries==0,"repair between mesh query and native impact retires the pending round");
 Setup();Panel(-.8f);noPeds=true;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;carmechanics::enabled=doDamage=coverWall=true;FireShot();
 Check(carmechanics::deliveries==1&&std::fabs(carmechanics::lastRay.limit-.5f)<.001f&&ignore==nullptr,"nearer real cover clips internal ray and restores ignored entity");
 Setup();Panel(-.8f);noPeds=true;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;carmechanics::enabled=doDamage=throwCover=true;try{FireShot();}catch(int){}Check(ignore==nullptr&&!gShot&&!gQuery,"cover exception restores ignored entity and both shot/query scopes");
 Setup();Panel(-.8f);carmechanics::enabled=true;{uint8_t gun[32]{};int t=17;memcpy(gun,&t,4);Shot query(gun,peds.data()+0x8d0*2);WorldEntry()(from,to,result,victim,true,true,true,true,true,false,false,true);}Check(carmechanics::deliveries==0,"read-only obstruction query cannot deliver module damage");

 Setup();Panel(-.8f);noPeds=true;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;carmechanics::enabled=true;carFire(w,cars.data(),true,false);
 Check(carmechanics::deliveries==1&&carmechanics::lastRay.budget==20,"actual drive-by type42 and player creator delivers accepted damage");
 Setup();Panel(-.8f);noPeds=true;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;carmechanics::enabled=true;reinterpret_cast<void (*)(CVector *,CVector *,int)>(entry["_Z22FireOneInstantHitRoundP7CVectorS0_i"])(&from,&to,15);
 Check(carmechanics::deliveries==1&&carmechanics::lastRay.budget==15,"actual mounted type23 null-creator round keeps native damage budget");
 auto deferred=[&](bool duplicate,bool mismatch){Setup();Panel(-.8f);noPeds=true;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;carmechanics::enabled=doDamage=true;
  static float step=1;game::api.timeStep=&step;nativebullet::Record r{};r.type=29;r.creator=peds.data()+0x8d0*2;r.active=1;r.position={from.x,from.y,from.z};r.velocity={12,0,0};r.damage=45;
  if(mismatch)r.position.y+=1;memcpy(deferredBullets.data(),&r,sizeof r);if(duplicate)memcpy(deferredBullets.data()+nativebullet::kStride,&r,sizeof r);
  reinterpret_cast<void (*)()>(entry["_ZN11CBulletInfo6UpdateEv"])();};
 deferred(false,false);Check(carmechanics::deliveries==1&&carmechanics::lastRay.budget==45,"unique actual deferred bullet metadata authorizes its zero-position native damage");
 deferred(true,false);Check(carmechanics::deliveries==0,"ambiguous deferred records cannot certify a round");
 deferred(false,true);Check(carmechanics::deliveries==0,"unmatched deferred source cannot authorize module damage");
 deferred(false,false);carmechanics::deliveries=0;{Shot update(nullptr,nullptr,true);WorldEntry()(from,to,result,victim,true,true,true,true,true,false,false,true);Hook_VehicleDamage(victim,nullptr,17,20,result.p);}Check(carmechanics::deliveries==0,"nonzero-position unrelated damage cannot steal a matched deferred ray");
 Setup();Panel(-.8f);noPeds=true;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;carmechanics::enabled=true;*reinterpret_cast<void **>(cars.data()+0x460)=&clump.atomics[0].frame;
 {Shot query(w,peds.data()+0x8d0*2);WorldEntry()(from,to,result,victim,true,true,true,true,true,false,false,true);Hook_Burst(victim,13,true);}Check(carmechanics::deliveries==0,"unrelated burst during an obstruction query lacks a native impact receipt");
 for(int corner=0;corner<4;corner++){Setup();Panel(-.8f);noPeds=true;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;carmechanics::enabled=doDamage=true;
  const size_t offsets[]={0x460,0x448,0x470,0x458};*reinterpret_cast<void **>(cars.data()+offsets[corner])=&clump.atomics[0].frame;FireShot();const int mapping[]={0,2,1,3};
  Check(carmechanics::deliveries==1&&cars[0x351+mapping[corner]]==1&&carmechanics::lastRay.layers[0].material==projectilemodel::Material::Rubber,"native collision-piece tyre burst certifies each wheel and rubber layer");
  FireShot();Check(carmechanics::deliveries==1,"already-burst tyre does not fabricate a new accepted burst");}
 Setup();Panel(-.8f);noPeds=true;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;carmechanics::enabled=doDamage=true;*reinterpret_cast<void **>(cars.data()+0x460)=&clump.atomics[0].frame;cars[0x299]=2;FireShot();Check(carmechanics::deliveries==0,"native tyreproof rejection cannot injure wheel modules");
 Setup();for(int i=0;i<65;i++)Panel(-.8f+i*.02f);noPeds=true;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;carmechanics::enabled=doDamage=true;FireShot();Check(victim==cars.data()&&carmechanics::deliveries==0,"layer overflow preserves native precise hit and rejects module effects");
 Setup();Panel(-.8f,89);Panel(-.5f);noPeds=true;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;carmechanics::enabled=doDamage=true;FireShot();Check(carmechanics::deliveries==1&&carmechanics::lastRay.count==2,"actual glass then body mesh crossings reach the material path");
 if(assets)for(const char *model:{"police","sentinel","deluxo"}){
  Setup();Check(LoadCar(model),"projectile path loads actual stock vehicle geometry");noPeds=true;*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;carmechanics::enabled=doDamage=true;
  bool accepted=false;for(int yi=-20;yi<=20&&!accepted;yi++)for(int zi=-5;zi<=10&&!accepted;zi++){
   from={-4,yi*.1f,zi*.1f};to={4,from.y,from.z};FireShot();accepted=carmechanics::deliveries>0;
  }
  Check(accepted&&carmechanics::lastRay.count>0&&carmechanics::lastRay.count<=64,"actual stock mesh delivers accepted bounded material path");
  printf("projectile asset %s: %zu material crossings\n",model,accepted?carmechanics::lastRay.count:0);
 }
 carmechanics::enabled=false;
 Setup();Panel(-.8f,89,"door_lf_hi_ok");*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;nativeGlassGate=doDamage=true;FireShot();Check(carglass::calls==1,"normal accepted zero-position impact breaks side glass without native windscreen-only occupant prepass");
 printf("shotcollision: %d failures\n",fail);return fail?1:0;
}

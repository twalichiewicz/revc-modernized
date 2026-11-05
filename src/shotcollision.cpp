#include "shotcollision.h"
#include "carglass.h"
#include "carmechanics.h"
#include "carparts.h"
#include "airframedamage.h"
#include "nativebullet.h"
#include "game.h"
#include "hooks.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <dlfcn.h>

namespace shotcollision { namespace {
// macVC 1.0.5217: CColLine has padded vectors, CColPoint is 0x28 bytes.
struct Line { CVector a; uint32_t padA; CVector b; uint32_t padB; };
struct Point { CVector p; uint32_t padA; CVector n; uint32_t padB; uint8_t surfaceA,pieceA,surfaceB,pieceB; float depth; };
static_assert(sizeof(Line)==0x20 && sizeof(Point)==0x28);
struct Triangle { uint16_t v[3],material; };
using Narrow=bool (*)(const Line &,const void *,void *,Point &,float &,bool,bool);
using World=bool (*)(const CVector &,const CVector &,Point &,uint8_t *&,bool,bool,bool,bool,bool,bool,bool,bool);
using Fire=bool (*)(uint8_t *,uint8_t *,CVector *);
using FireCar=bool (*)(uint8_t *,uint8_t *,bool,bool);
using Occupant=void (*)(uint8_t **,Point *,int,const CVector &,const CVector &);
Narrow gNarrow; World gWorld; Fire gFire,gInstant; FireCar gFireCar; Occupant gOccupant; void (*gUpdate)();void (*gRound)(CVector *,CVector *,int);
void (*gVehicleDamage)(uint8_t *,uint8_t *,int,float,CVector);
void (*gImpact)(uint8_t *,uint8_t *,uint8_t *,CVector *,CVector *,Point *,Vec2);
void (*gBurst)(uint8_t *,uint8_t,bool);int (*gWheelStatus)(void *,int);
const uint8_t *gBullets;
bool (*gPedDamage)(uint8_t *,uint8_t *,int,float,int,uint8_t);
struct RW {
 void *(*atomics)(void *,void *(*)(void *,void *),void *);
 uint32_t (*flags)(const void *);
 int (*objectType)(const void *);
 void *(*frame)(const void *),*(*geometry)(const void *);
 float *(*matrix)(void *);
 void *(*root)(const void *);
 const char *(*name)(void *);
 void *(*parent)(const void *);
 int (*nv)(const void *),(*nt)(const void *);
 const Triangle *(*triangles)(const void *);
 void *(*morph)(const void *,int);
 const CVector *(*vertices)(const void *);
 void *(*material)(const void *,int);
 const uint8_t *(*colour)(const void *);
} rw;
void **gVehiclePool,**gPedPool,**gModels;
void *(*gAnimate)(void *,void *);
bool gReady;
bool Finite(CVector p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
CVector Sub(CVector a,CVector b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
CVector Add(CVector a,CVector b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
CVector Mul(CVector a,float s){return {a.x*s,a.y*s,a.z*s};}
float Dot(CVector a,CVector b){return a.x*b.x+a.y*b.y+a.z*b.z;}
CVector Cross(CVector a,CVector b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
// Check the address against allocation bounds BEFORE asking a native pool for
// a handle. Native GetRef itself assumes its input is a valid pool address.
int Ref(const uint8_t *entity,void **pool,size_t stride,uint8_t *(*get)(int))
{
 if(!entity||!pool||!*pool||!get)return -1;
 auto p=static_cast<const uint8_t *>(*pool);const auto entries=*reinterpret_cast<uint8_t *const *>(p);
 const auto flags=*reinterpret_cast<uint8_t *const *>(p+8);const int cap=*reinterpret_cast<const int *>(p+16);
 const uintptr_t e=reinterpret_cast<uintptr_t>(entity),base=reinterpret_cast<uintptr_t>(entries);
 if(!entries||!flags||cap<=0||cap>100000||e<base||(e-base)%stride||((e-base)/stride)>=size_t(cap))return -1;
 const size_t index=(e-base)/stride;if(flags[index]&0x80)return -1;
 const int ref=int(index*256)|flags[index];return get(ref)==entity?ref:-1;
}
struct GlassFace {const void *atomic=nullptr;int triangle=-1;};
struct Certified {uint8_t *car=nullptr;int ref=-1;Point point{};GlassFace face{};CVector from{},to{};};
struct GlassContact {uint8_t *car=nullptr,*ped=nullptr;int carRef=-1,pedRef=-1;Point glass{},body{};GlassFace face{};};
struct Snapshot {
 uint8_t *car=nullptr;int ref=-1,model=-1;void *clump=nullptr,*vtable=nullptr;std::array<float,16> matrix{};
 bool Current()const{return car&&Ref(car,gVehiclePool,0x6f0,game::api.PoolsGetVehicle)==ref&&game::EntityModel(car)==model&&
  game::EntityClump(car)==clump&&*reinterpret_cast<void **>(car)==vtable&&!std::memcmp(car+8,matrix.data(),sizeof matrix);}
};
Snapshot Capture(uint8_t *car,int ref){Snapshot s;s.car=car;s.ref=ref;s.model=game::EntityModel(car);s.clump=game::EntityClump(car);s.vtable=*reinterpret_cast<void **>(car);std::memcpy(s.matrix.data(),car+8,sizeof s.matrix);return s;}
struct Pending {
 airframedamage::Token airToken;std::array<const void *,projectilemodel::kLayers> atomics{};std::array<CVector,projectilemodel::kLayers> normals{};
 Snapshot actor;carmechanics::BulletToken token;carparts::BulletToken partToken;int rotor=-1,component=0;float opening=-1;Point point{};CVector from{},to{};std::array<projectilemodel::Layer,projectilemodel::kLayers> layers{};size_t count=0;
 bool buildings=true,peds=true,objects=true,dummies=true,see=false,some=false,shoot=true;
};
bool BulletWeapon(int type){return (type>=17&&type<=29)||type==32||type==33||type==35;}
struct Shot;
thread_local Shot *gShot;
struct Shot {
 CVector muzzle{};bool muzzleKnown=false;
 Shot *parent=gShot;bool active=false,zeroDamagePoint=false,deferred=false;unsigned readOnly=0;uint8_t *shooter=nullptr,*damageCreator=nullptr;Certified last;GlassContact glass;Pending pending{};Snapshot materialConsumed{};CVector consumedPoint{};
 Snapshot occupantCar;uint8_t *armoredPed=nullptr;int armoredPedRef=-1;float occupantRatio=1;
 int type=-1,damageType=-1,deferredType=-1;float budget=0,deferredBudget=0;uint8_t *deferredCreator=nullptr;
 size_t deferredIndex=nativebullet::kCount;nativebullet::Record deferredRecord{};
 Shot(uint8_t *w,uint8_t *s,bool later=false,bool inlineDamage=false,const CVector *source=nullptr):zeroDamagePoint(inlineDamage),deferred(later),shooter(s),damageCreator(s){
  if(source&&Finite(*source)){muzzle=*source;muzzleKnown=true;}
  if(later)active=true;
  else if(w){
   const int t=game::WeaponType(w);const bool bullet=BulletWeapon(t);type=damageType=t;
   const uint8_t *info=bullet&&game::api.GetWeaponInfo?game::api.GetWeaponInfo(t):nullptr;
   int fireType=0;if(info){std::memcpy(&fireType,info,sizeof fireType);uint32_t damage=0;std::memcpy(&damage,info+0x14,4);if(damage<=10000)budget=float(damage);}active=bullet&&fireType==1;
  }
  gShot=this;
 }
 ~Shot(){gShot=parent;}
};
struct Query;
thread_local Query *gQuery;
struct Query {
 Query *parent=gQuery;bool peds;uint8_t *car=nullptr,*ped=nullptr;int carRef=-1,pedRef=-1;Point point{},glassPoint{};bool glass=false;
 GlassFace face{};Pending pending{};
 explicit Query(bool allow):peds(allow){gQuery=this;}
 ~Query(){gQuery=parent;}
};
// Invert a current RW affine transform; transform the segment, not every car
// vertex. The segment fraction remains comparable to the native closest hit.
struct Basis {
 CVector r,f,u,p,ir,iff,iu;
 bool Set(const float *m){
  if(!m)return false;r={m[0],m[1],m[2]};f={m[4],m[5],m[6]};u={m[8],m[9],m[10]};p={m[12],m[13],m[14]};
  const float det=Dot(r,Cross(f,u));if(!Finite(r)||!Finite(f)||!Finite(u)||!Finite(p)||!std::isfinite(det)||std::fabs(det)<1e-7f)return false;
  ir=Mul(Cross(f,u),1/det);iff=Mul(Cross(u,r),1/det);iu=Mul(Cross(r,f),1/det);return true;
 }
 CVector Local(CVector v)const{v=Sub(v,p);return {Dot(ir,v),Dot(iff,v),Dot(iu,v)};}
 CVector Vector(CVector v)const{return Add(Add(Mul(r,v.x),Mul(f,v.y)),Mul(u,v.z));}
 CVector Position(CVector v)const{return Add(p,Vector(v));}
 bool Append(const Basis &child){
  const CVector nr=Vector(child.r),nf=Vector(child.f),nu=Vector(child.u),np=Position(child.p);
  const float m[16]={nr.x,nr.y,nr.z,0,nf.x,nf.y,nf.z,0,nu.x,nu.y,nu.z,0,np.x,np.y,np.z,0};return Set(m);
 }
 CVector Normal(CVector v)const{return Add(Add(Mul(ir,v.x),Mul(iff,v.y)),Mul(iu,v.z));}
};
// CWorld runs actor controls before its later UpdateRW/UpdateRwFrame pass.
// Compose current child LOCAL frames onto the current native entity matrix;
// cached world LTMs can lag both the car root and an animated door. Read only.
bool CurrentBasis(void *frame,uint8_t *car,Basis &basis)
{
 if(!frame||!car||!basis.Set(reinterpret_cast<const float *>(car+8)))return false;
 void *root=rw.root(game::EntityClump(car));if(!root)return false;
 void *chain[32];unsigned count=0;
 while(frame!=root&&frame&&count<32){chain[count++]=frame;frame=rw.parent(frame);}
 if(frame!=root)return false;
 while(count){Basis local;if(!local.Set(rw.matrix(chain[--count]))||!basis.Append(local))return false;}
 return true;
}
bool TriangleHit(CVector a,CVector d,CVector x,CVector y,CVector z,float &t,CVector &normal)
{
 const CVector e=Sub(y,x),f=Sub(z,x),h=Cross(d,f);const float det=Dot(e,h);
 if(!std::isfinite(det)||std::fabs(det)<1e-9f)return false;
 const CVector s=Sub(a,x);const float u=Dot(s,h)/det;if(u<0||u>1)return false;
 const CVector q=Cross(s,e);const float v=Dot(d,q)/det;if(v<0||u+v>1)return false;
 const float fraction=Dot(f,q)/det;if(fraction<0||fraction>=t||!std::isfinite(fraction))return false;
 normal=Cross(e,f);const float length=std::sqrt(Dot(normal,normal));if(length<1e-8f)return false;
 normal=Mul(normal,1/length);if(Dot(normal,d)>0)normal=Mul(normal,-1);t=fraction;return true;
}
uint8_t Piece(void *frame,uint8_t *car)
{
 // Match current native wheel frames, including their atomic children.
 // SetUpWheelColModel defines these offsets/pieces; no name/handedness guess.
 for(int depth=0;frame&&depth<16;depth++,frame=rw.parent(frame)){
  if(car){
   for(const auto &wheel: {std::pair<size_t,uint8_t>{0x460,13},{0x470,15},{0x448,14},{0x458,16},{0x468,15},{0x450,16}})
    if(frame==*reinterpret_cast<void **>(car+wheel.first))return wheel.second;
  }
  const char *n=rw.name(frame);if(!n)continue;
  if(std::strstr(n,"windscreen"))return 17;
 }
 return 0;
}
int Component(void *frame)
{
 const char *names[]={"bonnet","boot","door_lf","door_rf","door_lr","door_rr","bump_front","bump_rear"};const int ids[]={5,6,7,8,9,10,16,17};
 for(int depth=0;frame&&depth<16;depth++,frame=rw.parent(frame)){const char *name=rw.name(frame);if(name)for(int i=0;i<8;i++)if(std::strstr(name,names[i]))return ids[i];}return 0;
}
int RotorModule(void *frame)
{
 for(int depth=0;frame&&depth<16;depth++,frame=rw.parent(frame)){
  const char *n=rw.name(frame);if(!n)continue;
  if(!std::strcmp(n,"toprotor")||!std::strcmp(n,"bonnet_hi_ok")||!std::strcmp(n,"bonnet_hi_dam"))return airframemodel::MainRotor;
  if(!std::strcmp(n,"rearrotor")||!std::strcmp(n,"backrotor")||!std::strcmp(n,"boot_hi_ok")||!std::strcmp(n,"boot_hi_dam"))return airframemodel::TailRotor;
 }return -1;
}
float SurfaceRatio(uint8_t *car,const Point &point,CVector from,CVector to,float budget)
{
 if(!projectilemodel::Armored(game::EntityModel(car))||point.surfaceB==23)return 1;
 if(!std::isfinite(budget)||budget<=0)return 0;const auto d=Sub(to,from);const float len=std::sqrt(Dot(d,d));if(!std::isfinite(len)||len<1e-6f)return 0;
 using namespace projectilemodel;const auto m=Surface(game::EntityModel(car),point.surfaceB==7?Material::Glass:Material::Body);
 return std::max(0.f,budget-Cost(m,std::clamp(std::fabs(Dot(point.n,d)/len),0.f,1.f)))/budget;
}
float Budget(){return !gShot?0:gShot->deferred?gShot->deferredBudget:gShot->budget;}
struct MeshQuery {const Line &line;float closest;uint8_t *car=nullptr;Point point{};bool hit=false,valid=true;unsigned count=0,triangles=0;bool ignoreSee=false,glass=false;float glassClosest=closest;Point glassPoint{};bool rawMesh=false;const void *hitAtomic=nullptr,*hitFrame=nullptr;GlassFace face{};bool collect=false,pathValid=true;Pending pending{};};
void *TestAtomic(void *atomic,void *data)
{
 auto &q=*static_cast<MeshQuery *>(data);if(!q.valid)return nullptr;
 if(++q.count>512){q.valid=false;return nullptr;}
 if(!(rw.flags(atomic)&4))return atomic;
 void *frame=rw.frame(atomic);const char *name=frame?rw.name(frame):nullptr;
 // Native cars remove _lo and keep _vlo solely for far rendering. Testing
 // both would fill open windows with the low-detail shell.
 if(name&&(std::strstr(name,"_vlo")||std::strstr(name,"_lo")))return atomic;
 void *geometry=rw.geometry(atomic);if(!geometry){q.valid=false;return nullptr;}
 const int nv=rw.nv(geometry),nt=rw.nt(geometry);
 // RpGeometryForAllMaterials reads the count at +0x88 on this native ABI.
 const int nm=*reinterpret_cast<const int *>(static_cast<const uint8_t *>(geometry)+0x88);
 if(nv<=0||nv>65536||nt<0||nt>100000||nm<=0||nm>65536||q.triangles+unsigned(nt)>250000){q.valid=false;return nullptr;}
 q.triangles+=unsigned(nt);const auto *tri=rw.triangles(geometry);void *morph=rw.morph(geometry,0);const auto *v=morph?rw.vertices(morph):nullptr;Basis basis;
 if(!v||(nt&&!tri)||!CurrentBasis(frame,q.car,basis)){q.valid=false;return nullptr;}
 const CVector a=basis.Local(q.line.a),d=Sub(basis.Local(q.line.b),a);const uint8_t piece=q.rawMesh?0:Piece(frame,game::IsAircraft(q.car)?nullptr:q.car);
 for(int i=0;i<nt;i++){
  const Triangle &f=tri[i];if(f.v[0]>=nv||f.v[1]>=nv||f.v[2]>=nv||f.material>=nm){q.valid=false;return nullptr;}
  if(!Finite(v[f.v[0]])||!Finite(v[f.v[1]])||!Finite(v[f.v[2]])){q.valid=false;return nullptr;}
  void *mat=rw.material(geometry,f.material);const uint8_t *colour=mat?rw.colour(mat):nullptr;
  if(!colour){q.valid=false;return nullptr;}if(!colour[3])continue;
  const bool wheel=piece>=13&&piece<=16;
  const int rotor=game::IsAircraft(q.car)?RotorModule(frame):-1;
  const bool glass=!q.rawMesh&&!wheel&&rotor<0&&(piece==17||colour[3]<255);
  if(glass&&q.ignoreSee)continue;
  CVector normal;float t=q.collect?1:glass?q.glassClosest:q.closest;if(!TriangleHit(a,d,v[f.v[0]],v[f.v[1]],v[f.v[2]],t,normal))continue;
  normal=basis.Normal(normal);const float length=std::sqrt(Dot(normal,normal));if(!std::isfinite(length)||length<1e-7f){q.valid=false;return nullptr;}
  Point hit{};hit.p=Add(q.line.a,Mul(Sub(q.line.b,q.line.a),t));hit.n=Mul(normal,1/length);
  if(q.collect&&q.pathValid){
   const CVector ray=Sub(q.line.b,q.line.a);const float distance=std::sqrt(Dot(ray,ray));
   if(!std::isfinite(distance)||distance<1e-6f)q.pathValid=false;
   else{
    using projectilemodel::Material;const auto material=projectilemodel::Surface(game::EntityModel(q.car),wheel?Material::Rubber:glass?Material::Glass:game::IsAircraft(q.car)?Material::Skin:Material::Body);
    const float incidence=std::clamp(std::fabs(Dot(hit.n,ray)/distance),0.f,1.f);bool duplicate=false;
    for(size_t n=0;n<q.pending.count;n++)if(q.pending.layers[n].material==material&&std::fabs(q.pending.layers[n].fraction-t)*distance<=.001f){q.pending.layers[n].incidence=std::min(incidence,q.pending.layers[n].incidence);duplicate=true;break;}
    if(!duplicate){if(q.pending.count==projectilemodel::kLayers)q.pathValid=false;else {q.pending.atomics[q.pending.count]=atomic;q.pending.normals[q.pending.count]=hit.n;q.pending.layers[q.pending.count++]={t,incidence,material};}}
   }
  }
  if(t>=(glass?q.glassClosest:q.closest))continue;
  // Native body/glass/tyre surfaces are6/7/23. Ordinary vehicle glass permits
  // occupant penetration; unlike opaque metal it cannot bound the body test.
  hit.surfaceB=wheel?23:glass?7:6;hit.pieceB=piece;
  if(glass){q.glassClosest=t;q.glass=true;q.glassPoint=hit;q.face={atomic,i};if(SurfaceRatio(q.car,hit,q.line.a,q.line.b,Budget())==0){q.closest=t;q.hit=true;q.point=hit;}}
  else{q.closest=t;q.hit=true;q.point=hit;q.hitAtomic=atomic;q.hitFrame=frame;q.pending.component=Component(frame);q.pending.rotor=rotor;}
 }
 return atomic;
}
bool Hook_Narrow(const Line &line,const void *matrix,void *model,Point &point,float &closest,bool a,bool b)
{
 if(!gReady||!gShot||!gShot->active||!gQuery||!matrix||!Finite(line.a)||!Finite(line.b)||!std::isfinite(closest)||closest<=0)
  return gNarrow(line,matrix,model,point,closest,a,b);
 const uintptr_t address=reinterpret_cast<uintptr_t>(matrix);auto *car=address>=8?reinterpret_cast<uint8_t *>(address-8):nullptr;
 const int ref=Ref(car,gVehiclePool,0x6f0,game::api.PoolsGetVehicle);
 if(ref<0||(game::EntityFlags(car)&7)!=2||((*reinterpret_cast<int *>(car+0x348)!=0||game::IsAircraft(car))&&!airframedamage::Enabled(car))||!game::EntityClump(car)||rw.objectType(game::EntityClump(car))!=2)
  return gNarrow(line,matrix,model,point,closest,a,b);
 MeshQuery q{line,closest,car};q.ignoreSee=a;q.collect=!gShot->readOnly&&(carmechanics::Enabled()||carparts::Enabled()||airframedamage::Enabled(car)||projectilemodel::Armored(game::EntityModel(car)));
 if(q.collect){q.pending.actor=Capture(car,ref);q.pending.token=carmechanics::BeginBullet(car);q.pending.partToken=carparts::BeginBullet(car);q.pending.airToken=airframedamage::BeginBullet(car);}
 rw.atomics(game::EntityClump(car),TestAtomic,&q);
 if(!q.valid||!q.count||!q.triangles)return gNarrow(line,matrix,model,point,closest,a,b);
 if(airframedamage::Enabled(car)||q.pending.token.revision){
  float opening=std::min(q.closest,q.glassClosest);if(carglass::Opening(car,{line.a.x,line.a.y,line.a.z},{line.b.x,line.b.y,line.b.z},opening)){
   Basis basis;if(basis.Set(reinterpret_cast<const float *>(car+8))){const auto a=basis.Local(line.a),b=basis.Local(line.b);float fraction=std::min(q.closest,q.glassClosest);Vec3 normal;
    if(airframedamage::Enabled(car)?airframedamage::ComponentHit(car,{a.x,a.y,a.z},{b.x,b.y,b.z},opening+.00001f,fraction,normal):carmechanics::ComponentHit(car,{a.x,a.y,a.z},{b.x,b.y,b.z},opening+.00001f,fraction,normal)){
     q.hit=true;q.closest=fraction;
     // Internal interception replaces the exterior contact: a farther mesh
     // cannot lend its door or rotor identity to this component strike.
     q.pending.component=0;q.pending.rotor=-1;q.hitAtomic=q.hitFrame=nullptr;q.point={};q.point.p=Add(line.a,Mul(Sub(line.b,line.a),fraction));const auto n=basis.Normal({normal.x,normal.y,normal.z});const float len=std::sqrt(Dot(n,n));if(len>1e-7f){q.point.n=Mul(n,1/len);q.point.surfaceB=6;q.pending.opening=opening;}
    }
   }
  }
 }
 uint8_t *victim=nullptr;int victimRef=-1;
 if(gQuery->peds){
  for(size_t offset=0x218;offset<0x260;offset+=8){
   auto *ped=*reinterpret_cast<uint8_t **>(car+offset);const int pr=Ref(ped,gPedPool,0x8d0,game::api.PoolsGetPed);
   if(pr<0||ped==gShot->shooter||(game::api.ignoreEntity&&ped==*game::api.ignoreEntity)||(game::EntityFlags(ped)&7)!=3||!(ped[0x193]&4)||ped[0x4c0]!=1||*reinterpret_cast<uint8_t **>(ped+0x4b8)!=car||!std::isfinite(*reinterpret_cast<float *>(ped+0x44c))||*reinterpret_cast<float *>(ped+0x44c)<=0||!game::EntityClump(ped))continue;
   const int modelId=game::EntityModel(ped);if(modelId<0||modelId>=6500||!gModels[modelId])continue;
   void *hitModel=gAnimate(gModels[modelId],game::EntityClump(ped));Point p{};float t=q.closest;
   if(hitModel&&gNarrow(line,ped+8,hitModel,p,t,a,b)&&std::isfinite(t)&&t>=0&&t<q.closest&&Finite(p.p)&&Finite(p.n)){q.hit=true;q.closest=t;q.point=p;victim=ped;victimRef=pr;}
  }
 }
 // Keep a native glass car impact when no exposed occupant wins. This
 // preserves glass/world cover while avoiding permanent side-window shields.
 const bool crossedGlass=q.glass&&q.glassClosest<q.closest;
 if(!victim&&crossedGlass){q.hit=true;q.closest=q.glassClosest;q.point=q.glassPoint;}
 if(!q.hit)return false;
 closest=q.closest;point=q.point;gQuery->car=car;gQuery->carRef=ref;gQuery->ped=victim;gQuery->pedRef=victimRef;gQuery->point=point;
 gQuery->glass=victim&&crossedGlass;gQuery->glassPoint=q.glassPoint;gQuery->face=(crossedGlass||(!victim&&q.glass&&q.closest==q.glassClosest))?q.face:GlassFace{};
 if(!victim&&q.collect&&q.pathValid&&q.pending.actor.Current()&&(q.pending.count||q.pending.airToken.revision||q.pending.token.revision)){q.pending.point=point;q.pending.from=line.a;q.pending.to=line.b;gQuery->pending=q.pending;}
 return true;
}
bool Hook_World(const CVector &a,const CVector &b,Point &point,uint8_t *&entity,bool buildings,bool vehicles,bool peds,bool objects,bool dummies,bool see,bool some,bool shoot)
{
 if(!gReady||!gShot||!gShot->active)return gWorld(a,b,point,entity,buildings,vehicles,peds,objects,dummies,see,some,shoot);
 const bool readOnly=gShot->readOnly!=0;
 if(!readOnly&&gShot->deferred){
  gShot->deferredIndex=nativebullet::kCount;gShot->deferredType=-1;gShot->deferredBudget=0;gShot->deferredCreator=nullptr;
  if(gBullets&&game::api.timeStep&&std::isfinite(*game::api.timeStep))for(size_t i=0;i<nativebullet::kCount;i++){
   const auto r=nativebullet::Read(gBullets,i);if(r.active!=1||!BulletWeapon(r.type)||!r.damage||r.damage>32767)continue;
   const CVector start{r.position.x,r.position.y,r.position.z},end=Add(start,Mul({r.velocity.x,r.velocity.y,r.velocity.z},*game::api.timeStep*.5f));
   if(std::memcmp(&a,&start,sizeof a)||std::fabs(end.x-b.x)>.001f||std::fabs(end.y-b.y)>.001f||std::fabs(end.z-b.z)>.001f)continue;
   if(gShot->deferredIndex!=nativebullet::kCount){gShot->deferredIndex=nativebullet::kCount;break;}
   gShot->deferredIndex=i;gShot->deferredRecord=r;
  }
  if(gShot->deferredIndex<nativebullet::kCount){const auto &r=gShot->deferredRecord;gShot->deferredType=r.type;gShot->deferredBudget=r.damage;gShot->deferredCreator=r.creator;}
 }
 Query q(peds);if(!readOnly){gShot->last={};gShot->glass={};gShot->pending={};gShot->materialConsumed={};gShot->armoredPed=nullptr;gShot->occupantRatio=1;}const bool hit=gWorld(a,b,point,entity,buildings,vehicles,peds,objects,dummies,see,some,shoot);
 if(hit&&entity==q.car&&q.car&&Ref(q.car,gVehiclePool,0x6f0,game::api.PoolsGetVehicle)==q.carRef&&!std::memcmp(&point,&q.point,sizeof point)){
  if(q.ped&&Ref(q.ped,gPedPool,0x8d0,game::api.PoolsGetPed)==q.pedRef){
   if(q.glass&&!readOnly){gShot->glass={q.car,q.ped,q.carRef,q.pedRef,q.glassPoint,point,q.face};
    gShot->occupantCar=Capture(q.car,q.carRef);gShot->armoredPed=q.ped;gShot->armoredPedRef=q.pedRef;gShot->occupantRatio=SurfaceRatio(q.car,q.glassPoint,a,b,Budget());}
   entity=q.ped;
  }
  else if(!q.ped&&!readOnly){gShot->last={q.car,q.carRef,point,q.face,a,b};if(q.pending.actor.Current()){
   q.pending.buildings=buildings;q.pending.peds=peds;q.pending.objects=objects;q.pending.dummies=dummies;q.pending.see=see;q.pending.some=some;q.pending.shoot=shoot;gShot->pending=q.pending;
  }}
 }
 return hit;
}
struct ImpactScope;thread_local ImpactScope *gImpactScope;
struct ImpactScope {
 ImpactScope *previous=gImpactScope;Shot *owner=gShot;Pending pending;uint8_t *car=nullptr;
 ImpactScope(uint8_t *weapon,uint8_t *by,uint8_t *vehicle,CVector *from,CVector *to,Point *point){
  const bool material=owner&&from&&to&&point&&owner->pending.actor.car==vehicle&&owner->pending.actor.Current()&&!std::memcmp(point,&owner->pending.point,sizeof *point)&&
   (!std::memcmp(from,&owner->pending.from,sizeof *from)||(owner->muzzleKnown&&!std::memcmp(from,&owner->muzzle,sizeof *from)))&&!std::memcmp(to,&owner->pending.to,sizeof *to);
  const bool surface=owner&&from&&to&&point&&owner->last.car==vehicle&&Ref(vehicle,gVehiclePool,0x6f0,game::api.PoolsGetVehicle)==owner->last.ref&&!std::memcmp(point,&owner->last.point,sizeof *point)&&
   (!std::memcmp(from,&owner->last.from,sizeof *from)||(owner->muzzleKnown&&!std::memcmp(from,&owner->muzzle,sizeof *from)))&&!std::memcmp(to,&owner->last.to,sizeof *to);
  if(gReady&&owner&&owner->active&&!owner->readOnly&&weapon&&game::WeaponType(weapon)==owner->type&&(by==owner->shooter||by==owner->damageCreator)&&from&&to&&point&&
     (material||surface)){
   if(owner->pending.actor.car==vehicle&&owner->pending.actor.Current())pending=owner->pending;
   car=vehicle;owner->pending={};
  }
  gImpactScope=this;
 }
 ~ImpactScope(){if(car&&owner&&gShot==owner)owner->pending={};gImpactScope=previous;}
};
Pending *PendingHit(){if(!gShot)return nullptr;return gImpactScope&&gImpactScope->owner==gShot?&gImpactScope->pending:&gShot->pending;}
bool DeferredCurrent(){return gShot&&gShot->deferredIndex<nativebullet::kCount&&gBullets&&
 !std::memcmp(gBullets+gShot->deferredIndex*nativebullet::kStride,&gShot->deferredRecord,sizeof(nativebullet::Record));}
bool DamageMetadata(int weapon,uint8_t *by)
{
 if(!gShot)return false;
 if(gShot->deferred)return DeferredCurrent()&&weapon==gShot->deferredType&&by==gShot->deferredCreator;
 return weapon==gShot->damageType&&by==gShot->damageCreator;
}
bool ZeroPosition(uint8_t *car,int weapon,uint8_t *by)
{
 return gShot&&(gShot->zeroDamagePoint||(gImpactScope&&gImpactScope->owner==gShot&&gImpactScope->car==car)||
  (DeferredCurrent()&&weapon==gShot->deferredType&&by==gShot->deferredCreator));
}
struct Delivery {Pending hit;carmechanics::BulletToken token;projectilemodel::Ray ray;bool ready=false;};
bool OccupantCover(const Snapshot &actor,const Line &line,uint8_t *ignored,Point &point,float &closest)
{
 bool hit=false;
 // Native world LOS skips seated automobile occupants without normal
 // collision enabled. Ignoring the struck car also skips Hook_Narrow's
 // precision occupant pass, so intersect its occupants explicitly here.
 for(size_t offset=0x218;offset<0x260&&actor.Current();offset+=8){
  auto *ped=*reinterpret_cast<uint8_t **>(actor.car+offset);const int ref=Ref(ped,gPedPool,0x8d0,game::api.PoolsGetPed);
  if(ref<0||ped==ignored||(gShot&&ped==gShot->shooter)||(game::EntityFlags(ped)&7)!=3||!(ped[0x193]&4)||ped[0x4c0]!=1||
     *reinterpret_cast<uint8_t **>(ped+0x4b8)!=actor.car||!std::isfinite(*reinterpret_cast<float *>(ped+0x44c))||*reinterpret_cast<float *>(ped+0x44c)<=0)continue;
  const int model=game::EntityModel(ped);auto *clump=game::EntityClump(ped);if(!clump||model<0||model>=6500||!gModels[model])continue;
  const auto info=gModels[model];std::array<float,16> matrix;std::memcpy(matrix.data(),ped+8,sizeof matrix);
  void *col=gAnimate(info,clump);Point candidate{};float fraction=closest;
  if(col&&gNarrow(line,ped+8,col,candidate,fraction,false,false)&&std::isfinite(fraction)&&fraction>=0&&fraction<closest&&Finite(candidate.p)&&Finite(candidate.n)&&
     Ref(ped,gPedPool,0x8d0,game::api.PoolsGetPed)==ref&&game::EntityModel(ped)==model&&game::EntityClump(ped)==clump&&gModels[model]==info&&
     !std::memcmp(matrix.data(),ped+8,sizeof matrix)&&*reinterpret_cast<uint8_t **>(ped+0x4b8)==actor.car){closest=fraction;point=candidate;hit=true;}
 }
 return hit;
}
void Prepare(const Pending &hit,float budget,Delivery &delivery)
{
 if((!carmechanics::Enabled()&&!hit.airToken.revision)||!std::isfinite(budget)||budget<=0||!hit.actor.Current()||(!hit.count&&!hit.airToken.revision&&!hit.token.revision)||!game::api.ignoreEntity)return;
 // Lease the actor when its mesh was sampled: a repair/restart before this
 // later native impact must retire the pending round, not issue a new lease.
 const auto token=hit.token;if((!token.revision&&!hit.airToken.revision)||!hit.actor.Current())return;
 Basis basis;if(!basis.Set(hit.actor.matrix.data()))return;
 const CVector d=Sub(hit.to,hit.from);const float length=std::sqrt(Dot(d,d));if(!std::isfinite(length)||length<1e-6f)return;
 const float entry=hit.opening>=0?hit.opening:Dot(Sub(hit.point.p,hit.from),d)/(length*length);if(entry<0||entry>=1)return;
 const float reach=std::min(24.f,(1-entry)*length);if(reach<=.002f)return;
 const CVector direction=Mul(d,1/length),start=Add(hit.point.p,Mul(direction,.001f)),end=Add(hit.point.p,Mul(direction,reach));
 Point cover{};uint8_t *entity=nullptr;bool blocked=false;
 {
  // This query is read-only and owns no pending shot. Preserve a nested
  // owner's replacement of the ignore binding rather than overwriting it.
  struct Ignore {uint8_t **slot,*previous,*car;~Ignore(){if(*slot==car)*slot=previous;}} ignore{game::api.ignoreEntity,*game::api.ignoreEntity,hit.actor.car};
  struct ReadOnly {Shot *owner=gShot;unsigned previous=owner?owner->readOnly:0;ReadOnly(){if(owner)++owner->readOnly;}~ReadOnly(){if(gShot==owner&&owner)owner->readOnly=previous;}} readOnly;
  *ignore.slot=ignore.car;Query query(hit.peds);
  blocked=gWorld(start,end,cover,entity,hit.buildings,true,hit.peds,hit.objects,hit.dummies,hit.see,hit.some,hit.shoot);
  if(hit.peds){
   const CVector coverRay=Sub(end,start);float closest=1;
   if(blocked){if(!Finite(cover.p))return;closest=std::clamp(Dot(Sub(cover.p,start),coverRay)/Dot(coverRay,coverRay),0.f,1.f);}
   if(OccupantCover(hit.actor,{start,0,end,0},ignore.previous,cover,closest))blocked=true;
  }
 }
 if(!hit.actor.Current())return;
 float limit=1;if(blocked){if(!Finite(cover.p))return;limit=std::clamp(Dot(Sub(cover.p,hit.from),d)/(length*length),entry,1.f);}
 delivery.hit=hit;delivery.token=token;const auto a=basis.Local(hit.from),b=basis.Local(hit.to);
 delivery.ray={{a.x,a.y,a.z},{b.x,b.y,b.z},entry,limit,budget,delivery.hit.layers.data(),delivery.hit.count};delivery.ready=true;
}
void Hook_Impact(uint8_t *weapon,uint8_t *by,uint8_t *car,CVector *from,CVector *to,Point *point,Vec2 screen)
{
 ImpactScope scope(weapon,by,car,from,to,point);gImpact(weapon,by,car,from,to,point,screen);
}
bool Hook_Fire(uint8_t *w,uint8_t *s,CVector *p){Shot shot(w,s,false,false,p);return gFire(w,s,p);}
bool Hook_Instant(uint8_t *w,uint8_t *s,CVector *p){Shot shot(w,s,false,false,p);return gInstant(w,s,p);}
bool Hook_FireCar(uint8_t *w,uint8_t *car,bool a,bool b){Shot shot(w,Ref(car,gVehiclePool,0x6f0,game::api.PoolsGetVehicle)>=0?*reinterpret_cast<uint8_t **>(car+0x218):nullptr,false,true);
 // Native FromCar uses WEAPONTYPE_DRIVEBY(42) and FindPlayerPed(), even
 // though weapon information and its damage budget come from this CWeapon.
 shot.damageType=42;shot.damageCreator=game::api.FindPlayerPed?game::api.FindPlayerPed():nullptr;return gFireCar(w,car,a,b);}
void Hook_Update(){Shot shot(nullptr,nullptr,true);gUpdate();}
void Hook_Round(CVector *from,CVector *to,int damage){Shot shot(nullptr,nullptr,true,true);shot.deferred=false;shot.damageType=23;shot.budget=float(damage);gRound(from,to,damage);}
// Native drive-bys and FireOneInstantHitRound inflict damage inline and never
// call CheckForShootingVehicleOccupant. These existing damage hooks supply
// their real impact boundary too; no LOS query by itself can break glass.
void Hook_VehicleDamage(uint8_t *car,uint8_t *by,int weapon,float damage,CVector at)
{
 Delivery delivery;Pending receipt;bool certified=false;Pending *pending=PendingHit();
 const bool zero=at.x==0&&at.y==0&&at.z==0;
 if(gShot&&gShot->active&&!gShot->readOnly&&gShot->materialConsumed.car==car&&gShot->materialConsumed.Current()&&DamageMetadata(weapon,by)&&((zero&&ZeroPosition(car,weapon,by))||!std::memcmp(&at,&gShot->consumedPoint,sizeof at)))return;
 if(gReady&&gShot&&gShot->active&&!gShot->readOnly&&DamageMetadata(weapon,by)&&pending&&pending->actor.car==car&&pending->actor.Current()&&
    ((!zero&&!std::memcmp(&at,&pending->point.p,sizeof at))||(zero&&ZeroPosition(car,weapon,by)))){
  receipt=*pending;certified=true;*pending={};Prepare(receipt,damage,delivery);
 }
 if(certified&&delivery.ready&&(receipt.airToken.revision||receipt.token.revision)){
  gShot->materialConsumed=receipt.actor;gShot->consumedPoint=receipt.point.p;
  // Native5217 writes last weapon/source only after its proof, mission and
  // aggressor vetoes (27d9e4), even for zero hull damage. Use that acceptance
  // receipt instead of depleting a global health pool to discover permission.
  const uint8_t oldWeapon=car[0x2dc];car[0x2dc]=0xff;
  gVehicleDamage(car,by,weapon,0,at);
  if(!receipt.actor.Current())return;
  const bool accepted=car[0x2dc]==uint8_t(weapon)&&*reinterpret_cast<uint8_t **>(car+0x2e0)==by;
  if(car[0x2dc]==0xff)car[0x2dc]=oldWeapon;
  if(!accepted)return;
  std::array<float,projectilemodel::kLayers> passed{};
  const bool applied=receipt.airToken.revision?airframedamage::Projectile(car,receipt.airToken,delivery.ray,&passed,receipt.rotor):carmechanics::Projectile(car,delivery.token,delivery.ray,&passed);
  if(applied&&receipt.actor.Current()){
   const auto surface=gShot->last;
   if(surface.car==car&&surface.face.atomic&&surface.point.surfaceB==7&&SurfaceRatio(car,surface.point,receipt.from,receipt.to,damage)>0){
    gShot->last={};const auto &p=surface.point;carglass::Bullet(car,surface.face.atomic,surface.face.triangle,{p.p.x,p.p.y,p.p.z},{p.n.x,p.n.y,p.n.z});
   }
   if(receipt.component>0&&receipt.point.surfaceB==6&&SurfaceRatio(car,receipt.point,receipt.from,receipt.to,damage)>0){
    const auto mat=receipt.airToken.revision?projectilemodel::Material::Skin:projectilemodel::Material::Body;
    carparts::Projectile(car,receipt.partToken,receipt.component,std::min(damage,projectilemodel::Cost(mat,1)));
   }
   const auto delta=Sub(receipt.to,receipt.from);
   for(size_t i=0;i<receipt.count&&receipt.actor.Current();i++)if(passed[i]>0&&receipt.layers[i].fraction<=delivery.ray.limit&&receipt.layers[i].material!=projectilemodel::Material::Glass&&receipt.layers[i].material!=projectilemodel::Material::ArmoredGlass){
    const auto p=Add(receipt.from,Mul(delta,receipt.layers[i].fraction)),n=receipt.normals[i];
    if(carglass::Perforate(car,receipt.atomics[i],{p.x,p.y,p.z},{n.x,n.y,n.z},damage)&&receipt.airToken.revision){
     const float radius=.015f+.0003f*std::min(damage,200.f);airframedamage::Perforated(car,receipt.airToken,3.14159265f*radius*radius);
    }
   }
  }return;
 }
 const Certified nearest=gShot?gShot->last:Certified{};
 const bool certifiedSurface=gReady&&gShot&&gShot->active&&!gShot->readOnly&&DamageMetadata(weapon,by)&&nearest.car==car&&Ref(car,gVehiclePool,0x6f0,game::api.PoolsGetVehicle)==nearest.ref&&((zero&&ZeroPosition(car,weapon,by))||!std::memcmp(&at,&nearest.point.p,sizeof at));
 const float transmission=certified?SurfaceRatio(car,receipt.point,receipt.from,receipt.to,damage):certifiedSurface?SurfaceRatio(car,nearest.point,nearest.from,nearest.to,damage):1;
 if(transmission<=0)return;
 const Certified hit=gShot?gShot->last:Certified{};
 // These two exact native fire scopes pass a zero damage position despite
 // retaining the real CColPoint from the immediately preceding world query.
 const bool nativeZero=zero&&ZeroPosition(car,weapon,by)&&DamageMetadata(weapon,by);
 if(gReady&&gShot&&gShot->active&&!gShot->readOnly&&hit.car==car&&hit.face.atomic&&hit.point.surfaceB==7&&
    Ref(car,gVehiclePool,0x6f0,game::api.PoolsGetVehicle)==hit.ref&&(nativeZero||!std::memcmp(&at,&hit.point.p,sizeof at))){
  gShot->last={};const Point &p=hit.point;
  carglass::Bullet(car,hit.face.atomic,hit.face.triangle,{p.p.x,p.p.y,p.p.z},{p.n.x,p.n.y,p.n.z});
 }
 const float before=certified?*reinterpret_cast<const float *>(car+0x2a0):0;
 gVehicleDamage(car,by,weapon,damage*transmission,at);
 if(certified&&receipt.actor.Current()&&std::isfinite(damage)&&damage>0&&receipt.point.surfaceB==6&&std::isfinite(before)&&before>0&&*reinterpret_cast<const float *>(car+0x2a0)<before)carparts::Projectile(car,receipt.partToken,receipt.component,damage*transmission);
 if(delivery.ready&&delivery.hit.actor.Current()&&std::isfinite(before)&&before>0&&
    std::isfinite(*reinterpret_cast<const float *>(car+0x2a0))&&*reinterpret_cast<const float *>(car+0x2a0)<before)
  carmechanics::Projectile(car,delivery.token,delivery.ray);
}
void Hook_Burst(uint8_t *car,uint8_t piece,bool random)
{
 const int mapping[]={0,2,1,3};const int wheel=piece>=13&&piece<=16?mapping[piece-13]:piece;
 Delivery delivery;Pending *pending=PendingHit();
 const bool impact=gShot&&(gShot->zeroDamagePoint||DeferredCurrent()||(gImpactScope&&gImpactScope->owner==gShot&&gImpactScope->car==car));
 if(gReady&&gShot&&gShot->active&&!gShot->readOnly&&impact&&pending&&pending->actor.car==car&&pending->actor.Current()&&gWheelStatus&&wheel>=0&&wheel<4&&
    pending->point.pieceB>=13&&pending->point.pieceB<=16&&mapping[pending->point.pieceB-13]==wheel){
  const float budget=gShot->budget>0?gShot->budget:DeferredCurrent()?gShot->deferredBudget:0;
  const Pending saved=*pending;*pending={};Prepare(saved,budget,delivery);
 }
 const int before=delivery.ready?gWheelStatus(car+0x34c,wheel):-1;
 gBurst(car,piece,random);
 if(delivery.ready&&delivery.hit.actor.Current()&&before==0&&gWheelStatus(car+0x34c,wheel)!=0)
  carmechanics::Projectile(car,delivery.token,delivery.ray);
}
bool Hook_PedDamage(uint8_t *ped,uint8_t *by,int weapon,float damage,int piece,uint8_t direction)
{
 const GlassContact hit=gShot?gShot->glass:GlassContact{};
 if(gReady&&gShot&&gShot->active&&!gShot->readOnly&&hit.ped==ped&&hit.face.atomic&&
    Ref(hit.car,gVehiclePool,0x6f0,game::api.PoolsGetVehicle)==hit.carRef&&
    Ref(ped,gPedPool,0x8d0,game::api.PoolsGetPed)==hit.pedRef){
  gShot->glass={};const Point &p=hit.glass;
  carglass::Bullet(hit.car,hit.face.atomic,hit.face.triangle,{p.p.x,p.p.y,p.p.z},{p.n.x,p.n.y,p.n.z});
 }
 if(gShot&&gShot->active&&!gShot->readOnly&&DamageMetadata(weapon,by)&&gShot->armoredPed==ped&&gShot->occupantCar.Current()&&Ref(ped,gPedPool,0x8d0,game::api.PoolsGetPed)==gShot->armoredPedRef)damage*=gShot->occupantRatio;
 return gPedDamage(ped,by,weapon,damage,piece,direction);
}
void Hook_Occupant(uint8_t **entity,Point *point,int weapon,const CVector &a,const CVector &b)
{
 // LOS is also used by read-only shot obstruction/prepass queries. Only
 // this real native impact stage may consume the pending windshield effect.
 const GlassContact glass=gShot?gShot->glass:GlassContact{};
 if(gReady&&gShot&&gShot->active&&!gShot->readOnly&&entity&&point&&glass.car&&*entity==glass.ped&&
    Ref(glass.car,gVehiclePool,0x6f0,game::api.PoolsGetVehicle)==glass.carRef&&
    Ref(glass.ped,gPedPool,0x8d0,game::api.PoolsGetPed)==glass.pedRef&&
    !std::memcmp(point,&glass.body,sizeof *point)){
  gShot->glass={};uint8_t *car=glass.car;Point hit=glass.glass;
  carglass::Bullet(car,glass.face.atomic,glass.face.triangle,{hit.p.x,hit.p.y,hit.p.z},{hit.n.x,hit.n.y,hit.n.z});
  // The predecessor owns panel progression/visibility, shatter and audio.
  // Its head-only retarget operates on temporaries, preserving the body hit.
  gOccupant(&car,&hit,weapon,a,b);return;
 }
 const Certified saved=gShot?gShot->last:Certified{};
 const bool exact=gReady&&gShot&&gShot->active&&!gShot->readOnly&&entity&&point&&*entity==saved.car&&saved.car&&Ref(saved.car,gVehiclePool,0x6f0,game::api.PoolsGetVehicle)==saved.ref&&!std::memcmp(point,&saved.point,sizeof *point);
 if(exact&&SurfaceRatio(saved.car,saved.point,a,b,Budget())==0)return;
 if(exact&&saved.face.atomic&&saved.point.surfaceB==7){
  gShot->last={};const auto &p=saved.point;
  carglass::Bullet(saved.car,saved.face.atomic,saved.face.triangle,{p.p.x,p.p.y,p.p.z},{p.n.x,p.n.y,p.n.z});
 }
 // Keep native windscreen damage/audio. Its head-only retarget must not move
 // a certified nearest metal/glass intersection through the car's body.
 gOccupant(entity,point,weapon,a,b);if(exact){*entity=saved.car;*point=saved.point;}
}
} // namespace
bool TraceVehicleMesh(uint8_t *vehicle,Vec3 from,Vec3 to,MeshHit &hit)
{
 hit={};const CVector a{from.x,from.y,from.z},b{to.x,to.y,to.z};
 if(!Finite(a)||!Finite(b)||!rw.atomics||!rw.flags||!rw.objectType||!rw.frame||!rw.geometry||!rw.matrix||!rw.root||!rw.name||!rw.parent||!rw.nv||!rw.nt||!rw.triangles||!rw.morph||!rw.vertices||!rw.material||!rw.colour)return false;
 const int ref=Ref(vehicle,gVehiclePool,0x6f0,game::api.PoolsGetVehicle);
 if(ref<0||(game::EntityFlags(vehicle)&7)!=2)return false;
 uint8_t *clump=game::EntityClump(vehicle);if(!clump||rw.objectType(clump)!=2)return false;
 const int16_t model=game::EntityModel(vehicle);
 float before[16];std::memcpy(before,vehicle+8,sizeof before);
 const Line line{a,0,b,0};MeshQuery q{line,1,vehicle};q.rawMesh=true;
 // Pure mesh mode includes glass and never classifies automobile wheel nodes;
 // CPlane and other vehicle subclasses do not share those member offsets.
 rw.atomics(clump,TestAtomic,&q);
 if(!q.valid||!q.hit||Ref(vehicle,gVehiclePool,0x6f0,game::api.PoolsGetVehicle)!=ref||
    game::EntityClump(vehicle)!=clump||rw.objectType(clump)!=2||game::EntityModel(vehicle)!=model||
    (game::EntityFlags(vehicle)&7)!=2||std::memcmp(before,vehicle+8,sizeof before))return false;
 hit.fraction=q.closest;hit.position={q.point.p.x,q.point.p.y,q.point.p.z};
 hit.normal={q.point.n.x,q.point.n.y,q.point.n.z};hit.atomic=q.hitAtomic;hit.frame=q.hitFrame;
 return true;
}
bool Install()
{
 if(gReady)return true;
#define BIND(v,s) v=reinterpret_cast<decltype(v)>(dlsym(RTLD_MAIN_ONLY,s));if(!v)return false
 BIND(gVehiclePool,"_ZN6CPools15ms_pVehiclePoolE");
 gBullets=static_cast<const uint8_t *>(dlsym(RTLD_MAIN_ONLY,"gaBulletInfo"));
 BIND(gWheelStatus,"_ZN14CDamageManager14GetWheelStatusEi");
 BIND(gPedPool,"_ZN6CPools11ms_pPedPoolE");
 BIND(gModels,"_ZN10CModelInfo16ms_modelInfoPtrsE");
 BIND(gAnimate,"_ZN13CPedModelInfo25AnimatePedColModelSkinnedEPN2rw5ClumpE");
 BIND(rw.atomics,"_Z20RpClumpForAllAtomicsPN2rw5ClumpEPFPNS_6AtomicES3_PvES4_");
 BIND(rw.flags,"_Z16RpAtomicGetFlagsPKN2rw6AtomicE");
 BIND(rw.objectType,"_Z15RwObjectGetTypePKN2rw6ObjectE");
 BIND(rw.frame,"_Z16RpAtomicGetFramePKN2rw6AtomicE");
 BIND(rw.geometry,"_Z19RpAtomicGetGeometryPKN2rw6AtomicE");
 BIND(rw.matrix,"_Z16RwFrameGetMatrixPN2rw5FrameE");
 BIND(rw.root,"_Z15RpClumpGetFramePKN2rw5ClumpE");
 BIND(rw.name,"_Z16GetFrameNodeNamePN2rw5FrameE");
 BIND(rw.parent,"_Z16RwFrameGetParentPKN2rw5FrameE");
 BIND(rw.nv,"_Z24RpGeometryGetNumVerticesPKN2rw8GeometryE");
 BIND(rw.nt,"_Z25RpGeometryGetNumTrianglesPKN2rw8GeometryE");
 BIND(rw.triangles,"_Z22RpGeometryGetTrianglesPKN2rw8GeometryE");
 BIND(rw.morph,"_Z24RpGeometryGetMorphTargetPKN2rw8GeometryEi");
 BIND(rw.vertices,"_Z24RpMorphTargetGetVerticesPKN2rw11MorphTargetE");
#undef BIND
 // RpGeometryGetMaterial takes a const Geometry pointer (same machine ABI).
 rw.material=reinterpret_cast<decltype(rw.material)>(dlsym(RTLD_MAIN_ONLY,"_Z21RpGeometryGetMaterialPKN2rw8GeometryEi"));
 rw.colour=reinterpret_cast<decltype(rw.colour)>(dlsym(RTLD_MAIN_ONLY,"_Z18RpMaterialGetColorPKN2rw8MaterialE"));
 if(!rw.material||!rw.colour||!game::api.PoolsGetPed||!game::api.PoolsGetVehicle||!game::api.GetWeaponInfo)return false;
#define CHAIN(v,s,f) if(!v)v=reinterpret_cast<decltype(v)>(hooks::Chain(s,reinterpret_cast<void *>(f)))
 CHAIN(gFire,"_ZN7CWeapon4FireEP7CEntityP7CVector",Hook_Fire);
 CHAIN(gInstant,"_ZN7CWeapon14FireInstantHitEP7CEntityP7CVector",Hook_Instant);
 CHAIN(gFireCar,"_ZN7CWeapon21FireInstantHitFromCarEP8CVehiclebb",Hook_FireCar);
 CHAIN(gUpdate,"_ZN11CBulletInfo6UpdateEv",Hook_Update);
 CHAIN(gRound,"_Z22FireOneInstantHitRoundP7CVectorS0_i",Hook_Round);
 CHAIN(gWorld,"_ZN6CWorld18ProcessLineOfSightERK7CVectorS2_R9CColPointRP7CEntitybbbbbbbb",Hook_World);
 CHAIN(gNarrow,"_ZN10CCollision18ProcessLineOfSightERK8CColLineRK7CMatrixR9CColModelR9CColPointRfbb",Hook_Narrow);
 CHAIN(gOccupant,"_ZN7CWeapon31CheckForShootingVehicleOccupantEPP7CEntityP9CColPoint11eWeaponTypeRK7CVectorS8_",Hook_Occupant);
 CHAIN(gVehicleDamage,"_ZN8CVehicle13InflictDamageEP7CEntity11eWeaponTypef7CVector",Hook_VehicleDamage);
 CHAIN(gImpact,"_ZN7CWeapon14DoBulletImpactEP7CEntityS1_P7CVectorS3_P9CColPoint9CVector2D",Hook_Impact);
 CHAIN(gBurst,"_ZN11CAutomobile9BurstTyreEhb",Hook_Burst);
 CHAIN(gPedDamage,"_ZN4CPed13InflictDamageEP7CEntity11eWeaponTypef14ePedPieceTypesh",Hook_PedDamage);
#undef CHAIN
 gReady=gFire&&gInstant&&gFireCar&&gUpdate&&gRound&&gWorld&&gNarrow&&gOccupant&&gVehicleDamage&&gPedDamage&&gImpact&&gBurst;return gReady;
}
} // namespace shotcollision

#include "carglass.h"
#include "glassmodel.h"
#include "game.h"
#include "config.h"
#include "hooks.h"
#include "log.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <new>
#include <vector>

namespace carglass {namespace {
using glassmodel::Pane;
struct Triangle {uint16_t v[3],material;};
// macVC 1.0.5217, shared with shotcollision's native mesh validation.
constexpr size_t kStride=0x6f0,kDamage=0x34c,kMaxCars=128,kMaxParts=64,kBudget=16*1024*1024;
struct RW {
 void *(*atomics)(void *,void *(*)(void *,void *),void *);
 void *(*root)(const void *),*(*frame)(const void *),*(*geometry)(const void *),*(*parent)(const void *);
 unsigned (*atomicFlags)(const void *),(*flags)(const void *);
 const char *(*name)(void *);float *(*matrix)(void *);
 int (*nv)(const void *),(*nt)(const void *),(*uvCount)(const void *);
 void *(*morph)(const void *,int),*(*vertices)(const void *),*(*normals)(const void *);
 void *(*colours)(const void *),*(*uv)(const void *,int),*(*triangles)(const void *),*(*material)(const void *,int);
 const uint8_t *(*colour)(const void *);
 void *(*texture)(const void *);const char *(*textureName)(void *);
 void *(*createGeometry)(int,int,unsigned);void (*destroyGeometry)(void *);void *(*unlock)(void *);
 void *(*triangleMaterial)(void *,Triangle *,void *),*(*sphere)(void *),*(*setSphere)(void *,const void *);
 void *(*createAtomic)();int (*destroyAtomic)(void *);void *(*setGeometry)(void *,void *,unsigned);
} rw;
void **gPool;
void (*gPanes)(unsigned,CVector,CVector,CVector,CVector,CVector,float,bool,bool,int,bool);
void (*gSound)(uint8_t,const CVector &);
int (*gDoorStatus)(void *,int),(*gPanelStatus)(void *,int);
void (*gDoor)(uint8_t *,int,int,bool),(*gBumper)(uint8_t *,int,int,bool),(*gFix)(uint8_t *),(*gDelete)(uint8_t *);
bool gReady;
struct Part {void *atomic,*holder,*copy;size_t bytes;};
struct Car {uint8_t *car;int ref,model;void *clump;uint8_t broken=0;std::vector<Part> parts;};
std::vector<Car> gCars;
size_t gBytes;
bool Finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
Vec3 Sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
Vec3 Transform(const float *m,Vec3 p)
{
 return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
// Allocation bounds first: native GetRef assumes a valid pool address.
int Ref(uint8_t *v)
{
 if(!v||!gPool||!*gPool||!game::api.PoolsGetVehicle)return -1;
 auto p=static_cast<const uint8_t *>(*gPool);auto entries=*reinterpret_cast<uint8_t *const *>(p);
 auto flags=*reinterpret_cast<uint8_t *const *>(p+8);int n=*reinterpret_cast<const int *>(p+16);
 const uintptr_t a=reinterpret_cast<uintptr_t>(v),base=reinterpret_cast<uintptr_t>(entries);
 if(!entries||!flags||n<=0||n>100000||a<base||(a-base)%kStride||(a-base)/kStride>=size_t(n))return -1;
 const size_t i=(a-base)/kStride;if(flags[i]&0x80)return -1;
 int r=int(i*256)|flags[i];return game::api.PoolsGetVehicle(r)==v?r:-1;
}
bool Eligible(uint8_t *v)
{
 return Ref(v)>=0&&(game::EntityFlags(v)&7)==2&&*reinterpret_cast<int *>(v+0x348)==0&&
        !game::IsAircraft(v)&&game::EntityModel(v)!=171&&game::EntityClump(v)&&(v[0x297]&0x40);
}
bool Same(const Car &c){return Ref(c.car)==c.ref&&game::EntityModel(c.car)==c.model&&game::EntityClump(c.car)==c.clump;}
void Release(Car &c,bool restore)
{
 // Holders have no frame or clump. They are safe to release after the car's
 // destruction too; never dereference a stale car/atomic during collection.
 for(auto &p:c.parts){
  if(restore&&rw.geometry(p.atomic)==p.copy)rw.setGeometry(p.atomic,rw.geometry(p.holder),0);
  rw.destroyAtomic(p.holder);gBytes-=p.bytes;
 }
 c.parts.clear();
}
void Collect()
{
 for(auto i=gCars.begin();i!=gCars.end();)if(!Same(*i)){Release(*i,false);i=gCars.erase(i);}else ++i;
}
void Forget(uint8_t *v,bool restore)
{
 for(auto i=gCars.begin();i!=gCars.end();)if(i->car==v){Release(*i,restore&&Same(*i));i=gCars.erase(i);}else ++i;
}
struct Objects {std::array<void *,256> a{};size_t n=0;bool valid=true;};
void *Gather(void *a,void *data)
{
 auto &o=*static_cast<Objects *>(data);if(o.n==o.a.size()){o.valid=false;return nullptr;}o.a[o.n++]=a;return a;
}
// Use current child LOCAL matrices, as bullet tracing does. Cached world
// matrices can still belong to the last physics frame or door angle.
bool InCar(void *frame,void *root,Vec3 p,Vec3 &out)
{
 for(unsigned depth=0;frame&&frame!=root&&depth<32;depth++,frame=rw.parent(frame)){
  const float *m=rw.matrix(frame);if(!m)return false;p=Transform(m,p);if(!Finite(p))return false;
 }
 if(frame!=root)return false;out=p;return true;
}
struct Mesh {
 void *geometry=nullptr,*frame=nullptr,*root=nullptr;const char *name=nullptr;
 int nv=0,nt=0,nm=0;const Vec3 *v=nullptr;const Triangle *t=nullptr;
 bool Read(void *g,void *a,void *r){
  geometry=g;root=r;frame=rw.frame(a);name=frame?rw.name(frame):nullptr;
  if(!g||!name||!r)return false;nv=rw.nv(g);nt=rw.nt(g);nm=*reinterpret_cast<const int *>(static_cast<uint8_t *>(g)+0x88);
  const int morphs=*reinterpret_cast<const int *>(static_cast<uint8_t *>(g)+0x20);
  if(nv<=0||nv>8192||nt<=0||nt>16384||nm<=0||nm>256||morphs!=1||(rw.flags(g)&0x01000000u))return false;
  void *m=rw.morph(g,0);v=m?static_cast<const Vec3 *>(rw.vertices(m)):nullptr;t=static_cast<const Triangle *>(rw.triangles(g));
  if(!v||!t)return false;
  for(int i=0;i<nt;i++)if(t[i].v[0]>=nv||t[i].v[1]>=nv||t[i].v[2]>=nv||t[i].material>=nm)return false;
  for(int i=0;i<nv;i++)if(!Finite(v[i]))return false;
  return true;
 }
 Pane Face(int i,Vec3 *points=nullptr)const{
  const auto &f=t[i];void *m=rw.material(geometry,f.material);const uint8_t *colour=m?rw.colour(m):nullptr;
  if(!colour||!colour[3]||colour[3]==255)return Pane::None;
  Vec3 p[3];for(int j=0;j<3;j++)if(!InCar(frame,root,v[f.v[j]],p[j]))return Pane::None;
  Vec3 centre{(p[0].x+p[1].x+p[2].x)/3,(p[0].y+p[1].y+p[2].y)/3,(p[0].z+p[1].z+p[2].z)/3};
  if(points)std::copy(p,p+3,points);
  void *texture=rw.texture(m);const char *textureName=texture?rw.textureName(texture):nullptr;
  return glassmodel::Classify(name,centre,Cross(Sub(p[1],p[0]),Sub(p[2],p[0])),colour[3],textureName);
 }
};
struct Pending {
 void *atomic=nullptr,*copy=nullptr,*holder=nullptr;Part *was=nullptr;size_t bytes=0;
 ~Pending(){if(copy)rw.destroyGeometry(copy);if(holder)rw.destroyAtomic(holder);}
 Pending()=default;Pending(const Pending &)=delete;Pending &operator=(const Pending &)=delete;
 Pending(Pending &&p)noexcept:atomic(p.atomic),copy(p.copy),holder(p.holder),was(p.was),bytes(p.bytes){p.copy=p.holder=nullptr;}
};
void *Copy(const Mesh &m,uint8_t mask,size_t &bytes,size_t available)
{
 const unsigned flags=rw.flags(m.geometry);const int uvs=rw.uvCount(m.geometry);
 if(uvs<0||uvs>8)return nullptr;
 bytes=size_t(m.nv)*(12+((flags&16)?12:0)+((flags&8)?4:0)+uvs*8)+size_t(m.nt)*8+4096;
 if(bytes>available)return nullptr;
 void *g=rw.createGeometry(m.nv,m.nt,(flags&0xffffu)|(unsigned(uvs)<<16));if(!g)return nullptr;
 void *morph=rw.morph(g,0),*sourceMorph=rw.morph(m.geometry,0);
 auto copy=[&](void *to,const void *from,size_t n){if(!to||!from)return false;std::memcpy(to,from,n);return true;};
 bool ok=morph&&copy(rw.vertices(morph),m.v,size_t(m.nv)*12);
 if(ok&&(flags&16))ok=copy(rw.normals(morph),rw.normals(sourceMorph),size_t(m.nv)*12);
 if(ok&&(flags&8))ok=copy(rw.colours(g),rw.colours(m.geometry),size_t(m.nv)*4);
 for(int i=0;ok&&i<uvs;i++)ok=copy(rw.uv(g,i+1),rw.uv(m.geometry,i+1),size_t(m.nv)*8);
 auto triangles=static_cast<Triangle *>(rw.triangles(g));ok=ok&&triangles;
 for(int i=0;ok&&i<m.nt;i++){
  triangles[i]=m.t[i];
  // Keep triangle ordinals stable for the certified bullet contact. Collapsed
  // glass emits no raster fragments, rain intersections or bullet hits; the
  // RT input cleaner already removes degenerate triangles. Metal is unchanged.
  if(mask&glassmodel::Bit(m.Face(i)))triangles[i].v[1]=triangles[i].v[2]=triangles[i].v[0];
  triangles[i].material=0xffff;
  void *material=rw.material(m.geometry,m.t[i].material);ok=material&&rw.triangleMaterial(g,&triangles[i],material);
 }
 const void *sphere=ok?rw.sphere(sourceMorph):nullptr;
 if(sphere)rw.setSphere(morph,sphere);else ok=false;
 if(ok)ok=rw.unlock(g)!=nullptr;
 if(!ok){rw.destroyGeometry(g);return nullptr;}return g;
}
void Break(uint8_t *v,Pane pane,Vec3 hit)
{
 if(!gReady||!gConfig.carGlass||pane==Pane::None||pane==Pane::Front||!Eligible(v)||!Finite(hit))return;
 Collect();auto it=std::find_if(gCars.begin(),gCars.end(),[&](const Car &c){return c.car==v;});
 if(it==gCars.end()){
  if(gCars.size()>=kMaxCars)return;
  Car c{v,Ref(v),game::EntityModel(v),game::EntityClump(v),0,{}};c.parts.reserve(kMaxParts);gCars.push_back(std::move(c));it=gCars.end()-1;
 }
 Car &car=*it;const uint8_t bit=glassmodel::Bit(pane);if(car.broken&bit)return;
 Objects objects;rw.atomics(car.clump,Gather,&objects);if(!objects.valid)return;
 std::vector<Pending> pending;pending.reserve(kMaxParts);Vec3 shard[3]{};bool haveShard=false;size_t extra=0,newParts=0;
 for(size_t i=0;i<objects.n;i++){
  void *atomic=objects.a[i];auto old=std::find_if(car.parts.begin(),car.parts.end(),[&](const Part &p){return p.atomic==atomic;});
  if(old!=car.parts.end()&&rw.geometry(atomic)!=old->copy)return;
  void *source=old==car.parts.end()?rw.geometry(atomic):rw.geometry(old->holder);
  Mesh m;if(!m.Read(source,atomic,rw.root(car.clump)))continue;
  bool changed=false;
  for(int t=0;t<m.nt;t++)if(m.Face(t)==pane){changed=true;if(!haveShard&&(rw.atomicFlags(atomic)&4)){m.Face(t,shard);haveShard=true;}}
  if(!changed)continue;
  if(pending.size()>=kMaxParts||car.parts.size()+newParts+(old==car.parts.end())>kMaxParts)return;
  pending.emplace_back();Pending &p=pending.back();p.atomic=atomic;p.was=old==car.parts.end()?nullptr:&*old;
  p.copy=Copy(m,car.broken|bit,p.bytes,kBudget-gBytes-extra);if(!p.copy)return;
  extra+=p.bytes;if(gBytes+extra>kBudget)return;
  if(!p.was){p.holder=rw.createAtomic();if(!p.holder)return;rw.setGeometry(p.holder,source,0);++newParts;}
 }
 if(pending.empty())return;
 // Commit only after every affected variant has a valid copy and reference.
 // Streaming, repair and another vehicle can never see half a transaction.
 for(auto &p:pending){
  rw.setGeometry(p.atomic,p.copy,0);
  if(p.was){gBytes-=p.was->bytes;p.was->copy=p.copy;p.was->bytes=p.bytes;}
  else{car.parts.push_back({p.atomic,p.holder,p.copy,p.bytes});p.holder=nullptr;}
  gBytes+=p.bytes;
 }
 car.broken|=bit;
 Log("car glass: model %d, pane %u broken (%zu private atomics; %zu KiB total)",car.model,unsigned(pane),car.parts.size(),gBytes/1024);
 if(haveShard){
  const float *matrix=reinterpret_cast<const float *>(v+8);Vec3 a=Transform(matrix,shard[0]),b=Transform(matrix,shard[1]),c=Transform(matrix,shard[2]);
  CVector speed=*reinterpret_cast<const CVector *>(v+0x84);
  if(Finite(a)&&Finite(b)&&Finite(c)&&Finite({speed.x,speed.y,speed.z})){
   Vec3 up=Sub(b,a),right=Sub(c,a);
   gPanes(2,{a.x,a.y,a.z},{up.x,up.y,up.z},{right.x,right.y,right.z},speed,{hit.x,hit.y,hit.z},.08f,false,false,2,true);
   gSound(57,{hit.x,hit.y,hit.z});
  }
 }
}
void Door(uint8_t *v,int node,int door,bool noFlying)
{
 gDoor(v,node,door,noFlying);
 if(gReady&&gConfig.carGlass&&Eligible(v)&&door>=0&&door<6&&gDoorStatus(v+kDamage,door)>=2)
  try{Break(v,glassmodel::DoorPane(door),Vec3{game::EntityPos(v).x,game::EntityPos(v).y,game::EntityPos(v).z});}catch(const std::bad_alloc &){}
}
void Bumper(uint8_t *v,int node,int panel,bool noFlying)
{
 gBumper(v,node,panel,noFlying);
 if(gReady&&gConfig.carGlass&&panel==6&&Eligible(v)&&gPanelStatus(v+kDamage,panel)==3)
  try{Break(v,Pane::Rear,Vec3{game::EntityPos(v).x,game::EntityPos(v).y,game::EntityPos(v).z});}catch(const std::bad_alloc &){}
}
void Fix(uint8_t *v){Forget(v,true);gFix(v);}
void Delete(uint8_t *v){Forget(v,false);gDelete(v);}
} // namespace
void Bullet(uint8_t *v,const void *atomic,int triangle,Vec3 hit,Vec3 normal)
{
 if(!gReady||!gConfig.carGlass||!Eligible(v)||(game::EntityFlags(v)&(1u<<17))||!Finite(hit)||!Finite(normal))return;
 Objects objects;rw.atomics(game::EntityClump(v),Gather,&objects);
 // Check identity against the live clump before touching the supplied atomic.
 if(!objects.valid||std::find(objects.a.begin(),objects.a.begin()+objects.n,atomic)==objects.a.begin()+objects.n||!(rw.atomicFlags(atomic)&4))return;
 Mesh m;if(!m.Read(rw.geometry(atomic),const_cast<void *>(atomic),rw.root(game::EntityClump(v)))||triangle<0||triangle>=m.nt)return;
 try{Break(v,m.Face(triangle),hit);}catch(const std::bad_alloc &){}
}
bool Install()
{
 if(gReady||!gConfig.carGlass)return true;
#define BIND(v,s) v=reinterpret_cast<decltype(v)>(dlsym(RTLD_MAIN_ONLY,s));if(!v)return false
 BIND(gPool,"_ZN6CPools15ms_pVehiclePoolE");
 BIND(rw.atomics,"_Z20RpClumpForAllAtomicsPN2rw5ClumpEPFPNS_6AtomicES3_PvES4_");
 BIND(rw.root,"_Z15RpClumpGetFramePKN2rw5ClumpE");BIND(rw.frame,"_Z16RpAtomicGetFramePKN2rw6AtomicE");
 BIND(rw.geometry,"_Z19RpAtomicGetGeometryPKN2rw6AtomicE");BIND(rw.atomicFlags,"_Z16RpAtomicGetFlagsPKN2rw6AtomicE");
 BIND(rw.name,"_Z16GetFrameNodeNamePN2rw5FrameE");BIND(rw.parent,"_Z16RwFrameGetParentPKN2rw5FrameE");BIND(rw.matrix,"_Z16RwFrameGetMatrixPN2rw5FrameE");
 BIND(rw.nv,"_Z24RpGeometryGetNumVerticesPKN2rw8GeometryE");BIND(rw.nt,"_Z25RpGeometryGetNumTrianglesPKN2rw8GeometryE");
 BIND(rw.flags,"_Z18RpGeometryGetFlagsPKN2rw8GeometryE");BIND(rw.uvCount,"_Z28RpGeometryGetNumTexCoordSetsPKN2rw8GeometryE");
 BIND(rw.morph,"_Z24RpGeometryGetMorphTargetPKN2rw8GeometryEi");BIND(rw.vertices,"_Z24RpMorphTargetGetVerticesPKN2rw11MorphTargetE");
 BIND(rw.normals,"_Z29RpMorphTargetGetVertexNormalsPKN2rw11MorphTargetE");BIND(rw.colours,"_Z27RpGeometryGetPreLightColorsPKN2rw8GeometryE");
 BIND(rw.uv,"_Z28RpGeometryGetVertexTexCoordsPKN2rw8GeometryE24RwTextureCoordinateIndex");BIND(rw.triangles,"_Z22RpGeometryGetTrianglesPKN2rw8GeometryE");
 BIND(rw.material,"_Z21RpGeometryGetMaterialPKN2rw8GeometryEi");BIND(rw.colour,"_Z18RpMaterialGetColorPKN2rw8MaterialE");
 BIND(rw.texture,"_Z20RpMaterialGetTexturePKN2rw8MaterialE");BIND(rw.textureName,"_Z16RwTextureGetNamePN2rw7TextureE");
 BIND(rw.triangleMaterial,"_Z29RpGeometryTriangleSetMaterialPN2rw8GeometryEPNS_8TriangleEPNS_8MaterialE");
 BIND(rw.sphere,"_Z30RpMorphTargetGetBoundingSpherePN2rw11MorphTargetE");BIND(rw.setSphere,"_Z30RpMorphTargetSetBoundingSpherePN2rw11MorphTargetEPKNS_6SphereE");
 BIND(rw.createGeometry,"_Z16RpGeometryCreateiij");BIND(rw.destroyGeometry,"_Z17RpGeometryDestroyPN2rw8GeometryE");BIND(rw.unlock,"_Z16RpGeometryUnlockPN2rw8GeometryE");
 BIND(rw.createAtomic,"_Z14RpAtomicCreatev");BIND(rw.destroyAtomic,"_Z15RpAtomicDestroyPN2rw6AtomicE");BIND(rw.setGeometry,"_Z19RpAtomicSetGeometryPN2rw6AtomicEPNS_8GeometryEj");
 BIND(gPanes,"_ZN6CGlass22GeneratePanesForWindowEj7CVectorS0_S0_S0_S0_fbbib");BIND(gSound,"_Z23PlayOneShotScriptObjecthRK7CVector");
 BIND(gDoorStatus,"_ZN14CDamageManager13GetDoorStatusEi");BIND(gPanelStatus,"_ZN14CDamageManager14GetPanelStatusEi");
#undef BIND
 if(!game::api.PoolsGetVehicle)return false;
 try{gCars.reserve(kMaxCars);}catch(const std::bad_alloc &){return false;}
#define CHAIN(v,s,f) if(!v)v=reinterpret_cast<decltype(v)>(hooks::Chain(s,reinterpret_cast<void *>(f)))
 CHAIN(gDoor,"_ZN11CAutomobile13SetDoorDamageEi6eDoorsb",Door);
 CHAIN(gBumper,"_ZN11CAutomobile15SetBumperDamageEi7ePanelsb",Bumper);
 CHAIN(gFix,"_ZN11CAutomobile3FixEv",Fix);CHAIN(gDelete,"_ZN7CEntity14DeleteRwObjectEv",Delete);
#undef CHAIN
 gReady=gDoor&&gBumper&&gFix&&gDelete;
 if(!gReady)Log("car glass: native lifecycle hooks missing; re-run install.sh");
 else Log("car glass: side/rear panes enabled; windscreen damage stays native");
 return gReady;
}
} // namespace carglass

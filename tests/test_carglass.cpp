// Exercise the production owner with reference-counted, shared native-shaped geometry.
#include "game.h"
#include "config.h"
#include "hooks.h"
#include "glassmodel.h"
#include "dentmodel.h"
#include <array>
#include <vector>
#include <string>
#include <map>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
static void *Lookup(void *,const char *);
#define dlsym Lookup
#include "../src/carglass.cpp"
#undef dlsym
namespace game {Api api{};}
Config gConfig;
void Log(const char *,...){}
namespace fixture {
using V=CVector;
using T=carglass::Triangle;
struct Mat {uint8_t colour[4]={255,255,255,160};const char *texture=nullptr;};
struct Geo {
 // Match the native count read in the production validation.
 uint8_t prefix[0x88]{};int materials=2;
 int refs=1;unsigned flags=0x3e;std::vector<V> vertices,normals;
 std::vector<T> triangles;std::array<Mat *,2> mats{};
 std::vector<uint32_t> colours;std::vector<std::array<float,2>> uv;
 std::array<float,4> sphere{0,0,0,3};
};
struct Frame {std::array<float,16> m{};Frame *parent=nullptr;std::string name;};
struct Atomic {Frame *frame=nullptr;Geo *geo=nullptr;unsigned flags=4;};
struct Clump {Frame root;std::vector<Atomic *> atomics;};
constexpr int kCars=20;
alignas(16) std::array<uint8_t,0x6f0*kCars> cars{};
uint8_t pool[24]{},carFlags[kCars]={1,2};void *poolPointer=pool;
Clump clumps[kCars];Frame frames[kCars][3];Atomic atoms[kCars][3];Mat glass,metal;
std::map<std::string,void *> entries;int failed=0,shards=0,sounds=0,created=0,destroyed=0,holders=0,nativeFix=0,nativeDelete=0,stripRebuilds=0;
int failCreate=-1;bool protectDoor=false;Geo *base=nullptr;
void Check(bool ok,const char *s){if(!ok){printf("FAIL: %s\n",s);++failed;}}
void Identity(float *m){std::fill(m,m+16,0);m[0]=m[5]=m[10]=m[15]=1;}
uint8_t *Car(int ref){int i=ref>>8;return i>=0&&i<kCars&&ref==((i<<8)|carFlags[i])&&!(carFlags[i]&0x80)?cars.data()+i*0x6f0:nullptr;}
void DestroyGeo(void *g){auto x=static_cast<Geo *>(g);if(!--x->refs){++destroyed;delete x;}}
void *SetGeometry(void *a,void *g,unsigned){auto x=static_cast<Atomic *>(a);if(x->geo)DestroyGeo(x->geo);x->geo=static_cast<Geo *>(g);if(g)++x->geo->refs;return a;}
void *CreateAtomic(){++holders;return new Atomic;}
int DestroyAtomic(void *a){auto x=static_cast<Atomic *>(a);if(x->geo)DestroyGeo(x->geo);delete x;--holders;return 1;}
void *CreateGeo(int nv,int nt,unsigned flags){if(failCreate==0)return nullptr;if(failCreate>0)--failCreate;auto g=new Geo;++created;*reinterpret_cast<int *>(g->prefix+0x20)=1;g->flags=flags;g->vertices.resize(nv);g->normals.resize(nv);g->triangles.resize(nt);g->colours.resize(nv);g->uv.resize(nv);return g;}
void *Atomics(void *c,void *(*cb)(void *,void *),void *d){for(auto a:static_cast<Clump *>(c)->atomics)if(!cb(a,d))break;return c;}
void *Root(const void *c){return const_cast<Frame *>(&static_cast<const Clump *>(c)->root);}
void *AFrame(const void *a){return static_cast<const Atomic *>(a)->frame;}
void *Geometry(const void *a){return static_cast<const Atomic *>(a)->geo;}
unsigned AFlags(const void *a){return static_cast<const Atomic *>(a)->flags;}
const char *Name(void *f){return static_cast<Frame *>(f)->name.c_str();}
void *Parent(const void *f){return static_cast<const Frame *>(f)->parent;}
float *Matrix(void *f){return static_cast<Frame *>(f)->m.data();}
int NV(const void *g){return int(static_cast<const Geo *>(g)->vertices.size());}
int NT(const void *g){return int(static_cast<const Geo *>(g)->triangles.size());}
unsigned Flags(const void *g){return static_cast<const Geo *>(g)->flags;}
int UVCount(const void *){return 1;}
void *Morph(const void *g,int){return const_cast<void *>(g);}
void *Vertices(const void *m){return const_cast<V *>(static_cast<const Geo *>(m)->vertices.data());}
void *Normals(const void *m){return const_cast<V *>(static_cast<const Geo *>(m)->normals.data());}
void *Colours(const void *g){return const_cast<uint32_t *>(static_cast<const Geo *>(g)->colours.data());}
void *UV(const void *g,int){return const_cast<std::array<float,2> *>(static_cast<const Geo *>(g)->uv.data());}
void *Triangles(const void *g){return const_cast<T *>(static_cast<const Geo *>(g)->triangles.data());}
void *Material(const void *g,int i){return static_cast<const Geo *>(g)->mats.at(i);}
const uint8_t *Colour(const void *m){return static_cast<const Mat *>(m)->colour;}
void *Texture(const void *m){return const_cast<char *>(static_cast<const Mat *>(m)->texture);}
const char *TextureName(void *t){return static_cast<const char *>(t);}
void *TriMaterial(void *g,T *t,void *m){auto geo=static_cast<Geo *>(g);int i=m==&metal?1:0;geo->mats[i]=static_cast<Mat *>(m);t->material=uint16_t(i);return g;}
void *Sphere(void *m){return static_cast<Geo *>(m)->sphere.data();}
void *SetSphere(void *m,const void *s){memcpy(static_cast<Geo *>(m)->sphere.data(),s,16);return m;}
void *Unlock(void *g){if(static_cast<Geo *>(g)->flags&1)++stripRebuilds;return g;}
void Generate(unsigned,V,V,V,V,V,float,bool,bool,int,bool){++shards;}
void Sound(uint8_t,const V &){++sounds;}
int DoorStatus(void *d,int door){return static_cast<uint8_t *>(d)[9+door];}
int PanelStatus(void *d,int panel){return (reinterpret_cast<uint32_t *>(d)[5]>>(panel*4))&15;}
void Door(uint8_t *c,int,int d,bool){if(protectDoor)c[0x34c+9+d]=1;}
void Bumper(uint8_t *,int,int,bool){}
void Fix(uint8_t *){++nativeFix;}
void Delete(uint8_t *){++nativeDelete;}
void Reset()
{
 for(auto &c:clumps)if(!c.atomics.empty())reinterpret_cast<void (*)(uint8_t *)>(entries["_ZN7CEntity14DeleteRwObjectEv"])(cars.data()+(&c-clumps)*0x6f0);
 for(auto &row:atoms)for(auto &a:row){if(a.geo)DestroyGeo(a.geo);a={};}
 if(base){DestroyGeo(base);base=nullptr;}
 cars={};carFlags[0]=1;carFlags[1]=2;failCreate=-1;protectDoor=false;
 shards=sounds=stripRebuilds=0;metal.colour[3]=255;
 base=static_cast<Geo *>(CreateGeo(8,4,0x3f));base->mats={&glass,&metal};
 base->vertices={{-.8f,0,.2f},{-.8f,1,.2f},{-.8f,1,1},{-.8f,0,1},{-.8f,0,-.3f},{-.8f,1,-.3f},{-.8f,1,.2f},{-.8f,0,.2f}};
 base->triangles={{{0,1,2},0},{{0,2,3},0},{{4,5,6},1},{{4,6,7},1}};
 for(int i=0;i<8;++i){base->normals[i]={1,0,0};base->colours[i]=0x12345678;base->uv[i]={float(i),.2f};}
 for(int i=0;i<kCars;i++){
  auto c=cars.data()+i*0x6f0;Identity(reinterpret_cast<float *>(c+8));*reinterpret_cast<unsigned *>(c+0x60)=2|0x20;c[0x297]=0x40;
  *reinterpret_cast<int16_t *>(c+0x6a)=145;*reinterpret_cast<void **>(c+0x58)=&clumps[i];Identity(clumps[i].root.m.data());clumps[i].atomics.clear();
  for(int j=0;j<3;j++){auto &f=frames[i][j];Identity(f.m.data());f.parent=&clumps[i].root;f.name=j==0?"door_lf_hi_ok":j==1?"door_lf_hi_dam":"door_rf_hi_ok";atoms[i][j]={&f,nullptr,j==1?0u:4u};SetGeometry(&atoms[i][j],base,0);clumps[i].atomics.push_back(&atoms[i][j]);}
 }
 *reinterpret_cast<void **>(pool)=cars.data();*reinterpret_cast<void **>(pool+8)=carFlags;*reinterpret_cast<int *>(pool+16)=kCars;
}
bool Collapsed(const Geo *g,int i){return g->triangles[i].v[0]==g->triangles[i].v[1]&&g->triangles[i].v[0]==g->triangles[i].v[2];}
void Bullet(int a=0){carglass::Bullet(cars.data(),&atoms[0][a],0,{-.8f,.5f,.5f},{1,0,0});}
}
namespace hooks {void *Chain(const char *n,void *f){using namespace fixture;void *old=entries[n];entries[n]=f;return old;} }
static void *Lookup(void *,const char *s)
{
 using namespace fixture;
#define BIND(n,f) if(!strcmp(s,n))return reinterpret_cast<void *>(f)
 BIND("_ZN6CPools15ms_pVehiclePoolE",&poolPointer);
 BIND("_Z20RpClumpForAllAtomicsPN2rw5ClumpEPFPNS_6AtomicES3_PvES4_",Atomics);
 BIND("_Z15RpClumpGetFramePKN2rw5ClumpE",Root);BIND("_Z16RpAtomicGetFramePKN2rw6AtomicE",AFrame);
 BIND("_Z19RpAtomicGetGeometryPKN2rw6AtomicE",Geometry);BIND("_Z16RpAtomicGetFlagsPKN2rw6AtomicE",AFlags);
 BIND("_Z16GetFrameNodeNamePN2rw5FrameE",Name);BIND("_Z16RwFrameGetParentPKN2rw5FrameE",Parent);BIND("_Z16RwFrameGetMatrixPN2rw5FrameE",Matrix);
 BIND("_Z24RpGeometryGetNumVerticesPKN2rw8GeometryE",NV);BIND("_Z25RpGeometryGetNumTrianglesPKN2rw8GeometryE",NT);
 BIND("_Z18RpGeometryGetFlagsPKN2rw8GeometryE",Flags);BIND("_Z28RpGeometryGetNumTexCoordSetsPKN2rw8GeometryE",UVCount);
 BIND("_Z24RpGeometryGetMorphTargetPKN2rw8GeometryEi",Morph);BIND("_Z24RpMorphTargetGetVerticesPKN2rw11MorphTargetE",Vertices);
 BIND("_Z29RpMorphTargetGetVertexNormalsPKN2rw11MorphTargetE",Normals);BIND("_Z27RpGeometryGetPreLightColorsPKN2rw8GeometryE",Colours);
 BIND("_Z28RpGeometryGetVertexTexCoordsPKN2rw8GeometryE24RwTextureCoordinateIndex",UV);BIND("_Z22RpGeometryGetTrianglesPKN2rw8GeometryE",Triangles);
 BIND("_Z21RpGeometryGetMaterialPKN2rw8GeometryEi",Material);BIND("_Z18RpMaterialGetColorPKN2rw8MaterialE",Colour);
 BIND("_Z20RpMaterialGetTexturePKN2rw8MaterialE",Texture);BIND("_Z16RwTextureGetNamePN2rw7TextureE",TextureName);
 BIND("_Z29RpGeometryTriangleSetMaterialPN2rw8GeometryEPNS_8TriangleEPNS_8MaterialE",TriMaterial);
 BIND("_Z30RpMorphTargetGetBoundingSpherePN2rw11MorphTargetE",Sphere);BIND("_Z30RpMorphTargetSetBoundingSpherePN2rw11MorphTargetEPKNS_6SphereE",SetSphere);
 BIND("_Z16RpGeometryCreateiij",CreateGeo);BIND("_Z17RpGeometryDestroyPN2rw8GeometryE",DestroyGeo);BIND("_Z16RpGeometryUnlockPN2rw8GeometryE",Unlock);
 BIND("_Z14RpAtomicCreatev",CreateAtomic);BIND("_Z15RpAtomicDestroyPN2rw6AtomicE",DestroyAtomic);BIND("_Z19RpAtomicSetGeometryPN2rw6AtomicEPNS_8GeometryEj",SetGeometry);
 BIND("_ZN6CGlass22GeneratePanesForWindowEj7CVectorS0_S0_S0_S0_fbbib",Generate);BIND("_Z23PlayOneShotScriptObjecthRK7CVector",Sound);
 BIND("_ZN14CDamageManager13GetDoorStatusEi",DoorStatus);BIND("_ZN14CDamageManager14GetPanelStatusEi",PanelStatus);
#undef BIND
 return nullptr;
}
int main()
{
 using namespace fixture;
 entries["_ZN11CAutomobile13SetDoorDamageEi6eDoorsb"]=reinterpret_cast<void *>(Door);
 entries["_ZN11CAutomobile15SetBumperDamageEi7ePanelsb"]=reinterpret_cast<void *>(Bumper);
 entries["_ZN11CAutomobile3FixEv"]=reinterpret_cast<void *>(Fix);
 entries["_ZN7CEntity14DeleteRwObjectEv"]=reinterpret_cast<void *>(Delete);
 game::api.PoolsGetVehicle=Car;gConfig.carGlass=gConfig.carDeformation=false;Check(carglass::Install()&&!carglass::gReady,"disabled startup installs no glass hooks");gConfig.carGlass=gConfig.carDeformation=true;
 Check(carglass::Install(),"installed native bindings");Reset();
 *reinterpret_cast<int16_t *>(cars.data()+0x6a)=181;*reinterpret_cast<int *>(cars.data()+0x348)=4;
 const auto original=atoms[0][0].geo;Check(carglass::Perforate(cars.data(),&atoms[0][0],{-.8f,.5f,-.1f},{1,0,0},80),"aircraft skin accepts certified local perforation");
 Check(atoms[0][0].geo!=original&&atoms[1][0].geo==original,"perforation uses private geometry on only the struck airframe");
 float area=0;auto g=atoms[0][0].geo;for(const auto &t:g->triangles)if(t.material==1){const auto a=g->vertices[t.v[0]],b=g->vertices[t.v[1]],c=g->vertices[t.v[2]];area+=std::fabs((b.y-a.y)*(c.z-a.z)-(b.z-a.z)*(c.y-a.y))*.5f;}
 Check(area<.5f&&area>.45f&&g->triangles.size()>original->triangles.size(),"perforated coarse skin retains surrounding surface with real local missing area");
 float opening=1;Check(carglass::Opening(cars.data(),{-2,.5f,-.1f},{2,.5f,-.1f},opening)&&opening>0&&opening<1,"later rays see the actual private skin opening");
 int accepted=0;for(int i=0;i<100;i++){const Vec3 at{-.8f,.03f+(i%10)*.095f,-.28f+(i/10)*.048f};accepted+=carglass::Perforate(cars.data(),&atoms[0][0],at,{1,0,0},20);}
 std::printf("hole stress accepted=%d triangles=%zu bytes=%zu\n",accepted,atoms[0][0].geo->triangles.size(),carglass::gBytes);
 Check(accepted>=90,"ongoing distinct bullet holes continue beyond64 within native topology and byte budget");
 carglass::Restore(cars.data());Check(atoms[0][0].geo==original,"airframe repair restores its original shared geometry");Reset();
 Bullet();
 Check(!(atoms[0][0].geo->flags&1)&&stripRebuilds==0&&base->flags==0x3f,"broken copies use triangle lists and retain the original strip geometry");
 Check(atoms[0][0].geo!=base&&Collapsed(atoms[0][0].geo,0)&&Collapsed(atoms[0][0].geo,1),"hit side window disappears");
 Check(!Collapsed(atoms[0][0].geo,2)&&!Collapsed(atoms[0][0].geo,3),"opaque door triangles preserved");
 Check(Collapsed(atoms[0][1].geo,0)&&atoms[0][1].flags==0,"hidden damaged door also loses its glass without reappearing");
 Check(atoms[0][2].geo==base&&atoms[1][0].geo==base&&!Collapsed(base,0),"other window and same-model car remain intact");
 Check(shards==1&&sounds==1,"native shard and audio effect once");Bullet();Check(shards==1,"repeat through opening has no effect");
 Check(!memcmp(atoms[0][0].geo->vertices.data(),base->vertices.data(),base->vertices.size()*sizeof(V))&&atoms[0][0].geo->uv==base->uv&&atoms[0][0].geo->colours==base->colours,"positions UVs and prelight preserved");
 Check(atoms[0][0].geo->mats==base->mats&&atoms[0][0].geo->sphere==base->sphere,"materials and bounds preserved");
 Bullet(2);Check(Collapsed(atoms[0][0].geo,0)&&Collapsed(atoms[0][2].geo,0)&&shards==2,"second window breaks independently without repairing the first");
 reinterpret_cast<void (*)(uint8_t *)>(entries["_ZN11CAutomobile3FixEv"])(cars.data());
 Check(atoms[0][0].geo==base&&atoms[0][1].geo==base&&holders==0&&nativeFix==1,"repair restores exact geometry and releases original references");
 Reset();failCreate=1;Bullet();Check(atoms[0][0].geo==base&&atoms[0][1].geo==base&&holders==0&&!shards,"second allocation failure rolls back whole window");
 Reset();base->triangles[0].v[1]=65000;Bullet();Check(atoms[0][0].geo==base&&!shards,"bad geometry index refuses mutation");
 Reset();*reinterpret_cast<int *>(base->prefix+0x20)=2;Bullet();Check(atoms[0][0].geo==base&&!shards,"multiple morph targets cannot be partially copied");
 Reset();{const size_t old=carglass::gBytes;const int count=created;carglass::gBytes=carglass::kBudget-1;Bullet();Check(atoms[0][0].geo==base&&!shards&&holders==0&&created==count,"geometry budget refuses before allocating a copy");carglass::gBytes=old;}
 Reset();gConfig.carGlass=false;Bullet();Check(atoms[0][0].geo==base&&!shards,"disabled retains native behavior");gConfig.carGlass=true;
 Reset();frames[0][0].name="windscreen_hi_ok";Bullet();Check(atoms[0][0].geo==base&&!shards,"windscreen remains native");
 Reset();cars[0x62]|=2;Bullet();Check(atoms[0][0].geo==base&&!shards,"native bullet-proof car");
 Reset();cars[0x297]=0;Bullet();Check(atoms[0][0].geo==base&&!shards,"native no-damage car");
 Reset();*reinterpret_cast<int *>(cars.data()+0x348)=4;Bullet();Check(atoms[0][0].geo==base&&!shards,"aircraft excluded");
 Reset();carFlags[0]=0x81;Bullet();Check(atoms[0][0].geo==base&&!shards,"unallocated pool slot excluded");
 Reset();Atomic foreign; carglass::Bullet(cars.data(),&foreign,0,{0,0,0},{1,0,0});Check(!shards,"foreign atomic refused before dereference");
 Reset();cars[0x34c+9+2]=1;reinterpret_cast<void (*)(uint8_t *,int,int,bool)>(entries["_ZN11CAutomobile13SetDoorDamageEi6eDoorsb"])(cars.data(),10,2,false);
 Check(!shards,"light door damage keeps glass");cars[0x34c+9+2]=2;reinterpret_cast<void (*)(uint8_t *,int,int,bool)>(entries["_ZN11CAutomobile13SetDoorDamageEi6eDoorsb"])(cars.data(),10,2,false);Check(shards==1,"severe native door damage shatters glass");
 Reset();protectDoor=true;cars[0x34c+9+2]=3;reinterpret_cast<void (*)(uint8_t *,int,int,bool)>(entries["_ZN11CAutomobile13SetDoorDamageEi6eDoorsb"])(cars.data(),10,2,false);Check(!shards,"native protected door result respected");
 Reset();frames[0][0].name="chassis_hi";frames[0][0].m[0]=0;frames[0][0].m[1]=1;frames[0][0].m[4]=-1;frames[0][0].m[5]=0;
 *reinterpret_cast<uint32_t *>(cars.data()+0x34c+0x14)=3u<<24;
 reinterpret_cast<void (*)(uint8_t *,int,int,bool)>(entries["_ZN11CAutomobile15SetBumperDamageEi7ePanelsb"])(cars.data(),13,6,false);
 Check(shards==1&&Collapsed(atoms[0][0].geo,0),"native missing rear bumper breaks rear window in chassis");
 Reset();for(auto &f:frames[0])f.name="chassis_hi";
 atoms[0][1].flags=4;
 frames[0][0].m[0]=0;frames[0][0].m[1]=1;frames[0][0].m[4]=-1;frames[0][0].m[5]=0;
 frames[0][1].m[13]=-1;frames[0][2].m[13]=-1;frames[0][2].m[0]=-1;
 *reinterpret_cast<uint32_t *>(cars.data()+0x34c+0x14)=3u<<24;
 reinterpret_cast<void (*)(uint8_t *,int,int,bool)>(entries["_ZN11CAutomobile15SetBumperDamageEi7ePanelsb"])(cars.data(),13,6,false);
 Check(shards==3&&Collapsed(atoms[0][0].geo,0)&&Collapsed(atoms[0][1].geo,0)&&Collapsed(atoms[0][2].geo,0),"severe rear crash shatters fixed rear quarters on two-door cars");
 Check(atoms[1][0].geo==base&&!Collapsed(base,0),"rear crash preserves another car's glass");
 Reset();dentmodel::Impact dent;dentmodel::Make({-.8f,.5f,-.3f},{1,0,0},13,dent);
 Check(carglass::Dent(cars.data(),dent),"collision creates a private body dent");
 const float dentedX=atoms[0][0].geo->vertices[4].x;
 Check(dentedX>-.8f&&dentedX<=-.55f&&atoms[0][1].geo->vertices[4].x==dentedX,"visible and hidden damaged metal share dent");
 Check(atoms[0][0].geo->vertices[0].x==-.8f&&atoms[1][0].geo==base,"glass vertices and other car stay rigid");
 Bullet();Check(Collapsed(atoms[0][0].geo,0)&&atoms[0][0].geo->vertices[4].x==dentedX,"glass break preserves existing dent");
 Check(carglass::Dent(cars.data(),dent)&&Collapsed(atoms[0][0].geo,0)&&atoms[0][0].geo->vertices[4].x>dentedX,"later dent preserves broken glass");
 for(int i=0;i<20;i++)carglass::Dent(cars.data(),dent);
 Check(atoms[0][0].geo->vertices[4].x>=-.55001f&&std::isfinite(atoms[0][0].geo->normals[4].x),"repeated dents remain capped with finite normals");
 Check(atoms[0][0].geo->sphere[3]>=3.25f,"dented mesh has conservative bounds");
 reinterpret_cast<void (*)(uint8_t *)>(entries["_ZN11CAutomobile3FixEv"])(cars.data());
 Check(atoms[0][0].geo==base&&holders==0,"repair restores both dent and broken glass");
 Reset();Bullet();failCreate=1;Check(!carglass::Dent(cars.data(),dent)&&Collapsed(atoms[0][0].geo,0)&&atoms[0][0].geo->vertices[4].x==-.8f,"failed dent transaction preserves existing glass and metal");
 Reset();gConfig.carDeformation=false;Check(!carglass::Dent(cars.data(),dent)&&atoms[0][0].geo==base,"deformation off leaves geometry native");gConfig.carDeformation=true;
 Reset();for(int i=0;i<16;i++)Check(carglass::Dent(cars.data()+i*0x6f0,dent),"bounded owner admits first sixteen deformed cars");
 Check(!carglass::Dent(cars.data()+16*0x6f0,dent)&&atoms[16][0].geo==base,"seventeenth car waits without restoring visible dents");
 reinterpret_cast<void (*)(uint8_t *)>(entries["_ZN11CAutomobile3FixEv"])(cars.data());
 Check(carglass::Dent(cars.data()+16*0x6f0,dent),"repair frees a deformation slot");
 Reset();carglass::Dent(cars.data(),dent);const float beforeSwing=atoms[0][0].geo->vertices[4].x;
 frames[0][0].m[0]=0;frames[0][0].m[1]=1;frames[0][0].m[4]=-1;frames[0][0].m[5]=0;
 Bullet();Check(atoms[0][0].geo->vertices[4].x==beforeSwing,"glass rebuild retains dent in a swinging part's local frame");
 Reset();Bullet();++carFlags[0];reinterpret_cast<void (*)(uint8_t *)>(entries["_ZN7CEntity14DeleteRwObjectEv"])(cars.data());Check(holders==0,"stale generation releases holders without restoring stale atomics");
 Reset();Bullet();reinterpret_cast<void (*)(uint8_t *)>(entries["_ZN7CEntity14DeleteRwObjectEv"])(cars.data());Check(holders==0&&nativeDelete>0,"clump deletion releases references and chains native");
 Reset();for(auto &c:clumps)c.atomics.clear();for(auto &row:atoms)for(auto &a:row)if(a.geo){DestroyGeo(a.geo);a.geo=nullptr;}DestroyGeo(base);base=nullptr;
 Check(created==destroyed,"all allocated geometry freed");printf("carglass: %s\n",failed?"FAILED":"all passed");return failed?1:0;
}

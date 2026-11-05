// Optional integration regression against the real librw mesh builder.
// Unlike test_carglass's owner fixture, Geometry::unlock is not stubbed.
// Build with the NULL backend; no game process, window, or saved data needed.
#include "rw.h"
#include "game.h"
#include "config.h"
#include "hooks.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif
#include "../src/carglass.cpp"
#include "../src/airframedebris.cpp"
#include "nativejetshadowarm.h"
#include "../tools/bake/gamefiles.h"

namespace game {Api api{};}
Config gConfig;
void Log(const char *,...){}
namespace hooks {void *Chain(const char *,void *){return nullptr;}}

namespace {
void Check(bool,const char *);
std::map<const void *,std::string> frameNames;
void RegisterFrameNames(){
 rw::Frame::registerPlugin(0,0x253f2fe,[](void *p,int32_t,int32_t)->void *{frameNames[p]="door_lf_hi_ok";return p;},[](void *p,int32_t,int32_t)->void *{frameNames.erase(p);return p;},[](void *d,void *s,int32_t,int32_t)->void *{frameNames[d]=frameNames[s];return d;});
 rw::Frame::registerPluginStream(0x253f2fe,[](rw::Stream *stream,int32_t length,void *p,int32_t,int32_t)->rw::Stream *{Check(length>=0&&length<256,"bounded native frame name");std::vector<char> name(size_t(length)+1);stream->read8(name.data(),length);frameNames[p]=name.data();return stream;},nullptr,nullptr);
}
static_assert(offsetof(rw::Geometry,matList)+offsetof(rw::MaterialList,numMaterials)==0x88,
              "fixture must match macVC's measured geometry ABI");
static_assert(sizeof(rw::Triangle)==sizeof(carglass::Triangle));
void Check(bool ok,const char *message)
{
 if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(2);}
}
void Bind()
{
 auto &r=carglass::rw;
 r.atomics=[](void *c,void *(*cb)(void *,void *),void *data)->void *{auto clump=static_cast<rw::Clump *>(c);for(auto link=clump->atomics.link.next;link!=&clump->atomics.link;link=link->next)if(!cb(rw::Atomic::fromClump(link),data))break;return c;};
 r.root=[](const void *c)->void *{return static_cast<const rw::Clump *>(c)->getFrame();};
 r.geometry=[](const void *a)->void *{return static_cast<const rw::Atomic *>(a)->geometry;};
 r.atomicFlags=[](const void *a)->unsigned{return static_cast<const rw::Atomic *>(a)->getFlags();};
 r.createAtomic=[]()->void *{return rw::Atomic::create();};
 r.destroyAtomic=[](void *a){static_cast<rw::Atomic *>(a)->destroy();return 1;};
 r.setGeometry=[](void *a,void *g,unsigned flags)->void *{static_cast<rw::Atomic *>(a)->setGeometry(static_cast<rw::Geometry *>(g),flags);return a;};
 r.frame=[](const void *a)->void *{return static_cast<const rw::Atomic *>(a)->getFrame();};
 r.name=[](void *f){return frameNames.at(f).c_str();};
 r.parent=[](const void *f)->void *{return static_cast<const rw::Frame *>(f)->getParent();};
 r.matrix=[](void *f){return reinterpret_cast<float *>(&static_cast<rw::Frame *>(f)->matrix);};
 r.nv=[](const void *g){return static_cast<const rw::Geometry *>(g)->numVertices;};
 r.nt=[](const void *g){return static_cast<const rw::Geometry *>(g)->numTriangles;};
 r.flags=[](const void *g)->unsigned{return static_cast<const rw::Geometry *>(g)->flags;};
 r.uvCount=[](const void *g){return static_cast<const rw::Geometry *>(g)->numTexCoordSets;};
 r.morph=[](const void *g,int i)->void *{return static_cast<const rw::Geometry *>(g)->morphTargets+i;};
 r.vertices=[](const void *m)->void *{return static_cast<const rw::MorphTarget *>(m)->vertices;};
 r.normals=[](const void *m)->void *{return static_cast<const rw::MorphTarget *>(m)->normals;};
 r.colours=[](const void *g)->void *{return static_cast<const rw::Geometry *>(g)->colors;};
 r.uv=[](const void *g,int i)->void *{return static_cast<const rw::Geometry *>(g)->texCoords[i-1];};
 r.triangles=[](const void *g)->void *{return static_cast<const rw::Geometry *>(g)->triangles;};
 r.material=[](const void *g,int i)->void *{return static_cast<const rw::Geometry *>(g)->matList.materials[i];};
 r.colour=[](const void *m){return reinterpret_cast<const uint8_t *>(&static_cast<const rw::Material *>(m)->color);};
 r.texture=[](const void *m)->void *{return static_cast<const rw::Material *>(m)->texture;};
 r.textureName=[](void *t)->const char *{return static_cast<const rw::Texture *>(t)->name;};
 r.createGeometry=[](int nv,int nt,unsigned flags)->void *{return rw::Geometry::create(nv,nt,flags);};
 r.destroyGeometry=[](void *g){static_cast<rw::Geometry *>(g)->destroy();};
 r.unlock=[](void *g)->void *{static_cast<rw::Geometry *>(g)->unlock();return g;};
 r.triangleMaterial=[](void *g,carglass::Triangle *t,void *material)->void *{
  auto geo=static_cast<rw::Geometry *>(g);auto mat=static_cast<rw::Material *>(material);
  int index=geo->matList.findIndex(mat);if(index<0)index=geo->matList.appendMaterial(mat);
  t->material=uint16_t(index);return g;
 };
 r.sphere=[](void *m)->void *{return &static_cast<rw::MorphTarget *>(m)->boundingSphere;};
 r.setSphere=[](void *m,const void *s)->void *{static_cast<rw::MorphTarget *>(m)->boundingSphere=*static_cast<const rw::Sphere *>(s);return m;};
 carglass::gPanes=+[](unsigned,CVector,CVector,CVector,CVector,CVector,float,bool,bool,int,bool){};carglass::gSound=+[](uint8_t,const CVector &){};
}
const float *NativeLTM(void *f){return reinterpret_cast<float *>(static_cast<rw::Frame *>(f)->getLTM());}
void Shadow(rw::Atomic *atomic,rw::Geometry *geometry)
{
 using namespace nativejetshadowtest;
 Code code;code.Add(0x10016c540,0x10016ca90);code.Add(0x1002a9480,0x1002a94a4);
 code.bindings={{0x10029c378,reinterpret_cast<void *>(NativeLTM)}};code.Finish();
 std::vector<float> coordinates;coordinates.reserve(1024);
 using Append=int (*)(std::vector<float> &,rw::Atomic *,bool,std::vector<float> *);
 auto append=reinterpret_cast<Append>(code.Address(0x10016c540));
 atomic->setGeometry(geometry,0);append(coordinates,atomic,false,nullptr);
#if __has_feature(address_sanitizer)
 // Native instructions update vector::end without libc++'s ASan annotations.
 // Only expose the size that native append actually returned, within capacity.
 Check(coordinates.size()<=coordinates.capacity(),"native shadow append respects capacity");
 __asan_unpoison_memory_region(coordinates.data(),coordinates.size()*sizeof(float));
#endif
 size_t at=0;
 for(int i=0;i<geometry->numTriangles;i++){
  const auto &t=geometry->triangles[i];
  for(auto index:t.v){const auto p=geometry->morphTargets[0].vertices[index];
   if(at+3>coordinates.size()){Check(false,"native shadow receives every metal vertex");return;}
   Check(coordinates[at]==p.x&&coordinates[at+1]==p.y&&coordinates[at+2]==p.z,"native shadow uses exact dented CPU vertex positions");at+=3;
  }
 }
 Check(at==coordinates.size(),"native shadow retains all dynamic triangle ordinals");
}
void Copy(bool strip,bool broken,bool glassOnly,bool dented)
{
 const unsigned flags=0x3eu|(strip?1u:0u);const int nv=glassOnly?4:8,nt=glassOnly?2:4;
 auto source=rw::Geometry::create(nv,nt,flags);
 auto glass=rw::Material::create(),metal=rw::Material::create();glass->color.alpha=160;
 source->matList.appendMaterial(glass);if(!glassOnly)source->matList.appendMaterial(metal);
 const rw::V3d vertices[]={{-.8f,0,.2f},{-.8f,1,.2f},{-.8f,1,1},{-.8f,0,1},
                         {-.8f,0,-.3f},{-.8f,1,-.3f},{-.8f,1,.2f},{-.8f,0,.2f}};
 const rw::Triangle triangles[]={{{0,1,2},0},{{0,2,3},0},{{4,5,6},1},{{4,6,7},1}};
 std::memcpy(source->morphTargets[0].vertices,vertices,size_t(nv)*sizeof(vertices[0]));
 std::memcpy(source->triangles,triangles,size_t(nt)*sizeof(triangles[0]));
 for(int i=0;i<nv;i++){
  source->morphTargets[0].normals[i]={1,0,0};source->colors[i]={10,20,30,255};source->texCoords[0][i]={float(i),.2f};
 }
 source->calculateBoundingSphere();source->unlock(); // intact strip is a passing control
 auto root=rw::Frame::create(),frame=rw::Frame::create();root->addChild(frame);
 auto atomic=rw::Atomic::create();atomic->setFrame(frame);atomic->setGeometry(source,0);
 carglass::Mesh mesh;Check(mesh.Read(source,atomic,root),"production mesh reads real geometry");
 size_t bytes=0;std::printf("real mesh builder: source strip=%d, broken=%d, glass only=%d\n",strip,broken,glassOnly);std::fflush(stdout);
 std::vector<Vec3> offsets;if(dented){offsets.resize(size_t(nv));for(int i=4;i<nv;i++)offsets[i]={.15f,0,0};}
 auto copy=static_cast<rw::Geometry *>(carglass::Copy(mesh,broken?glassmodel::Bit(glassmodel::Pane::LeftFront):0,bytes,carglass::kBudget,&offsets));
 Check(copy&&copy->meshHeader,"production copy rebuilt a real native mesh");
 Check(copy->numTriangles==nt&&copy->numVertices==nv,"triangle ordinals and vertex count retained");
 Check(source->flags==flags&&!std::memcmp(source->triangles,triangles,size_t(nt)*sizeof(triangles[0])),"shared original geometry untouched");
 for(int i=0;i<nv;i++){const auto p=copy->morphTargets[0].vertices[i];Check(std::fabs(p.x-(vertices[i].x+(dented&&i>=4?.15f:0)))<1e-6f&&p.y==vertices[i].y&&p.z==vertices[i].z,"private metal offsets applied; glass stays rigid");}
 if(dented){Check(copy->morphTargets[0].boundingSphere.radius>=source->morphTargets[0].boundingSphere.radius+.15f,"dented real mesh has conservative sphere");Shadow(atomic,copy);}
 for(int i=0;i<nt;i++){
  Check(copy->matList.materials[copy->triangles[i].matId]==source->matList.materials[source->triangles[i].matId],"material mapping retained");
  const auto &t=copy->triangles[i];Check((t.v[0]==t.v[1]&&t.v[0]==t.v[2])==(broken&&i<2),"only broken glass collapsed");
 }
 if(broken){Check(!(copy->flags&rw::Geometry::TRISTRIP)&&!(copy->meshHeader->flags&rw::MeshHeader::TRISTRIP),"broken copy uses triangle lists");Check(copy->meshHeader->totalIndices==unsigned(nt*3),"all triangle-list indices retained");}
 std::vector<skinmodel::Hole> holes{{{-.8f,.5f,.5f},{1,0,0},.07f}};size_t skinBytes=0;
 auto skin=static_cast<rw::Geometry *>(carglass::CopySkin(mesh,holes,skinBytes,carglass::kBudget));Check(skin&&skin->meshHeader,"perforated skin builds real librw triangle-list topology");
 Check(skin->numTriangles>nt&&skin->meshHeader->totalIndices==unsigned(skin->numTriangles*3),"real coarse source retains triangulated surface surrounding hole");
 for(int i=0;i<skin->numVertices;i++){auto n=skin->morphTargets[0].normals[i];Check(std::isfinite(n.x)&&std::fabs(n.x-1)<1e-5f,"interpolated real skin normals retained");}
 skin->destroy();copy->destroy();atomic->destroy();source->destroy();root->destroyHierarchy();glass->destroy();metal->destroy();
}
alignas(16) uint8_t vehicle[0x6f0]{},pool[24]{};uint8_t allocation=1;void *poolAddress=pool;
uint8_t *GetVehicle(int ref){return ref==1?vehicle:nullptr;}
alignas(16) uint8_t physicalPiece[0x1e8]{};int16_t temporaryPieces=0;int physicalAdds=0,sourceRefs=0;uint32_t physicsTime=1000;uint8_t sourceInfo[0x80]{},objectInfo[0x80]{};uint8_t *physicsModels[6500]{};
void BindDebris()
{
 auto &n=airframedebris::n;airframedebris::gReady=true;game::api.AircraftDebris=airframedebris::Emit;game::api.timeMs=&physicsTime;game::api.modelInfoPtrs=physicsModels;physicsModels[245]=objectInfo;
 n.temporary=&temporaryPieces;n.reference=[](uint8_t *v){return v==vehicle?1:-1;};
 n.allocate=[](size_t size)->void *{Check(size==sizeof physicalPiece,"exact native CObject allocation");return temporaryPieces?nullptr:physicalPiece;};n.construct=[](uint8_t *o){std::memset(o,0,sizeof physicalPiece);o[0x1b5]=1;o[0x1b6]=0x5a;};
 n.destroy=[](uint8_t *o){auto c=*reinterpret_cast<rw::Clump **>(o+0x58);if(c)c->destroy();if(o[0x1b4]==3)--temporaryPieces;--sourceRefs;std::memset(o,0,sizeof physicalPiece);};
 n.setModel=[](uint8_t *o,unsigned model){Check(model==245,"native collision proxy for physical component");*reinterpret_cast<int16_t *>(o+0x6a)=int16_t(model);};n.referenceModel=[](uint8_t *,int model){Check(model==game::EntityModel(vehicle),"debris pins the actual aircraft's model and textures");++sourceRefs;};
 n.attach=[](uint8_t *o,void *c){auto clump=static_cast<rw::Clump *>(c);*reinterpret_cast<void **>(o+0x58)=c;std::memcpy(o+8,&clump->getFrame()->matrix,64);};
 n.add=[](uint8_t *o){++physicalAdds;Check(o[0x1b4]==3&&temporaryPieces==1&&*reinterpret_cast<uint32_t *>(o+0x1c8)==physicsTime+12000,"native temporary lifetime and count belong to the object");Check(*reinterpret_cast<float *>(o+0xcc)>0&&reinterpret_cast<Vec3 *>(o+0x84)->z>0,"native debris has mass and separation velocity");};
 n.createClump=[]()->void *{return rw::Clump::create();};n.destroyClump=[](void *c){static_cast<rw::Clump *>(c)->destroy();return 1;};n.createFrame=[]()->void *{return rw::Frame::create();};n.destroyFrame=[](void *f){static_cast<rw::Frame *>(f)->destroy();return 1;};
 n.createAtomic=[]()->void *{return rw::Atomic::create();};n.destroyAtomic=[](void *a){static_cast<rw::Atomic *>(a)->destroy();return 1;};n.setClumpFrame=[](void *c,void *f)->void *{static_cast<rw::Clump *>(c)->setFrame(static_cast<rw::Frame *>(f));return c;};n.addChild=[](void *p,void *c)->void *{static_cast<rw::Frame *>(p)->addChild(static_cast<rw::Frame *>(c));return p;};n.addAtomic=[](void *c,void *a)->void *{static_cast<rw::Clump *>(c)->addAtomic(static_cast<rw::Atomic *>(a));return c;};n.setAtomicFrame=[](void *a,void *f)->void *{static_cast<rw::Atomic *>(a)->setFrame(static_cast<rw::Frame *>(f));return a;};n.setGeometry=carglass::rw.setGeometry;n.setFlags=[](void *a,unsigned flags)->void *{static_cast<rw::Atomic *>(a)->setFlags(flags);return a;};n.update=[](void *f)->void *{static_cast<rw::Frame *>(f)->updateObjects();return f;};n.matrix=carglass::rw.matrix;n.verticesCount=carglass::rw.nv;n.morph=carglass::rw.morph;n.vertices=[](const void *m){return reinterpret_cast<const Vec3 *>(static_cast<const rw::MorphTarget *>(m)->vertices);};n.renderCallback=[](void *,void *){};
}
void StockSkin(const char *file,int model)
{
 const char *data=std::getenv("GAMEDATA");if(!data)data="/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";
 ImgArchive img;Check(img.Open(std::string(data)+"/models/gta3.img"),"stock IMG opens");std::vector<uint8_t> bytes;Check(img.Read(file,bytes),"stock helicopter DFF exists");
 rw::Texture::setLoadTextures(false);rw::StreamMemory stream;stream.open(bytes.data(),uint32_t(bytes.size()));
 Check(rw::findChunk(&stream,rw::ID_CLUMP,nullptr,nullptr),"stock clump found");auto clump=rw::Clump::streamRead(&stream);Check(clump,"stock clump reads into real RenderWare");
 physicsModels[model]=sourceInfo;
 std::memset(vehicle,0,sizeof vehicle);*reinterpret_cast<void **>(pool)=vehicle;*reinterpret_cast<void **>(pool+8)=&allocation;*reinterpret_cast<int *>(pool+16)=1;
 *reinterpret_cast<void **>(vehicle+0x58)=clump;*reinterpret_cast<unsigned *>(vehicle+0x60)=2;*reinterpret_cast<int16_t *>(vehicle+0x6a)=int16_t(model);vehicle[0x297]=0x40;*reinterpret_cast<int *>(vehicle+0x348)=model==181?4:model==195?1:0;
 auto matrix=reinterpret_cast<float *>(vehicle+8);matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;
 carglass::gPool=&poolAddress;carglass::gReady=true;gConfig.carDeformation=true;game::api.PoolsGetVehicle=GetVehicle;
 unsigned tested=0;
 for(auto link=clump->atomics.link.next;link!=&clump->atomics.link;link=link->next){
  auto atomic=rw::Atomic::fromClump(link);if(!(atomic->getFlags()&4))continue;auto source=atomic->geometry;auto original=std::vector<rw::Triangle>(source->triangles,source->triangles+source->numTriangles);
  for(int t=0;t<source->numTriangles;t++){
   const auto tri=source->triangles[t];auto mat=source->matList.materials[tri.matId];if(mat->color.alpha!=255)continue;
   const auto verts=source->morphTargets[0].vertices;const Vec3 a{verts[tri.v[0]].x,verts[tri.v[0]].y,verts[tri.v[0]].z},b{verts[tri.v[1]].x,verts[tri.v[1]].y,verts[tri.v[1]].z},c{verts[tri.v[2]].x,verts[tri.v[2]].y,verts[tri.v[2]].z};
   auto n=carglass::Cross(carglass::Sub(b,a),carglass::Sub(c,a));const float len=std::sqrt(skinmodel::Dot(n,n));if(len<.03f)continue;n={n.x/len,n.y/len,n.z/len};const Vec3 centre{(a.x+b.x+c.x)/3,(a.y+b.y+c.y)/3,(a.z+b.z+c.z)/3};
   Vec3 hit,origin,normalPoint;Check(carglass::InCar(atomic->getFrame(),clump->getFrame(),centre,hit)&&carglass::InCar(atomic->getFrame(),clump->getFrame(),{},origin)&&carglass::InCar(atomic->getFrame(),clump->getFrame(),n,normalPoint),"stock frame hierarchy transforms hit");const auto normal=carglass::Sub(normalPoint,origin);
   Check(carglass::Perforate(vehicle,atomic,hit,normal,80),"stock helicopter accepts production private skin perforation");auto copy=atomic->geometry;
   Check(copy!=source&&copy->meshHeader&&!(copy->flags&rw::Geometry::TRISTRIP),"stock hole builds a private native triangle list");
   Check(!std::memcmp(original.data(),source->triangles,original.size()*sizeof(rw::Triangle)),"stock shared triangles preserved");
   bool covered=false;for(int j=0;j<copy->numTriangles;j++){auto face=copy->triangles[j];auto v=copy->morphTargets[0].vertices;std::array<Vec3,3> p;for(int k=0;k<3;k++)p[k]={v[face.v[k]].x,v[face.v[k]].y,v[face.v[k]].z};
    // Restrict to the original sheet; the opposite cabin wall can remain.
    if(std::fabs(skinmodel::Dot(carglass::Sub(p[0],centre),n))>.005f)continue;
    const auto cross0=carglass::Cross(carglass::Sub(p[1],p[0]),carglass::Sub(centre,p[0])),cross1=carglass::Cross(carglass::Sub(p[2],p[1]),carglass::Sub(centre,p[1])),cross2=carglass::Cross(carglass::Sub(p[0],p[2]),carglass::Sub(centre,p[2]));
    if(skinmodel::Dot(cross0,cross1)>=-1e-10f&&skinmodel::Dot(cross1,cross2)>=-1e-10f&&skinmodel::Dot(cross2,cross0)>=-1e-10f)covered=true;
   }Check(!covered,"actual stock skin has an open hole at impact, not a texture overlay");
   carglass::Restore(vehicle);Check(atomic->geometry==source,"stock repair restores shared original");++tested;break;
  }
 }
 if(model==227){dentmodel::Impact impact;Check(dentmodel::Make({-.11f,3.48f,.13f},{0,-1,0},29.3f,impact)&&carglass::Dent(vehicle,impact),"recorded Police Maverick nose impact deforms actual stock fuselage geometry");carglass::Restore(vehicle);}
 const int beforeParts=physicalAdds;Check(carglass::SeverSection(vehicle,{0,-1,0},0),"stock aircraft whole tail skin splits across all source atomics");Check(physicalAdds==beforeParts+1&&temporaryPieces==1,"stock split produces one real RenderWare assembly on a native-shaped physical object");
 auto debris=*reinterpret_cast<rw::Clump **>(physicalPiece+0x58);Check(debris&&debris->countAtomics()>0,"physical detached section retains real source materials and triangles");
 Check(physicalPiece[0x1b5]==0x80&&physicalPiece[0x1b6]==0x5a,"debris flag byte preserves adjacent packed fields");
 carglass::Restore(vehicle);airframedebris::n.destroy(physicalPiece);Check(temporaryPieces==0&&sourceRefs==0,"native debris cleanup releases geometry and parent model references");
 Check(tested>0,"stock helicopter tested actual opaque skin");std::printf("stock skin: %s %u atomics passed\n",file,tested);clump->destroy();
}
void StockCar(const char *file,int model){
 const char *env=std::getenv("GAMEDATA");const std::string base=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";ImgArchive img;Check(img.Open(base+"/models/gta3.img"),"real car archive opens");std::vector<uint8_t> bytes;Check(img.Read(file,bytes),"real car DFF reads");rw::StreamMemory stream;stream.open(bytes.data(),rw::uint32(bytes.size()));Check(rw::findChunk(&stream,rw::ID_CLUMP,nullptr,nullptr),"real car clump found");auto clump=rw::Clump::streamRead(&stream);Check(clump,"real car loads with native frame names");
 std::memset(vehicle,0,sizeof vehicle);*reinterpret_cast<void **>(pool)=vehicle;*reinterpret_cast<void **>(pool+8)=&allocation;*reinterpret_cast<int *>(pool+16)=1;*reinterpret_cast<void **>(vehicle+0x58)=clump;*reinterpret_cast<unsigned *>(vehicle+0x60)=2;*reinterpret_cast<int16_t *>(vehicle+0x6a)=int16_t(model);vehicle[0x297]=64;auto m=reinterpret_cast<float *>(vehicle+8);m[0]=m[5]=m[10]=m[15]=1;carglass::gPool=&poolAddress;carglass::gReady=true;gConfig.carGlass=gConfig.carDeformation=true;game::api.PoolsGetVehicle=GetVehicle;
 struct Pane {void *atomic;int face;glassmodel::Pane pane;Vec3 hit,normal;};std::vector<Pane> panes;uint8_t selected=0;
 for(auto link=clump->atomics.link.next;link!=&clump->atomics.link;link=link->next){auto a=rw::Atomic::fromClump(link);if(!(a->getFlags()&4))continue;carglass::Mesh mesh;if(!mesh.Read(a->geometry,a,clump->getFrame()))continue;
  for(int i=0;i<mesh.nt;i++){Vec3 p[3];const auto pane=mesh.Face(i,p);const auto bit=glassmodel::Bit(pane);if(pane==glassmodel::Pane::None||pane==glassmodel::Pane::Front||(selected&bit))continue;selected|=bit;panes.push_back({a,i,pane,{(p[0].x+p[1].x+p[2].x)/3,(p[0].y+p[1].y+p[2].y)/3,(p[0].z+p[1].z+p[2].z)/3},carglass::Cross(carglass::Sub(p[1],p[0]),carglass::Sub(p[2],p[0]))});}
 }
 Check(panes.size()>=2,"actual car exposes its modeled side/rear glazing");
 for(const auto &pane:panes){carglass::Bullet(vehicle,pane.atomic,pane.face,pane.hit,pane.normal);const auto car=std::find_if(carglass::gCars.begin(),carglass::gCars.end(),[](const auto &c){return c.car==vehicle;});Check(car!=carglass::gCars.end()&&(car->broken&glassmodel::Bit(pane.pane)),"actual side/rear impact commits its pane");
  for(auto link=clump->atomics.link.next;link!=&clump->atomics.link;link=link->next){auto a=rw::Atomic::fromClump(link);carglass::Mesh mesh;if(!mesh.Read(a->geometry,a,clump->getFrame()))continue;for(int i=0;i<mesh.nt;i++)Check(mesh.Face(i)!=pane.pane,"all actual rendered and damaged variants lose the struck pane");}
 }
 std::printf("real car panes: %s %zu passed\n",file,panes.size());carglass::Restore(vehicle);clump->destroy();
}

}
int main()
{
 Check(rw::Engine::init(),"NULL renderer initialized");RegisterFrameNames();Check(rw::Engine::open(nullptr)&&rw::Engine::start(),"NULL renderer starts");Bind();BindDebris();
 for(bool strip:{false,true})for(bool glassOnly:{false,true})for(bool broken:{false,true})for(bool dented:{false,true})Copy(strip,broken,glassOnly,dented);
 StockSkin("sparrow.dff",199);StockSkin("maverick.dff",217);StockSkin("polmav.dff",227);StockSkin("deaddodo.dff",181);StockSkin("skimmer.dff",195);
 StockCar("deluxo.dff",211);StockCar("hermes.dff",204);StockCar("benson.dff",229);
 Check(rw::Geometry::numAllocated==0&&rw::Material::numAllocated==0,"all native geometry/material references released");
 rw::Engine::stop();rw::Engine::close();rw::Engine::term();std::puts("carglass real mesh builder: all passed");
}

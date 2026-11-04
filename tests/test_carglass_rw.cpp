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
#include "../src/carglass.cpp"

namespace game {Api api{};}
Config gConfig;
void Log(const char *,...){}
namespace hooks {void *Chain(const char *,void *){return nullptr;}}

namespace {
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
 r.frame=[](const void *a)->void *{return static_cast<const rw::Atomic *>(a)->getFrame();};
 r.name=[](void *){return "door_lf_hi_ok";};
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
}
void Copy(bool strip,bool broken,bool glassOnly)
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
 auto copy=static_cast<rw::Geometry *>(carglass::Copy(mesh,broken?glassmodel::Bit(glassmodel::Pane::LeftFront):0,bytes,carglass::kBudget));
 Check(copy&&copy->meshHeader,"production copy rebuilt a real native mesh");
 Check(copy->numTriangles==nt&&copy->numVertices==nv,"triangle ordinals and vertex count retained");
 Check(source->flags==flags&&!std::memcmp(source->triangles,triangles,size_t(nt)*sizeof(triangles[0])),"shared original geometry untouched");
 Check(!std::memcmp(copy->morphTargets[0].vertices,vertices,size_t(nv)*sizeof(vertices[0])),"vertex data retained");
 for(int i=0;i<nt;i++){
  Check(copy->matList.materials[copy->triangles[i].matId]==source->matList.materials[source->triangles[i].matId],"material mapping retained");
  const auto &t=copy->triangles[i];Check((t.v[0]==t.v[1]&&t.v[0]==t.v[2])==(broken&&i<2),"only broken glass collapsed");
 }
 if(broken){Check(!(copy->flags&rw::Geometry::TRISTRIP)&&!(copy->meshHeader->flags&rw::MeshHeader::TRISTRIP),"broken copy uses triangle lists");Check(copy->meshHeader->totalIndices==unsigned(nt*3),"all triangle-list indices retained");}
 copy->destroy();atomic->destroy();source->destroy();root->destroyHierarchy();glass->destroy();metal->destroy();
}
}
int main()
{
 Check(rw::Engine::init()&&rw::Engine::open(nullptr)&&rw::Engine::start(),"NULL renderer initialized");Bind();
 for(bool strip:{false,true})for(bool glassOnly:{false,true})for(bool broken:{false,true})Copy(strip,broken,glassOnly);
 Check(rw::Geometry::numAllocated==0&&rw::Material::numAllocated==0,"all native geometry/material references released");
 rw::Engine::stop();rw::Engine::close();rw::Engine::term();std::puts("carglass real mesh builder: all passed");
}

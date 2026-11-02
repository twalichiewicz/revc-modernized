// Actual Learjet DFF -> real model preparation -> native-shaped geometry and
// material ownership/render consumer. No pre-labelled glass/wheel fixture.
#include "../tools/bake/gamefiles.h"
#include "../src/airportjetcollision.h"
#include <dlfcn.h>
static void *ModelSymbol(void *,const char *);
const float *LTM(void *);
#define dlsym ModelSymbol
#include "../src/airportjetmodel.cpp"
#undef dlsym
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <mach-o/loader.h>

namespace {
int failures=0,liveGeometry=0,liveMaterial=0,liveFrame=0,liveAtomic=0,allocation=0,failAt=0,invalidUnlocks=0;bool failAllocation=false,missingBuffer=false;
bool AllocationFails(){++allocation;return failAllocation||(failAt&&allocation==failAt);}
#define CHECK(c) do{if(!(c)){++failures;std::printf("FAIL %d: %s\n",__LINE__,#c);}}while(0)
struct Color {uint8_t r=255,g=255,b=255,a=255;};
struct Tex {std::string name;};
struct Mat {int refs=1;Color color{};Tex *texture=nullptr;};
struct Tri {uint16_t v[3],material;};
struct Geo {int refs=1;uint32_t flags=0,lastLock=0;bool missing=false;std::vector<CVector> vertices,normals;std::vector<CVector2D> uv;std::vector<Color> colors;std::vector<Tri> triangles;std::vector<Mat *> materials;};
struct Frame {char name[24]{};float matrix[16]{},ltm[16]{};intptr_t visibility=0;Frame *parent=nullptr;std::vector<Frame *> children;};
struct Atomic {Geo *geometry=nullptr;Frame *frame=nullptr;uint32_t flags=5;};
struct Clump {Frame *root;std::vector<Atomic *> atomics;};
template<class T>T Get(const std::vector<uint8_t> &b,size_t p){T v{};CHECK(p+sizeof v<=b.size());if(p+sizeof v<=b.size())std::memcpy(&v,b.data()+p,sizeof v);return v;}
struct Chunk {uint32_t id,version;size_t a,z;};
std::vector<Chunk> Chunks(const std::vector<uint8_t> &b,size_t a,size_t z){std::vector<Chunk> out;while(a+12<=z&&a+12<=b.size()){const size_t end=a+12+Get<uint32_t>(b,a+4);if(end>z||end>b.size())break;out.push_back({Get<uint32_t>(b,a),Get<uint32_t>(b,a+8),a+12,end});a=end;}return out;}
void *MaterialCreate(){if(AllocationFails())return nullptr;++liveMaterial;return new Mat;}
bool MaterialDestroy(void *p){auto *m=static_cast<Mat *>(p);if(!--m->refs){delete m;--liveMaterial;}return true;}
void *GeometryCreate(int nv,int nt,uint32_t flags){if(AllocationFails())return nullptr;auto *g=new Geo;++liveGeometry;g->flags=flags;g->missing=missingBuffer;g->vertices.resize(nv);g->normals.resize(nv);g->uv.resize(nv);g->colors.resize(nv);g->triangles.resize(nt);for(auto &t:g->triangles)t.material=0xffff;return g;}
bool GeometryDestroy(void *p){auto *g=static_cast<Geo *>(p);if(!--g->refs){for(auto *m:g->materials)MaterialDestroy(m);delete g;--liveGeometry;}return true;}
void *GeometryLock(void *p,int flags){static_cast<Geo *>(p)->lastLock=uint32_t(flags);return p;}void *GeometryUnlock(void *p){auto *g=static_cast<Geo *>(p);for(const auto &t:g->triangles)if(t.material>=g->materials.size()){++invalidUnlocks;return nullptr;}return p;}
int VertexCount(const void *p){return int(static_cast<const Geo *>(p)->vertices.size());}int TriangleCount(const void *p){return int(static_cast<const Geo *>(p)->triangles.size());}
void *Morph(const void *p,int){return const_cast<void *>(p);}const CVector *Vertices(const void *p){const auto *g=static_cast<const Geo *>(p);return g->missing?nullptr:g->vertices.data();}const CVector *Normals(const void *p){return static_cast<const Geo *>(p)->normals.data();}
const CVector2D *UV(const void *p,int index){return index==1?static_cast<const Geo *>(p)->uv.data():nullptr;}const Color *Colors(const void *p){return static_cast<const Geo *>(p)->colors.data();}const Tri *Triangles(const void *p){return static_cast<const Geo *>(p)->triangles.data();}
void *GetMaterial(const void *p,int i){auto *g=static_cast<const Geo *>(p);return i>=0&&size_t(i)<g->materials.size()?g->materials[size_t(i)]:nullptr;}
void *TriangleMaterial(void *p,void *triangle,void *material){auto *g=static_cast<Geo *>(p);auto *m=static_cast<Mat *>(material);auto found=std::find(g->materials.begin(),g->materials.end(),m);if(found==g->materials.end()){if(AllocationFails())return nullptr;g->materials.push_back(m);++m->refs;found=g->materials.end()-1;}static_cast<Tri *>(triangle)->material=uint16_t(found-g->materials.begin());return g;}
uint32_t Flags(const void *p){return static_cast<const Geo *>(p)->flags;}void SetFlags(void *p,uint32_t v){static_cast<Geo *>(p)->flags=v;}
void SetColor(void *p,const Color *c){static_cast<Mat *>(p)->color=*c;}const Color *GetColor(const void *p){return &static_cast<const Mat *>(p)->color;}
void *Texture(const void *p){return static_cast<const Mat *>(p)->texture;}const char *TextureName(void *p){return static_cast<Tex *>(p)->name.c_str();}
void *FrameCreate(){if(AllocationFails())return nullptr;auto *f=new Frame;++liveFrame;f->matrix[0]=f->matrix[5]=f->matrix[10]=f->matrix[15]=1;return f;}
bool FrameDestroy(void *p){auto *f=static_cast<Frame *>(p);if(f->parent){auto &siblings=f->parent->children;siblings.erase(std::remove(siblings.begin(),siblings.end(),f),siblings.end());}while(!f->children.empty())FrameDestroy(f->children.back());delete f;--liveFrame;return true;}
float *Matrix(void *p){return static_cast<Frame *>(p)->matrix;}char *FrameName(void *p){return static_cast<Frame *>(p)->name;}
void *FrameChild(void *p,void *c){auto *parent=static_cast<Frame *>(p),*child=static_cast<Frame *>(c);parent->children.push_back(child);child->parent=parent;return p;}
void *Children(void *p,void *(*callback)(void *,void *),void *data){for(auto *c:static_cast<Frame *>(p)->children)callback(c,data);return p;}
void *UpdateFrame(void *p){return p;}void HierarchyId(void *f,intptr_t id){static_cast<Frame *>(f)->visibility=id;}
void *AtomicCreate(){if(AllocationFails())return nullptr;++liveAtomic;return new Atomic;}
bool AtomicDestroy(void *p){auto *a=static_cast<Atomic *>(p);if(a->geometry)GeometryDestroy(a->geometry);delete a;--liveAtomic;return true;}
uint32_t AtomicFlags(const void *p){return static_cast<const Atomic *>(p)->flags;}
void *AtomicSetFlags(void *p,uint32_t flags){static_cast<Atomic *>(p)->flags=flags;return p;}
void *AtomicGeometry(const void *p){return static_cast<const Atomic *>(p)->geometry;}void *AtomicFrame(const void *p){return static_cast<const Atomic *>(p)->frame;}
void *SetGeometry(void *p,void *g,uint32_t){auto *a=static_cast<Atomic *>(p);if(a->geometry)GeometryDestroy(a->geometry);a->geometry=static_cast<Geo *>(g);++a->geometry->refs;return p;}
void *SetFrame(void *p,void *f){static_cast<Atomic *>(p)->frame=static_cast<Frame *>(f);return p;}
void *ClumpFrame(const void *p){return static_cast<const Clump *>(p)->root;}
void *Each(void *p,void *(*callback)(void *,void *),void *data){for(auto *a:static_cast<Clump *>(p)->atomics)if(!callback(a,data))break;return p;}
void *AddAtomic(void *p,void *a){static_cast<Clump *>(p)->atomics.push_back(static_cast<Atomic *>(a));return p;}
void Sphere(void *,void *){}
void Destroy(Clump &c){for(auto *a:c.atomics)AtomicDestroy(a);c.atomics.clear();FrameDestroy(c.root);}
Geo *Load(const char *entry="ap_learjet1_01.dff",int geometryIndex=0){
 const char *env=std::getenv("GAMEDATA");const std::string base=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";
 ImgArchive archive;CHECK(archive.Open(base+"/models/gta3.img"));std::vector<uint8_t> bytes;CHECK(archive.Read(entry,bytes));
 Chunk geometry{};int index=0;for(auto c:Chunks(bytes,12,bytes.size()))if(c.id==26)for(auto g:Chunks(bytes,c.a,c.z))if(g.id==15&&index++==geometryIndex)geometry=g;
 const auto kids=Chunks(bytes,geometry.a,geometry.z);CHECK(!kids.empty());if(kids.empty())return nullptr;const auto st=kids.front();size_t p=st.a;const uint32_t flags=Get<uint32_t>(bytes,p),nt=Get<uint32_t>(bytes,p+4),nv=Get<uint32_t>(bytes,p+8);p+=16;
 const uint32_t lib=st.version&0xffff0000u?(((st.version>>14)&0x3ff00u)+0x30000u)|((st.version>>16)&0x3fu):st.version<<8;if(lib<0x34000)p+=12;
 auto *g=static_cast<Geo *>(GeometryCreate(nv,nt,flags));if(flags&8){std::memcpy(g->colors.data(),bytes.data()+p,nv*4);p+=nv*4;}
 std::memcpy(g->uv.data(),bytes.data()+p,nv*8);p+=nv*8;
 for(uint32_t i=0;i<nt;++i,p+=8){const uint16_t b=Get<uint16_t>(bytes,p),a=Get<uint16_t>(bytes,p+2),m=Get<uint16_t>(bytes,p+4),c=Get<uint16_t>(bytes,p+6);g->triangles[i]={{a,b,c},m};}
 p+=16;const auto hasVertices=Get<uint32_t>(bytes,p),hasNormals=Get<uint32_t>(bytes,p+4);p+=8;CHECK(hasVertices);std::memcpy(g->vertices.data(),bytes.data()+p,nv*12);p+=nv*12;
 if(hasNormals)std::memcpy(g->normals.data(),bytes.data()+p,nv*12);
 static std::map<std::string,Tex> textures;
 for(auto list:kids)if(list.id==8)for(auto material:Chunks(bytes,list.a,list.z))if(material.id==7){auto *m=static_cast<Mat *>(MaterialCreate());
  for(auto child:Chunks(bytes,material.a,material.z)){if(child.id==1)m->color=Get<Color>(bytes,child.a+4);
   if(child.id==6)for(auto text:Chunks(bytes,child.a,child.z))if(text.id==2){const std::string name(reinterpret_cast<const char *>(bytes.data()+text.a),strnlen(reinterpret_cast<const char *>(bytes.data()+text.a),text.z-text.a));if(!name.empty()){auto &texture=textures[name];texture.name=name;m->texture=&texture;}break;}
  }g->materials.push_back(m);
 }
 return g;
}
Frame *Find(Frame *root,const char *name){if(!std::strcmp(root->name,name))return root;for(auto *f:root->children)if(auto *found=Find(f,name))return found;return nullptr;}
Atomic *Part(Clump &c,const char *name){for(auto *a:c.atomics)if(!std::strcmp(a->frame->name,name))return a;return nullptr;}
uint64_t GeometryFingerprint(const Geo &g){
 uint64_t hash=1469598103934665603ull;auto bytes=[&](const void *p,size_t count){const auto *v=static_cast<const uint8_t *>(p);for(size_t i=0;i<count;++i){hash^=v[i];hash*=1099511628211ull;}};
 bytes(&g.flags,sizeof g.flags);bytes(g.vertices.data(),g.vertices.size()*sizeof(CVector));bytes(g.normals.data(),g.normals.size()*sizeof(CVector));bytes(g.uv.data(),g.uv.size()*sizeof(CVector2D));bytes(g.colors.data(),g.colors.size()*sizeof(Color));bytes(g.triangles.data(),g.triangles.size()*sizeof(Tri));for(auto *material:g.materials){bytes(&material->color,sizeof(Color));if(material->texture)bytes(material->texture->name.data(),material->texture->name.size());}return hash;
}
void NativeContract(){
 const char *env=std::getenv("GAMEDATA");const std::string base=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";
 std::vector<uint8_t> bytes;CHECK(ReadGameFile(base+"/../../MacOS/macVC",bytes));if(bytes.size()<sizeof(mach_header_64))return;
 const auto header=Get<mach_header_64>(bytes,0);CHECK(header.magic==MH_MAGIC_64);size_t offset=sizeof header;bool found=false;
 for(uint32_t i=0;i<header.ncmds&&offset+sizeof(load_command)<=bytes.size();++i){const auto command=Get<load_command>(bytes,offset);if(command.cmdsize<8||offset+command.cmdsize>bytes.size())break;
  if(command.cmd==LC_SEGMENT_64){const auto segment=Get<segment_command_64>(bytes,offset);constexpr uint64_t address=0x100170b1c;
   if(address>=segment.vmaddr&&address+4<=segment.vmaddr+segment.filesize){
    // Actual exported RpGeometryGetVertexTexCoords: cbz w1,returnNull.
    // Native rwTEXTURECOORDINATEINDEX0 therefore starts at1, never0.
    CHECK(Get<uint32_t>(bytes,size_t(segment.fileoff+address-segment.vmaddr))==0x34000081);CHECK(Get<uint32_t>(bytes,size_t(segment.fileoff+0x100170e78-segment.vmaddr))==0x39000801);CHECK(Get<uint32_t>(bytes,size_t(segment.fileoff+0x100170e80-segment.vmaddr))==0x39400800);found=true;
    // Installed GL3 path: Lock ORs attribute dirty bits into Geometry+0x14;
    // Unlock preserves them; instance tests and uploads current morph data.
    const std::pair<uint64_t,uint32_t> upload[]={
     {0x10029d51c,0x79402808},{0x10029d520,0x2a010108},{0x10029d524,0x79002808},{0x10029d528,0x360001c1},
     {0x10029d564,0xf9404808},{0x10029d568,0xb4000048},{0x10029d570,0x14000001},
     {0x1002be574,0x79402a68},{0x1002be578,0x34000e48},{0x1002be584,0x52800022},{0x1002be740,0x79002a7f},
     {0x1002be890,0x79402aa8},{0x1002be894,0x37081468},{0x1002be89c,0x371015e8},{0x1002beb90,0x361802a8},{0x1002bec14,0x3627ff49},
     {0x1002bec70,0xb9403e69},{0x1002bec74,0xb94012ea},{0x1002bec78,0x1b097d41},{0x1002bec7c,0xf9401262},{0x1002bec88,0xd63f0100}};
    for(auto proof:upload){CHECK(proof.first>=segment.vmaddr&&proof.first+4<=segment.vmaddr+segment.filesize);CHECK(Get<uint32_t>(bytes,size_t(segment.fileoff+proof.first-segment.vmaddr))==proof.second);}

   }
  }offset+=command.cmdsize;
 }CHECK(found);
}
}
static void *ModelSymbol(void *,const char *name){
#define B(n,f) if(std::strstr(name,n))return reinterpret_cast<void *>(f)
 B("RpGeometryCreate",GeometryCreate);B("RpGeometryDestroy",GeometryDestroy);B("RpGeometryLock",GeometryLock);B("RpGeometryUnlock",GeometryUnlock);
 B("RpGeometryGetNumVertices",VertexCount);B("RpGeometryGetNumTriangles",TriangleCount);B("RpGeometryGetMorphTarget",Morph);B("RpMorphTargetGetVertices",Vertices);B("RpMorphTargetGetVertexNormals",Normals);B("RpGeometryGetVertexTexCoords",UV);B("RpGeometryGetPreLightColors",Colors);B("RpGeometryGetTriangles",Triangles);B("RpGeometryGetMaterial",GetMaterial);B("RpGeometryTriangleSetMaterial",TriangleMaterial);B("RpGeometryGetFlags",Flags);B("RpGeometrySetFlags",SetFlags);
 B("RpMaterialCreate",MaterialCreate);B("RpMaterialDestroy",MaterialDestroy);B("RpMaterialSetColor",SetColor);B("RpMaterialGetColor",GetColor);B("RpMaterialGetTexture",Texture);B("RwTextureGetName",TextureName);
 B("RwFrameCreate",FrameCreate);B("RwFrameDestroy",FrameDestroy);B("RwFrameGetMatrix",Matrix);B("RwFrameGetLTM",LTM);B("RwFrameAddChild",FrameChild);B("RwFrameForAllChildren",Children);B("RwFrameUpdateObjects",UpdateFrame);B("GetFrameNodeName",FrameName);B("SetFrameHierarchyId",HierarchyId);
 B("RpAtomicGetFlags",AtomicFlags);B("RpAtomicSetFlags",AtomicSetFlags);B("RpAtomicCreate",AtomicCreate);B("RpAtomicDestroy",AtomicDestroy);B("RpAtomicGetGeometry",AtomicGeometry);B("RpAtomicGetFrame",AtomicFrame);B("RpAtomicSetGeometry",SetGeometry);B("RpAtomicSetFrame",SetFrame);B("RpClumpGetFrame",ClumpFrame);B("RpClumpForAllAtomics",Each);B("RpClumpAddAtomic",AddAtomic);B("MorphTargetCalcBoundingSphere",Sphere);B("MorphTargetSetBoundingSphere",Sphere);
#undef B
 return nullptr;
}
const float *LTM(void *p){
 auto *f=static_cast<Frame *>(p);if(!f->parent)return f->matrix;const float *parent=LTM(f->parent),*local=f->matrix;float *out=f->ltm;
 for(int column=0;column<3;++column)for(int row=0;row<3;++row)out[column*4+row]=parent[row]*local[column*4]+parent[4+row]*local[column*4+1]+parent[8+row]*local[column*4+2];
 for(int row=0;row<3;++row)out[12+row]=parent[row]*local[12]+parent[4+row]*local[13]+parent[8+row]*local[14]+parent[12+row];return out;
}
CVector ModelPoint(Frame *frame,CVector p){const float *m=LTM(frame);return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};}
Clump LoadCockpit(){
 Clump clump{static_cast<Frame *>(FrameCreate()),{}};std::strcpy(clump.root->name,"polmav");
 auto *parent=static_cast<Frame *>(FrameCreate()),*frame=static_cast<Frame *>(FrameCreate());std::strcpy(parent->name,"chassis_dummy");std::strcpy(frame->name,"chassis_hi");parent->matrix[12]=.1f;parent->matrix[13]=.2f;frame->matrix[12]=-.1f;frame->matrix[13]=-.2f;FrameChild(clump.root,parent);FrameChild(parent,frame);
 auto *atomic=static_cast<Atomic *>(AtomicCreate());atomic->geometry=Load("polmav.dff",1);atomic->frame=frame;clump.atomics.push_back(atomic);CHECK(atomic->geometry&&atomic->geometry->vertices.size()==2076&&atomic->geometry->triangles.size()==2079);return clump;
}
void CockpitProvenance(Clump &clump,Clump &donor,Geo *shell){
 auto *cabin=Part(clump,"jet_cabin");CHECK(cabin);if(!cabin)return;auto *g=cabin->geometry,*native=donor.atomics[0]->geometry;CHECK(g->triangles.size()==320);std::vector<bool> used(native->triangles.size());
 for(const auto &triangle:g->triangles){bool found=false;CHECK(g->materials[triangle.material]==native->materials[1]);
  for(size_t k=0;k<native->triangles.size()&&!found;++k){const auto &source=native->triangles[k];if(source.material!=1)continue;bool matches=true;
   for(int j=0;j<3;++j){auto p=g->vertices[triangle.v[j]];const auto q=native->vertices[source.v[j]];if((k>=567&&k<=578)||(k>=1168&&k<=1179))p.z-=.15f;const CVector back{p.x/.76f,(p.y-5)/.68f+1.476f,(p.z+.65f)/.85f-.09952587f};const auto a=g->uv[triangle.v[j]],b=native->uv[source.v[j]];
    matches=matches&&std::fabs(back.x-q.x)<.00001f&&std::fabs(back.y-q.y)<.00001f&&std::fabs(back.z-q.z)<.00001f&&std::fabs(a.x-b.x)<.00001f&&std::fabs(a.y-b.y)<.00001f;
   }if(matches){used[k]=true;found=true;}
  }CHECK(found);
 }
 // Independently audited original seat, instrument console, cyclic, floor
 // face indices. Replacing these with boxes or only a textured facade fails.
 for(int k=1963;k<=2078;++k)CHECK(used[size_t(k)]);
 for(int k:{12,13,527,549,1128,1150})CHECK(used[size_t(k)]);
 // The actual eye-height forward rays are above every imported inner face.
 // The separate Learjet windshield remains transparent and unmodified here.
 for(const auto &p:g->vertices)CHECK(p.x>=-.66f&&p.x<=.66f&&p.y>4.45f&&p.y<6.01f&&p.z> -1.04f&&p.z< -.05f);
 // Actual triangle ray containment catches a cabin fitted through the
 // narrow lower fuselage even when a simple cockpit bounding box would pass.
 auto minus=[](CVector a,CVector b){return CVector{a.x-b.x,a.y-b.y,a.z-b.z};};
 auto cross=[](CVector a,CVector b){return CVector{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
 auto dot=[](CVector a,CVector b){return a.x*b.x+a.y*b.y+a.z*b.z;};
 int outside=0;for(const auto &eye:g->vertices){std::vector<float> hits;const CVector direction{1,.000123f,.000257f};
  for(const auto &face:shell->triangles){const CVector a=shell->vertices[face.v[0]],e1=minus(shell->vertices[face.v[1]],a),e2=minus(shell->vertices[face.v[2]],a),h=cross(direction,e2);const float determinant=dot(e1,h);if(std::fabs(determinant)<.0000001f)continue;
   const float inverse=1/determinant;const CVector s=minus(eye,a);const float u=inverse*dot(s,h);if(u<0||u>1)continue;const CVector q=cross(s,e1);const float v=inverse*dot(direction,q);if(v<0||u+v>1)continue;const float distance=inverse*dot(e2,q);if(distance<=.00001f)continue;
   bool duplicate=false;for(float old:hits)duplicate=duplicate||std::fabs(distance-old)<.0001f;if(!duplicate)hits.push_back(distance);
  }outside+=hits.size()%2==0;
 }CHECK(outside==0);
 std::printf("Police Maverick cabin: %zu actual triangles, two58-face seats, atlas/UV provenance preserved\n",g->triangles.size());
}
float SurfaceArea(Geo *g){float area=0;for(const auto &face:g->triangles){const auto a=g->vertices[face.v[0]],b=g->vertices[face.v[1]],c=g->vertices[face.v[2]];const CVector x{b.x-a.x,b.y-a.y,b.z-a.z},y{c.x-a.x,c.y-a.y,c.z-a.z};const CVector cross{x.y*y.z-x.z*y.y,x.z*y.x-x.x*y.z,x.x*y.y-x.y*y.x};area+=.5f*std::sqrt(cross.x*cross.x+cross.y*cross.y+cross.z*cross.z);}return area;}
bool InsideFuselage(Geo *shell,CVector eye){
 using namespace airportjetcollision;bool positive=false,negative=false;const CVector direction{1,0,0};
 for(size_t i=0;i<shell->triangles.size();++i){
  // Audited shell bands, excluding overlapping window decals/wing/gear.
  if(!(i<=50||(i>=299&&i<=349)||i==1211||(i>=735&&i<=737)||(i>=214&&i<=224)||(i>=563&&i<=576)||(i>=1104&&i<=1111)||(i>=257&&i<=258)||(i>=693&&i<=694)||(i>=207&&i<=210)||(i>=550&&i<=556)||(i>=1112&&i<=1158)))continue;
  const auto &face=shell->triangles[i];const CVector a=shell->vertices[face.v[0]],e1=Sub(shell->vertices[face.v[1]],a),e2=Sub(shell->vertices[face.v[2]],a),h=airportjetmodel::Cross(direction,e2);const CVector delta=Sub(eye,a),normal=airportjetmodel::Cross(e1,e2);const double ee=Dot(e1,e1),ef=Dot(e1,e2),ff=Dot(e2,e2),den=ee*ff-ef*ef;if(den>1e-12&&std::fabs(Dot(delta,normal))/Length(normal)<.00003){const double u=(Dot(delta,e1)*ff-Dot(delta,e2)*ef)/den,v=(Dot(delta,e2)*ee-Dot(delta,e1)*ef)/den;if(u>=-.0001&&v>=-.0001&&u+v<=1.0001)return true;}const float determinant=Dot(e1,h);if(std::fabs(determinant)<.0000001f)continue;
  const float inverse=1/determinant;const CVector s=Sub(eye,a);const float u=inverse*Dot(s,h);if(u<0||u>1)continue;const CVector q=airportjetmodel::Cross(s,e1);const float v=inverse*Dot(direction,q);if(v<0||u+v>1)continue;const float distance=inverse*Dot(e2,q);if(std::fabs(distance)<.00003f)return true;positive=positive||distance>0;negative=negative||distance<0;
 }return positive&&negative;
}
bool SourceSkinHeight(Geo *source,CVector p,float &low,float &high){
 low=1e9f;high=-1e9f;for(const auto &t:source->triangles){const auto a=source->vertices[t.v[0]],b=source->vertices[t.v[1]],c=source->vertices[t.v[2]];const double ex=b.x-a.x,ey=b.y-a.y,fx=c.x-a.x,fy=c.y-a.y,det=ex*fy-ey*fx;if(std::fabs(det)<1e-10)continue;const double dx=p.x-a.x,dy=p.y-a.y,u=(dx*fy-dy*fx)/det,v=(ex*dy-ey*dx)/det;if(u<-.00001||v<-.00001||u+v>1.00001)continue;const float z=a.z+float(u)*(b.z-a.z)+float(v)*(c.z-a.z);low=std::min(low,z);high=std::max(high,z);}return high>low+.03f;
}
bool InsideAirframe(Geo *source,CVector p,float tolerance=.0001f){
 double low=1e9,high=-1e9;
 for(size_t i=0;i<source->triangles.size();++i){if((i>=211&&i<=213)||(i>=560&&i<=562)||(i>=577&&i<=660)||(i>=775&&i<=1047))continue;const auto &t=source->triangles[i];const CVector a=source->vertices[t.v[0]],b=source->vertices[t.v[1]],c=source->vertices[t.v[2]];const double x=b.x-a.x,y=b.y-a.y,u=c.x-a.x,v=c.y-a.y,det=x*v-y*u;if(std::fabs(det)<1e-10)continue;const double dx=p.x-a.x,dy=p.y-a.y,s=(dx*v-dy*u)/det,tv=(x*dy-y*dx)/det;if(s<-.00001||tv<-.00001||s+tv>1.00001)continue;const double z=a.z+s*(b.z-a.z)+tv*(c.z-a.z);low=std::min(low,z);high=std::max(high,z);
 }return p.z>low+tolerance&&p.z<high-tolerance;
}
float Ray(Clump &clump,CVector eye,CVector direction,const char *only=nullptr,bool opaqueOnly=false,bool frontOnly=false){
 using namespace airportjetcollision;float best=1e9f;
 for(auto *atomic:clump.atomics){if(!(atomic->flags&4))continue;if(only&&std::strcmp(atomic->frame->name,only))continue;auto *g=atomic->geometry;
  for(const auto &face:g->triangles){if(opaqueOnly&&g->materials[face.material]->color.a<100)continue;
   const CVector a=ModelPoint(atomic->frame,g->vertices[face.v[0]]),e1=Sub(ModelPoint(atomic->frame,g->vertices[face.v[1]]),a),e2=Sub(ModelPoint(atomic->frame,g->vertices[face.v[2]]),a),h=airportjetmodel::Cross(direction,e2);const float determinant=Dot(e1,h);if(std::fabs(determinant)<.0000001f||(frontOnly&&determinant<=0))continue;
   const float inverse=1/determinant;const CVector s=Sub(eye,a);const float u=inverse*Dot(s,h);if(u<0||u>1)continue;const CVector q=airportjetmodel::Cross(s,e1);const float v=inverse*Dot(direction,q);if(v<0||u+v>1)continue;const float distance=inverse*Dot(e2,q);if(distance>.00001f)best=std::min(best,distance);
  }
 }return best;
}
void Interior(Clump &clump,Geo *shell){
 auto *lining=Part(clump,"jet_lining");CHECK(lining);if(!lining)return;
 for(auto *m:lining->geometry->materials)CHECK(!m->texture&&m->color.a==255);
 int outside=0;for(const auto &p:lining->geometry->vertices)if(!((std::fabs(p.z+.93f)<.00002f||(p.y>=-1.3021f&&p.y<=-.027f&&p.z<=-.6999f))?InsideAirframe(shell,p,-.0001f):InsideFuselage(shell,p))){++outside;if(outside<8)std::printf("Lining outside hull: %.5f %.5f %.5f\n",p.x,p.y,p.z);}CHECK(outside==0);
 // camera::InVehicle uses the native seated head +.048m and an .08m near
 // clip. Registered model248 is a plane, so seateye adds no car-only lift.
 // The actual CAR_sit head places the seated eye near+.0034, not+.30.
 const CVector eye{-.334334f,4.748779f,.003372f};
 for(CVector direction:std::array<CVector,4>{{{-1,0,0},{1,0,0},{0,0,1},{0,-1,0}}})CHECK(Ray(clump,{eye.x,3.12f,eye.z},direction,"jet_lining",true,true)<9);
 CHECK(Ray(clump,eye,{0,1,0},nullptr,true)>2); // windshield stays open
 // Every real rear-cabin window opens through both shell and inward lining.
 for(float y:{2.76f,1.85f,.93f,.07f,-.81f})for(float side:{-1.f,1.f})CHECK(Ray(clump,{0,y,-.06f},{side,0,0},nullptr,true)>2);
 // Actual door skin closes the aperture; its steps drop with the door.
 CHECK(Ray(clump,{0,3.7f,-.1f},{-1,0,0},"jet_door",true)<1.1f);
 // The actual camera's allowed seat yaw/pitch must not put its near plane
 // beyond an opaque lining panel, including upper corners while looking up.
 for(CVector cameraEye:std::array<CVector,2>{{eye,{0,5.098779f,.085372f}}})for(float yaw:{-2.6f,0.f,2.6f})for(float pitch:{-.7f,0.f,.6f}){
  const CVector front{std::sin(yaw)*std::cos(pitch),std::cos(yaw)*std::cos(pitch),std::sin(pitch)},right{std::cos(yaw),-std::sin(yaw),0},up=airportjetmodel::Cross(right,front);
  for(float x:{-.056f,.056f})for(float y:{-.032f,.032f}){const CVector offset=airportjetcollision::Add(airportjetcollision::Mul(front,.08f),airportjetcollision::Add(airportjetcollision::Mul(right,x),airportjetcollision::Mul(up,y)));CHECK(Ray(clump,cameraEye,airportjetmodel::Unit(offset),"jet_lining",true,true)>airportjetcollision::Length(offset));}
 }
}
void SourceAttributes(Clump &clump,Geo *source){
 using namespace airportjetcollision;int checked=0,repainted=0;
 const char *env=std::getenv("GAMEDATA");const std::string base=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";ImgArchive archive;CHECK(archive.Open(base+"/models/gta3.img"));std::vector<uint8_t> txd,pixels;int width=0,height=0;CHECK(archive.Read("ap_learjets.txd",txd)&&DecodeTxdTexture(txd,"ap_learjet",width,height,pixels));auto dot64=[](CVector a,CVector b){return double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z;};
 for(const char *name:{"jet_elevator_l","jet_elevator_r","jet_rudder","jet_strut_n","jet_strut_l","jet_strut_r","jet_door","jet_flap_l","jet_flap_r","jet_fairing_l","jet_fairing_r","jet_link_l","jet_link_r","jet_baydoor_nl","jet_baydoor_nr","jet_baydoor_ml","jet_baydoor_mr","jet_baydoor_mli","jet_baydoor_mri","jet_baydoor_mli_b","jet_baydoor_mri_b"}){
  auto *part=Part(clump,name);CHECK(part);if(!part)continue;auto *g=part->geometry;
  for(const auto &face:g->triangles){bool found=false;
   for(const auto &old:source->triangles){const bool repaint=source->materials[old.material]->texture&&source->materials[old.material]->texture->name=="black64";if(g->materials[face.material]!=source->materials[repaint?0:old.material])continue;
    const CVector a=source->vertices[old.v[0]],e=Sub(source->vertices[old.v[1]],a),f=Sub(source->vertices[old.v[2]],a);const double ee=dot64(e,e),ef=dot64(e,f),ff=dot64(f,f),denominator=ee*ff-ef*ef;if(denominator<1e-12f)continue;bool matches=true;
    for(int j=0;j<3&&matches;++j){const CVector p=!std::strncmp(name,"jet_baydoor_",12)?g->vertices[face.v[j]]:ModelPoint(part->frame,g->vertices[face.v[j]]),d=Sub(p,a);const double u=(dot64(d,e)*ff-dot64(d,f)*ef)/denominator,v=(dot64(d,f)*ee-dot64(d,e)*ef)/denominator,w=1-u-v;
     if(u<-.0002f||v<-.0002f||w<-.0002f||Length(Sub(p,Add(a,Add(Mul(e,u),Mul(f,v)))))>.00003f){matches=false;break;}
     const CVector2D x=source->uv[old.v[0]],y=source->uv[old.v[1]],z=source->uv[old.v[2]],uv=g->uv[face.v[j]];
     if(repaint){const auto &t=source->triangles[16];const CVector pa=source->vertices[t.v[0]],pb=source->vertices[t.v[1]],pc=source->vertices[t.v[2]];const double ex=pb.x-pa.x,ey=pb.y-pa.y,fx=pc.x-pa.x,fy=pc.y-pa.y,det=ex*fy-ey*fx;double expectedU=0,expectedV=0;for(int k=0;k<3;++k){const auto position=source->vertices[old.v[k]];const double dx=std::fabs(position.x)-pa.x,dy=position.y-pa.y,a=(dx*fy-dy*fx)/det,b=(ex*dy-ey*dx)/det,weight=k==0?w:(k==1?u:v);const auto ua=source->uv[t.v[0]],ub=source->uv[t.v[1]],uc=source->uv[t.v[2]];expectedU+=weight*(ua.x+a*(ub.x-ua.x)+b*(uc.x-ua.x));expectedV+=weight*(ua.y+a*(ub.y-ua.y)+b*(uc.y-ua.y));}const auto color=g->colors[face.v[j]];matches=std::fabs(uv.x-expectedU)<.00005&&std::fabs(uv.y-expectedV)<.00005&&color.r==21&&color.g==21&&color.b==21&&color.a==255;}else matches=std::fabs(uv.x-(w*x.x+u*y.x+v*z.x))<.00005f&&std::fabs(uv.y-(w*x.y+u*y.y+v*z.y))<.00005f;
     CVector oldNormal[3];for(int k=0;k<3;++k)oldNormal[k]=Length(source->normals[old.v[k]])<.1f?airportjetmodel::Unit(airportjetmodel::Cross(e,f)):source->normals[old.v[k]];const CVector normal=airportjetmodel::Unit(Add(Mul(oldNormal[0],w),Add(Mul(oldNormal[1],u),Mul(oldNormal[2],v))));matches=matches&&Dot(normal,g->normals[face.v[j]])>.96f;
    }if(matches){found=true;if(repaint){++repainted;const auto material=g->materials[face.material]->color;for(int a=0;a<=8;++a)for(int b=0;b<=8-a;++b){const float u=a/8.f,v=b/8.f,w=1-u-v;const auto x=g->uv[face.v[0]],y=g->uv[face.v[1]],z=g->uv[face.v[2]];const float uvx=w*x.x+u*y.x+v*z.x,uvy=w*x.y+u*y.y+v*z.y;const int ix=std::clamp(int((uvx-std::floor(uvx))*width),0,width-1),iy=std::clamp(int((uvy-std::floor(uvy))*height),0,height-1),at=(iy*width+ix)*4;CHECK(pixels[at]>64&&pixels[at+1]>64&&pixels[at+2]>64);const auto pre=g->colors[face.v[0]];CHECK(pre.r==21&&pre.g==21&&pre.b==21&&pixels[at]*material.r/255*pre.r/255<40);}}break;}
   }if(!found)std::printf("Source mismatch %s\n",name);CHECK(found);++checked;
  }
 }
 CHECK(repainted>0&&repainted<60);std::printf("Intentional exterior shadow repaint: %d source-child faces, effective atlas/material/prelight all painted\n",repainted);
 std::printf("Articulated source skin: %d faces retain actual positions, atlas UV, normals and native material refs\n",checked);
}
struct SurfaceTriangle {CVector p[3],lo,hi;const char *label=nullptr;};
std::vector<SurfaceTriangle> Surface(Clump &clump,const char *name){
 std::vector<SurfaceTriangle> out;
 for(auto *part:clump.atomics){if(!(part->flags&4)||std::strcmp(part->frame->name,name))continue;auto *g=part->geometry;
  for(const auto &f:g->triangles){SurfaceTriangle t{};t.label=name;for(int j=0;j<3;++j)t.p[j]=ModelPoint(part->frame,g->vertices[f.v[j]]);if(airportjetcollision::Length(airportjetmodel::Cross(airportjetcollision::Sub(t.p[1],t.p[0]),airportjetcollision::Sub(t.p[2],t.p[0])))<.00000001f)continue;t.lo=t.hi=t.p[0];for(auto p:t.p){t.lo={std::min(t.lo.x,p.x),std::min(t.lo.y,p.y),std::min(t.lo.z,p.z)};t.hi={std::max(t.hi.x,p.x),std::max(t.hi.y,p.y),std::max(t.hi.z,p.z)};}out.push_back(t);}
 }return out;
}
bool InsideSkin(const std::vector<SurfaceTriangle> &surface,CVector p,float tolerance){double low=1e9,high=-1e9;for(const auto &t:surface){const CVector a=t.p[0],b=t.p[1],c=t.p[2];const double x=b.x-a.x,y=b.y-a.y,u=c.x-a.x,v=c.y-a.y,det=x*v-y*u;if(std::fabs(det)<1e-10)continue;const double dx=p.x-a.x,dy=p.y-a.y,s=(dx*v-dy*u)/det,tv=(x*dy-y*dx)/det;if(s<-.00001||tv<-.00001||s+tv>1.00001)continue;const double z=a.z+s*(b.z-a.z)+tv*(c.z-a.z);low=std::min(low,z);high=std::max(high,z);}return p.z>=low-tolerance&&p.z<=high+tolerance;}
bool Through(CVector from,CVector to,const SurfaceTriangle &t){
 struct V {double x,y,z;};auto sub=[](V a,V b){return V{a.x-b.x,a.y-b.y,a.z-b.z};};auto cross=[](V a,V b){return V{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};auto dot=[](V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;};auto length=[&](V a){return std::sqrt(dot(a,a));};auto cv=[](CVector a){return V{a.x,a.y,a.z};};const V a=cv(from),b=cv(to),p=cv(t.p[0]),e=sub(cv(t.p[1]),p),f=sub(cv(t.p[2]),p),n=cross(e,f);const double norm=length(n);if(norm<1e-10)return false;const double da=dot(sub(a,p),n)/norm,db=dot(sub(b,p),n)/norm;
 // A native float seam or shared hinge edge is not a solid penetration.
 if(!((da>.0005&&db<-.0005)||(db>.0005&&da<-.0005)))return false;const double along=da/(da-db);const V hit{a.x+(b.x-a.x)*along,a.y+(b.y-a.y)*along,a.z+(b.z-a.z)*along},d=sub(hit,p);const double ee=dot(e,e),ef=dot(e,f),ff=dot(f,f),den=ee*ff-ef*ef;if(den<1e-20)return false;const double u=(dot(d,e)*ff-dot(d,f)*ef)/den,v=(dot(d,f)*ee-dot(d,e)*ef)/den;if(u<=1e-7||v<=1e-7||u+v>=1-1e-7)return false;
 for(int i=0;i<3;++i){const V x=cv(t.p[i]),edge=sub(cv(t.p[(i+1)%3]),x);if(length(cross(edge,sub(hit,x)))/length(edge)<.0005)return false;}return true;
}
bool Crosses(const SurfaceTriangle &a,const SurfaceTriangle &b){
 if(a.hi.x<b.lo.x||b.hi.x<a.lo.x||a.hi.y<b.lo.y||b.hi.y<a.lo.y||a.hi.z<b.lo.z||b.hi.z<a.lo.z)return false;
 for(int j=0;j<3;++j)if(Through(a.p[j],a.p[(j+1)%3],b)||Through(b.p[j],b.p[(j+1)%3],a))return true;return false;
}

// Every native vehicle component is rendered two-sided, including original
// overlays and faces within a single atomic. Check real shared surface area,
// rather than just whether a lining centroid happens to lie on the hull.
double SharedArea(const SurfaceTriangle &a,const SurfaceTriangle &b,double gap=.001){
 using namespace airportjetcollision;const CVector n=airportjetmodel::Cross(Sub(a.p[1],a.p[0]),Sub(a.p[2],a.p[0]));const double len=Length(n);if(len<1e-8)return 0;const CVector m=airportjetmodel::Cross(Sub(b.p[1],b.p[0]),Sub(b.p[2],b.p[0]));if(Length(m)<1e-8||std::fabs(Dot(n,m)/(len*Length(m)))<.99999)return 0;for(auto p:b.p)if(std::fabs(Dot(n,Sub(p,a.p[0])))/len>gap)return 0;
 int axis=std::fabs(n.x)>std::fabs(n.y)?0:1;if(std::fabs(n.z)>std::fabs(axis==0?n.x:n.y))axis=2;struct P{double x,y;};auto cv=[&](CVector p){return axis==0?P{p.y,p.z}:axis==1?P{p.x,p.z}:P{p.x,p.y};};auto side=[](P p,P q,P r){return (q.x-p.x)*(r.y-p.y)-(q.y-p.y)*(r.x-p.x);};P tri[3]={cv(a.p[0]),cv(a.p[1]),cv(a.p[2])};std::vector<P> poly={cv(b.p[0]),cv(b.p[1]),cv(b.p[2])};const double sign=side(tri[0],tri[1],tri[2])>0?1:-1;
 for(int edge=0;edge<3&&!poly.empty();++edge){std::vector<P> next;P p=poly.back();double dp=sign*side(tri[edge],tri[(edge+1)%3],p);for(P q:poly){double dq=sign*side(tri[edge],tri[(edge+1)%3],q);if((dp>0)!=(dq>0)){double t=dp/(dp-dq);next.push_back({p.x+(q.x-p.x)*t,p.y+(q.y-p.y)*t});}if(dq>=0)next.push_back(q);p=q;dp=dq;}poly=std::move(next);}
 double area=0;for(size_t i=0;i<poly.size();++i){P p=poly[i],q=poly[(i+1)%poly.size()];area+=p.x*q.y-p.y*q.x;}return std::fabs(area)*.5*len/std::fabs(axis==0?n.x:axis==1?n.y:n.z);
}
void AllComponentDepth(Clump &clump){
 airportjetmodel::Pose pose;double worst=0;int checks=0;
 for(float down:{0.f,.5f,1.f}){for(float door:{0.f,.25f,.5f,.75f,1.f}){
  pose.gearDown=down;pose.doorOpen=door;airportjetmodel::Animate(&clump,pose);std::vector<SurfaceTriangle> all;
  for(auto *part:clump.atomics){if(!(part->flags&4))continue;bool opaque=false;for(auto *m:part->geometry->materials)opaque=opaque||m->color.a>=100;if(!opaque)continue;auto faces=Surface(clump,part->frame->name);all.insert(all.end(),faces.begin(),faces.end());}
  double area=0;std::map<std::string,double> groups;
  for(size_t i=0;i<all.size();++i)for(size_t j=i+1;j<all.size();++j){const auto &a=all[i],&b=all[j];if(a.hi.x+.001<b.lo.x||b.hi.x+.001<a.lo.x||a.hi.y+.001<b.lo.y||b.hi.y+.001<a.lo.y||a.hi.z+.001<b.lo.z||b.hi.z+.001<a.lo.z)continue;const double overlap=SharedArea(a,b);if(overlap<=1e-7)continue;area+=overlap;groups[std::string(a.label)+" / "+b.label]+=overlap;}
  if(area>.00001)for(const auto &g:groups)if(g.second>.00001)std::printf("Depth conflict down%.2f door%.2f %s %.8fm2\n",down,door,g.first.c_str(),g.second);
  worst=std::max(worst,area);++checks;
 }}
 std::printf("All-component CULLNONE: %d combined poses, worst shared area %.9fm2\n",checks,worst);CHECK(worst<.00001);airportjetmodel::Animate(&clump,airportjetmodel::Pose{});
}
void CabinCleanup(Clump &clump){
 airportjetmodel::Animate(&clump,airportjetmodel::Pose{});
 // The first visible floor must remain the cockpit's level all the way
 // through the aisle. A raised rear deck can no longer conceal folded gear.
 int floorSteps=0;for(float y:{-2.f,-1.f,0.f,1.f,2.f,3.f,3.7f,4.1f,4.4f,5.8f}){const float drop=Ray(clump,{0,y,-.60f},{0,0,-1},"jet_lining",true);if(std::fabs(drop-.33f)>.002f){std::printf("Floor gap Y%.3f drop%.6f\n",y,drop);++floorSteps;}}std::printf("Level floor: %d raised/missing aisle samples\n",floorSteps);CHECK(floorSteps==0);
 auto *rear=Part(clump,"jet_cabin_rear");CHECK(rear&&rear->geometry->triangles.size()>=232);if(rear)for(auto p:rear->geometry->vertices){p=ModelPoint(rear->frame,p);CHECK(std::fabs(p.x)>.22f&&p.y<2.85f&&p.y> -2.5f&&p.z>=-.931f);}
 // Only the original attached fairings/struts may hang beside the wheels.
 // Closing patches are absent throughout deployed and ordinary travel.
 for(const char *name:{"jet_baywell","jet_bayrim"})CHECK(!Part(clump,name));
 airportjetmodel::Pose traveling;traveling.gearDown=.5f;airportjetmodel::Animate(&clump,traveling);int extraPanels=0;for(auto *part:clump.atomics)if(!std::strncmp(part->frame->name,"jet_baydoor_",12))extraPanels+=(part->flags&4)!=0;std::printf("Ordinary gear travel: %d added visible panels\n",extraPanels);CHECK(extraPanels==0);
 int tireFloor=0;for(int i=0;i<=100;++i){traveling.gearDown=i*.01f;airportjetmodel::Animate(&clump,traveling);for(const char *name:{"jet_tire_l","jet_tire_r","jet_fairing_l","jet_fairing_r","jet_strut_l","jet_strut_r","jet_link_l","jet_link_r"})for(const auto &t:Surface(clump,name))for(auto p:t.p)tireFloor+=p.z>-.9299f&&p.y>=-2.5f&&p.y<=5.45f&&std::fabs(p.x)<.617792826f+.022847854f*p.y-.0001f;}std::printf("Visible gear above level cabin floor: %d vertices\n",tireFloor);CHECK(tireFloor==0);
 int treadWallHits=0;airportjetmodel::Pose pose;
 for(int i=0;i<=100;++i){pose.doorOpen=i*.01f;airportjetmodel::Animate(&clump,pose);auto fixed=Surface(clump,"jet_hull");auto lining=Surface(clump,"jet_lining");fixed.insert(fixed.end(),lining.begin(),lining.end());for(const char *name:{"jet_steps","jet_step_1","jet_step_2","jet_step_3"})for(const auto &t:Surface(clump,name))for(const auto &wall:fixed)if(Crosses(t,wall)){if(treadWallHits<8)std::printf("Door hit %s / %s at%.2f p%.5f %.5f %.5f\n",name,wall.label,pose.doorOpen,t.p[0].x,t.p[0].y,t.p[0].z);++treadWallHits;}}
 std::printf("Door/inner tread full sweep: %d wall intersections\n",treadWallHits);CHECK(treadWallHits==0);airportjetmodel::Animate(&clump,airportjetmodel::Pose{});
}

Clump ClonePrepared(const Clump &source){
 std::map<Frame *,Frame *> frames;auto copy=[&](auto &&self,Frame *old)->Frame *{auto *f=static_cast<Frame *>(FrameCreate());std::memcpy(f->name,old->name,sizeof f->name);std::memcpy(f->matrix,old->matrix,sizeof f->matrix);f->visibility=old->visibility;frames[old]=f;for(auto *child:old->children)FrameChild(f,self(self,child));return f;};Clump clone{copy(copy,source.root),{}};
 for(auto *old:source.atomics){auto *a=static_cast<Atomic *>(AtomicCreate());a->geometry=old->geometry;++a->geometry->refs;a->frame=frames[old->frame];a->flags=old->flags;clone.atomics.push_back(a);}return clone;
}
void GearPackingOwnership(Clump &model){
 auto *basis=Part(model,"jet_tire_l_basis"),*original=Part(model,"jet_tire_l");CHECK(basis&&original);if(!basis||!original)return;const auto fingerprint=GeometryFingerprint(*basis->geometry);auto *originalGeometry=original->geometry;const auto originalFingerprint=GeometryFingerprint(*originalGeometry);CHECK(!(basis->flags&4));
 auto first=ClonePrepared(model),second=ClonePrepared(model);airportjetmodel::Pose pose;pose.gearDown=.3f;
 const int geometries=liveGeometry;failAllocation=true;airportjetmodel::Animate(&first,airportjetmodel::Pose{});failAllocation=false;CHECK(liveGeometry==geometries&&Part(first,"jet_tire_l")->geometry==originalGeometry);
 airportjetmodel::Animate(&first,pose);pose.gearDown=.8f;airportjetmodel::Animate(&second,pose);
 CHECK(Part(first,"jet_tire_l")->geometry!=Part(second,"jet_tire_l")->geometry&&Part(first,"jet_tire_l")->geometry!=basis->geometry);CHECK(GeometryFingerprint(*basis->geometry)==fingerprint&&original->geometry==originalGeometry&&GeometryFingerprint(*originalGeometry)==originalFingerprint);
 auto *buffer=Part(first,"jet_tire_l")->geometry;const auto indices=buffer->triangles;const size_t capacity=buffer->vertices.size();buffer->lastLock=0;airportjetmodel::Matrix(first.root,{123,-44,29},{.2f,.4f,1},.83f);
 const int privateGeometries=liveGeometry;for(int i=0;i<20;++i){pose.gearDown=i*.05f;airportjetmodel::Animate(&first,pose);}CHECK(liveGeometry==privateGeometries);CHECK(buffer->lastLock==0x1e&&buffer->vertices.size()==capacity&&buffer->triangles.size()==indices.size()&&!std::memcmp(buffer->triangles.data(),indices.data(),indices.size()*sizeof(Tri)));CHECK(capacity==basis->geometry->triangles.size()*60);
 const float *body=LTM(first.root);for(const auto &t:Surface(first,"jet_tire_l"))for(auto world:t.p){const CVector d{world.x-body[12],world.y-body[13],world.z-body[14]};const CVector p{d.x*body[0]+d.y*body[1]+d.z*body[2],d.x*body[4]+d.y*body[5]+d.z*body[6],d.x*body[8]+d.y*body[9]+d.z*body[10]};CHECK(p.z<=-.9299f||std::fabs(p.x)>=.617792826f+.022847854f*p.y-.0001f);}pose.gearDown=.37f;for(auto &wheel:pose.wheels)wheel.spin=.42f;airportjetmodel::Animate(&first,pose);airportjetmodel::Animate(&second,pose);for(const char *name:airportjetmodel::kPackingNames){const auto *a=Part(first,name)->geometry,*b=Part(second,name)->geometry;CHECK(a->vertices.size()==b->vertices.size());for(size_t i=0;i<a->vertices.size();++i)CHECK(airportjetcollision::Length(airportjetcollision::Sub(a->vertices[i],b->vertices[i]))<.0001f);}
 Destroy(first);Destroy(second);CHECK(liveGeometry==geometries);std::puts("Gear clip ownership: immutable model basis, independent actor buffers, allocation rollback and reuse passed");
}
std::vector<SurfaceTriangle> GearView(Clump &clump,bool original){
 std::vector<SurfaceTriangle> surface;for(auto *atomic:clump.atomics){const char *name=atomic->frame->name;bool main=false;for(const char *part:airportjetmodel::kPackingNames)main=main||!std::strcmp(name,part);if(!(atomic->flags&4)&&!(original&&main))continue;auto *g=atomic->geometry;if(original&&main){std::string key=std::string(name)+"_basis";if(auto *basis=Part(clump,key.c_str()))g=basis->geometry;}
  for(const auto &f:g->triangles){if(g->materials[f.material]->color.a<100)continue;SurfaceTriangle t{};t.label=name;for(int j=0;j<3;++j)t.p[j]=ModelPoint(atomic->frame,g->vertices[f.v[j]]);if(airportjetcollision::Length(airportjetmodel::Cross(airportjetcollision::Sub(t.p[1],t.p[0]),airportjetcollision::Sub(t.p[2],t.p[0])))>.00000001f)surface.push_back(t);}
 }return surface;
}
float AxisRay(const std::vector<SurfaceTriangle> &surface,CVector eye,int axis,float direction){
 double best=1e9;for(const auto &t:surface){auto u=[&](CVector p){return axis==2?p.x:p.y;};auto v=[&](CVector p){return axis==2?p.y:p.z;};auto w=[&](CVector p){return axis==2?p.z:p.x;};const double a=u(t.p[0]),b=v(t.p[0]),ex=u(t.p[1])-a,ey=v(t.p[1])-b,fx=u(t.p[2])-a,fy=v(t.p[2])-b,det=ex*fy-ey*fx;if(std::fabs(det)<1e-12)continue;const double dx=u(eye)-a,dy=v(eye)-b,s=(dx*fy-dy*fx)/det,r=(ex*dy-ey*dx)/det;if(s<0||r<0||s+r>1)continue;const double depth=direction*(w(t.p[0])+s*(w(t.p[1])-w(t.p[0]))+r*(w(t.p[2])-w(t.p[0]))-w(eye));if(depth>.00001)best=std::min(best,depth);}return float(best);
}
void GearVisibleContinuity(Clump &clump){
 airportjetmodel::Pose pose;pose.gearDown=.55f;airportjetmodel::Animate(&clump,pose);const auto before=GearView(clump,false);pose.gearDown=.549f;airportjetmodel::Animate(&clump,pose);const auto after=GearView(clump,false),unclipped=GearView(clump,true);int vanished=0,naturalEdges=0,exposed=0;
 for(int y=0;y<30;++y)for(int x=0;x<75;++x){const CVector eye{-1.5f+x*.04f,-1.5f+y*.04f,-4};const float old=AxisRay(before,eye,2,1),now=AxisRay(after,eye,2,1),source=AxisRay(unclipped,eye,2,1);if(old<2.9f)++exposed;if(now-old>.05f){if(source-old>.05f)++naturalEdges;else ++vanished;}}
 std::printf("Gear visibility continuity .55->.549: %d exterior rays, %d artificial jumps, %d ordinary silhouette-edge crossings\n",exposed,vanished,naturalEdges);CHECK(vanished==0);
 int changes=0,rays=0;float worst=0;
 for(int step=0;step<=50;++step){pose.gearDown=step*.02f;airportjetmodel::Animate(&clump,pose);const auto actual=GearView(clump,false),source=GearView(clump,true);auto compare=[&](CVector eye,int axis,float direction){const float expected=AxisRay(source,eye,axis,direction),now=AxisRay(actual,eye,axis,direction),error=std::fabs(expected-now);++rays;if(expected<1e8f&&error>.0005f){if(changes<5)std::printf("Exposed clip change down%.3f axis%d eye%.3f %.3f %.3f error%.6f\n",pose.gearDown,axis,eye.x,eye.y,eye.z,error);++changes;worst=std::max(worst,error);}};
  for(int y=0;y<13;++y)for(int x=0;x<31;++x)compare({-1.5f+x*.1f,-1.5f+y*.1f,-4},2,1);
  for(float side:{-1.f,1.f})for(int y=0;y<13;++y)for(int z=0;z<16;++z)compare({side*15,-1.5f+y*.1f,-2.25f+z*.1f},0,-side);
 }
 std::printf("Full gear exposed-source visibility: %d rays, %d changes, max%.6fm\n",rays,changes,worst);CHECK(changes==0);airportjetmodel::Animate(&clump,airportjetmodel::Pose{});
}
double BelowFloorArea(const CVector input[3]){
 struct P {double x,y,z;};std::vector<P> polygon;P a{input[2].x,input[2].y,input[2].z};double da=-.93-a.z;
 for(int i=0;i<3;++i){const P b{input[i].x,input[i].y,input[i].z};const double db=-.93-b.z;if((da>=0)!=(db>=0)){const double t=da/(da-db);polygon.push_back({a.x+t*(b.x-a.x),a.y+t*(b.y-a.y),-.93});}if(db>=0)polygon.push_back(b);a=b;da=db;}
 double area=0;for(size_t i=1;i+1<polygon.size();++i){const auto a=polygon[0],b=polygon[i],c=polygon[i+1];const double ex=b.x-a.x,ey=b.y-a.y,ez=b.z-a.z,fx=c.x-a.x,fy=c.y-a.y,fz=c.z-a.z;const double nx=ey*fz-ez*fy,ny=ez*fx-ex*fz,nz=ex*fy-ey*fx;area+=.5*std::sqrt(nx*nx+ny*ny+nz*nz);}return area;
}
void PackedSourceConservation(Clump &clump){
 airportjetmodel::Pose pose;int fragments=0;double worst=0;
 for(float spin:{0.f,.77f}){for(float down:{0.f,.2f,.3f,.4f,.549f,.55f,1.f}){pose.gearDown=down;for(auto &wheel:pose.wheels)wheel.spin=spin;airportjetmodel::Animate(&clump,pose);
  for(const char *name:airportjetmodel::kPackingNames){auto *part=Part(clump,name);auto *basis=Part(clump,(std::string(name)+"_basis").c_str());CHECK(part&&basis);if(!part||!basis)continue;auto *source=basis->geometry,*target=part->geometry;CHECK(target->triangles.size()==source->triangles.size()*20);double expected=0,actual=0;
   for(size_t i=0;i<source->triangles.size();++i){const auto original=source->triangles[i];CVector native[3];for(int j=0;j<3;++j)native[j]=ModelPoint(part->frame,source->vertices[original.v[j]]);expected+=BelowFloorArea(native);
    const auto a=source->vertices[original.v[0]],e=airportjetcollision::Sub(source->vertices[original.v[1]],a),f=airportjetcollision::Sub(source->vertices[original.v[2]],a);const double ee=airportjetcollision::Dot(e,e),ef=airportjetcollision::Dot(e,f),ff=airportjetcollision::Dot(f,f),den=ee*ff-ef*ef;
    for(size_t fragment=0;fragment<20;++fragment){const auto face=target->triangles[i*20+fragment];CVector points[3];for(int j=0;j<3;++j)points[j]=ModelPoint(part->frame,target->vertices[face.v[j]]);actual+=BelowFloorArea(points);if(airportjetcollision::Length(airportjetmodel::Cross(airportjetcollision::Sub(points[1],points[0]),airportjetcollision::Sub(points[2],points[0])))<.00000001f)continue;++fragments;CHECK(target->materials[face.material]==source->materials[original.material]);
     for(int j=0;j<3;++j){const auto p=target->vertices[face.v[j]],d=airportjetcollision::Sub(p,a);const double u=(airportjetcollision::Dot(d,e)*ff-airportjetcollision::Dot(d,f)*ef)/den,v=(airportjetcollision::Dot(d,f)*ee-airportjetcollision::Dot(d,e)*ef)/den,w=1-u-v;CHECK(u>=-.0005&&v>=-.0005&&w>=-.0005);const auto x=source->uv[original.v[0]],y=source->uv[original.v[1]],z=source->uv[original.v[2]],uv=target->uv[face.v[j]];CHECK(std::fabs(uv.x-(w*x.x+u*y.x+v*z.x))<.00005&&std::fabs(uv.y-(w*x.y+u*y.y+v*z.y))<.00005);}
    }
   }const double difference=std::fabs(expected-actual);worst=std::max(worst,difference);CHECK(difference<.00002);
  }
 }}
 std::printf("Packed source conservation: %d real fragments retain native position/UV/material; below-floor area error %.9fm2\n",fragments,worst);airportjetmodel::Animate(&clump,airportjetmodel::Pose{});
}
void GearBays(Clump &clump){
 const char *seals[]={"jet_baydoor_nl","jet_baydoor_nr","jet_baydoor_ml","jet_baydoor_mr","jet_baydoor_mli","jet_baydoor_mri","jet_baydoor_mli_b","jet_baydoor_mri_b"};for(const char *name:seals)CHECK(Part(clump,name));
 int intersections=0,states=0;std::map<std::string,int> groups;airportjetmodel::Pose pose;
 for(float door:{0.f,.25f,.5f,.75f,1.f}){pose.doorOpen=door;airportjetmodel::Animate(&clump,pose);std::vector<SurfaceTriangle> fixed;
  for(const char *name:{"jet_hull","jet_door","jet_cabin","jet_cabin_rear","jet_dashboard","jet_window_trim","jet_lining","jet_steps","jet_step_1","jet_step_2","jet_step_3"}){auto faces=Surface(clump,name);fixed.insert(fixed.end(),faces.begin(),faces.end());}
  for(int mode=0;mode<4;++mode)for(int sample=0;sample<=100;++sample){for(auto &wheel:pose.wheels){wheel.compression=mode?-jetwheels::kTravel:0;wheel.spin=.1f*mode;}pose.wheels[0].steer=mode==2?.7853982f:(mode==3?-.7853982f:0);pose.gearDown=float(sample)/100;airportjetmodel::Animate(&clump,pose);auto obstacle=fixed;
   for(const char *name:seals){auto faces=Surface(clump,name);obstacle.insert(obstacle.end(),faces.begin(),faces.end());auto *part=Part(clump,name);CHECK(part&&bool(part->flags&4)==(sample<=18));if(part)CHECK((part->flags&~4u)==1u);}
   for(const char *name:{"jet_tire_n","jet_tire_l","jet_tire_r","jet_strut_n","jet_strut_l","jet_strut_r","jet_fairing_l","jet_fairing_r","jet_link_l","jet_link_r"})for(const auto &moving:Surface(clump,name))for(const auto &solid:obstacle)if(Crosses(moving,solid)){++groups[std::string(name)+" / "+solid.label];++intersections;}
   for(const char *left:{"jet_tire_l","jet_strut_l","jet_link_l","jet_fairing_l"})for(const char *right:{"jet_tire_r","jet_strut_r","jet_link_r","jet_fairing_r"})for(const auto &a:Surface(clump,left))for(const auto &b:Surface(clump,right))if(Crosses(a,b)){++groups[std::string(left)+" / "+right];++intersections;}
   ++states;
  }
 }
 for(const auto &group:groups)std::printf("Gear hits %s %d\n",group.first.c_str(),group.second);
 std::printf("Combined door/gear cycles: %d states, %d solid intersections\n",states,intersections);CHECK(intersections==0);airportjetmodel::Animate(&clump,airportjetmodel::Pose{});
}
void RearSeatProvenance(Clump &clump,Clump &donor,Geo *shell){
 auto *rear=Part(clump,"jet_cabin_rear");CHECK(rear);if(!rear)return;auto *g=rear->geometry,*source=donor.atomics[0]->geometry;CHECK(g->triangles.size()==348);int matches=0;
 for(const auto &face:g->triangles){bool found=false;for(float row:{2.2f,.7f,-.8f})for(size_t i=1963;i<=2078&&!found;++i){const auto old=source->triangles[i];if(g->materials[face.material]!=source->materials[old.material])continue;bool same=true;
   for(int j=0;j<3;++j){const auto p=source->vertices[old.v[j]],q=g->vertices[face.v[j]];const CVector expected{p.x*.76f+(p.x<0?-.12f:.12f),row+(p.y-1.476f)*.68f+.016475f,-.674597f+(p.z+.09952587f)*.85f};const auto a=g->uv[face.v[j]],b=source->uv[old.v[j]];same=same&&airportjetcollision::Length(airportjetcollision::Sub(q,expected))<.00002f&&std::fabs(a.x-b.x)<.00001f&&std::fabs(a.y-b.y)<.00001f;}
   found=found||same;
  }CHECK(found);matches+=found;
  for(int a=0;a<=4;++a)for(int b=0;b<=4-a;++b){const auto p=airportjetcollision::Add(airportjetcollision::Mul(g->vertices[face.v[0]],1-(a+b)/4.f),airportjetcollision::Add(airportjetcollision::Mul(g->vertices[face.v[1]],a/4.f),airportjetcollision::Mul(g->vertices[face.v[2]],b/4.f)));CHECK(InsideAirframe(shell,p,-.0001f));}
 }std::printf("Rear seats: %d actual donor faces, six seats; shell containment/0.50m aisle/door vestibule clear\n",matches);
}
void DashboardProvenance(Clump &clump,Clump &donor,Geo *shell){
 auto *part=Part(clump,"jet_dashboard");CHECK(part);if(!part)return;auto *g=part->geometry,*source=donor.atomics[0]->geometry;int matched=0;std::vector<bool> used(g->triangles.size());
 auto base=[](CVector p){return CVector{p.x*.76f,5+(p.y-1.476f)*.68f,-.65f+(p.z+.09952587f)*.85f};};
 auto panel=[](float z){return 5.48f+(z+.38f)/.29f*.16f-.003f;};
 auto find=[&](int index,float centre,bool radio){const auto &native=source->triangles[size_t(index)];CVector target[3];int original[3];for(int j=0;j<3;++j){original[j]=native.v[radio&&j?3-j:j];const CVector p=base(source->vertices[original[j]]);if(radio){const float z=-.34f+(p.z+.954682f)/(.954682f-.527374f)*.22f;target[j]={p.x*(.054f/.081508f),panel(z)-.004f,z};}else{const float f=(p.z+.514744f)/.308188f,z=-.35f+.24f*f;target[j]={centre+p.x*(.1175f/.148288f),panel(z)+(p.y-(5.732515f+.077171f*f))*.8f,z};}}
  bool found=false;for(size_t i=0;i<g->triangles.size();++i){const auto &triangle=g->triangles[i];if(used[i]||g->materials[triangle.material]!=source->materials[native.material])continue;bool same=true;for(int j=0;j<3;++j){const int k=triangle.v[j],old=original[j];const auto uv=g->uv[k],expected=source->uv[old];same=same&&airportjetcollision::Length(airportjetcollision::Sub(g->vertices[k],target[j]))<.00002f&&std::fabs(uv.x-expected.x)<.000001f&&std::fabs(uv.y-expected.y)<.000001f&&!std::memcmp(&g->colors[k],&source->colors[old],4);}if(same){used[i]=true;found=true;++matched;break;}}CHECK(found);
 };
 for(float centre:{-.43f,-.18f,.18f,.43f})for(int index:{45,48,588,589,1189,1190})find(index,centre,false);for(int index:{46,47})find(index,0,true);CHECK(matched==26);
 for(const auto &face:g->triangles){for(int a=0;a<=4;++a)for(int b=0;b<=4-a;++b){const CVector p=airportjetcollision::Add(airportjetcollision::Mul(g->vertices[face.v[0]],1-(a+b)/4.f),airportjetcollision::Add(airportjetcollision::Mul(g->vertices[face.v[1]],a/4.f),airportjetcollision::Mul(g->vertices[face.v[2]],b/4.f)));CHECK(InsideFuselage(shell,p));}}
 std::printf("Dashboard: 24 folded native instrument faces +2 native radio faces retain exact atlas/color/materials; fitted shell containment passed\n");
}
void PanelReadability(Clump &clump){
 // Reconstructed shipped CAR_sit + player.dff agrees with recorded native
 // neck to1mm. Native head+.048 gives this actual seated camera position.
 const CVector eye{-.334334f,4.748779f,.003372f};
 CHECK(Ray(clump,eye,{0,1,0},"jet_dashboard",true,true)>2);
 for(float pitch:{-.174533f,-.261799f,-.349066f}){const float distance=Ray(clump,eye,{0,std::cos(pitch),std::sin(pitch)},"jet_dashboard",true,true);CHECK(distance>.1f&&distance<1.5f);}
}
void CockpitView(Clump &clump){
 CHECK(Part(clump,"jet_dashboard"));
 int blackExterior=0;for(auto *part:clump.atomics)if(!std::strcmp(part->frame->name,"jet_hull")||!std::strncmp(part->frame->name,"jet_baydoor_",12))for(auto *m:part->geometry->materials)blackExterior+=m->texture&&m->texture->name=="black64";CHECK(blackExterior==0);
 int leaks=0,well=0;
 const CVector eye{-.334334f,4.748779f,.003372f};
 for(float yaw:{-2.6f,0.f,2.6f})for(float pitch:{-.7f,0.f,.6f}){const CVector f{std::sin(yaw)*std::cos(pitch),std::cos(yaw)*std::cos(pitch),std::sin(pitch)},r{std::cos(yaw),-std::sin(yaw),0},u=airportjetmodel::Cross(r,f);
  for(float x:{-.8f,-.4f,0.f,.4f,.8f})for(float y:{-.8f,-.4f,0.f,.4f,.8f}){const CVector d=airportjetmodel::Unit(airportjetcollision::Add(f,airportjetcollision::Add(airportjetcollision::Mul(r,x*.7002075f),airportjetcollision::Mul(u,y*.3938667f))));const float glass=Ray(clump,eye,d,"jet_glass",false,false);float nearest=1e9f;const char *name="";for(auto *part:clump.atomics){const float hit=Ray(clump,eye,d,part->frame->name,true,true);if(hit<nearest){nearest=hit;name=part->frame->name;}}if(glass<nearest+.0001f)continue;well+=!std::strcmp(name,"jet_baywell");const bool leak=nearest>1e8f||!std::strcmp(name,"jet_hull")||!std::strcmp(name,"jet_bayrim")||!std::strncmp(name,"jet_baydoor_",12);if(leak||!std::strcmp(name,"jet_baywell"))std::printf("Cockpit gap yaw%.2f pitch%.2f x%.1f y%.1f first%s distance%.5f glass%.5f dir%.5f %.5f %.5f\n",yaw,pitch,x,y,name,nearest,glass,d.x,d.y,d.z);leaks+=leak;}
 }
 std::printf("Actual seated cockpit grid: %d exterior leaks, %d exposed well rays\n",leaks,well);CHECK(leaks==0&&well==0);
}
void WindowRims(Clump &clump){
 // Independent dense render found source pane edges whose collinear YZ
 // vertices carry a real5cm X bend. These oblique native-eye pixels escaped
 // the coarse camera grid and must see either actual glass or inward trim.
 auto *glassPart=Part(clump,"jet_glass"),*trimPart=Part(clump,"jet_window_trim");CHECK(glassPart&&trimPart);int missingBends=0;
 if(glassPart&&trimPart)for(CVector p:glassPart->geometry->vertices)if(p.y<3){bool represented=false;for(CVector q:trimPart->geometry->vertices)represented=represented||airportjetcollision::Length(airportjetcollision::Sub(p,q))<.00003f;missingBends+=!represented;}CHECK(missingBends==0);
 struct Pixel {float yaw;bool behind;int x,y;};int gaps=0;
 for(Pixel sample:std::array<Pixel,12>{{{-2.6f,false,87,101},{-2.6f,false,108,130},{-2.6f,false,109,133},{-2.6f,false,90,107},{2.6f,false,288,104},{2.6f,false,287,108},{2.6f,false,470,113},{2.6f,false,355,118},{3.14159265f,true,158,119},{3.14159265f,true,321,119},{3.14159265f,true,184,122},{3.14159265f,true,282,125}}}){
  const CVector eye=sample.behind?CVector{0,5.098779f,.085372f}:CVector{-.334355f,4.749025f,.003372f};const float x=(2*(sample.x+.5f)/480-1)*.700207538f,z=(1-2*(sample.y+.5f)/270)*.393866740f;const CVector direction{x*std::cos(sample.yaw)+std::sin(sample.yaw),std::cos(sample.yaw)-x*std::sin(sample.yaw),z};
  const float glass=Ray(clump,eye,direction,"jet_glass",false,false);float nearest=1e9f;const char *name="";for(auto *part:clump.atomics){const float hit=Ray(clump,eye,direction,part->frame->name,true,true);if(hit<nearest){nearest=hit;name=part->frame->name;}}
  const bool covered=glass<nearest+.0001f||(!std::strcmp(name,"jet_window_trim")||!std::strcmp(name,"jet_lining"));if(!covered)++gaps;CHECK(covered);
 }
 std::printf("Oblique native pane-rim regressions: %d/12 gaps\n",gaps);
 // Actual CULLNONE pixel30,56 in the240x135 yaw2.6/pitch-.4 view.
 // A nonlinear per-section inset moved this clipped T-junction away from
 // the adjacent edge, exposing the painted right hull from the cabin.
 const CVector seamEye{-.334355f,4.749025f,.003372f},seamRay{.935193180f,-.541450346f,-.330299406f};
 const float inner=Ray(clump,seamEye,seamRay,"jet_lining",true,false),outer=Ray(clump,seamEye,seamRay,"jet_hull",true,false);
 CHECK(inner<outer-.001f);std::printf("Oblique shell T-junction: inside%.6fm paint%.6fm\n",inner,outer);

}
void BeaconClearance(Clump &clump,Geo *source){
 // These two native-eye image regions contain the18 confirmed ceiling
 // intrusion pixels from the original full top-beacon octahedron.
 int intrusion=0;for(bool behind:{false,true}){const float yaw=behind?3.14159265f:2.6f;const CVector eye=behind?CVector{0,5.098779f,.085372f}:CVector{-.334355f,4.749025f,.003372f};
  for(int y=behind?98:82;y<=(behind?99:84);++y)for(int x=behind?238:408;x<=(behind?241:413);++x){const float u=(2*(x+.5f)/480-1)*.700207538f,v=(1-2*(y+.5f)/270)*.393866740f;const CVector direction{u*std::cos(yaw)+std::sin(yaw),std::cos(yaw)-u*std::sin(yaw),v};const float lens=Ray(clump,eye,direction,"jet_lenses",true,true);float inner=1e9f;for(auto *part:clump.atomics)if(std::strcmp(part->frame->name,"jet_lenses"))inner=std::min(inner,Ray(clump,eye,direction,part->frame->name,true,true));const float glass=Ray(clump,eye,direction,"jet_glass",false,false);intrusion+=lens<inner&&lens<glass;}
 }std::printf("Top beacon inside ceiling: %d exposed pixels\n",intrusion);CHECK(intrusion==0);
 auto *g=Part(clump,"jet_lenses")->geometry;const auto &top=jetlights::kLenses[3];const CVector anchor{top.position.x,top.position.y,top.position.z};CHECK(top.position.x==0&&top.position.y==.5516f&&top.position.z==.61f&&top.radius==.07f);
 int buried=0;for(CVector p:g->vertices)if(airportjetcollision::Length(airportjetcollision::Sub(p,anchor))<=top.radius+.00001f){float low,high;CHECK(SourceSkinHeight(source,p,low,high));buried+=p.z<high+.0008f;}CHECK(buried==0);
 for(size_t i:{size_t(0),size_t(1),size_t(2),size_t(4)}){const auto &lens=jetlights::kLenses[i];airportjetmodel::Mesh expected;airportjetmodel::Lens(expected,lens.position,lens.radius,nullptr);for(const auto &face:expected.faces){bool found=false;for(auto t:g->triangles){bool same=true;for(int j=0;j<3;++j)same=same&&airportjetcollision::Length(airportjetcollision::Sub(face.v[j].p,g->vertices[t.v[j]]))<.000001f;found=found||same;}CHECK(found);}}
}
void NativeExterior(Clump &clump,Geo *source){
 // Native CRenderer sets CULLNONE for vehicles. Reverse-winding geometry
 // at the same position remains visible and fights the actual paint.
 auto overlap=[&](const char *outer,const char *inner){int coplanar=0;const auto outside=Surface(clump,outer);for(auto t:Surface(clump,inner)){const CVector p=airportjetcollision::Mul(airportjetcollision::Add(t.p[0],airportjetcollision::Add(t.p[1],t.p[2])),1.f/3),tn=airportjetmodel::Unit(airportjetmodel::Cross(airportjetcollision::Sub(t.p[1],t.p[0]),airportjetcollision::Sub(t.p[2],t.p[0])));for(auto q:outside){const CVector e=airportjetcollision::Sub(q.p[1],q.p[0]),f=airportjetcollision::Sub(q.p[2],q.p[0]),d=airportjetcollision::Sub(p,q.p[0]),n=airportjetmodel::Cross(e,f);const double ee=airportjetcollision::Dot(e,e),ef=airportjetcollision::Dot(e,f),ff=airportjetcollision::Dot(f,f),den=ee*ff-ef*ef;if(den<1e-12||std::fabs(airportjetcollision::Dot(tn,airportjetmodel::Unit(n)))<.999||std::fabs(airportjetcollision::Dot(d,n))/airportjetcollision::Length(n)>.001)continue;const double u=(airportjetcollision::Dot(d,e)*ff-airportjetcollision::Dot(d,f)*ef)/den,v=(airportjetcollision::Dot(d,f)*ee-airportjetcollision::Dot(d,e)*ef)/den;if(u>=-.00001&&v>=-.00001&&u+v<=1.00001){++coplanar;break;}}}return coplanar;};
 airportjetmodel::Pose pose;pose.gearDown=0;airportjetmodel::Animate(&clump,pose);const int lining=overlap("jet_hull","jet_lining"),door=overlap("jet_door","jet_steps");std::printf("Native two-sided exterior: %d lining / %d door coplanar faces\n",lining,door);CHECK(lining==0&&door==0);
 float mainArea=0;int bright=0;for(auto *part:clump.atomics)if(!std::strncmp(part->frame->name,"jet_baydoor_",12)){const bool main=part->frame->name[12]=='m';if(main){mainArea+=SurfaceArea(part->geometry);CVector lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};for(auto p:part->geometry->vertices){lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);}CHECK(hi.x-lo.x<.81f&&hi.y-lo.y<.81f);}for(auto color:part->geometry->colors)bright+=color.r>80||color.g>80||color.b>80;}std::printf("Small main bay doors: %.5fm2; %d unnaturally bright vertices\n",mainArea,bright);CHECK(mainArea<2.1f&&bright==0);
 Frame frame{};frame.matrix[0]=frame.matrix[5]=frame.matrix[10]=frame.matrix[15]=1;Atomic atomic{source,&frame};std::vector<airportjetmodel::Face> native;CHECK(airportjetmodel::Read(&atomic,native));std::vector<int> wheel;CHECK(airportjetmodel::Wheels(native,wheel));Geo expected=*source;expected.triangles.clear();for(size_t i=0;i<native.size();++i)if(wheel[i]<0&&airportjetmodel::Strut(native[i])<0&&!(i>=1104&&i<=1111))expected.triangles.push_back(source->triangles[i]);atomic.geometry=&expected;Clump original{&frame,{&atomic}};Geo panes=expected;panes.triangles.clear();for(size_t i=0;i<native.size();++i)if(airportjetmodel::SideGlass(native[i])||airportjetmodel::Glass(native[i]))panes.triangles.push_back(source->triangles[i]);Atomic paneAtomic{&panes,&frame};Clump originalPanes{&frame,{&paneAtomic}};
 int silhouette=0;float worst=0;for(float x=-1.45f;x<1.5f;x+=.1f)for(float y=-1.5f;y<-.34f;y+=.1f)for(float side:{-1.f,1.f}){const CVector eye{x,y,side*3},direction{0,0,-side};const float old=Ray(original,eye,direction,nullptr,false,false),now=Ray(clump,eye,direction,nullptr,false,false),error=std::fabs(old-now);const float pane=Ray(originalPanes,eye,direction,nullptr,false,false);const float reveal=Ray(clump,eye,direction,"jet_window_trim",false,false);if(std::fabs(old-pane)<.08f||(std::fabs(now-reveal)<.0001f&&error<.01f))continue; // measured real glazing replaces its original opaque backing
 if(old<1e8&&error>.001f){if(silhouette<6)std::printf("Silhouette mismatch %.3f %.3f side%.0f oldZ%.6f nowZ%.6f\n",x,y,side,side*(3-old),side*(3-now));++silhouette;worst=std::max(worst,error);}}
 std::printf("Original wing/body silhouette: %d changed rays, max%.5fm\n",silhouette,worst);CHECK(silhouette==0);
 int lateral=0;float lateralWorst=0;for(float y=-2.45f;y<=6.f;y+=.125f)for(float z=-1.28f;z<=.5f;z+=.125f)for(float side:{-1.f,1.f}){const CVector eye{side*15,y,z},direction{-side,0,0};const float old=Ray(original,eye,direction),now=Ray(clump,eye,direction),pane=Ray(originalPanes,eye,direction),error=std::fabs(old-now);if(std::fabs(old-pane)<.08f)continue;if(old<1e8f&&error>.001f){if(lateral<5)std::printf("Lateral exterior change Y%.3f Z%.3f side%.0f %.6fm\n",y,z,side,error);++lateral;lateralWorst=std::max(lateralWorst,error);}}
 std::printf("Original lateral exterior: %d changed rays, max%.6fm\n",lateral,lateralWorst);CHECK(lateral==0);

 airportjetmodel::Animate(&clump,airportjetmodel::Pose{});
}
void Collision(Clump &clump,Geo *source){
 using namespace airportjetcollision;
 Input input{Each,AtomicGeometry,Morph,VertexCount,TriangleCount,Vertices,reinterpret_cast<const Triangle *(*)(const void *)>(Triangles),AtomicFrame,LTM,reinterpret_cast<const char *(*)(void *)>(FrameName)};
 std::vector<CVector> samples;CHECK(Samples(&clump,input,samples));
 alignas(16) uint8_t original[0x68]{},oldBoxes[56]{},collision[0x68]{},spheres[32*20]{},boxes[56]{};
 const char *env=std::getenv("GAMEDATA");const std::string base=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";
 std::vector<uint8_t> bytes;CHECK(ReadGameFile(base+"/models/coll/generic.col",bytes));bool found=false;
 for(size_t at=0;at+80<=bytes.size();){const size_t end=at+8+Get<uint32_t>(bytes,at+4);if(end>bytes.size()||end<=at)break;
  if(std::string(reinterpret_cast<const char *>(bytes.data()+at+8),strnlen(reinterpret_cast<const char *>(bytes.data()+at+8),22))=="ap_learjet1_01"){
   found=true;Vector(original,0)=Get<CVector>(bytes,at+36);*reinterpret_cast<float *>(original+12)=Get<float>(bytes,at+32);Vector(original,0x10)=Get<CVector>(bytes,at+48);Vector(original,0x1c)=Get<CVector>(bytes,at+60);
   *reinterpret_cast<int16_t *>(original+0x28)=2;*reinterpret_cast<int16_t *>(original+0x2a)=2;size_t cursor=at+116;cursor+=4+size_t(Get<uint32_t>(bytes,cursor))*16;CHECK(Get<uint32_t>(bytes,cursor)==2);cursor+=4;CHECK(cursor+56<=end);std::memcpy(oldBoxes,bytes.data()+cursor,56);*reinterpret_cast<uint8_t **>(original+0x48)=oldBoxes;break;
  }at=end;
 }
 CHECK(found);CHECK(Build(samples,original,collision,spheres,boxes));
 if(!found)return;CHECK(std::fabs(Vector(collision,0x10).z-jetwheels::kTireBottom)<.00001f);
 for(int i=0;i<32;++i)CHECK(Vector(spheres,i*20).z-*reinterpret_cast<float *>(spheres+i*20+12)>=jetwheels::kTireBottom+.199f);
 auto gap=[&](CVector p){float best=1e9f;for(int i=0;i<32;++i)best=std::min(best,Length(Sub(p,Vector(spheres,i*20)))-*reinterpret_cast<float *>(spheres+i*20+12));return best;};
 int total=0,uncovered=0;float worst=0;
 for(size_t k=0;k<source->triangles.size();++k){const bool gear=(k>=577&&k<=660)||(k>=775&&k<=1047)||(k>=211&&k<=213)||(k>=560&&k<=562);if(gear)continue;const auto &face=source->triangles[k];
  const CVector a=source->vertices[face.v[0]],b=source->vertices[face.v[1]],c=source->vertices[face.v[2]];
  CHECK(gap(a)<=.05f&&gap(b)<=.05f&&gap(c)<=.05f);
  CHECK(gap(Mul(Add(a,b),.5f))<=.05f&&gap(Mul(Add(a,c),.5f))<=.05f&&gap(Mul(Add(b,c),.5f))<=.05f);
  for(int i=1;i<10;++i)for(int j=1;j<10-i;++j){const float u=i*.1f,v=j*.1f;const CVector p=Add(a,Add(Mul(Sub(b,a),u),Mul(Sub(c,a),v)));const float g=gap(p);++total;uncovered+=g>.05f;worst=std::max(worst,g);}
 }
 std::printf("Prepared model -> native frame transforms -> body COL: %d/%d uncovered, worst %.4fm, %zu samples\n",uncovered,total,worst,samples.size());CHECK(uncovered==0);
 // Prepared flap vertices are local to a nonzero hinge pivot: this would fail
 // if collection interpreted them as untransformed original mesh vertices.
 for(const char *part:{"jet_glass","jet_flap_l","jet_flap_r"}){auto *atomic=Part(clump,part);CHECK(atomic);if(atomic)for(const auto &p:atomic->geometry->vertices)CHECK(gap(ModelPoint(atomic->frame,p))<=.05f);}
}
int main(){
 NativeContract();
 CHECK(airportjetmodel::Install());Clump donor=LoadCockpit();auto *source=Load();if(!source)return 1;++source->refs;
 Clump clump{static_cast<Frame *>(FrameCreate()),{}};std::strcpy(clump.root->name,"ap_learjet1_01");auto *a=static_cast<Atomic *>(AtomicCreate());a->geometry=source;a->frame=clump.root;clump.atomics.push_back(a);
 const uint64_t sourceFingerprint=GeometryFingerprint(*source),donorFingerprint=GeometryFingerprint(*donor.atomics[0]->geometry);
 const auto before=source->triangles;const int rootInfo=0x12345678;clump.root->visibility=rootInfo;
 CHECK(!airportjetmodel::Prepare(&clump)); // never silently keep the procedural cabin when source227 is unavailable
 CHECK(!airportjetmodel::Prepare(&clump,&clump));
 auto *policeGeometry=donor.atomics[0]->geometry;const auto policeFaces=policeGeometry->triangles;const Color policeColor=policeGeometry->materials[1]->color;
 std::strcpy(donor.atomics[0]->frame->name,"wrong_chassis");CHECK(!airportjetmodel::Prepare(&clump,&donor));std::strcpy(donor.atomics[0]->frame->name,"chassis_hi");
 auto *atlas=policeGeometry->materials[1]->texture;policeGeometry->materials[1]->texture=nullptr;CHECK(!airportjetmodel::Prepare(&clump,&donor));policeGeometry->materials[1]->texture=atlas;
 CHECK(clump.atomics.size()==1&&a->geometry==source&&clump.root->visibility==rootInfo);
 // A missing native buffer must destroy the still-locked, unpopulated
 // geometry without asking native unlock/buildMeshes to read0xffff faces.
 {const int geometries=liveGeometry,materials=liveMaterial,frames=liveFrame,atomics=liveAtomic;missingBuffer=true;CHECK(!airportjetmodel::Prepare(&clump,&donor));missingBuffer=false;CHECK(invalidUnlocks==0);CHECK(liveGeometry==geometries&&liveMaterial==materials&&liveFrame==frames&&liveAtomic==atomics);}
 // Every allocation boundary must leave the original template and its native
 // root visibility union intact. The same template is retried after failure.
 bool prepared=false;for(int at=1;at<512;++at){failAt=at;allocation=0;const int geometries=liveGeometry,materials=liveMaterial,frames=liveFrame,atomics=liveAtomic;
  if(airportjetmodel::Prepare(&clump,&donor)){prepared=true;break;}
  CHECK(clump.atomics.size()==1&&a->geometry==source&&clump.root->visibility==rootInfo);
  CHECK(liveGeometry==geometries&&liveMaterial==materials&&liveFrame==frames&&liveAtomic==atomics);
 }
 failAt=0;CHECK(invalidUnlocks==0);CHECK(prepared&&clump.root->visibility==rootInfo);if(failures){Destroy(donor);Destroy(clump);GeometryDestroy(source);return 1;}
 GearPackingOwnership(clump);CabinCleanup(clump);GearVisibleContinuity(clump);PackedSourceConservation(clump);AllComponentDepth(clump);
 RearSeatProvenance(clump,donor,source);CockpitProvenance(clump,donor,source);
 CHECK(policeGeometry->triangles.size()==policeFaces.size()&&!std::memcmp(policeGeometry->triangles.data(),policeFaces.data(),policeFaces.size()*sizeof(Tri)));
 CHECK(!std::memcmp(&policeGeometry->materials[1]->color,&policeColor,sizeof policeColor));
 Collision(clump,source);
 // Native source has fixed tail surfaces, three actual struts and a painted
 // closed door. Preparation must partition these into independently owned
 // geometry before any vehicle clones the template.
 for(const char *name:{"jet_elevator_l","jet_elevator_r","jet_rudder","jet_strut_n","jet_strut_l","jet_strut_r","jet_door","jet_lining","jet_steps"}){
  auto *part=Part(clump,name);CHECK(part&&part->geometry&&!part->geometry->triangles.empty());
 }
 float area=0;for(auto *part:clump.atomics)if(!std::strstr(part->frame->name,"_basis")&&std::strncmp(part->frame->name,"jet_cabin",9)&&std::strcmp(part->frame->name,"jet_lenses")&&std::strcmp(part->frame->name,"jet_lining")&&std::strncmp(part->frame->name,"jet_step",8)&&std::strcmp(part->frame->name,"jet_baywell")&&std::strcmp(part->frame->name,"jet_dashboard")&&std::strcmp(part->frame->name,"jet_window_trim")&&std::strcmp(part->frame->name,"jet_bayrim"))area+=SurfaceArea(part->geometry);
 // Measured source-union removal includes the exact ten native pane
 // footprints (0.02410m2 more than the old approximate rounded rectangles),
 // localized root housings and buried gear skins; all other source area stays.
 std::printf("Source skin area %.7f original %.7f removed %.7f\n",area,SurfaceArea(source),SurfaceArea(source)-area);// Independent double-precision clipping of the actual asset gives these
 // three removals; the retained area is not fitted to the new model output.
 constexpr float windowBacking=3.07115064f,staticShadow=1.80838765f,buriedCaps=.007880535f;
 CHECK(std::fabs(area+windowBacking+staticShadow+buriedCaps-SurfaceArea(source))<.002f);
 auto *glass=Part(clump,"jet_glass");CHECK(glass&&glass->geometry->triangles.size()==94);
 if(glass)for(auto *material:glass->geometry->materials)CHECK(material->color.a<=40&&!material->texture);
 CHECK(Part(clump,"jet_hull")&&Part(clump,"jet_cabin"));
 auto *cabin=Part(clump,"jet_cabin");CHECK(cabin);if(cabin){CHECK(cabin->geometry->triangles.size()>=300);for(auto *m:cabin->geometry->materials)CHECK(m->texture&&m->texture->name=="polmav8bit128");}
 for(const char *name:{"jet_tire_n","jet_tire_l","jet_tire_r"}){auto *tire=Part(clump,name);CHECK(tire&&tire->geometry->triangles.size()==(name[9]=='n'?92u:1840u));}
 CHECK(Part(clump,"jet_flap_l")&&Part(clump,"jet_flap_r"));CHECK(Part(clump,"jet_lenses"));
 CHECK(source->triangles.size()==before.size()&&!std::memcmp(source->triangles.data(),before.data(),before.size()*sizeof(Tri)));CHECK(source->materials[0]->color.a==255&&source->materials[0]->texture);
 const size_t parts=clump.atomics.size();CHECK(airportjetmodel::Prepare(&clump)&&clump.atomics.size()==parts);
 std::array<jetwheels::WheelPose,3> pose{};pose[0].spin=.7f;pose[0].compression=.05f;pose[0].steer=.2f;airportjetmodel::Animate(&clump,pose,.3f,.1f);
 auto *wheel=Find(clump.root,"jet_wheel_n"),*tire=Find(clump.root,"jet_tire_n");CHECK(wheel&&tire);if(wheel&&tire){CHECK(std::fabs(wheel->matrix[14]-(jetwheels::kGear[0].center.z+.05f))<.0001f);CHECK(std::fabs(tire->matrix[6]-std::sin(.7f))<.0001f);CHECK(std::fabs(wheel->matrix[1]-std::sin(.2f))<.0001f);}
 auto *flap=Find(clump.root,"jet_flap_l");CHECK(flap&&std::fabs(flap->matrix[6])>.1f);
 airportjetmodel::Animate(&clump,airportjetmodel::Pose{});Interior(clump,source);SourceAttributes(clump,source);DashboardProvenance(clump,donor,source);PanelReadability(clump);CockpitView(clump);WindowRims(clump);BeaconClearance(clump,source);NativeExterior(clump,source);GearBays(clump);


 airportjetmodel::Pose systems;systems.elevator=.3f;systems.rudder=.25f;systems.gearDown=0;systems.doorOpen=1;airportjetmodel::Animate(&clump,systems);
 auto *elevator=Find(clump.root,"jet_elevator_l"),*rudder=Find(clump.root,"jet_rudder"),*door=Find(clump.root,"jet_door");
 CHECK(elevator&&std::fabs(elevator->matrix[6])>.1f);CHECK(rudder&&std::fabs(rudder->matrix[1])>.1f);CHECK(door&&std::fabs(door->matrix[8])>.5f);
 for(const char *name:{"jet_tire_n","jet_tire_l","jet_tire_r","jet_strut_n","jet_strut_l","jet_strut_r"}){
  auto *part=Part(clump,name);CHECK(part);if(part)for(const auto &t:Surface(clump,name)){const auto a=airportjetcollision::Sub(t.p[1],t.p[0]),b=airportjetcollision::Sub(t.p[2],t.p[0]);if(airportjetcollision::Length(airportjetmodel::Cross(a,b))<.0000001f)continue;for(auto q:t.p){CHECK(airportjetcollision::Finite(q));CHECK(InsideAirframe(source,q));}}
 }
 CHECK(Ray(clump,{0,3.7f,-.1f},{-1,0,0},nullptr,true)>2);
 for(const char *name:{"jet_step_1","jet_step_2","jet_step_3"}){auto *step=Part(clump,name);CHECK(step);if(step){float low=1e9f,high=-1e9f,left=1e9f,right=-1e9f;for(auto p:step->geometry->vertices){p=ModelPoint(step->frame,p);low=std::min(low,p.z);high=std::max(high,p.z);left=std::min(left,p.x);right=std::max(right,p.x);}std::printf("Inner door tread %s X%.6f..%.6f Z%.6f..%.6f\n",name,left,right,low,high);CHECK(high-low<.0001f&&high< -1.1f);}}
 // Moving excluded gear/door geometry must never contaminate the fixed body
 // sampling contract. Samples retain the source hull after animation too.
 airportjetcollision::Input native{Each,AtomicGeometry,Morph,VertexCount,TriangleCount,Vertices,reinterpret_cast<const airportjetcollision::Triangle *(*)(const void *)>(Triangles),AtomicFrame,LTM,reinterpret_cast<const char *(*)(void *)>(FrameName)};
 std::vector<CVector> deployed,stowed;airportjetmodel::Animate(&clump,airportjetmodel::Pose{});CHECK(airportjetcollision::Samples(&clump,native,deployed));systems.elevator=systems.rudder=0;airportjetmodel::Animate(&clump,systems);CHECK(airportjetcollision::Samples(&clump,native,stowed));CHECK(deployed.size()==stowed.size()&&!std::memcmp(deployed.data(),stowed.data(),deployed.size()*sizeof(CVector)));
 for(int cycle=0;cycle<4;++cycle){airportjetmodel::Animate(&clump,airportjetmodel::Pose{});airportjetmodel::Animate(&clump,systems);}
 for(auto *part:clump.atomics)for(const auto &p:part->geometry->vertices)CHECK(airportjetcollision::Finite(ModelPoint(part->frame,p)));
 CHECK(GeometryFingerprint(*source)==sourceFingerprint&&GeometryFingerprint(*donor.atomics[0]->geometry)==donorFingerprint);
 Destroy(donor); // prepared native geometry owns material refs independently
 if(cabin)for(auto *m:cabin->geometry->materials)CHECK(m->texture&&m->texture->name=="polmav8bit128");
 Destroy(clump);GeometryDestroy(source);CHECK(liveGeometry==0&&liveMaterial==0&&liveFrame==0&&liveAtomic==0);
 if(!failures)std::puts("airportjetmodel actual assets: all passed");return failures?1:0;
}

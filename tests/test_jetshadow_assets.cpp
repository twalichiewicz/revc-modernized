// Actual private Learjet preparation -> exact native dynamic geometry
// consumer. The fixture adapts native-shaped RW objects, never replaces the
// native eligibility or triangle/material/raster predicates with a model.
#define main ExistingModelChecks
#include "test_airportjetmodel.cpp"
#undef main
#include "nativejetshadowarm.h"
#include "../src/planes.cpp"
#include <stdexcept>

Config gConfig;
namespace game {Api api;}
void Log(const char *,...){}
namespace hooks {void *Install(const char *,void *){return nullptr;}}
namespace flightpath {void Raise(std::vector<Vec3> &) {}}
namespace shaders {Sky CurrentSky(){Sky out{};out.sun={0,0,1};return out;}CVector CameraPosition(){return {};}}
namespace sky {bool Through(const CVector &,const CVector &,float &shows){shows=1;return true;}bool SunAt(const CVector &,float &sun){sun=.4f;return true;}}

namespace {
using nativejetshadowtest::Store;
struct NativeAtomic {
 alignas(16)uint8_t atomic[0x80]{},geometry[0xa0]{},morph[0x30]{},frame[0x80]{};
 std::vector<std::array<uint8_t,16>> materials;
 std::vector<std::array<uint8_t,32>> rasters;
 std::vector<std::array<uint8_t,8>> textures;
 std::vector<void *> materialPointers;
 NativeAtomic(Atomic *source,const std::map<std::string,uint32_t> &formats){
  auto *g=source->geometry;materials.resize(g->materials.size());rasters.resize(g->materials.size());textures.resize(g->materials.size());materialPointers.resize(g->materials.size());
  std::memcpy(frame,LTM(source->frame),64);std::strncpy(reinterpret_cast<char *>(frame+64),source->frame->name,23);
  atomic[2]=uint8_t(source->flags);Store(atomic+8,frame);Store(atomic+0x28,geometry);Store(geometry+0x10,g->flags);Store(geometry+0x18,int(g->triangles.size()));Store(geometry+0x28,g->triangles.data());Store(geometry+0x78,morph);Store(morph+0x18,g->vertices.data());Store(geometry+0x80,materialPointers.data());Store(geometry+0x88,int(materials.size()));
  for(size_t i=0;i<materials.size();++i){auto *m=g->materials[i];materials[i][11]=m->color.a;materialPointers[i]=materials[i].data();if(m->texture){const auto f=formats.find(m->texture->name);CHECK(f!=formats.end());if(f==formats.end())continue;Store(rasters[i].data()+0x10,f->second);Store(textures[i].data(),rasters[i].data());Store(materials[i].data(),textures[i].data());}}
 }
};
const float *NativeLTM(void *p){return static_cast<float *>(p);}
const char *NativeName(void *p){return reinterpret_cast<const char *>(static_cast<uint8_t *>(p)+64);}
int NativeType(void *){return 2;}
const char *NativeStrstr(const char *a,const char *b){return std::strstr(a,b);}
const uint8_t *MaterialBytes(const void *p){return reinterpret_cast<const uint8_t *>(GetColor(p));}
void SetMaterialBytes(void *p,const uint8_t *c){SetColor(p,reinterpret_cast<const Color *>(c));}
const uint16_t *TriangleWords(const void *p){return reinterpret_cast<const uint16_t *>(Triangles(p));}
void *directHandle;std::array<float,4> lastDirect{};
void LightColor(void *light,const float *color){if(light==directHandle)std::copy(color,color+4,lastDirect.begin());}
int lightRestores=0;
void RestoreAmbient(){++lightRestores;}
std::map<Mat *,Color> renderOriginal;
Clump *renderClump;int drawDepth=0,drawCalls=0;bool nestedDraw=false,throwDraw=false;uint8_t renderAlpha=127;
void DrawPlane(uint8_t *p){
 struct Depth {Depth(){++drawDepth;}~Depth(){--drawDepth;}} depth;++drawCalls;
 auto check=[&]{for(const auto &[material,color]:renderOriginal){int alpha=color.a;for(int i=0;i<drawDepth;++i)alpha=alpha*renderAlpha/255;CHECK(material->color.a==alpha);CHECK(material->color.r==color.r&&material->color.g==color.g&&material->color.b==color.b);}if(renderAlpha<255)for(auto *a:renderClump->atomics)CHECK(a->geometry->flags&planes::kModulate);};
 check();if(nestedDraw&&drawDepth==1){planes::Hook_Render(p);check();}if(throwDraw&&drawDepth==1)throw std::runtime_error("native draw interrupted");
}
void CloudRender(uint8_t *plane,Clump &clump,float shows=.5f){
 planes::gObjectType=[](const void *){return 2;};planes::gForAllAtomics=Each;planes::gAtomicGeometry=AtomicGeometry;
 planes::gGeometryFlags=Flags;planes::gSetGeometryFlags=SetFlags;planes::gMaterial=GetMaterial;planes::gMaterialColour=MaterialBytes;planes::gSetMaterialColour=SetMaterialBytes;planes::gNumTriangles=TriangleCount;planes::gTriangles=TriangleWords;
 static int ambientMarker,directMarker;static void *ambient=&ambientMarker,*direct=&directMarker;directHandle=direct;static const float color[]={1,1,1,1};planes::gAmbient=&ambient;planes::gDirect=&direct;planes::gDirectionalColour=color;planes::gSetLightColour=LightColor;planes::gCarsAmbient=RestoreAmbient;planes::orig_Render=DrawPlane;
 renderClump=&clump;renderAlpha=uint8_t(shows*255);Store(plane+0x58,&clump);planes::gSeen={{plane,shows}};planes::Hook_Render(plane);
}
std::map<std::string,uint32_t> Formats(){
 const char *env=std::getenv("GAMEDATA");const std::string base=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";ImgArchive archive;CHECK(archive.Open(base+"/models/gta3.img"));std::map<std::string,uint32_t> out;
 for(const char *name:{"ap_learjets.txd","polmav.txd"}){std::vector<uint8_t>b;CHECK(archive.Read(name,b));for(size_t t=28;t+112<b.size()&&Get<uint32_t>(b,t)==21;t+=12+Get<uint32_t>(b,t+4)){const size_t s=t+24;const std::string texture(reinterpret_cast<const char *>(b.data()+s+8),strnlen(reinterpret_cast<const char *>(b.data()+s+8),32));out[texture]=Get<uint32_t>(b,s+72);}}
 CHECK(out["ap_learjet"]==0x200&&out["black64"]==0x200);return out;
}
}
int main(){
 using namespace nativejetshadowtest;
 CHECK(airportjetmodel::Install());Clump donor=LoadCockpit();auto *source=Load();if(!source)return 1;++source->refs;
 Clump clump{static_cast<Frame *>(FrameCreate()),{}};std::strcpy(clump.root->name,"ap_learjet1_01");auto *initial=static_cast<Atomic *>(AtomicCreate());initial->geometry=source;initial->frame=clump.root;clump.atomics.push_back(initial);CHECK(airportjetmodel::Prepare(&clump,&donor));if(failures)return 1;
 const auto formats=Formats();Code code;code.Add(0x10016c540,0x10016ca90);code.Add(0x10016ca90,0x10016d25c);code.Add(0x1002a9480,0x1002a94a4);
 const auto poolOffset=code.Add(0x10016b0ec,0x10016b158);Store(code.code+poolOffset+0x6c,uint32_t(0xd61f02c0));
 CHECK(code.image.Word(0x10016b100)==0x5280de09); // native stride0x6f0
 CHECK(code.image.Word(0x10016b13c)==0x52800002); // ordinary vehicle modefalse
 code.bindings={{0x10029c378,reinterpret_cast<void *>(NativeLTM)},{0x1002329a4,reinterpret_cast<void *>(NativeName)},{0x10016f5a0,reinterpret_cast<void *>(NativeType)},{0x1002f5940,reinterpret_cast<void *>(NativeStrstr)}};
 code.Finish();using Append=void (*)(uint8_t *,const CVector &,float,bool);auto append=reinterpret_cast<Append>(code.Address(0x10016ca90));auto poolBlock=reinterpret_cast<void (*)()>(code.Address(0x10016b0ec));
 // A real native vector with ample backing avoids irrelevant growth/allocator
 // calls while retaining native triangle and world-transform writes.
 std::vector<float> coordinates(200000);float *start=coordinates.data();Store(code.Page(0x1007cc1f8),start);Store(code.Page(0x1007cc200),start);Store(code.Page(0x1007cc208),start+coordinates.size());
 GuardedPlane plane;Store(plane.entity+0x6a,int16_t(248));Store(plane.entity+0x348,int32_t(4));
 std::map<Mat *,Color> saved,sourceColors;std::map<Geo *,uint32_t> savedFlags;for(auto *a:clump.atomics){savedFlags.emplace(a->geometry,a->geometry->flags);for(auto *m:a->geometry->materials)saved.emplace(m,m->color);}renderOriginal=saved;
 for(auto *m:source->materials)sourceColors.emplace(m,m->color);for(auto *a:donor.atomics)for(auto *m:a->geometry->materials)sourceColors.emplace(m,m->color);
 auto restored=[&]{for(const auto &[material,color]:saved)CHECK(!std::memcmp(&material->color,&color,sizeof color));for(const auto &[material,color]:sourceColors)CHECK(!std::memcmp(&material->color,&color,sizeof color));for(const auto &[geometry,flags]:savedFlags)CHECK(geometry->flags==flags);};
 CloudRender(plane.entity,clump);restored();
 int damaged=0;for(const auto &[material,color]:saved)if(std::memcmp(&material->color,&color,sizeof color))++damaged;
 std::printf("Actual plane cloud Render retained %d modified aliased material colors\n",damaged);CHECK(damaged==0);
 nestedDraw=true;CloudRender(plane.entity,clump);nestedDraw=false;restored();
 const int lightBefore=lightRestores;throwDraw=true;try{CloudRender(plane.entity,clump);CHECK(false);}catch(const std::runtime_error &){}throwDraw=false;restored();CHECK(lightRestores==lightBefore+1);CHECK((lastDirect==std::array<float,4>({1,1,1,1})));
 const int calls=drawCalls,earlyLights=lightRestores;CloudRender(plane.entity,clump,.01f);CHECK(drawCalls==calls&&lightRestores==earlyLights);restored();CloudRender(plane.entity,clump,1);restored();
 // The same geometry referenced by two atomics must also fade exactly once.
 auto *alias=clump.atomics.front();clump.atomics.push_back(alias);CloudRender(plane.entity,clump);clump.atomics.pop_back();restored();
 alignas(16)uint8_t pool[24]{};uint8_t *poolPointer=pool,flags=0;Store(pool,plane.entity);Store(pool+8,&flags);Store(pool+0x10,int32_t(1));
 // Exercise a positioned/rotated actor hierarchy, as the cloned CPlane's
 // attached root would be updated by CMatrix::UpdateRW/UpdateRwFrame.
 const float world[]={0,1,0,0,-1,0,0,0,0,0,1,0,132,-273,19,1};std::memcpy(clump.root->matrix,world,sizeof world);Store(plane.entity+0x38,CVector{132,-273,19});
 int fullHull=0,fullTotal=0;
 for(float gear:{1.f,.5f,0.f})for(float door:{0.f,.5f,1.f}){
  airportjetmodel::Pose pose;pose.gearDown=gear;pose.doorOpen=door;airportjetmodel::Animate(&clump,pose);
  alignas(16)uint8_t nativeClump[0x20]{};uint8_t *head=nativeClump+0x10,*previous=head;std::vector<std::unique_ptr<NativeAtomic>> native;
  for(auto *a:clump.atomics){native.push_back(std::make_unique<NativeAtomic>(a,formats));auto *node=native.back()->atomic+0x58;Store(previous,node);Store(node+8,previous);previous=node;}Store(previous,head);Store(head+8,previous);Store(plane.entity+0x58,nativeClump);
  const CVector camera{132,-273,25};Store(code.Page(0x1007cc200),start);VehiclePool(poolBlock,&poolPointer,&camera);const auto count=(Read<float *>(code.Page(0x1007cc200))-start)/9;int expected=0;
  for(size_t i=0;i<native.size();++i){const auto *g=clump.atomics[i]->geometry;int part=0;if(!(clump.atomics[i]->flags&4))continue;for(auto t:g->triangles){const auto *m=g->materials[t.material];const uint32_t format=m->texture?formats.at(m->texture->name)&0xf00:0;if(m->color.a>=200&&format!=0x100&&format!=0x300&&format!=0x500)++part;}expected+=part;if(!std::strcmp(clump.atomics[i]->frame->name,"jet_hull"))fullHull=part;}
  CHECK(count==expected&&count>1000&&fullHull>500);for(ptrdiff_t i=0;i<count*9;++i){const float centre[]={132,-273,19};CHECK(std::isfinite(coordinates[size_t(i)])&&std::fabs(coordinates[size_t(i)]-centre[i%3])<30);}fullTotal=int(count);
  // The actual native world-distance gate excludes the same prepared actor.
  Store(code.Page(0x1007cc200),start);append(plane.entity,{333,-273,19},200,false);CHECK(Read<float *>(code.Page(0x1007cc200))==start);
  flags=128;VehiclePool(poolBlock,&poolPointer,&camera);CHECK(Read<float *>(code.Page(0x1007cc200))==start);flags=0;
 }
 std::printf("Native MetalRT prepared Learjet: %d dynamic tris; %d hull tris; nine gear/door poses checked at model248/class4\n",fullTotal,fullHull);
 Destroy(clump);GeometryDestroy(source);Destroy(donor);CHECK(liveGeometry==0&&liveMaterial==0&&liveFrame==0&&liveAtomic==0);
 // Retain the ordinary native airliner's real draw and exact material and
 // geometry flags around cloud fading, without an owned-model special case.
 Clump airliner{static_cast<Frame *>(FrameCreate()),{}};auto *airlinerGeometry=Load("airtrain.dff");CHECK(airlinerGeometry);if(airlinerGeometry){auto *a=static_cast<Atomic *>(AtomicCreate());a->geometry=airlinerGeometry;a->frame=airliner.root;airliner.atomics.push_back(a);Store(plane.entity+0x6a,int16_t(180));renderOriginal.clear();for(auto *m:airlinerGeometry->materials)renderOriginal.emplace(m,m->color);const auto originalFlags=airlinerGeometry->flags;CloudRender(plane.entity,airliner);for(const auto &[material,color]:renderOriginal)CHECK(!std::memcmp(&material->color,&color,sizeof color));CHECK(airlinerGeometry->flags==originalFlags);}
 Destroy(airliner);CHECK(liveGeometry==0&&liveMaterial==0&&liveFrame==0&&liveAtomic==0);
 if(!failures)std::puts("jetshadow real assets/native consumer: all passed");return failures?1:0;
}

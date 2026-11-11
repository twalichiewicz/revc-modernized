// Per-actor material bodies own geometry. Native class physics owns motion;
// weapon/contact adapters queue incidents, and a safe world boundary publishes
// the resulting render mesh, collision mesh, components and detached bodies.
#include "common.h"
#include "main.h"
#include "AircraftBody.h"
#include "aircraftbodymodel.h"
#include "boundsindex.h"
#include "BodyContact.h"
#include "BodyLoads.h"
#include "BodyAffine.h"
#include "BodyPublication.h"
#include "DodoRig.h"
#include "GameplayWing.h"
#include "Plane.h"
#include "Vehicle.h"
#include "Automobile.h"
#include "Bike.h"
#include "Boat.h"
#include "Heli.h"
#include "VehicleModelInfo.h"
#include "VisibilityPlugins.h"
#include "RwHelper.h"
#include "SourceGates.h"
#include "WeaponType.h"
#include "Object.h"
#include "Pools.h"
#include "World.h"
#include "Timer.h"
#include "NodeName.h"
#include "ModelIndices.h"
#include "Particle.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
namespace {
using Role=aircraftbodymodel::Role;
CVector V(Vec3 p){return CVector(p.x,p.y,p.z);}Vec3 P(CVector p){return {p.x,p.y,p.z};}
bool Finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
// Native sector registration writes outside CWorld::ms_aSectors past the grid
// (skyline planes fly south of y=-2000). Nothing is re-registered out there.
bool InSectorGrid(CEntity *e){if(!e)return false;CRect r=e->GetBoundRect();return bodypublication::InsideGrid(r.left,r.bottom,r.right,r.top,WORLD_MIN_X,WORLD_MIN_Y,WORLD_MAX_X,WORLD_MAX_Y);}
CMatrix Local(RwFrame *frame,RwFrame *root){CMatrix result;result.SetUnity();for(;frame&&frame!=root;frame=RwFrameGetParent(frame)){CMatrix matrix(RwFrameGetMatrix(frame),false);result=matrix*result;}return result;}
CMatrix InverseFrame(CMatrix matrix,bool *valid=nullptr){auto r=matrix.GetRight(),f=matrix.GetForward(),u=matrix.GetUp(),p=matrix.GetPosition();bodyaffine::Matrix input{{r.x,r.y,r.z,f.x,f.y,f.z,u.x,u.y,u.z,p.x,p.y,p.z}},out;bool ok=bodyaffine::Inverse(input,out);if(valid)*valid=ok;CMatrix result;result.SetUnity();if(ok){result.GetRight()=CVector(out[0],out[1],out[2]);result.GetForward()=CVector(out[3],out[4],out[5]);result.GetUp()=CVector(out[6],out[7],out[8]);result.GetPosition()=CVector(out[9],out[10],out[11]);}return result;}
struct Vertex {CVector p,n;rw::TexCoords uv;rw::RGBA color;};
struct Face {std::array<Vertex,3> v;rw::Material *material;uint32_t group;bool glass=false;};
struct RenderTriangle {std::array<Vertex,3> v;rw::Material *material;bool glass=false;};
struct MeshDeleter{void operator()(rw::Geometry *p)const{if(p)p->destroy();}};
using Geometry=std::unique_ptr<rw::Geometry,MeshDeleter>;
Geometry GeometryFor(const std::vector<RenderTriangle> &triangles){
 if(triangles.empty())return {};
 if(triangles.size()>16000)return {};
 Geometry geometry(rw::Geometry::create(triangles.size()*3,triangles.size(),rw::Geometry::POSITIONS|rw::Geometry::NORMALS|rw::Geometry::PRELIT|rw::Geometry::TEXTURED|rw::Geometry::LIGHT|rw::Geometry::MODULATE));if(!geometry)return {};
 for(size_t i=0;i<triangles.size();++i){auto &t=triangles[i];int material=geometry->matList.findIndex(t.material);if(material<0)material=geometry->matList.appendMaterial(t.material);if(material<0)return {};geometry->triangles[i].matId=material;
  auto normal=CrossProduct(t.v[1].p-t.v[0].p,t.v[2].p-t.v[0].p);if(normal.MagnitudeSqr()>1e-12f)normal.Normalise();
  for(int j=0;j<3;++j){size_t index=i*3+j;auto &v=t.v[j];geometry->triangles[i].v[j]=index;geometry->morphTargets[0].vertices[index]=rw::makeV3d(v.p.x,v.p.y,v.p.z);geometry->morphTargets[0].normals[index]=rw::makeV3d(normal.x,normal.y,normal.z);geometry->colors[index]=v.color;geometry->texCoords[0][index]=v.uv;}
 }geometry->calculateBoundingSphere();geometry->unlock();return geometry;
}
std::unique_ptr<CColModel> CollisionFor(const std::vector<RenderTriangle> &triangles){
 if(triangles.empty()||triangles.size()>16000||!CPools::GetColModelPool()->GetNoOfFreeSpaces())return {};
 std::unique_ptr<CColModel> col(new CColModel);col->numTriangles=triangles.size();col->vertices=(CompressedVector*)RwMalloc(triangles.size()*3*sizeof(CompressedVector));col->triangles=(CColTriangle*)RwMalloc(triangles.size()*sizeof(CColTriangle));if(!col->vertices||!col->triangles)return {};
 CVector lo(1e10f,1e10f,1e10f),hi(-1e10f,-1e10f,-1e10f);
 for(size_t i=0;i<triangles.size();++i){for(int j=0;j<3;++j){auto p=triangles[i].v[j].p;col->vertices[i*3+j].Set(p.x,p.y,p.z);lo.x=Min(lo.x,p.x);lo.y=Min(lo.y,p.y);lo.z=Min(lo.z,p.z);hi.x=Max(hi.x,p.x);hi.y=Max(hi.y,p.y);hi.z=Max(hi.z,p.z);}col->triangles[i].Set(i*3,i*3+1,i*3+2,triangles[i].glass?7:6);}
 col->boundingBox.Set(lo,hi);col->boundingSphere.Set((hi-lo).Magnitude()*.5f,(hi+lo)*.5f);return col;
}
// The same instance COL from committed model-space vertices (three per triangle).
std::unique_ptr<CColModel> CollisionFrom(const std::vector<Vec3> &positions,const std::vector<uint8_t> &surfaces){
 const size_t count=surfaces.size();if(!count||count>bodypublication::kMaxTriangles||positions.size()!=count*3||!CPools::GetColModelPool()->GetNoOfFreeSpaces())return {};
 std::unique_ptr<CColModel> col(new CColModel);col->numTriangles=count;col->vertices=(CompressedVector*)RwMalloc(count*3*sizeof(CompressedVector));col->triangles=(CColTriangle*)RwMalloc(count*sizeof(CColTriangle));if(!col->vertices||!col->triangles)return {};
 CVector lo(1e10f,1e10f,1e10f),hi(-1e10f,-1e10f,-1e10f);
 for(size_t i=0;i<count;++i){for(int j=0;j<3;++j){auto p=V(positions[i*3+j]);col->vertices[i*3+j].Set(p.x,p.y,p.z);lo.x=Min(lo.x,p.x);lo.y=Min(lo.y,p.y);lo.z=Min(lo.z,p.z);hi.x=Max(hi.x,p.x);hi.y=Max(hi.y,p.y);hi.z=Max(hi.z,p.z);}col->triangles[i].Set(i*3,i*3+1,i*3+2,surfaces[i]);}
 col->boundingBox.Set(lo,hi);col->boundingSphere.Set((hi-lo).Magnitude()*.5f,(hi+lo)*.5f);return col;
}
// Native suspension lines extend the parent's private COL bounds downward.
bool AttachSuspension(CColModel &collision,const std::vector<CColLine> &suspension){
 if(suspension.empty())return true;collision.numLines=suspension.size();collision.lines=(CColLine*)RwMalloc(suspension.size()*sizeof(CColLine));if(!collision.lines)return false;std::copy(suspension.begin(),suspension.end(),collision.lines);for(auto &line:suspension){collision.boundingBox.min.z=Min(collision.boundingBox.min.z,Min(line.p0.z,line.p1.z));}collision.boundingSphere.Set((collision.boundingBox.max-collision.boundingBox.min).Magnitude()*.5f,(collision.boundingBox.max+collision.boundingBox.min)*.5f);return true;
}
// Face roles, glazing and render-only atomics: aircraftbodymodel::ClassifyFace,
// ClassifyMaterial and RenderOnlyAtomic, shared with the asset tests.
struct SweepPoint{CVector now,previous;size_t source=0;};
struct AtomicSelection {std::vector<uint8_t> glassMaterials;rw::Atomic *atomic=nullptr;std::string name;uint32 variant=0,flags=0;size_t ordinal=0;CMatrix local;};
bool SameSelection(const AtomicSelection &a,const AtomicSelection &b){return a.name==b.name&&a.variant==b.variant&&a.ordinal==b.ordinal;}
const AtomicSelection *SelectionFor(const std::vector<AtomicSelection> &selections,const AtomicSelection &key){for(auto &selection:selections)if(SameSelection(selection,key))return &selection;return nullptr;}
struct Binding{rw::Atomic *atomic=nullptr;rw::Geometry *original=nullptr,*current=nullptr;CMatrix local;std::string name;uint32 flags=0;size_t selection=0;std::array<float,12> sampledPose{{1,0,0,0,1,0,0,0,1,0,0,0}};
 // Pose last committed to the body's model for this group (a fresh model is unposed).
 std::array<float,12> modelPose{{1,0,0,0,1,0,0,0,1,0,0,0}};CMatrix inverseLocal;bool inverseLocalReady=false;};
struct Witness{Vec3 point,material,normal;float closing;uint32_t frame;size_t source;float lift=-1;bool penetrating=false;};
struct Event{enum Kind{Bullet,Contact,Burn,Yield}kind;Vec3 a,b;float value,limit;uint32_t when;int region=-1;size_t source=0;};
struct Prototype {int model;float mass;uint64_t key;aircraftbodymodel::Model modelState;};
std::vector<Prototype> prototypes;bool worldSuspended=false;
uint64_t SurfaceKey(const std::vector<aircraftbodymodel::Surface>&surfaces){uint64_t key=1469598103934665603ull;auto add=[&](const void* p,size_t bytes){auto b=(const unsigned char*)p;while(bytes--){key^=*b++;key*=1099511628211ull;}};for(auto&s:surfaces){add(&s.group,sizeof(s.group));auto role=int(s.role);add(&role,sizeof(role));add(&s.glass,sizeof(s.glass));add(&s.structural,sizeof(s.structural));for(auto p:s.points){add(&p.x,sizeof(float));add(&p.y,sizeof(float));add(&p.z,sizeof(float));}}return key;}
bool SameSurfaces(const std::vector<aircraftbodymodel::Surface>&a,const std::vector<aircraftbodymodel::Surface>&b){if(a.size()!=b.size())return false;for(size_t i=0;i<a.size();++i){if(a[i].group!=b[i].group||a[i].role!=b[i].role||a[i].glass!=b[i].glass||a[i].structural!=b[i].structural)return false;for(int k=0;k<3;++k)if(a[i].points[k].x!=b[i].points[k].x||a[i].points[k].y!=b[i].points[k].y||a[i].points[k].z!=b[i].points[k].z)return false;}return true;}
struct Body {
 CVehicle *plane=nullptr;int reference=-1,modelId=-1;uint64_t generation=0;bool captured=false;float yieldWork=0;float originalMass=0,originalTurnMass=0;CVector originalCOM,materialCOMOffset=CVector(0,0,0);aircraftbodymodel::Definition definition;std::vector<CColLine> suspension;CVector buoyancyMin,buoyancyMax;rw::Clump *clump=nullptr;std::vector<AtomicSelection> selections;std::vector<Binding> bindings;std::vector<Face> faces;aircraftbodymodel::Model model;std::vector<Event> events;std::vector<Witness> witnesses;std::vector<SweepPoint> sweep;CMatrix before;
 AircraftBodySnapshot snapshot;bodyloads::State loads;bool geometryLive=true,repair=false,healthyCapture=false,restoreOnDestruct=true;const Body *previous=nullptr;std::array<float,4> controls{};uint64_t published=~uint64_t(0);size_t bullets=0,contacts=0;double maxMs=0;
 // Publication health, committed parent-COL cache for pose-only publication,
 // and which parts carry material faces (only those accept a model pose).
 bodypublication::Gate publication;bodypublication::Failure publishFailure=bodypublication::Failure::None;bodypublication::Cache publishedMesh;std::vector<uint8_t> partFaces;bool replacementReported=false;
 bool Current()const{return geometryLive&&plane&&CPools::GetVehicle(reference)==plane&&plane->GetModelIndex()==modelId&&plane->m_rwObject&&RwObjectGetType(plane->m_rwObject)==rpCLUMP&&plane->GetClump()==clump&&std::all_of(bindings.begin(),bindings.end(),[](const Binding &b){return b.atomic->geometry==b.current;});}
 void RestoreGeometry(){for(auto &b:bindings){b.atomic->setGeometry(b.original,0);b.current=b.original;}for(auto &selection:selections)selection.atomic->setFlags(selection.flags);}
 ~Body(){if(restoreOnDestruct&&geometryLive&&plane&&CPools::GetVehicle(reference)==plane&&plane->m_rwObject&&RwObjectGetType(plane->m_rwObject)==rpCLUMP&&plane->GetClump()==clump)RestoreGeometry();for(auto &b:bindings)if(b.original)b.original->destroy();}
 static rw::Atomic *CaptureAtomic(rw::Atomic *atomic,void *data){auto &body=*(Body*)data;AtomicSelection selection;selection.atomic=atomic;auto frame=atomic->getFrame();auto node=frame?GetFrameNodeName(frame):nullptr;selection.name=node?node:"";selection.variant=CVisibilityPlugins::GetAtomicId(atomic)&(ATOMIC_FLAG_OK|ATOMIC_FLAG_DAM);selection.flags=atomic->getFlags();selection.local.SetUnity();if(frame)selection.local=Local(frame,body.clump->getFrame());for(auto &other:body.selections)if(other.name==selection.name&&other.variant==selection.variant)++selection.ordinal;
  if(atomic->geometry)for(int i=0;i<atomic->geometry->matList.numMaterials;++i)selection.glassMaterials.push_back(atomic->geometry->matList.materials[i]->color.alpha<255);
  if(body.previous)if(auto old=SelectionFor(body.previous->selections,selection)){selection.flags=old->flags;selection.local=old->local;if(old->glassMaterials.size()==selection.glassMaterials.size())selection.glassMaterials=old->glassMaterials;}
  if(body.healthyCapture){if(selection.variant&ATOMIC_FLAG_DAM)selection.flags&=~uint32(4);else if(selection.variant&ATOMIC_FLAG_OK)selection.flags|=4;}
  const size_t selected=body.selections.size();body.selections.push_back(selection);
  if(!(selection.flags&4)||!atomic->geometry||!frame)return atomic;if(node&&(strstr(node,"_vlo")||strstr(node,"_lo")||aircraftbodymodel::RenderOnlyAtomic(node)))return atomic;if(node&&body.definition.kind==aircraftbodymodel::Kind::Helicopter&&body.modelId!=165&&(!strcmp(node,"toprotor")||!strcmp(node,"rearrotor")||!strcmp(node,"backrotor")||!strcmp(node,"windscreen_dummy")||!strcmp(node,"bump_rear_dummy")))return atomic;
  auto g=atomic->geometry;if(body.previous&&body.previous->geometryLive&&body.previous->clump==body.clump)for(auto &old:body.previous->bindings)if(old.atomic==atomic&&SameSelection(body.previous->selections[old.selection],selection)){g=old.original;break;}if(g->numMorphTargets!=1||!g->morphTargets[0].vertices)return atomic;
  Binding binding;binding.atomic=atomic;binding.original=g;binding.current=atomic->geometry;binding.flags=selection.flags;binding.local=selection.local;binding.name=selection.name;binding.selection=selected;uint32_t group=body.bindings.size();body.bindings.push_back(binding);g->addRef();
  for(int i=0;i<g->numTriangles;++i){auto tri=g->triangles[i];Face face;face.group=group;face.material=g->matList.materials[tri.matId];face.glass=tri.matId<selection.glassMaterials.size()?selection.glassMaterials[tri.matId]!=0:face.material->color.alpha<255;for(int k=0;k<3;++k){int n=tri.v[k];auto v=g->morphTargets[0].vertices[n];auto &out=face.v[k];out.p=binding.local*CVector(v.x,v.y,v.z);out.n=CVector(0,0,1);out.uv=g->numTexCoordSets?g->texCoords[0][n]:rw::TexCoords{0,0};out.color=g->colors?g->colors[n]:rw::RGBA{255,255,255,255};}body.faces.push_back(face);}return atomic;
 }
 bool Initialize(){
  std::vector<aircraftbodymodel::Surface> surfaces;surfaces.reserve(faces.size());CVector lo(1e10f,1e10f,1e10f),hi(-1e10f,-1e10f,-1e10f);std::array<CVector,4> wheelSum{};std::array<int,4> wheelCount{};
  for(auto &face:faces){aircraftbodymodel::Surface surface;surface.group=face.group;CVector center(0,0,0);for(int k=0;k<3;++k){auto p=face.v[k].p;surface.points[k]=P(p);center+=p/3;lo.x=Min(lo.x,p.x);lo.y=Min(lo.y,p.y);lo.z=Min(lo.z,p.z);hi.x=Max(hi.x,p.x);hi.y=Max(hi.y,p.y);hi.z=Max(hi.z,p.z);}auto name=bindings[face.group].name.c_str();surface.role=aircraftbodymodel::ClassifyFace(name,P(center),definition.kind,modelId);
   const auto normal=CrossProduct(face.v[1].p-face.v[0].p,face.v[2].p-face.v[0].p);const auto material=aircraftbodymodel::ClassifyMaterial(surface.role,definition.kind,name,P(center),P(normal),face.glass,face.material?face.material->color.alpha:255,face.material&&face.material->texture?face.material->texture->name:nullptr);surface.glass=material.glass;surface.laminated=material.laminated;surface.structural=material.structural;
   if(surface.role==Role::Wheel){int w=(center.x<0?0:2)+(center.y<0?1:0);wheelSum[w]+=center;++wheelCount[w];}surfaces.push_back(surface);
  }
  if(surfaces.empty())return false;definition.model=modelId;definition.bounds={P(lo),P(hi)};definition.legacyDodo=modelId==181;definition.armor=(materialmodel::Armored(modelId)||modelId==162)?8.f:1.f;definition.hasWheels=definition.kind==aircraftbodymodel::Kind::Automobile||definition.kind==aircraftbodymodel::Kind::Bike||definition.kind==aircraftbodymodel::Kind::Plane;
  for(int i=0;i<4;++i)definition.wheels[i]=wheelCount[i]?P(wheelSum[i]/wheelCount[i]):Vec3{(i<2?lo.x:hi.x)*.8f,(i%2?lo.y:hi.y)*.65f,lo.z};
  if(definition.kind==aircraftbodymodel::Kind::Bike){definition.wheels[0]=definition.wheels[2];definition.wheels[1]=definition.wheels[3];}else if(definition.kind==aircraftbodymodel::Kind::Plane&&wheelCount[0]==0&&wheelCount[2]>0)definition.wheels[0]=definition.wheels[2];
  const auto key=SurfaceKey(surfaces);for(auto &p:prototypes)if(p.model==modelId&&p.mass==originalMass&&p.key==key&&SameSurfaces(p.modelState.Surfaces(),surfaces)){model=p.modelState;return true;}
  bool ok=model.Initialize(surfaces,originalMass,definition);if(ok&&prototypes.size()<128)prototypes.push_back({modelId,originalMass,key,model});
  // Rejected admission keeps native collision. Report it once per asset.
  if(!ok){static std::set<std::pair<int,size_t>> reported;if(reported.insert({modelId,surfaces.size()}).second)std::fprintf(stderr,"[vehicle-body] material admission rejected model=%d faces=%zu (%s); native collision retained\n",modelId,surfaces.size(),surfaces.size()>4096?"over the 4096-face limit":"invalid material topology");}
  return ok;
 }

};
struct FragmentActor{CObject *object;int reference;CMatrix before;std::vector<SweepPoint> sweep;};
unsigned overlapQueries=0;
std::map<const void*,std::unique_ptr<Body>> bodies;std::vector<FragmentActor> fragments;uint64_t nextGeneration=1;
// Render callbacks ask about every vehicle atomic. Candidate owners are indexed
// by atomic/clump and re-validated per query; the index is rebuilt whenever a
// body is admitted, rebound, replaced or retired (ownersEpoch).
uint64_t ownersEpoch=1,indexedEpoch=0;std::unordered_multimap<const void*,const void*> ownedAtomics,ownedClumps;
bool IndexOwners(){if(indexedEpoch==ownersEpoch)return true;try{ownedAtomics.clear();ownedClumps.clear();for(auto &entry:bodies){auto &b=*entry.second;if(!b.captured)continue;ownedClumps.emplace(b.clump,entry.first);for(auto &binding:b.bindings)ownedAtomics.emplace(binding.atomic,entry.first);}indexedEpoch=ownersEpoch;return true;}catch(const std::bad_alloc&){ownedAtomics.clear();ownedClumps.clear();indexedEpoch=0;return false;}}
bool OwnsLive(const Body &body){return body.captured&&body.geometryLive&&CPools::GetVehicle(body.reference)==body.plane&&body.plane->m_rwObject&&RwObjectGetType(body.plane->m_rwObject)==rpCLUMP&&body.plane->GetClump()==body.clump;}
// Regular-game smoke only: native actors left unowned as A/B references.
std::vector<const void*> smokeUnowned;
CVehicle *Eligible(const void *v){
 if(worldSuspended)return nullptr;if(!smokeUnowned.empty()&&std::getenv("VC_REGULAR_SMOKE")&&std::find(smokeUnowned.begin(),smokeUnowned.end(),v)!=smokeUnowned.end())return nullptr;auto experiment=std::getenv("VC_EXPERIMENTAL_WING");if(experiment&&!strcmp(experiment,"1"))return nullptr;auto pool=CPools::GetVehiclePool();if(!v||!pool)return nullptr;CVehicle *vehicle=nullptr;for(int i=0;i<pool->GetSize();++i)if(pool->GetSlot(i)==v){vehicle=pool->GetSlot(i);break;}
 if(!vehicle||!vehicle->m_rwObject||RwObjectGetType(vehicle->m_rwObject)!=rpCLUMP)return nullptr;auto info=CModelInfo::GetModelInfo(vehicle->GetModelIndex());if(!info||info->GetModelType()!=MITYPE_VEHICLE)return nullptr;
 if(vehicle->GetModelIndex()==181&&!DodoRigReady((CPlane*)vehicle))return nullptr;return vehicle;
}
Body *Find(const void *v){auto it=bodies.find(v);if(it==bodies.end())return nullptr;auto b=it->second.get();return CPools::GetVehicle(b->reference)==v&&b->plane->GetModelIndex()==b->modelId?b:nullptr;}
Body *Lease(const void *v){auto vehicle=Eligible(v);if(!vehicle)return nullptr;if(auto old=Find(v))return old;auto body=std::unique_ptr<Body>(new Body);body->plane=vehicle;body->reference=CPools::GetVehicleRef(vehicle);body->modelId=vehicle->GetModelIndex();body->generation=nextGeneration++;body->snapshot.generation=body->generation;body->clump=vehicle->GetClump();body->before=vehicle->GetMatrix();body->originalMass=vehicle->m_fMass;body->originalTurnMass=vehicle->m_fTurnMass;body->originalCOM=vehicle->m_vecCentreOfMass;
 auto info=(CVehicleModelInfo*)CModelInfo::GetModelInfo(body->modelId);int kind=info->m_vehicleType;body->definition.kind=kind==VEHICLE_TYPE_BOAT?aircraftbodymodel::Kind::Boat:kind==VEHICLE_TYPE_BIKE?aircraftbodymodel::Kind::Bike:kind==VEHICLE_TYPE_PLANE?aircraftbodymodel::Kind::Plane:kind==VEHICLE_TYPE_HELI?aircraftbodymodel::Kind::Helicopter:aircraftbodymodel::Kind::Automobile;
 for(int id:{155,177,195,199,217,218,227,231})if(body->modelId==id)body->definition.kind=aircraftbodymodel::Kind::Helicopter;
 if(body->modelId==190||body->modelId==194)body->definition.kind=aircraftbodymodel::Kind::Plane;
 auto result=body.get();bodies[v]=std::move(body);++ownersEpoch;return result;
}
bool Capture(Body &body){
 if(body.captured&&body.Current())return true;if(body.captured||!body.plane->m_rwObject){if(std::getenv("VC_REGULAR_SMOKE")){std::fprintf(stderr,"[vehicle-body] capture unavailable model=%d captured=%d live=%d clump_match=%d\n",body.modelId,body.captured,body.geometryLive,body.plane->m_rwObject&&RwObjectGetType(body.plane->m_rwObject)==rpCLUMP&&body.plane->GetClump()==body.clump);if(body.plane->m_rwObject&&RwObjectGetType(body.plane->m_rwObject)==rpCLUMP&&body.plane->GetClump()==body.clump)for(auto&b:body.bindings)if(b.atomic->geometry!=b.current)std::fprintf(stderr,"[vehicle-body] foreign geometry model=%d group=%s actual=%p expected=%p\n",body.modelId,b.name.c_str(),(void*)b.atomic->geometry,(void*)b.current);}return false;}
 Body staged;staged.restoreOnDestruct=false;staged.plane=body.plane;staged.reference=body.reference;staged.modelId=body.modelId;staged.generation=body.generation;staged.clump=body.plane->GetClump();staged.definition=body.definition;staged.originalMass=body.originalMass;
 RpClumpForAllAtomics(staged.clump,Body::CaptureAtomic,&staged);auto col=body.plane->GetColModel();if(col){staged.buoyancyMin=col->boundingBox.min;staged.buoyancyMax=col->boundingBox.max;}if(col&&col->lines&&col->numLines>0)staged.suspension.assign(col->lines,col->lines+col->numLines);
 if(!staged.Initialize())return false;body.clump=staged.clump;body.definition=staged.definition;body.selections=std::move(staged.selections);body.bindings=std::move(staged.bindings);body.faces=std::move(staged.faces);body.model=std::move(staged.model);body.suspension=std::move(staged.suspension);body.buoyancyMin=staged.buoyancyMin;body.buoyancyMax=staged.buoyancyMax;body.captured=true;body.geometryLive=true;staged.geometryLive=false;body.publishedMesh=bodypublication::Cache{};body.partFaces.clear();++ownersEpoch;
 std::fprintf(stderr,"[aircraft-body] acquired ref=%d model=%d source_faces=%zu native=motion material=body\n",body.reference,body.modelId,body.faces.size());return true;
}

bool WheelPresentFor(const Body &,const aircraftbodymodel::Model &,int);
// Mechanical/effect fields only; the geometry-derived fields depend solely on
// the model's committed revision (SnapshotFor computes both).
void SnapshotState(const Body &body,const aircraftbodymodel::Model &model,AircraftBodySnapshot &out){
 out.generation=body.generation;auto e=model.Effects();auto f=model.Fire(body.plane->bEngineOn);out.power=e.power;out.leftLift=e.leftLift;out.rightLift=e.rightLift;out.pitch=e.pitch;out.roll=e.roll;out.yaw=e.yaw;out.rotor=e.rotor;out.antiTorque=e.antiTorque;out.drag=e.drag;out.heat=f.heat;out.fuel=f.fuel;out.engine=f.engine;out.electrical=f.electrical;out.breach=f.breach;out.lighting=mechanicalmodel::Lighting(model.Mechanical());for(int i=0;i<4;++i){auto w=mechanicalmodel::Wheel(model.Mechanical(),i);out.wheels[i]={w.grip,w.drive,w.brake,w.toe,w.steering};}
}
AircraftBodySnapshot SnapshotFor(const Body &body,const aircraftbodymodel::Model &model){
 AircraftBodySnapshot out;SnapshotState(body,model,out);
 for(auto &fragment:model.Fragments())if(fragment.attached){auto &name=body.bindings[fragment.group].name;if(name=="dodo_hull")for(auto &sample:fragment.polygon)if(fabs(sample.position.x)>4.5f){float delta=(V(model.Deform(sample.position,fragment.group))-V(sample.position)).Magnitude();if(sample.position.x<0)out.tipLeft=Max(out.tipLeft,delta);else out.tipRight=Max(out.tipRight,delta);}for(int i=0;i<3;++i)if(name==(i==0?"dodo_tire_n":i==1?"dodo_tire_l":"dodo_tire_r"))for(size_t k=1;k+1<fragment.polygon.size();++k){auto a=V(fragment.polygon[0].position),b=V(fragment.polygon[k].position),c=V(fragment.polygon[k+1].position);if(CrossProduct(b-a,c-a).MagnitudeSqr()>1e-14f)++out.wheelTriangles[i];}}
 for(int i=0;i<4;++i)out.wheelPresent[i]=WheelPresentFor(body,model,i);out.detachedBodies=fragments.size();out.revision=body.plane->InstanceCollisionRevision();return out;
}
bool WheelPresentFor(const Body &body,const aircraftbodymodel::Model &model,int index){
 auto wheelIndex=[&](const aircraftbodymodel::Surface&s){auto centre=(V(s.points[0])+V(s.points[1])+V(s.points[2]))/3;int best=0;float distance=1e30f;for(int i=0;i<4;++i){float d=(centre-V(body.definition.wheels[i])).MagnitudeSqr();if(d<distance){best=i;distance=d;}}return best;};
 bool baseline=false;std::vector<bool> belongs(model.Surfaces().size());for(size_t i=0;i<model.Surfaces().size();++i){auto&s=model.Surfaces()[i];if(s.role==Role::Wheel&&wheelIndex(s)==index)baseline=belongs[i]=true;}
 if(!baseline)return true;for(auto&f:model.Fragments())if(f.attached&&belongs[f.source]&&f.polygon.size()>2)return true;return false;
}
struct PreparedFragment{CObject *object=nullptr;rw::Clump *clump=nullptr;std::unique_ptr<CColModel> col;float mass=0,inertia=0;CVector center;std::vector<SweepPoint> sweep;~PreparedFragment(){if(object)delete object;else if(clump)clump->destroy();}};
bool ContainsName(const std::string &name,const char *part){for(size_t i=0;i<name.size();++i){size_t k=0;while(part[k]&&i+k<name.size()&&std::tolower(static_cast<unsigned char>(name[i+k]))==part[k])++k;if(!part[k])return true;}return false;}
int NativeDoorFor(const Body &body,size_t group){const auto &name=body.bindings[group].name;if(ContainsName(name,"door_lf"))return DOOR_FRONT_LEFT;if(ContainsName(name,"door_rf"))return DOOR_FRONT_RIGHT;if(ContainsName(name,"door_lr"))return DOOR_REAR_LEFT;if(ContainsName(name,"door_rr"))return DOOR_REAR_RIGHT;if(body.definition.kind!=aircraftbodymodel::Kind::Helicopter){if(ContainsName(name,"bonnet"))return DOOR_BONNET;if(ContainsName(name,"boot"))return DOOR_BOOT;}return -1;}
void SyncNativeDoors(const Body &body,const aircraftbodymodel::Model &model){
 if(!body.plane->IsCar())return;auto car=(CAutomobile*)body.plane;
 if(body.repair){car->Damage.ResetDamageStatus();if(car->pHandling->Flags&HANDLING_NO_DOORS)for(int door:{DOOR_FRONT_LEFT,DOOR_FRONT_RIGHT,DOOR_REAR_LEFT,DOOR_REAR_RIGHT})car->Damage.SetDoorStatus(door,DOOR_STATUS_MISSING);}
 std::array<bool,6> known{},attached{};std::vector<int> doors(body.bindings.size());for(size_t i=0;i<body.bindings.size();++i){int door=doors[i]=NativeDoorFor(body,i);if(door>=0)known[door]=true;}
 for(auto &fragment:model.Fragments())if(fragment.attached&&fragment.polygon.size()>2&&fragment.group<body.bindings.size()){int door=doors[fragment.group];if(door>=0)attached[door]=true;}
 for(int door=0;door<6;++door)if(known[door]&&!attached[door])car->Damage.SetDoorStatus(door,DOOR_STATUS_MISSING);
}
// These bounds only select candidates. The exact tests remain the same native
// routines used by LOS and ProcessColModels, over the current authoritative COL.
// Probe centres inside a solid box take the nearest-face exit, never native's
// centre-based normal and largest-side depth (bodycontact::InsideBox).
bool InsideBoxes(const CColModel &col,CVector point,float radius,CColPoint &hit){bool found=false;float best=0;for(int i=0;i<col.numBoxes;++i){const auto &box=col.boxes[i];Vec3 p,n;float depth;if(!bodycontact::InsideBox(P(point),radius,P(box.min),P(box.max),p,n,depth)||(found&&depth>=best))continue;found=true;best=depth;hit.point=V(p);hit.normal=V(n);hit.depth=depth;hit.surfaceA=6;hit.pieceA=0;hit.surfaceB=box.surface;hit.pieceB=box.piece;}return found;}
struct TargetQuery {
 CColModel &col;boundsindex::Index index;std::vector<size_t> candidates;
 static std::vector<boundsindex::Box> Bounds(CColModel &col){std::vector<boundsindex::Box> out;out.reserve(col.numTriangles);for(int i=0;i<col.numTriangles;++i){auto t=col.triangles[i];auto a=col.vertices[t.a].Get(),b=col.vertices[t.b].Get(),c=col.vertices[t.c].Get();out.push_back({{Min(a.x,Min(b.x,c.x)),Min(a.y,Min(b.y,c.y)),Min(a.z,Min(b.z,c.z))},{Max(a.x,Max(b.x,c.x)),Max(a.y,Max(b.y,c.y)),Max(a.z,Max(b.z,c.z))}});}return out;}
 TargetQuery(CColModel &model):col(model),index(Bounds(model)){candidates.reserve(col.numTriangles);CCollision::CalculateTrianglePlanes(&col);}
 void Select(const boundsindex::Box &box){candidates.clear();index.Query(box,[&](size_t i){candidates.push_back(i);});std::sort(candidates.begin(),candidates.end());}
 bool Line(CVector a,CVector b,CColPoint &hit){CColLine line(a,b);if(!CCollision::TestLineBox(line,col.boundingBox))return false;float fraction=1;for(int i=0;i<col.numSpheres;++i)CCollision::ProcessLineSphere(line,col.spheres[i],hit,fraction);for(int i=0;i<col.numBoxes;++i)CCollision::ProcessLineBox(line,col.boxes[i],hit,fraction);boundsindex::Box bounds{{Min(a.x,b.x),Min(a.y,b.y),Min(a.z,b.z)},{Max(a.x,b.x),Max(a.y,b.y),Max(a.z,b.z)}};Select(bounds);for(auto i:candidates)CCollision::ProcessLineTriangle(line,col.vertices,col.triangles[i],col.trianglePlanes[i],hit,fraction,nil);return fraction<1;}
 bool Rest(CVector point,float radius,CColPoint &hit,int &triangle){triangle=-1;CColSphere sphere;sphere.Set(radius,point,6,0);if(!CCollision::TestSphereBox(sphere,col.boundingBox))return false;float distance=1e10f;bool found=false;for(int i=0;i<col.numSpheres;++i)found|=CCollision::ProcessSphereSphere(sphere,col.spheres[i],hit,distance);for(int i=0;i<col.numBoxes;++i)found|=CCollision::ProcessSphereBox(sphere,col.boxes[i],hit,distance);boundsindex::Box bounds{{point.x-radius,point.y-radius,point.z-radius},{point.x+radius,point.y+radius,point.z+radius}};Select(bounds);for(auto i:candidates)if(CCollision::ProcessSphereTriangle(sphere,col.vertices,col.triangles[i],col.trianglePlanes[i],hit,distance)){found=true;triangle=int(i);}if(InsideBoxes(col,point,radius,hit)){triangle=-2;return true;}return found;}
};
// Build every allocation before changing the resident parent's mesh/COL.
bool Publish(Body &body,aircraftbodymodel::Model &candidate){
 if(!InSectorGrid(body.plane)){body.publishFailure=bodypublication::Failure::Outside;return false;}
 std::vector<std::vector<RenderTriangle>> parents(body.bindings.size());std::map<size_t,std::vector<RenderTriangle>> detached;std::vector<CVector> dust;float removedMass=0;CVector removedMoment(0,0,0);float removedInertia=0;std::vector<RenderTriangle> parentCollision;std::vector<std::pair<std::array<int,3>,SweepPoint>> sweep;
 // Unclassified failures are resource shortages that may clear; fixed native
 // limits and impossible candidates are classified where they are detected.
 using bodypublication::Failure;body.publishFailure=Failure::Transient;bodypublication::Cache published;published.valid=true;published.render.assign(body.bindings.size(),0);std::vector<bodypublication::Hash> partHash(body.bindings.size());
 // One frame inverse per part, not per fragment (same value, same evaluation).
 std::vector<CMatrix> inverses(body.bindings.size());std::vector<uint8_t> inverseState(body.bindings.size());
 for(auto &part:candidate.Fragments()){if(part.source>=body.faces.size()||part.polygon.size()<3){body.publishFailure=Failure::Invalid;return false;}auto &source=body.faces[part.source];if(source.group>=inverses.size()){body.publishFailure=Failure::Invalid;return false;}if(!inverseState[source.group]){bool invertible=false;inverses[source.group]=InverseFrame(Local(body.bindings[source.group].atomic->getFrame(),body.clump->getFrame()),&invertible);inverseState[source.group]=invertible?1:2;}if(inverseState[source.group]!=1)return false;const auto &inverse=inverses[source.group];
  for(size_t i=1;i+1<part.polygon.size();++i){const skinmodel::Sample samples[]={part.polygon[0],part.polygon[i],part.polygon[i+1]};RenderTriangle local{},car{};local.material=car.material=source.material;local.glass=car.glass=candidate.Surfaces()[part.source].glass;
   for(int k=0;k<3;++k){auto weights=samples[k].weights;const float w[]={weights.x,weights.y,weights.z};auto &out=car.v[k];out.p=V(candidate.Deform(samples[k].position,source.group));out.n=CVector(0,0,1);out.uv={0,0};float colors[4]{};for(int n=0;n<3;++n){out.uv.u+=source.v[n].uv.u*w[n];out.uv.v+=source.v[n].uv.v*w[n];colors[0]+=source.v[n].color.red*w[n];colors[1]+=source.v[n].color.green*w[n];colors[2]+=source.v[n].color.blue*w[n];colors[3]+=source.v[n].color.alpha*w[n];}out.color=rw::RGBA{uint8_t(colors[0]),uint8_t(colors[1]),uint8_t(colors[2]),uint8_t(colors[3])};local.v[k]=out;local.v[k].p=inverse*out.p;}
   const bool degenerate=CrossProduct(car.v[1].p-car.v[0].p,car.v[2].p-car.v[0].p).MagnitudeSqr()<1e-14f;
   if(part.attached){bodypublication::Triangle record;record.rest={{samples[0].position,samples[1].position,samples[2].position}};record.group=source.group;record.source=part.source;const auto role=candidate.Surfaces()[part.source].role;record.sweep=role!=Role::Wheel&&role!=Role::Interior;record.col=degenerate?-1:int32_t(parentCollision.size());published.triangles.push_back(record);}
   if(degenerate)continue;
   if(part.attached){parents[source.group].push_back(local);parentCollision.push_back(car);
    // Fixed native mesh limits: stop at the first triangle past either cap.
    if(parentCollision.size()>bodypublication::kMaxTriangles||parents[source.group].size()>bodypublication::kMaxTriangles){body.publishFailure=Failure::Capacity;return false;}
    auto &digest=partHash[source.group];digest.Add(&local.material,sizeof local.material);for(int k=0;k<3;++k){const auto &v=local.v[k];const float exact[]={v.p.x,v.p.y,v.p.z,v.uv.u,v.uv.v};const uint8_t colour[]={v.color.red,v.color.green,v.color.blue,v.color.alpha};digest.Add(exact,sizeof exact);digest.Add(colour,sizeof colour);published.positions.push_back(P(car.v[k].p));}published.surfaces.push_back(car.glass?7:6);
    auto role=candidate.Surfaces()[part.source].role;if(role!=Role::Wheel&&role!=Role::Interior)for(int k=0;k<3;++k){auto p=car.v[k].p;auto key=std::array<int,3>{{int(std::round(p.x*10000)),int(std::round(p.y*10000)),int(std::round(p.z*10000))}};sweep.push_back({key,{p,V(body.model.Deform(samples[k].position,source.group)),part.source}});}
   }else detached[part.component].push_back(car);
  }
 }
 if(parentCollision.empty()){body.publishFailure=Failure::Invalid;return false;}
 // Sub-gram, sub-centimetre material islands are dust, not one-kilogram rigid
 // actors. Geometry/mass still leave the carrier; heavyweight debris is bounded.
 for(auto it=detached.begin();it!=detached.end();){float area=0;for(auto &t:it->second){float triangleArea=CrossProduct(t.v[1].p-t.v[0].p,t.v[2].p-t.v[0].p).Magnitude()*.5f;area+=triangleArea;removedMoment+=(t.v[0].p+t.v[1].p+t.v[2].p)*(triangleArea*body.model.SurfaceDensity()/3);CVector r[3]={t.v[0].p-body.plane->m_vecCentreOfMass,t.v[1].p-body.plane->m_vecCentreOfMass,t.v[2].p-body.plane->m_vecCentreOfMass};float radius2=r[0].MagnitudeSqr()+r[1].MagnitudeSqr()+r[2].MagnitudeSqr()+DotProduct(r[0],r[1])+DotProduct(r[0],r[2])+DotProduct(r[1],r[2]);removedInertia+=triangleArea*body.model.SurfaceDensity()*radius2/9;}removedMass+=area*body.model.SurfaceDensity();if(area<.0025f){if(dust.size()<32)dust.push_back(it->second[0].v[0].p);it=detached.erase(it);}else ++it;}
 if(detached.size()>40){body.publishFailure=Failure::Invalid;return false;}if(detached.size()+CObject::nNoTempObjects>40)return false;
 fragments.reserve(fragments.size()+detached.size());std::vector<SweepPoint> parentSweep;bodypublication::LastByKey(sweep,parentSweep);
 // A part whose local render triangles are byte-identical to its committed
 // mesh keeps that mesh: only changed parts are rebuilt and re-instanced.
 std::vector<Geometry> render;render.reserve(parents.size());std::vector<uint8_t> kept(parents.size());for(size_t i=0;i<parents.size();++i){auto &mesh=parents[i];auto &binding=body.bindings[i];const auto &old=body.publishedMesh;kept[i]=!mesh.empty()&&old.valid&&i<old.partHash.size()&&i<old.partCount.size()&&old.partCount[i]==mesh.size()&&old.partHash[i]==partHash[i].value&&binding.current&&binding.current!=binding.original&&binding.atomic->geometry==binding.current;Geometry geometry;if(!kept[i]){geometry=GeometryFor(mesh);if(!mesh.empty()&&!geometry)return false;}render.push_back(std::move(geometry));}
 auto collision=CollisionFor(parentCollision);if(collision&&!AttachSuspension(*collision,body.suspension))return false;if(!collision||!body.plane->CanReplaceInstanceCollision(collision.get()))return false;
 std::vector<std::unique_ptr<PreparedFragment>> prepared;
 for(auto &pair:detached){auto &mesh=pair.second;CVector lo(1e10f,1e10f,1e10f),hi(-1e10f,-1e10f,-1e10f);float area=0;for(auto &t:mesh){area+=CrossProduct(t.v[1].p-t.v[0].p,t.v[2].p-t.v[0].p).Magnitude()*.5f;for(auto v:t.v){lo.x=Min(lo.x,v.p.x);lo.y=Min(lo.y,v.p.y);lo.z=Min(lo.z,v.p.z);hi.x=Max(hi.x,v.p.x);hi.y=Max(hi.y,v.p.y);hi.z=Max(hi.z,v.p.z);}}
  auto next=std::unique_ptr<PreparedFragment>(new PreparedFragment);next->center=CVector(0,0,0);for(auto &t:mesh){float a=CrossProduct(t.v[1].p-t.v[0].p,t.v[2].p-t.v[0].p).Magnitude()*.5f;next->center+=(t.v[0].p+t.v[1].p+t.v[2].p)*(a/3);}next->center/=Max(area,1e-9f);next->mass=std::max(.001f,area*body.model.SurfaceDensity());for(auto &t:mesh)for(auto &v:t.v)v.p-=next->center;for(auto &t:mesh){float a=CrossProduct(t.v[1].p-t.v[0].p,t.v[2].p-t.v[0].p).Magnitude()*.5f;float radius2=t.v[0].p.MagnitudeSqr()+t.v[1].p.MagnitudeSqr()+t.v[2].p.MagnitudeSqr()+DotProduct(t.v[0].p,t.v[1].p)+DotProduct(t.v[0].p,t.v[2].p)+DotProduct(t.v[1].p,t.v[2].p);next->inertia+=a*body.model.SurfaceDensity()*radius2/9;}next->sweep.reserve(mesh.size()*3);for(auto &t:mesh)for(auto &v:t.v)next->sweep.push_back({v.p,v.p});if(mesh.size()>bodypublication::kMaxTriangles){body.publishFailure=Failure::Capacity;return false;}auto geometry=GeometryFor(mesh);next->col=CollisionFor(mesh);if(!geometry||!next->col)return false;
  auto clump=rw::Clump::create();if(!clump)return false;next->clump=clump;auto frame=rw::Frame::create();if(!frame)return false;clump->setFrame(frame);auto child=rw::Frame::create();if(!child)return false;frame->addChild(child);auto atomic=rw::Atomic::create();if(!atomic)return false;atomic->setFrame(child);atomic->setGeometry(geometry.get(),0);clump->addAtomic(atomic);
  if(!CPools::GetObjectPool()->GetNoOfFreeSpaces())return false;next->object=new CObject(MI_CAR_PANEL,false);next->object->AttachToRwObject((RwObject*)clump);next->clump=nullptr;if(!next->object->CanReplaceInstanceCollision(next->col.get()))return false;prepared.push_back(std::move(next));
 }
 // Consuming disconnected material is part of this candidate transaction.
 std::vector<aircraftconnectivity::Fragment> transfer;if(!candidate.TransferDetached(transfer))return false;float newMass=body.repair?body.originalMass:std::max(body.originalMass*.1f,body.plane->m_fMass-removedMass);if(!candidate.SetMass(newMass))return false;auto snapshot=SnapshotFor(body,candidate);snapshot.detachedBodies+=prepared.size();
 if(!body.Current())return false;
 auto plane=body.plane;const auto oldVelocity=plane->m_vecMoveSpeed,oldCOM=plane->m_vecCentreOfMass;const float oldMass=plane->m_fMass;CVector newCOM=body.repair?body.originalCOM:(oldCOM*oldMass-removedMoment)/newMass;const bool resident=SourceCollisionIsResident(plane);if(resident)CWorld::Remove(plane);body.materialCOMOffset=body.repair?CVector(0,0,0):body.materialCOMOffset+(newCOM-oldCOM);plane->m_fMass=newMass;plane->m_vecCentreOfMass=newCOM;plane->m_vecMoveSpeed=oldVelocity;if(body.repair)plane->m_fTurnMass=body.originalTurnMass;else plane->m_fTurnMass=Max(body.originalTurnMass*.1f,plane->m_fTurnMass-removedInertia-newMass*(newCOM-oldCOM).MagnitudeSqr()*2/3);if(body.repair&&plane->IsBike()){auto bike=(CBike*)plane;bike->m_wheelStatus[0]=bike->m_wheelStatus[1]=WHEEL_STATUS_OK;bike->bIsDamaged=false;bike->bIsOnFire=false;}for(auto &selection:body.selections)selection.atomic->setFlags(selection.flags);for(size_t i=0;i<body.bindings.size();++i){auto &binding=body.bindings[i];if(kept[i])binding.atomic->setFlags(binding.flags);else if(render[i]){binding.atomic->setGeometry(render[i].get(),0);binding.current=render[i].get();binding.atomic->setFlags(binding.flags);}else binding.atomic->setFlags(0);}
 if(!plane->ReplaceInstanceCollision(collision.release()))return false;SyncNativeDoors(body,candidate);if(resident)CWorld::Add(plane);
 for(auto &next:prepared){auto object=next->object;{const CVector at=plane->GetMatrix()*next->center;const float m=next->col?(next->col->boundingBox.max-next->col->boundingBox.min).Magnitude()+1:1;if(!bodypublication::InsideGrid(at.x-m,at.y+m,at.x+m,at.y-m,WORLD_MIN_X,WORLD_MIN_Y,WORLD_MAX_X,WORLD_MAX_Y))continue;}object->GetMatrix()=plane->GetMatrix();object->SetPosition(plane->GetMatrix()*next->center);object->m_vecMoveSpeed=oldVelocity+CrossProduct(plane->m_vecTurnSpeed,Multiply3x3(plane->GetMatrix(),next->center));object->m_vecTurnSpeed=plane->m_vecTurnSpeed;object->m_fMass=next->mass;object->m_fBuoyancy=next->mass*GRAVITY/.75f;object->m_fTurnMass=std::max(.001f,next->inertia);object->m_fAirResistance=.99f;object->m_fElasticity=.05f;object->RefModelInfo(plane->GetModelIndex());object->ObjectCreatedBy=TEMP_OBJECT;object->m_nEndOfLifeTime=CTimer::GetTimeInMilliseconds()+20000;object->bUsesCollision=true;object->bDontStream=true;object->bStreamingDontDelete=true;object->bIsBIGBuilding=false;object->SetIsStatic(false);object->m_pCollidingEntity=plane;object->ReplaceInstanceCollision(next->col.release());object->GetMatrix().UpdateRW();object->UpdateRwFrame();++CObject::nNoTempObjects;CWorld::Add(object);fragments.push_back({object,CPools::GetObjectRef(object),object->GetMatrix(),std::move(next->sweep)});std::fprintf(stderr,"[aircraft-body] transferred component ref=%d mass=%.3f\n",CPools::GetObjectRef(object),object->m_fMass);next->object=nullptr;}
 for(auto p:dust)CParticle::AddParticle(PARTICLE_CAR_DEBRIS,plane->GetMatrix()*p,plane->m_vecMoveSpeed,nil,.025f);body.sweep=std::move(parentSweep);body.model=std::move(candidate);body.published=body.model.Revision();snapshot.revision=plane->InstanceCollisionRevision();body.snapshot=snapshot;published.revision=body.published;published.partHash.resize(parents.size());published.partCount.resize(parents.size());for(size_t i=0;i<parents.size();++i){published.render[i]=!parents[i].empty();published.partHash[i]=partHash[i].value;published.partCount[i]=parents[i].size();}body.publishedMesh=std::move(published);body.publishFailure=Failure::None;if(!body.events.empty()||!prepared.empty()||CTimer::GetFrameCounter()%120==0)std::fprintf(stderr,"[aircraft-body] committed ref=%d revision=%llu triangles=%zu detached=%zu incidents=%zu/%zu\n",body.reference,(unsigned long long)body.published,parentCollision.size(),prepared.size(),body.bullets,body.contacts);return true;
}
bool SamplePoses(Body &body,aircraftbodymodel::Model &model){
 for(size_t i=0;i<body.bindings.size();++i){auto &binding=body.bindings[i];bool valid=false;auto inverse=InverseFrame(binding.local,&valid);if(!valid)return false;auto matrix=Local(binding.atomic->getFrame(),body.clump->getFrame())*inverse;auto r=matrix.GetRight(),f=matrix.GetForward(),u=matrix.GetUp(),p=matrix.GetPosition();std::array<float,12> pose{{r.x,r.y,r.z,f.x,f.y,f.z,u.x,u.y,u.z,p.x,p.y,p.z}};if(!model.SetGroupPose(i,pose))return false;binding.sampledPose=binding.modelPose=pose;}return true;
}
std::unique_ptr<Body> StageReplacement(const Body &old,bool repair){
 if(!old.plane->m_rwObject||RwObjectGetType(old.plane->m_rwObject)!=rpCLUMP)return {};
 std::unique_ptr<Body> next(new Body);next->restoreOnDestruct=false;next->plane=old.plane;next->reference=old.reference;next->modelId=old.modelId;next->generation=old.generation;next->clump=old.plane->GetClump();next->definition=old.definition;next->originalMass=old.originalMass;next->originalTurnMass=old.originalTurnMass;next->originalCOM=old.originalCOM;next->materialCOMOffset=old.materialCOMOffset;next->suspension=old.suspension;next->buoyancyMin=old.buoyancyMin;next->buoyancyMax=old.buoyancyMax;next->controls=old.controls;next->loads=old.loads;next->events=old.events;next->bullets=old.bullets;next->contacts=old.contacts;next->maxMs=old.maxMs;next->before=old.plane->GetMatrix();next->previous=&old;next->repair=next->healthyCapture=repair;
 // Capture reads desired visibility and immutable source geometry. It never
 // changes either the current atomic flags or the resident private meshes.
 RpClumpForAllAtomics(next->clump,Body::CaptureAtomic,next.get());if(!next->Initialize())return {};
 if(!repair){if(next->bindings.size()!=old.bindings.size()||!SameSurfaces(next->model.Surfaces(),old.model.Surfaces())){if(std::getenv("VC_REGULAR_SMOKE")&&old.modelId==217){std::fprintf(stderr,"[vehicle-rebind] model=217 bindings=%zu/%zu faces=%zu/%zu\n",next->bindings.size(),old.bindings.size(),next->model.Surfaces().size(),old.model.Surfaces().size());auto &a=next->model.Surfaces();auto &b=old.model.Surfaces();for(size_t i=0;i<std::min(a.size(),b.size());++i)if(a[i].glass!=b[i].glass||a[i].role!=b[i].role||a[i].structural!=b[i].structural||a[i].group!=b[i].group||(V(a[i].points[0])-V(b[i].points[0])).MagnitudeSqr()>1e-8f){std::fprintf(stderr,"[vehicle-rebind] first=%zu group=%u/%u name=%s/%s glass=%d/%d role=%d/%d structural=%d/%d delta=%.8f\n",i,a[i].group,b[i].group,next->bindings[a[i].group].name.c_str(),old.bindings[b[i].group].name.c_str(),a[i].glass,b[i].glass,int(a[i].role),int(b[i].role),a[i].structural,b[i].structural,(V(a[i].points[0])-V(b[i].points[0])).Magnitude());break;}}return {};}for(size_t i=0;i<next->bindings.size();++i)if(!SameSelection(next->selections[next->bindings[i].selection],old.selections[old.bindings[i].selection]))return {};next->model=old.model;}
 next->captured=true;return next;
}
const CMatrix &InverseLocal(Binding &binding){if(!binding.inverseLocalReady){binding.inverseLocal=InverseFrame(binding.local);binding.inverseLocalReady=true;}return binding.inverseLocal;}
// Same arithmetic as the boundary pose sampling; the captured-frame inverse is immutable.
std::array<float,12> CurrentPose(Body &body,Binding &binding){auto matrix=Local(binding.atomic->getFrame(),body.clump->getFrame())*InverseLocal(binding);auto r=matrix.GetRight(),f=matrix.GetForward(),u=matrix.GetUp(),p=matrix.GetPosition();return {{r.x,r.y,r.z,f.x,f.y,f.z,u.x,u.y,u.z,p.x,p.y,p.z}};}
// Only parts with material faces accept a model pose.
const std::vector<uint8_t> &PartFaces(Body &body){if(body.partFaces.size()!=body.bindings.size()){body.partFaces.assign(body.bindings.size(),0);for(auto &face:body.faces)if(face.group<body.partFaces.size())body.partFaces[face.group]=1;}return body.partFaces;}
void ReportDeferred(const Body &body,const char *what,const bodypublication::Verdict &verdict,size_t incidents){if(verdict.report)std::fprintf(stderr,"%s ref=%d reason=%s incidents=%zu%s; old revision retained\n",what,body.reference,bodypublication::Name(body.publishFailure),incidents,verdict.drop?" retired":"");}
// Rigid part animation: a local render vertex is the inverse current frame of
// its posed model vertex, so committed RW meshes stay valid. Only the posed
// parts' instance-COL vertices (and the sweep, when such parts carry sweep
// points) are republished. The body's model already carries the new poses.
enum class Animated{Committed,Deferred,Full};
Animated PublishPoses(Body &body,const std::vector<uint8_t> &posed){
 if(!InSectorGrid(body.plane))return Animated::Deferred;
 auto &cache=body.publishedMesh;std::vector<Vec3> next;bool sweepMoved=false;
 const auto patch=bodypublication::PoseOnly(cache,posed,[&](Vec3 p,uint32_t group){return body.model.Deform(p,group);},[](Vec3 a,Vec3 b,Vec3 c){return CrossProduct(V(b)-V(a),V(c)-V(a)).MagnitudeSqr()<1e-14f;},next,sweepMoved);
 if(patch==bodypublication::Patch::Rebuild)return Animated::Full;body.publishFailure=bodypublication::Failure::Transient;
 auto collision=CollisionFrom(next,cache.surfaces);if(!collision||!AttachSuspension(*collision,body.suspension)||!body.plane->CanReplaceInstanceCollision(collision.get()))return Animated::Deferred;
 // Same ordered de-duplication as a full publication; previous is the committed position.
 std::vector<SweepPoint> parentSweep;if(sweepMoved){std::vector<std::pair<std::array<int,3>,SweepPoint>> entries;entries.reserve(cache.positions.size());for(const auto &t:cache.triangles){if(t.col<0||!t.sweep)continue;for(size_t k=0;k<3;++k){const size_t at=size_t(t.col)*3+k;const auto p=V(next[at]);entries.push_back({std::array<int,3>{{int(std::round(p.x*10000)),int(std::round(p.y*10000)),int(std::round(p.z*10000))}},SweepPoint{p,V(cache.positions[at]),t.source}});}}bodypublication::LastByKey(entries,parentSweep);}
 auto snapshot=SnapshotFor(body,body.model);if(!body.Current())return Animated::Deferred;
 auto plane=body.plane;const bool resident=SourceCollisionIsResident(plane);if(resident)CWorld::Remove(plane);for(auto &selection:body.selections)selection.atomic->setFlags(selection.flags);for(size_t i=0;i<body.bindings.size();++i)body.bindings[i].atomic->setFlags(i<cache.render.size()&&cache.render[i]?body.bindings[i].flags:0);
 if(!plane->ReplaceInstanceCollision(collision.get())){if(resident)CWorld::Add(plane);return Animated::Deferred;}collision.release();if(resident)CWorld::Add(plane);
 if(sweepMoved)body.sweep=std::move(parentSweep);else for(auto &point:body.sweep)point.previous=point.now;
 cache.positions=std::move(next);body.published=body.model.Revision();cache.revision=body.published;snapshot.revision=plane->InstanceCollisionRevision();body.snapshot=snapshot;body.publishFailure=bodypublication::Failure::None;
 if(CTimer::GetFrameCounter()%120==0)std::fprintf(stderr,"[aircraft-body] committed ref=%d revision=%llu triangles=%zu detached=0 incidents=%zu/%zu animated=1\n",body.reference,(unsigned long long)body.published,cache.surfaces.size(),body.bullets,body.contacts);
 return Animated::Committed;
}
// Render-boundary part animation without restaging the model or rebuilding
// render meshes. Poses apply in place and are reverted on any failure, so the
// committed model always matches the committed geometry. False hands the
// decision to the full publication path.
bool AnimatePublish(Body &body,const std::vector<std::array<float,12>> &poses){
 const auto &cache=body.publishedMesh;if(body.repair||!cache.valid||cache.revision!=body.published||body.published!=body.model.Revision()||poses.size()!=body.bindings.size())return false;
 const auto &faces=PartFaces(body);std::vector<uint8_t> posed(poses.size());bool any=false;for(size_t i=0;i<poses.size();++i){if(!faces[i])return false;if(poses[i]!=body.bindings[i].modelPose){posed[i]=1;any=true;}}
 if(any){
  std::vector<size_t> applied;applied.reserve(poses.size());auto revert=[&](){bool restored=true;for(auto i:applied)restored&=body.model.SetGroupPose(i,body.bindings[i].modelPose);if(restored){body.published=body.model.Revision();body.publishedMesh.revision=body.published;}};
  for(size_t i=0;i<poses.size();++i)if(posed[i]){if(!body.model.SetGroupPose(i,poses[i])){revert();return false;}applied.push_back(i);}
  const auto result=PublishPoses(body,posed);
  if(result!=Animated::Committed){revert();if(result==Animated::Full)return false;ReportDeferred(body,"[vehicle-body] animated publication deferred",body.publication.Failed(bodypublication::Failure::Transient,false),0);return true;}
  for(auto i:applied)body.bindings[i].modelPose=poses[i];
 }
 for(size_t i=0;i<poses.size();++i)body.bindings[i].sampledPose=poses[i];return true;
}
// No incidents, no pending publication and unchanged part poses: nothing to
// stage, so the body ticks in place. Only a tick that changes geometry (flex)
// stages a candidate; if that publication defers, the next world frame
// republishes through the staged path.
bool QuietFrame(Body &body,float dt,std::chrono::steady_clock::time_point stamp){
 if(!body.events.empty()||body.repair||body.modelId==181||body.published!=body.model.Revision())return false;
 const auto &faces=PartFaces(body);for(size_t i=0;i<body.bindings.size();++i)if(faces[i]&&CurrentPose(body,body.bindings[i])!=body.bindings[i].modelPose)return false;
 const auto worldLoad=body.loads.Sample(P(body.plane->m_vecMoveSpeed*50),dt,false);const auto localLoad=Multiply3x3(Invert(body.plane->GetMatrix()),V(worldLoad));
 const auto revision=body.model.Revision();body.model.Tick(dt,body.plane->bEngineOn,P(localLoad));
 if(body.model.Revision()==revision){auto snapshot=body.snapshot;SnapshotState(body,body.model,snapshot);snapshot.detachedBodies=fragments.size();snapshot.revision=body.plane->InstanceCollisionRevision();body.snapshot=snapshot;}
 else{auto candidate=body.model;if(!Publish(body,candidate)){ReportDeferred(body,"[aircraft-body] publication deferred",body.publication.Failed(body.publishFailure,false),0);return true;}body.publication.Committed();}
 body.before=body.plane->GetMatrix();double milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-stamp).count();body.maxMs=std::max(body.maxMs,milliseconds);if(std::getenv("VC_REGULAR_SMOKE"))std::fprintf(stderr,"[aircraft-body] frame_ms=%.3f\n",milliseconds);return true;
}
}
bool AircraftBodyFirePosition(const CEntity *entity,CVector &position){auto body=Find(entity);if(!body||!body->captured)return false;position=body->plane->GetMatrix()*V(body->model.FireSource());return true;}
bool VCSourceAircraftBodyOwned(const void *v){auto body=Find(v);return body&&body->captured;}
bool VCSourceAircraftBodyRead(const void *v,AircraftBodySnapshot *out){auto body=Find(v);if(!body||!body->captured||!out)return false;*out=body->snapshot;return true;}
bool VCSourceAircraftBodyBullet(void *v,Vec3 a,Vec3 b,float budget,float limit){auto body=Lease(v);if(!body||!body->plane->bCanBeDamaged||body->plane->bBulletProof||!Capture(*body)||!Finite(a)||!Finite(b)||!std::isfinite(budget)||budget<=0||!std::isfinite(limit)||limit<=0||limit>1||body->events.size()>=256||!body->publication.AcceptsHoles())return false;body->events.push_back({Event::Bullet,a,b,budget,limit,CTimer::GetTimeInMilliseconds()});return true;}
bool VCSourceAircraftBodyContact(void *v,Vec3 p,Vec3 n,float dv,uint32_t when){auto body=Lease(v);if(!body||!Capture(*body)||!Finite(p)||!Finite(n)||!std::isfinite(dv)||dv<=0||body->events.size()>=256)return false;if(overlapQueries)return true;int region=p.y<-3?2:p.y>1.5f?3:p.x<0?4:5;float work=-1,best=.05f*.05f;const Vec3 queried=p;for(auto &w:body->witnesses){if(w.frame!=CTimer::GetFrameCounter())continue;auto difference=V(w.point)-V(queried);if(difference.MagnitudeSqr()>best||DotProduct(V(w.normal),V(n))<.9f)continue;best=difference.MagnitudeSqr();p=w.material;if(w.source<body->model.Surfaces().size()){auto role=body->model.Surfaces()[w.source].role;region=role==Role::WingLeft?0:role==Role::WingRight?1:p.y<-3?2:p.y>1.5f?3:p.x<0?4:5;}auto lever=Multiply3x3(body->plane->GetMatrix(),V(p));auto worldNormal=Multiply3x3(body->plane->GetMatrix(),V(n));float separating=DotProduct(body->plane->GetSpeed(lever)*50,worldNormal);work=materialmodel::ImpulseWork(body->plane->m_fMass*dv,w.closing,separating);}if(work==0)return true;body->events.push_back({Event::Contact,p,n,dv,work,when,region});return true;}
// Native main-rotor disc overlap -> material work on the actual struck blade.
// The drawn rotor frame supplies hub and plane; the posed MainRotor material
// supplies blade extent, inertia and the struck station (aircraftbodybridge.h).
namespace {
bool MainRotorSurface(const aircraftbodymodel::Surface &s){return s.role==Role::MainRotor;}
bool BladeFrame(const Body &body,CVector &hub,CVector &axis,CVector &e1,CVector &e2){
 for(const auto &s:body.model.Surfaces())if(s.role==Role::MainRotor&&s.group<body.bindings.size()){auto frame=body.bindings[s.group].atomic->getFrame();if(!frame)return false;
  auto m=Local(frame,body.clump->getFrame());hub=m.GetPosition();axis=m.GetUp();e1=m.GetRight();if(axis.MagnitudeSqr()<1e-8f)return false;axis.Normalise();e1=e1-axis*DotProduct(e1,axis);if(e1.MagnitudeSqr()<1e-8f)return false;e1.Normalise();e2=CrossProduct(axis,e1);return true;}
 return false;
}
// One accepted strike per kernel contact episode: repeats within 350ms spend only
// a stronger load, so a continued overlap cannot rerun the dry-run every frame.
struct BladeStrikeRecord {uint64_t generation=0;uint32_t when=0;float work=0;};std::map<const void *,BladeStrikeRecord> bladeStrikes;
}
bool VCSourceAircraftBodyBladeRadius(const void *v,float *radius){
 auto body=Find(v);CVector hub,axis,e1,e2;if(!body||!body->captured||!radius||!body->Current()||!BladeFrame(*body,hub,axis,e1,e2))return false;
 float outer,inertia,axial;aircraftbodyblade::Samples(body->model,P(hub),P(axis),P(e1),P(e2),MainRotorSurface,outer,inertia,axial);*radius=outer;return true;
}
bool VCSourceAircraftBodyBladeStrike(void *v,float spin,uint32_t when){
 auto body=Lease(v);if(!body||!Capture(*body)||!std::isfinite(spin)||spin<=0||spin>1||body->events.size()>=256)return false;
 auto plane=body->plane;CVector hub,axis,e1,e2;if(!plane->bCanBeDamaged||plane->bCollisionProof||!BladeFrame(*body,hub,axis,e1,e2))return false;
 float outer,inertia,axial;const auto samples=aircraftbodyblade::Samples(body->model,P(hub),P(axis),P(e1),P(e2),MainRotorSurface,outer,inertia,axial);if(samples.empty()||!(outer>0))return false;
 // Nearest solid obstacle inside the attached blade disc, sampled on the tip plane.
 const CMatrix &m=plane->GetMatrix();const CVector start=m*hub;CEntity *ignored=CWorld::pIgnoreEntity;CWorld::pIgnoreEntity=plane;
 constexpr int kRays=48;float best=1;int bearing=-1;CColPoint hit;CEntity *obstacle=nullptr;
 for(int k=0;k<kRays;++k){const float a=k*TWOPI/kRays;const CVector end=m*(hub+(e1*Cos(a)+e2*Sin(a))*outer+axis*axial);CColPoint point;CEntity *entity=nullptr;
  if(!CWorld::ProcessLineOfSight(start,end,point,entity,true,true,false,true,false,false,false,false))continue;
  if(entity&&entity->IsObject()&&((CObject*)entity)->m_pCollidingEntity==plane)continue;
  const float t=(point.point-start).Magnitude()/Max((end-start).Magnitude(),.001f);if(t<best){best=t;bearing=k;hit=point;obstacle=entity;}}
 CWorld::pIgnoreEntity=ignored;if(bearing<0)return false;
 const float angle=bearing*TWOPI/kRays,radius=best*outer;const CVector station=hub+(e1*Cos(angle)+e2*Sin(angle))*radius;
 const CVector relative=(plane->GetSpeed(Multiply3x3(m,station))-AircraftBodySurfaceSpeed(obstacle,hit.point))*50.0f,airframe=Multiply3x3(Invert(m),relative);
 auto &record=bladeStrikes[v];if(record.generation!=body->generation){record={};record.generation=body->generation;}
 const float minimum=when>=record.when&&when-record.when<350?record.work:0;
 const auto plan=aircraftbodyblade::Plan(body->model,samples,P(axis),outer,inertia,angle,radius,spin*50,P(airframe),when,minimum,MainRotorSurface);if(!plan.valid)return false;
 record.when=when;record.work=plan.work;body->events.push_back({Event::Contact,plan.point,plan.inward,0,plan.work,when,-1});
 std::fprintf(stderr,"[vehicle-body] blade strike ref=%d model=%d station=%.2f/%.2fm work=%.0fJ root=%d released=%.2fm2\n",body->reference,body->modelId,plan.radius,outer,plan.work,plan.root,plan.released);return true;
}
bool VCSourceAircraftBodyTrace(const void *v,Vec3 a,Vec3 b,float entry,float *fraction,Vec3 *normal){auto body=Lease(v);return body&&Capture(*body)&&fraction&&normal&&body->model.TraceComponent(a,b,entry,*fraction,*normal);}
void VCSourceAircraftBodyBurn(void *v,float dt){auto body=Lease(v);if(body&&body->plane->bCanBeDamaged&&!body->plane->bFireProof&&Capture(*body)&&std::isfinite(dt)&&dt>0&&dt<=1&&body->events.size()<256)body->events.push_back({Event::Burn,{},{},dt,1,0});}
void VCSourceAircraftBodyRepair(void *v){if(auto body=Lease(v)){body->repair=true;body->generation=nextGeneration++;body->snapshot.generation=body->generation;body->events.clear();}}
void VCSourceAircraftBodyControls(void *v,float l,float r,float e,float y){if(auto body=Lease(v))if(std::isfinite(l)&&std::isfinite(r)&&std::isfinite(e)&&std::isfinite(y))body->controls={{l,r,e,y}};}
void AircraftBodyDeleting(const CEntity *entity){
 auto it=bodies.find(entity);if(it!=bodies.end()){auto &b=*it->second;if(b.Current())b.RestoreGeometry();b.geometryLive=false;b.generation=nextGeneration++;b.snapshot.generation=b.generation;}
 fragments.erase(std::remove_if(fragments.begin(),fragments.end(),[&](const FragmentActor &p){return p.object==entity;}),fragments.end());
}
// Start pose of each active native shift pass, innermost last.
namespace {struct ShiftPose {const CPhysical *entity;CMatrix pose;};std::vector<ShiftPose> shiftPoses;const CMatrix *ShiftStart(const CPhysical *entity){for(auto it=shiftPoses.rbegin();it!=shiftPoses.rend();++it)if(it->entity==entity)return &it->pose;return nullptr;}}
AircraftBodyOverlapQuery::AircraftBodyOverlapQuery(){++overlapQueries;}
AircraftBodyOverlapQuery::AircraftBodyOverlapQuery(CPhysical *entity){++overlapQueries;if(entity)try{shiftPoses.push_back({entity,entity->GetMatrix()});shift=true;}catch(const std::bad_alloc&){}}
AircraftBodyOverlapQuery::~AircraftBodyOverlapQuery(){--overlapQueries;if(shift&&!shiftPoses.empty())shiftPoses.pop_back();}
void AircraftBodyShutdown(){worldSuspended=true;bodies.clear();fragments.clear();prototypes.clear();}
void AircraftBodyFrameImpl(){
 worldSuspended=false;
 const auto frameStart=std::chrono::steady_clock::now();
 for(auto it=bodies.begin();it!=bodies.end();){auto &b=*it->second;if(CPools::GetVehicle(b.reference)!=b.plane||b.plane->GetModelIndex()!=b.modelId){b.geometryLive=false;it=bodies.erase(it);++ownersEpoch;continue;}++it;}
 for(auto &part:fragments)if(CPools::GetObject(part.reference)==part.object)part.before=part.object->GetMatrix();
 const char *experiment=std::getenv("VC_EXPERIMENTAL_WING");if(experiment&&!strcmp(experiment,"1"))return;
 // Every resident vehicle gets its real material envelope before motion.
 // Waiting for a proxy impulse would miss a first wing/door contact outside
 // the stock COL. Immutable prototypes amortize graph setup across traffic.
 // A captured, current owner needs neither the pool scan in Eligible nor a re-capture.
 auto pool=CPools::GetVehiclePool();if(pool)for(int i=0;i<pool->GetSize();++i){auto v=pool->GetSlot(i);if(!v)continue;if(auto known=Find(v))if(known->captured&&known->Current())continue;if(Eligible(v)){auto b=Lease(v);if(b)Capture(*b);}}
 for(auto &entry:bodies){
  if(entry.second->captured&&(entry.second->repair||!entry.second->Current())&&InSectorGrid(entry.second->plane)){
   auto replacement=StageReplacement(*entry.second,entry.second->repair);if(!replacement)continue;auto candidate=replacement->model;if(!SamplePoses(*replacement,candidate)||!Publish(*replacement,candidate)){if(!entry.second->replacementReported)std::fprintf(stderr,"[vehicle-body] replacement publication deferred ref=%d reason=%s\n",entry.second->reference,bodypublication::Name(replacement->publishFailure));entry.second->replacementReported=true;continue;}
   // A rebound (non-repair) body keeps its saturated capacity; repair starts clear.
   const bool saturated=!entry.second->repair&&entry.second->publication.saturated;
   replacement->previous=nullptr;replacement->repair=replacement->healthyCapture=false;replacement->restoreOnDestruct=true;entry.second->restoreOnDestruct=false;entry.second=std::move(replacement);entry.second->publication.saturated=saturated;++ownersEpoch;
  }
  auto &body=*entry.second;
  body.before=body.plane->GetMatrix();for(auto &point:body.sweep)point.previous=point.now;body.witnesses.clear();if(!body.captured)continue;
  // Out of the grid nothing ticks or publishes; incidents wait for its return.
  if(!InSectorGrid(body.plane))continue;
  auto stamp=std::chrono::steady_clock::now();float dt=CTimer::GetTimeStepInSeconds();if(!std::isfinite(dt)||dt<=0||dt>.1f)continue;
  if(QuietFrame(body,dt,stamp))continue;
  auto candidate=body.model;bool changed=body.published!=body.model.Revision(),spentContactWork=false;
  // All witnesses refer to the same committed shape. Spend the strongest
  // manifold load for each existing contact episode before its geometry bends.
  std::array<int,6> strongest{{-1,-1,-1,-1,-1,-1}};std::vector<bool> selected(body.events.size(),true);
  auto workFor=[&](const Event &e){return e.limit>=0?e.limit:.5f*std::min(120.f,body.plane->m_fMass*.035f)*e.value*e.value*.55f;};
  for(size_t i=0;i<body.events.size();++i){auto &event=body.events[i];if(event.kind!=Event::Contact||event.region<0||event.region>=6)continue;auto &index=strongest[event.region];if(index<0)index=i;else if(workFor(event)>workFor(body.events[index])){selected[index]=false;index=i;}else selected[i]=false;}
  for(size_t incident=0;incident<body.events.size();++incident){if(!selected[incident])continue;auto &event=body.events[incident];aircraftbodymodel::Result result;if(event.kind==Event::Bullet){result=candidate.Bullet(event.a,event.b,event.value,event.limit);++body.bullets;if(std::getenv("VC_REGULAR_SMOKE"))std::fprintf(stderr,"[vehicle-body] bullet model=%d budget=%.1f from=%.6f/%.6f/%.6f to=%.6f/%.6f/%.6f valid=%d changed=%d holes=%zu deposited=%.4f remaining=%.4f\n",body.modelId,event.value,event.a.x,event.a.y,event.a.z,event.b.x,event.b.y,event.b.z,result.valid,result.changed,result.holes,result.deposited,result.remaining);}else if(event.kind==Event::Contact){result=event.limit>=0?candidate.ContactEnergy(event.a,event.b,event.limit,event.when,dt):candidate.Contact(event.a,event.b,event.value,event.when,dt);++body.contacts;}else if(event.kind==Event::Yield){int share=0;for(auto &e:body.events)share+=e.kind==Event::Yield&&e.when==event.when;result=candidate.Yield(event.source,event.a,event.b,event.value,event.limit/Max(1,share),event.when);if(result.deposited>0)body.yieldWork+=result.deposited;}else{candidate.Burn(event.value);result.valid=true;}if(!result.valid){std::fprintf(stderr,"[aircraft-body] rejected incident ref=%d kind=%d\n",body.reference,int(event.kind));continue;}spentContactWork|=event.kind==Event::Contact&&result.deposited>0;changed|=result.changed;}
  std::vector<CMatrix> savedFrames;savedFrames.reserve(body.bindings.size());for(auto &binding:body.bindings){CMatrix value;value=CMatrix(RwFrameGetMatrix(binding.atomic->getFrame()),false);savedFrames.push_back(value);}
  struct RestoreFrames{Body &body;std::vector<CMatrix> &saved;bool committed=false;~RestoreFrames(){if(!committed){for(size_t i=0;i<saved.size();++i){CMatrix frame(RwFrameGetMatrix(body.bindings[i].atomic->getFrame()),false);frame=saved[i];frame.UpdateRW();}RwFrameUpdateObjects(body.clump->getFrame());}}} restoreFrames{body,savedFrames};
  static auto pose=reinterpret_cast<void(*)(void*,float,float,float,float)>(dlsym(RTLD_DEFAULT,"VCSourceDodoSurfacePose"));if(pose&&body.modelId==181)pose(body.clump,body.controls[0],body.controls[1],body.controls[2],body.controls[3]);
  std::vector<std::pair<size_t,std::array<float,12>>> posedParts;posedParts.reserve(body.bindings.size());for(size_t i=0;i<body.bindings.size();++i){const auto transform=CurrentPose(body,body.bindings[i]);if(!candidate.SetGroupPose(i,transform))continue;posedParts.push_back({i,transform});}
  const auto worldLoad=body.loads.Sample(P(body.plane->m_vecMoveSpeed*50),dt,spentContactWork);const auto localLoad=Multiply3x3(Invert(body.plane->GetMatrix()),V(worldLoad));candidate.Tick(dt,body.plane->bEngineOn,P(localLoad));changed|=candidate.Revision()!=body.model.Revision();
  // A failed candidate is never replayed indefinitely: capacity/invalid
  // incidents retire at once, resource shortages after a bounded retry.
  if(changed){if(!Publish(body,candidate)){const size_t incidents=body.events.size();ReportDeferred(body,"[aircraft-body] publication deferred",bodypublication::Retire(body.publication,body.publishFailure,body.events),incidents);continue;}body.publication.Committed();}else{auto snapshot=SnapshotFor(body,candidate);body.model=std::move(candidate);body.snapshot=snapshot;}
  for(auto &part:posedParts)body.bindings[part.first].modelPose=part.second;
  restoreFrames.committed=true;body.events.clear();body.repair=false;body.before=body.plane->GetMatrix();double milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-stamp).count();body.maxMs=std::max(body.maxMs,milliseconds);if(std::getenv("VC_REGULAR_SMOKE"))std::fprintf(stderr,"[aircraft-body] frame_ms=%.3f\n",milliseconds);
 }
 for(auto &part:fragments)if(CPools::GetObject(part.reference)==part.object)part.before=part.object->GetMatrix();
 if(std::getenv("VC_REGULAR_SMOKE"))std::fprintf(stderr,"[vehicle-body] actors=%zu frame_ms=%.3f\n",bodies.size(),std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frameStart).count());
}
void AircraftBodyFrame(){try{AircraftBodyFrameImpl();}catch(const std::bad_alloc &){std::fprintf(stderr,"[aircraft-body] allocation unavailable; committed revision retained\n");}}
// Each native pair pass starts here; its building responses are averaged.
namespace {bodycontact::Pass responsePass;
// Regular-game smoke only: one traced actor's material contact passes.
int traceReference=-1,traceAlso=-1;bool Traced(const CPhysical *physical){static const bool smoke=std::getenv("VC_REGULAR_SMOKE")!=nullptr;static const int model=[]{const char *m=std::getenv("VC_REGULAR_TRACE_MODEL");return m?std::atoi(m):-1;}();if(!smoke||(traceReference<0&&traceAlso<0&&model<0)||!physical||!const_cast<CPhysical*>(physical)->IsVehicle())return false;const int ref=CPools::GetVehicleRef((CVehicle*)physical);return ref==traceReference||ref==traceAlso||(model>=0&&const_cast<CPhysical*>(physical)->GetModelIndex()==model);}
void TraceContacts(const CPhysical *physical,const CEntity *other,const CColPoint *points,int count,bool fallback,bool shift){if(!Traced(physical))return;auto mutableActor=const_cast<CPhysical*>(physical);float depth=0,low=1,high=-1;for(int i=0;i<count;++i){depth=Max(depth,points[i].depth);low=Min(low,points[i].normal.z);high=Max(high,points[i].normal.z);}std::fprintf(stderr,"[curb-trace] contacts frame=%u other=%d/%s type=%d count=%d overlap=%u shift=%d fallback=%d max_depth=%.4f nz=%.2f..%.2f pos=%.3f %.3f %.3f up=%.3f %.3f %.3f\n",CTimer::GetFrameCounter(),other?other->GetModelIndex():-1,other?CModelInfo::GetModelInfo(other->GetModelIndex())->GetModelName():"-",other?int(other->GetType()):-1,count,overlapQueries,shift,fallback,depth,count?low:0,count?high:0,mutableActor->GetPosition().x,mutableActor->GetPosition().y,mutableActor->GetPosition().z,mutableActor->GetMatrix().GetUp().x,mutableActor->GetMatrix().GetUp().y,mutableActor->GetMatrix().GetUp().z);for(int i=0;i<count;++i)if(points[i].depth>.05f||!std::isfinite(points[i].depth))std::fprintf(stderr,"[curb-trace]  deep point depth=%.4f point=%.3f %.3f %.3f normal=%.3f %.3f %.3f\n",points[i].depth,points[i].point.x,points[i].point.y,points[i].point.z,points[i].normal.x,points[i].normal.y,points[i].normal.z);}
}
void AircraftBodyTrace(int reference){traceReference=reference;}
void AircraftBodyTraceAlso(int reference){traceAlso=reference;}
std::map<int,std::pair<float,int>> smokeWork;
void AircraftBodySmokeWork(int reference,float &work,int &events,bool reset){auto &w=smokeWork[reference];work=w.first;events=w.second;if(reset)w={0.f,0};}
bool AircraftBodySmokeModules(CVehicle *vehicle,char *out,size_t size){auto body=Find(vehicle);if(!body||!body->captured||!size)return false;const auto &m=body->model.Mechanical();int n=std::snprintf(out,size,"coolant=%.2f heat=%.2f fuel=%.2f",m.coolant,m.heat,m.fuel);for(int i=0;i<mechanicalmodel::kModules&&n>0&&size_t(n)<size;++i)n+=std::snprintf(out+n,size-n," %s=%.3f",mechanicalmodel::Name(i),m.damage[i]);return true;}
void AircraftBodySmokeUnowned(const void *vehicle,bool unowned){auto at=std::find(smokeUnowned.begin(),smokeUnowned.end(),vehicle);if(unowned&&at==smokeUnowned.end())smokeUnowned.push_back(vehicle);if(!unowned&&at!=smokeUnowned.end())smokeUnowned.erase(at);}
bool AircraftBodySmokeLowestPart(CVehicle *vehicle,char *out,size_t size){auto body=Find(vehicle);if(!body||!body->captured||!body->Current()||body->sweep.empty()||!out||!size)return false;float z=1e30f;size_t source=0;for(const auto &point:body->sweep){const float h=(vehicle->GetMatrix()*point.now).z;if(h<z){z=h;source=point.source;}}const char *name=source<body->faces.size()&&body->faces[source].group<body->bindings.size()?body->bindings[body->faces[source].group].name.c_str():"?";std::snprintf(out,size,"%s",name);return true;}
bool AircraftBodySmokeLowest(CVehicle *vehicle,float &z){auto body=Find(vehicle);if(!body||!body->captured||!body->Current()||body->sweep.empty())return false;z=1e30f;for(const auto &point:body->sweep)z=Min(z,(vehicle->GetMatrix()*point.now).z);return true;}
// Last static response's yield state (applied impulse, or -1 when rigid).
namespace {bool yieldedLast=false;float yieldNative=0,yieldApplied=-1;std::map<int,std::array<float,7>> smokeMomentum;}
int AircraftBodyContacts(CPhysical *physical,CEntity *other,CColPoint *points){
 responsePass.Begin(physical,other,CTimer::GetFrameCounter());
 CMatrix previous;const std::vector<SweepPoint> *vertices=nullptr;bool owned=false;if(auto body=Find(physical);body&&body->captured&&body->published!=~uint64_t(0)&&body->Current()){previous=body->before;vertices=&body->sweep;owned=true;}else for(auto &part:fragments)if(part.object==physical&&CPools::GetObject(part.reference)==physical){previous=part.before;vertices=&part.sweep;owned=true;break;}
 // A captured actor whose private material COL is installed but whose sweep set
 // is not current (clump replacement pending) keeps its stock rigid volumes.
 // Native ProcessColModels never tests the triangle-only private mesh as A.
 if(!owned&&other&&Find(physical)){auto stock=CModelInfo::GetModelInfo(physical->GetModelIndex())->GetColModel(),current=physical->GetColModel(),target=other->GetColModel();if(stock&&current&&target&&stock!=current&&(stock->numSpheres||stock->numBoxes)){CColModel core;core.numSpheres=stock->numSpheres;core.spheres=stock->spheres;core.numBoxes=stock->numBoxes;core.boxes=stock->boxes;core.boundingBox=stock->boundingBox;core.boundingSphere=stock->boundingSphere;core.ownsCollisionVolumes=false;const int fallback=CCollision::ProcessColModels(physical->GetMatrix(),core,other->GetMatrix(),*target,points,nil,nil);TraceContacts(physical,other,points,fallback,true,false);return fallback;}}
 if(!owned||!other)return -1;auto model=physical->GetColModel(),target=other->GetColModel();if(!model||!target)return -1;int count=0;auto targetInverse=Invert(other->GetMatrix());
 // Floor tests are judged from the body's highest world point at the sweep's
 // start pose (bodycontact::WorldTop), which stays above the floor on its roof.
 const CMatrix *shiftStart=overlapQueries?ShiftStart(physical):nullptr;const CMatrix &startPose=shiftStart?*shiftStart:previous;const CVector centre=startPose*((model->boundingBox.min+model->boundingBox.max)*.5f),reference=V(bodycontact::WorldTop(P(startPose*((model->boundingBox.min+model->boundingBox.max)*.5f)),P(startPose.GetRight()),P(startPose.GetForward()),P(startPose.GetUp()),P((model->boundingBox.max-model->boundingBox.min)*.5f)));
 const float thickness=bodycontact::kRestSkin;std::unique_ptr<TargetQuery> query;try{query.reset(new TargetQuery(*target));}catch(const std::bad_alloc &){/* Same exact native tests, without the optional lookup. */}
 CColModel probe;CColSphere sphere;sphere.Set(thickness,CVector(0,0,0),6,0);probe.numSpheres=1;probe.spheres=&sphere;probe.ownsCollisionVolumes=false;probe.boundingBox.Set(CVector(-thickness,-thickness,-thickness),CVector(thickness,thickness,thickness));probe.boundingSphere.Set(thickness,CVector(0,0,0));
 // A shift pass sweeps only its own move of the committed material shape.
 for(auto &vertex:*vertices){if(count==MAX_COLLISION_POINTS)break;auto a=shiftStart?*shiftStart*vertex.now:previous*vertex.previous,b=physical->GetMatrix()*vertex.now;auto localA=targetInverse*a,localB=targetInverse*b;auto bounds=target->boundingBox;
  if(std::min(localA.x,localB.x)>bounds.max.x+thickness||std::max(localA.x,localB.x)<bounds.min.x-thickness||std::min(localA.y,localB.y)>bounds.max.y+thickness||std::max(localA.y,localB.y)<bounds.min.y-thickness||std::min(localA.z,localB.z)>bounds.max.z+thickness||std::max(localA.z,localB.z)<bounds.min.z-thickness)continue;
  CColPoint hit{};bool crossing=false,penetrating=false;if((!overlapQueries||shiftStart)&&(b-a).MagnitudeSqr()>1e-12f){if(query){crossing=query->Line(localA,localB,hit);if(crossing){hit.point=other->GetMatrix()*hit.point;hit.normal=Multiply3x3(other->GetMatrix(),hit.normal);}}else{float fraction=1;crossing=CCollision::ProcessLineOfSight(CColLine(a,b),other->GetMatrix(),*target,hit,fraction,false,false);}}
  if(crossing){auto normal=P(hit.normal);const bool stationary=other->IsBuilding()||other->GetIsStatic();crossing=shiftStart?bodycontact::ShiftCrossing(P(a),P(b),P(hit.point),normal,hit.depth):bodycontact::Sweep(P(a),P(b),P(hit.point),normal,P(reference),stationary,hit.depth);hit.normal=V(normal);}
  if(!crossing){if(query){int triangle=-1;if(!query->Rest(localB,thickness,hit,triangle))continue;penetrating=triangle==-2;hit.point=other->GetMatrix()*hit.point;hit.normal=Multiply3x3(other->GetMatrix(),hit.normal);if(triangle>=0&&(other->IsBuilding()||other->GetIsStatic())){CVector face;target->trianglePlanes[triangle].GetNormal(face);face=Multiply3x3(other->GetMatrix(),face);Vec3 n;float exit;if(bodycontact::PassedThroughFace(P(hit.normal),P(face),P(hit.point),P(face.z>.5f?reference:centre),hit.depth,thickness,n,exit)){hit.normal=V(n);hit.depth=exit;penetrating=true;}}}else{CMatrix pose;pose.SetUnity();pose.GetPosition()=b;CColPoint resting[MAX_COLLISION_POINTS];if(!CCollision::ProcessColModels(pose,probe,other->GetMatrix(),*target,resting,nil,nil))continue;bodycontact::Store(hit,resting[0]);CColPoint inside;if(InsideBoxes(*target,localB,thickness,inside)){inside.point=other->GetMatrix()*inside.point;inside.normal=Multiply3x3(other->GetMatrix(),inside.normal);bodycontact::Store(hit,inside);penetrating=true;}}}

  bool duplicate=false;for(int i=0;i<count;++i)duplicate|=(points[i].point-hit.point).MagnitudeSqr()<.04f&&DotProduct(points[i].normal,hit.normal)>.95f;if(duplicate)continue;if(auto body=Find(physical))if(!overlapQueries&&body->witnesses.size()<256&&(other->IsBuilding()||other->GetIsStatic())){auto inverse=Invert(physical->GetMatrix());auto localPoint=P(inverse*hit.point);auto worldVelocity=physical->GetSpeed(hit.point-physical->GetPosition())*50;auto n=Multiply3x3(inverse,hit.normal);float lift=-1;if(query){CColPoint top{};CVector into(-hit.normal.x,-hit.normal.y,0);const float across=into.Magnitude();const CVector probe=across>.3f?b+into*(.05f/across):b;const CVector above=probe+CVector(0,0,.25f);if(query->Line(targetInverse*above,targetInverse*probe,top)){const CVector at=other->GetMatrix()*top.point,up=Multiply3x3(other->GetMatrix(),top.normal);if(up.z>.7f&&at.z>=b.z)lift=at.z-b.z;}}body->witnesses.push_back({localPoint,P(vertex.now),P(n),Max(0.f,-DotProduct(worldVelocity,hit.normal)),CTimer::GetFrameCounter(),vertex.source,lift,penetrating||crossing});}hit.Set(hit.depth,6,0,hit.surfaceB,hit.pieceB);bodycontact::Store(points[count++],hit);if(Traced(physical)&&!(points[count-1].depth==hit.depth))std::fprintf(stderr,"[curb-trace] stored depth mismatch stored=%g computed=%g crossing=%d\n",points[count-1].depth,hit.depth,int(crossing));
 }TraceContacts(physical,other,points,count,false,shiftStart!=nullptr);return count;
}
void AircraftBodyAcceptedImpulse(CPhysical *physical,CEntity *other,const CColPoint &hit,float impulse,float closing,float outward){
 if(overlapQueries||!physical||!physical->IsVehicle())return;auto vehicle=(CVehicle*)physical;
 // A yielded contact applied only its cap; damage books the native (rigid) work.
 if(yieldApplied>0&&yieldNative>yieldApplied&&std::isfinite(outward))outward=-closing+(outward+closing)*yieldNative/yieldApplied;
 if(Traced(physical)){auto inv=Invert(vehicle->GetMatrix());auto lp=inv*hit.point;auto ln=Multiply3x3(inv,hit.normal);std::fprintf(stderr,"[curb-trace] native-impulse ref=%d frame=%u other=%d closing=%.2f outward=%.2f impulse=%.0f point=%.2f %.2f %.2f normal=%.2f %.2f %.2f\n",CPools::GetVehicleRef(vehicle),CTimer::GetFrameCounter(),other?other->GetModelIndex():-1,closing,outward,impulse*50,lp.x,lp.y,lp.z,ln.x,ln.y,ln.z);}
 // ProcessCollisionSectorList averages a building pass over its contacts.
 const bool averaged=other&&responsePass.Matches(physical,other,CTimer::GetFrameCounter())&&(other->IsBuilding()||other->bIsStuck||((other->IsVehicle()||other->IsPed()||other->IsObject())&&((CPhysical*)other)->m_phy_flagA08));
 if(!vehicle->bCanBeDamaged||vehicle->bCollisionProof||(vehicle->GetStatus()!=STATUS_PLAYER&&vehicle->bOnlyDamagedByPlayer&&other!=FindPlayerPed()&&other!=FindPlayerVehicle())||closing<4.5f||!std::isfinite(impulse)||impulse<=0)return;
 float work=bodycontact::ChassisWork(impulse*50,closing,outward,false);if(work<=0)return;
 auto body=Lease(vehicle);if(!body||!Capture(*body)||body->events.size()>=256)return;
 auto inverse=Invert(vehicle->GetMatrix());Vec3 point=P(inverse*hit.point),normal=P(Multiply3x3(inverse,hit.normal));
 float best=.1f*.1f;for(auto &w:body->witnesses){auto d=V(w.point)-V(point);if(w.frame==CTimer::GetFrameCounter()&&d.MagnitudeSqr()<best){best=d.MagnitudeSqr();point=w.material;}}
 auto box=body->definition.bounds;float ymid=(box.lo.y+box.hi.y)*.5f,xmid=(box.lo.x+box.hi.x)*.5f;
 int region=point.y<ymid-(box.hi.y-box.lo.y)*.25f?2:point.y>ymid+(box.hi.y-box.lo.y)*.25f?3:point.x<xmid?4:5;
 // Wheeled ground vehicles: springs and tyres absorb bottoming and kerbed-tyre
 // loads that the native rigid response delivers to the chassis.
 const auto kind=body->definition.kind;if((kind==aircraftbodymodel::Kind::Automobile||kind==aircraftbodymodel::Kind::Bike)&&body->definition.hasWheels){float wheelZ=0;for(auto w:body->definition.wheels)wheelZ+=w.z*.25f;const float radius=std::max(.1f,wheelZ-box.lo.z);if(bodycontact::Underbody(point,normal,wheelZ,radius)||bodycontact::Tyre(point,body->definition.wheels.data(),4,radius))work=bodycontact::ChassisWork(impulse*50,closing,outward,true);if(work<=0)return;}
 body->events.push_back({Event::Contact,point,normal,impulse*50/Max(vehicle->m_fMass,.001f),work,CTimer::GetTimeInMilliseconds(),region});
 if(Traced(physical)){auto &w=smokeWork[CPools::GetVehicleRef(vehicle)];w.first+=work;++w.second;}
 if(Traced(physical))std::fprintf(stderr,"[curb-trace] impulse ref=%d frame=%u other=%d closing=%.2f outward=%.2f impulse=%.0f work=%.0f region=%d point=%.2f %.2f %.2f normal=%.2f %.2f %.2f\n",CPools::GetVehicleRef(vehicle),CTimer::GetFrameCounter(),other?other->GetModelIndex():-1,closing,outward,impulse*50,work,region,point.x,point.y,point.z,normal.x,normal.y,normal.z);
 if(averaged){const float divisor=responsePass.Record(body->events.size()-1,work,region);for(auto e:responsePass.Events())if(e.first<body->events.size()&&body->events[e.first].kind==Event::Contact)body->events[e.first].limit=e.second/divisor;}
}

void AircraftBodyAcceptedWeapon(CVehicle *vehicle,int weapon,float damage,const CVector &worldPoint){
 if(!vehicle||!std::isfinite(damage)||damage<=0)return;
 const bool explosion=weapon==WEAPONTYPE_EXPLOSION||weapon==WEAPONTYPE_GRENADE||weapon==WEAPONTYPE_ROCKET||weapon==WEAPONTYPE_MOLOTOV;
 const bool fire=weapon==WEAPONTYPE_FLAMETHROWER;
 const bool melee=weapon>=WEAPONTYPE_UNARMED&&weapon<=WEAPONTYPE_CHAINSAW;
 if(!explosion&&!fire&&!melee)return;auto body=Lease(vehicle);if(!body||!Capture(*body)||body->events.size()>=256)return;
 if(fire){body->events.push_back({Event::Burn,{},{},Min(.5f,damage*.005f),1,0});return;}
 const auto local=Invert(vehicle->GetMatrix())*worldPoint;CVector point=local,normal(0,0,1);float distance=1e30f;
 for(auto &fragment:body->model.Fragments())if(fragment.attached&&fragment.polygon.size()>=3){CVector centre(0,0,0);for(auto &v:fragment.polygon)centre+=V(body->model.Deform(v.position,fragment.group));centre/=fragment.polygon.size();float d=(centre-local).MagnitudeSqr();if(d<distance){distance=d;point=centre;auto a=V(body->model.Deform(fragment.polygon[0].position,fragment.group)),b=V(body->model.Deform(fragment.polygon[1].position,fragment.group)),c=V(body->model.Deform(fragment.polygon[2].position,fragment.group));normal=CrossProduct(b-a,c-a);if(normal.MagnitudeSqr()>1e-12f)normal.Normalise();}}
 // The same finite contact patch consumes accepted blast/melee work. Native
 // explosion propagation, mission proof tests and rigid impulses remain native.
 if(explosion&&(point-local).MagnitudeSqr()>1e-10f){normal=point-local;normal.Normalise();}
 body->events.push_back({Event::Contact,P(point),P(normal),0,damage*(explosion?1800.f:30.f),CTimer::GetTimeInMilliseconds(),-1});
}

bool AircraftBodyBuoyancyBounds(CPhysical *physical,CVector &lo,CVector &hi){auto b=Find(physical);if(!b||!b->captured||!b->plane->IsBoat())return false;lo=b->buoyancyMin;hi=b->buoyancyMax;return true;}

CVector AircraftBodySurfaceSpeed(CEntity *entity,const CVector &point){if(entity&&(entity->IsVehicle()||entity->IsObject()||entity->IsPed()))return ((CPhysical*)entity)->GetSpeed(point-entity->GetPosition());return CVector(0,0,0);}

// Yield-limited static-world response for wheeled material vehicles. The
// struck part (this frame's witness vertex and its surface) carries at most
// its yield force over the native step; the motion receives the capped
// impulse and the plastic push takes the material out of the obstacle.
const CPhysical *yieldPhysical=nullptr;
float AircraftBodyYieldImpulse(CPhysical *physical,CEntity *other,const CColPoint &hit,float impulse){
 yieldedLast=false;yieldApplied=-1;yieldPhysical=physical;struct ClassTrace{const CPhysical *p;const CColPoint &hit;float impulse;~ClassTrace(){if(Traced(p))std::fprintf(stderr,"[curb-trace] class frame=%u depth=%.4f nz=%.3f point_z=%.3f impulse=%.1f applied=%.1f rest_or_yield=%d\n",CTimer::GetFrameCounter(),hit.depth,hit.normal.z,hit.point.z,impulse*50,yieldApplied>=0?yieldApplied*50:impulse*50,int(yieldedLast));}}classTrace{physical,hit,impulse};if(!physical||!physical->IsVehicle()||!other||!(other->IsBuilding()||other->GetIsStatic())||!std::isfinite(impulse)||impulse<=0||overlapQueries)return impulse;
 auto body=Find(physical);if(!body||!body->captured||!body->Current()||!body->definition.hasWheels)return impulse;const auto kind=body->definition.kind;if(kind!=aircraftbodymodel::Kind::Automobile&&kind!=aircraftbodymodel::Kind::Bike)return impulse;
 auto vehicle=(CVehicle*)physical;auto inverse=Invert(vehicle->GetMatrix());const Vec3 point=P(inverse*hit.point);const Witness *w=nullptr;float best=.1f*.1f;
 for(auto &x:body->witnesses){auto d=V(x.point)-V(point);if(x.frame==CTimer::GetFrameCounter()&&d.MagnitudeSqr()<best){best=d.MagnitudeSqr();w=&x;}}if(!w)return impulse;
 if(!bodycontact::Yieldable(P(hit.normal))){yieldedLast=bodycontact::Resting(hit.depth,false,w->penetrating);return impulse;}
 const float force=body->model.YieldForce(w->source,w->material);bool yielded=false;const float applied=bodycontact::Applied(impulse,bodycontact::YieldCap(force,CTimer::GetTimeStep()),yielded);
 if(!yielded){yieldedLast=bodycontact::Resting(hit.depth,false,w->penetrating);return impulse;}yieldedLast=true;yieldNative=impulse;yieldApplied=applied;
 const float closing=-DotProduct(vehicle->GetSpeed(hit.point-vehicle->GetPosition())*50,hit.normal);const Vec3 push=bodycontact::Push(P(hit.normal),bodycontact::BackExit(Max(hit.depth,.002f),closing,CTimer::GetTimeStep()),w->lift);const CVector local=V(bodycontact::BodyPush(P(Multiply3x3(inverse,V(push)))));const float length=local.Magnitude();
 if(length>1e-5f&&body->events.size()<256)body->events.push_back({Event::Yield,w->material,P(local*(1/length)),Min(.5f,length),force,CTimer::GetFrameCounter(),-1,w->source});
 if(Traced(physical)){auto &m=smokeMomentum[CPools::GetVehicleRef(vehicle)];m[6]+=1;std::fprintf(stderr,"[curb-trace] yield frame=%u native=%.1f applied=%.1f force=%.0f zone=%d pitch=%.3f push=%.3f world=%.3f %.3f %.3f local=%.3f %.3f %.3f lift=%.3f point=%.2f %.2f %.2f\n",CTimer::GetFrameCounter(),impulse*50,applied*50,force,body->model.YieldZone(w->source,w->material),vehicle->GetForward().z,length,push.x,push.y,push.z,local.x,local.y,local.z,w->lift,w->material.x,w->material.y,w->material.z);}
 return applied;
}
bool AircraftBodyYielded(){const bool out=yieldedLast;yieldedLast=false;return out;}
float AircraftBodyAppliedImpulse(float native){return yieldApplied>=0?yieldApplied:native;}
bool AircraftBodyRigid(int responses,int yielded){const bool rigid=bodycontact::Stuck(responses,yielded);if(Traced(yieldPhysical))std::fprintf(stderr,"[curb-trace] pass frame=%u responses=%d rest_or_yield=%d rigid=%d\n",CTimer::GetFrameCounter(),responses,yielded,int(rigid));return rigid;}
// Smoke only: applied static-response and shift-damping velocity changes (m/s).
void AircraftBodySmokeMomentum(CPhysical *physical,int kind,const CVector &delta){if(!Traced(physical))return;auto &m=smokeMomentum[CPools::GetVehicleRef((CVehicle*)physical)];const int k=kind?3:0;m[k]+=delta.x*50;m[k+1]+=delta.y*50;m[k+2]+=delta.z*50;}
float AircraftBodySmokeYieldWork(CVehicle *vehicle){auto body=Find(vehicle);return body?body->yieldWork:0;}
void AircraftBodySmokeMomentumTake(int reference,float out[7],bool reset){auto &m=smokeMomentum[reference];for(int i=0;i<7;++i)out[i]=m[i];if(reset)m={};}
CVector AircraftBodyCOMOffset(CPhysical *physical){auto b=Find(physical);return b?b->materialCOMOffset:CVector(0,0,0);}
CVector AircraftBodyComposeCOM(CPhysical *physical,const CVector &native){return native+AircraftBodyCOMOffset(physical);}

void AircraftBodyRenderBoundary(){
 const auto stamp=std::chrono::steady_clock::now();
 try{for(auto &entry:bodies){auto &body=*entry.second;if(!body.captured||!body.Current()||body.published==~uint64_t(0)||!InSectorGrid(body.plane))continue;std::vector<std::array<float,12>> poses;poses.reserve(body.bindings.size());bool dirty=false;
  for(auto &binding:body.bindings){const auto pose=CurrentPose(body,binding);for(size_t k=0;k<12;++k)dirty|=std::fabs(pose[k]-binding.sampledPose[k])>1e-6f;poses.push_back(pose);}
  // Pose-only animation patches the committed COL in place of a full rebuild.
  if(!dirty||AnimatePublish(body,poses))continue;auto candidate=body.model;bool valid=true;for(size_t i=0;i<poses.size();++i)valid&=candidate.SetGroupPose(i,poses[i]);
  if(valid&&(candidate.Revision()==body.model.Revision()||Publish(body,candidate))){for(size_t i=0;i<poses.size();++i)body.bindings[i].sampledPose=body.bindings[i].modelPose=poses[i];}else{if(!valid)body.publishFailure=bodypublication::Failure::Invalid;ReportDeferred(body,"[vehicle-body] animated publication deferred",body.publication.Failed(body.publishFailure,false),0);}
 }}catch(const std::bad_alloc&){std::fprintf(stderr,"[vehicle-body] animation allocation unavailable; committed material retained\n");}
 if(std::getenv("VC_REGULAR_SMOKE"))std::fprintf(stderr,"[vehicle-body] render_ms=%.3f\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-stamp).count());
}

void AircraftBodyPrepareClump(CEntity *entity){if(auto b=Find(entity))if(b->captured&&b->plane->GetModelIndex()==b->modelId&&((CVehicleModelInfo*)CModelInfo::GetModelInfo(b->modelId))->m_numComps>0)CVehicleModelInfo::SetComponentsToUse(b->plane->m_aExtras[0],b->plane->m_aExtras[1]);}
void AircraftBodyClumpCreated(CEntity *entity){auto b=Find(entity);if(!b||!b->captured||!entity->m_rwObject)return;auto v=b->plane;
 if(v->IsCar()){auto car=(CAutomobile*)v;car->SetupModelNodes();if(car->IsRealHeli()){for(int node:{CAR_WHEEL_LF,CAR_WHEEL_RF,CAR_WHEEL_LB,CAR_WHEEL_RB}){if(v->GetModelIndex()==MI_HUNTER&&(node==CAR_WHEEL_LF||node==CAR_WHEEL_RF))continue;if(car->m_aCarNodes[node]){auto atomic=(RpAtomic*)GetFirstObject(car->m_aCarNodes[node]);if(atomic)RpAtomicSetFlags(atomic,0);}}}}else if(v->IsBike())((CBike*)v)->SetupModelNodes();else if(v->IsBoat())((CBoat*)v)->SetupModelNodes();
 else if(v->IsHeli()){auto h=(CHeli*)v;for(int i=0;i<NUM_HELI_NODES;++i)h->m_aHeliNodes[i]=nil;CClumpModelInfo::FillFrameArray(v->GetClump(),h->m_aHeliNodes);}
#ifdef CPLANE_ROTORS
 else if(v->IsPlane()){auto p=(CPlane*)v;for(int i=0;i<NUM_PLANE_NODES;++i)p->m_aPlaneNodes[i]=nil;CClumpModelInfo::FillFrameArray(v->GetClump(),p->m_aPlaneNodes);}
#endif
}
void AircraftBodyAcceptedMelee(CVehicle *v,CEntity *shooter,int weapon,float work,const CVector &point){if(!v||!v->bCanBeDamaged||v->bMeleeProof||(v->GetStatus()!=STATUS_PLAYER&&v->bOnlyDamagedByPlayer&&shooter!=FindPlayerPed()&&shooter!=FindPlayerVehicle()))return;AircraftBodyAcceptedWeapon(v,weapon,work,point);v->m_nLastWeaponDamage=weapon;v->m_pLastDamageEntity=shooter;}

// Indexed candidates, each re-validated exactly as the full scan would; the
// scan remains only as the allocation-failure fallback.
bool AircraftBodyOwnsAtomic(const void *atomic){
 if(IndexOwners()){auto range=ownedAtomics.equal_range(atomic);for(auto it=range.first;it!=range.second;++it){auto found=bodies.find(it->second);if(found==bodies.end())continue;auto &body=*found->second;if(!OwnsLive(body))continue;for(auto &binding:body.bindings)if(binding.atomic==atomic)return true;}return false;}
 for(auto &entry:bodies){auto &body=*entry.second;if(!OwnsLive(body))continue;for(auto &binding:body.bindings)if(binding.atomic==atomic)return true;}return false;}
bool AircraftBodyOwnsClump(const void *clump){
 if(IndexOwners()){auto range=ownedClumps.equal_range(clump);for(auto it=range.first;it!=range.second;++it){auto found=bodies.find(it->second);if(found!=bodies.end()&&found->second->clump==clump&&OwnsLive(*found->second))return true;}return false;}
 for(auto &entry:bodies){auto &body=*entry.second;if(body.clump==clump&&OwnsLive(body))return true;}return false;}

void AircraftBodyAdded(CEntity *entity){
 static bool inside=false;if(inside||!entity||!entity->IsVehicle())return;inside=true;struct Guard{bool &flag;~Guard(){flag=false;}} guard{inside};
 try{auto body=Lease(entity);if(!body||body->captured||!Capture(*body))return;auto candidate=body->model;if(!Publish(*body,candidate))std::fprintf(stderr,"[vehicle-body] admission publication deferred ref=%d\n",body->reference);}
 catch(const std::bad_alloc&){std::fprintf(stderr,"[vehicle-body] admission allocation unavailable; world boundary will retry\n");}
}

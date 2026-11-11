#include "aircraftcontrols.h"
#include "game.h"
#include "log.h"
#include <array>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <map>
#include <vector>
namespace aircraftcontrols {namespace {
struct Tri {uint16_t v[3],material;};
void *(*gEach)(void *,void *(*)(void *,void *),void *),*(*gFrame)(const void *),*(*gGeometry)(const void *),*(*gRoot)(const void *),*(*gParent)(const void *),*(*gMorph)(const void *,int);
const Vec3 *(*gVertices)(const void *);const Tri *(*gTriangles)(const void *);float *(*gMatrix)(void *);const char *(*gName)(void *);
int (*gNV)(const void *),(*gNT)(const void *),(*gType)(const void *);unsigned (*gFlags)(const void *);bool gReady;
using vm::Add;using vm::Sub;using vm::Mul;using vm::Dot;
vm::Vec3 V(Vec3 p){return {p.x,p.y,p.z};}bool Finite(vm::Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
vm::Vec3 Transform(const float *m,vm::Vec3 p){return {m[12]+m[0]*p.x+m[4]*p.y+m[8]*p.z,m[13]+m[1]*p.x+m[5]*p.y+m[9]*p.z,m[14]+m[2]*p.x+m[6]*p.y+m[10]*p.z};}
bool InCar(void *frame,void *root,vm::Vec3 point,vm::Vec3 &out){for(int n=0;frame&&frame!=root&&n<32;n++,frame=gParent(frame)){const auto m=gMatrix(frame);if(!m)return false;point=Transform(m,point);if(!Finite(point))return false;}if(frame!=root)return false;out=point;return true;}
struct Anchor {uint8_t *car=nullptr;int model=-1;void *clump=nullptr,*atomic=nullptr,*geometry=nullptr;Tri triangle{};float radius=.025f;};Anchor gAnchor;
struct Search {uint8_t *car;void *root;vm::Vec3 seat;float best=1e9f;Anchor anchor{};};
void *Find(void *atomic,void *data){
 auto &q=*static_cast<Search *>(data);if(!(gFlags(atomic)&4))return atomic;void *frame=gFrame(atomic);const char *name=frame?gName(frame):nullptr;if(!name||std::strstr(name,"rotor")||std::strstr(name,"_lo")||std::strstr(name,"_vlo"))return atomic;
 void *geometry=gGeometry(atomic);if(!geometry)return atomic;const int nv=gNV(geometry),nt=gNT(geometry);if(nv<3||nv>65535||nt<1||nt>32768)return atomic;
 const auto vertices=gVertices(gMorph(geometry,0));const auto triangles=gTriangles(geometry);if(!vertices||!triangles)return atomic;std::vector<vm::Vec3> points(size_t(nv),vm::Vec3{});for(int i=0;i<nv;i++)if(!InCar(frame,q.root,V(vertices[i]),points[size_t(i)]))return atomic;
 struct Face {int original,parent;};std::vector<Face> selected;std::map<std::array<int,3>,int> welded;
 auto root=[&](int i){while(selected[size_t(i)].parent!=i){selected[size_t(i)].parent=selected[size_t(selected[size_t(i)].parent)].parent;i=selected[size_t(i)].parent;}return i;};
 for(int i=0;i<nt;i++){
  const auto t=triangles[i];bool inside=true;for(auto k:t.v){if(k>=nv){inside=false;break;}const auto p=Sub(points[k],q.seat);inside&=std::fabs(p.x)<.23f&&p.y>.18f&&p.y<.95f&&p.z>.08f&&p.z<.68f;}if(!inside)continue;
  const int at=int(selected.size());selected.push_back({i,at});for(auto k:t.v){const auto p=points[k];const std::array<int,3> key{int(std::lround(p.x*10000)),int(std::lround(p.y*10000)),int(std::lround(p.z*10000))};auto found=welded.find(key);if(found==welded.end())welded[key]=at;else selected[size_t(root(at))].parent=root(found->second);}
 }
 struct Group {std::vector<int> faces;vm::Vec3 lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};};std::map<int,Group> groups;
 for(int i=0;i<int(selected.size());i++){auto &g=groups[root(i)];g.faces.push_back(selected[size_t(i)].original);for(auto k:triangles[selected[size_t(i)].original].v){const auto p=points[k];g.lo={std::min(g.lo.x,p.x),std::min(g.lo.y,p.y),std::min(g.lo.z,p.z)};g.hi={std::max(g.hi.x,p.x),std::max(g.hi.y,p.y),std::max(g.hi.z,p.z)};}}
 for(const auto &entry:groups){const auto &g=entry.second;const auto extent=Sub(g.hi,g.lo);if(g.faces.size()<4||extent.x>.22f||extent.y>.42f||extent.z<.025f)continue;const vm::Vec3 top{(g.lo.x+g.hi.x)*.5f,(g.lo.y+g.hi.y)*.5f,g.hi.z};const auto desired=Add(q.seat,{0,.5f,.37f});const auto delta=Sub(top,desired);const float score=Dot(delta,delta);if(score>=q.best)continue;
  float nearest=1e9f;int face=-1;for(int i:g.faces){const auto t=triangles[i];const auto centre=Mul(Add(Add(points[t.v[0]],points[t.v[1]]),points[t.v[2]]),1.f/3);const auto d=Sub(centre,top);const float distance=Dot(d,d);if(distance<nearest){nearest=distance;face=i;}}
  if(face>=0){q.best=score;q.anchor={q.car,game::EntityModel(q.car),game::EntityClump(q.car),atomic,geometry,triangles[face],std::clamp(extent.x*.5f,.015f,.045f)};}
 }return atomic;
}
struct Resolve {const Anchor &anchor;void *root;bool found=false;vm::Vec3 point{},up{};};
void *ResolveAnchor(void *atomic,void *data){auto &q=*static_cast<Resolve *>(data);if(atomic!=q.anchor.atomic||!(gFlags(atomic)&4)||gGeometry(atomic)!=q.anchor.geometry)return atomic;
 const int n=gNV(q.anchor.geometry);const auto p=gVertices(gMorph(q.anchor.geometry,0));if(!p)return atomic;vm::Vec3 local{};for(auto k:q.anchor.triangle.v){if(k>=n)return atomic;local=Add(local,Mul(V(p[k]),1.f/3));}vm::Vec3 origin,up;
 auto frame=gFrame(atomic);if(!InCar(frame,q.root,local,q.point)||!InCar(frame,q.root,{},origin)||!InCar(frame,q.root,{0,0,1},up))return atomic;q.up=vm::Normal(Sub(up,origin));q.found=true;return atomic;
}
}
bool Grips(uint8_t *car,uint8_t *ped,wheelgrip::Grip &left,wheelgrip::Grip &right){
 if(!gReady||!car||!ped||!game::IsAircraft(car)||!game::PedInVehicle(ped)||game::PedVehicle(ped)!=car||*reinterpret_cast<uint8_t **>(car+0x218)!=ped)return false;void *clump=game::EntityClump(car);if(!clump||gType(clump)!=2)return false;void *root=gRoot(clump);if(!root)return false;
 const auto m=reinterpret_cast<float *>(car+8);
 if(game::EntityModel(car)==181){static auto named=reinterpret_cast<bool(*)(void*,Vec3*,Vec3*,Vec3*)>(dlsym(RTLD_DEFAULT,"VCSourceDodoGrips"));Vec3 a,b,up;if(named&&named(clump,&a,&b,&up)){vm::Vec3 forward{m[4],m[5],m[6]};left=wheelgrip::OnStick(V(a),V(up),forward,.019f,true);right=wheelgrip::OnStick(V(b),V(up),forward,.019f,false);return Finite(left.hand.pos)&&Finite(right.hand.pos);}}
 const auto d=Sub(V(*reinterpret_cast<Vec3 *>(ped+0x38)),V(*reinterpret_cast<Vec3 *>(car+0x38)));const vm::Vec3 seat{Dot(d,{m[0],m[1],m[2]}),Dot(d,{m[4],m[5],m[6]}),Dot(d,{m[8],m[9],m[10]})};if(!Finite(seat))return false;
 Resolve resolved{gAnchor,root};if(gAnchor.car==car&&gAnchor.model==game::EntityModel(car)&&gAnchor.clump==clump)gEach(clump,ResolveAnchor,&resolved);
 if(!resolved.found){Search search{car,root,seat};gEach(clump,Find,&search);gAnchor=search.anchor;if(!gAnchor.atomic)return false;gEach(clump,ResolveAnchor,&resolved);if(!resolved.found)return false;Log("aircraft controls: model %d tracking stick at %.3f %.3f %.3f",gAnchor.model,resolved.point.x,resolved.point.y,resolved.point.z);}
 const auto point=Transform(m,resolved.point),origin=Transform(m,{});const auto up=vm::Normal(Sub(Transform(m,resolved.up),origin)),forward=vm::Normal(vm::Vec3{m[4],m[5],m[6]});
 left=wheelgrip::OnStick(point,up,forward,gAnchor.radius,true);right=wheelgrip::OnStick(point,up,forward,gAnchor.radius,false);return Finite(left.hand.pos)&&Finite(right.hand.pos);
}
bool Install(){if(gReady)return true;
#define B(v,s) v=reinterpret_cast<decltype(v)>(dlsym(RTLD_MAIN_ONLY,s));if(!v)return false
 B(gEach,"_Z20RpClumpForAllAtomicsPN2rw5ClumpEPFPNS_6AtomicES3_PvES4_");B(gFrame,"_Z16RpAtomicGetFramePKN2rw6AtomicE");B(gGeometry,"_Z19RpAtomicGetGeometryPKN2rw6AtomicE");B(gRoot,"_Z15RpClumpGetFramePKN2rw5ClumpE");B(gParent,"_Z16RwFrameGetParentPKN2rw5FrameE");B(gMatrix,"_Z16RwFrameGetMatrixPN2rw5FrameE");B(gMorph,"_Z24RpGeometryGetMorphTargetPKN2rw8GeometryEi");B(gVertices,"_Z24RpMorphTargetGetVerticesPKN2rw11MorphTargetE");B(gTriangles,"_Z22RpGeometryGetTrianglesPKN2rw8GeometryE");B(gName,"_Z16GetFrameNodeNamePN2rw5FrameE");B(gNV,"_Z24RpGeometryGetNumVerticesPKN2rw8GeometryE");B(gNT,"_Z25RpGeometryGetNumTrianglesPKN2rw8GeometryE");B(gFlags,"_Z16RpAtomicGetFlagsPKN2rw6AtomicE");B(gType,"_Z15RwObjectGetTypePKN2rw6ObjectE");
#undef B
 return gReady=true;
}
}

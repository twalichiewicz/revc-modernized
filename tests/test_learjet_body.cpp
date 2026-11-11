// The mod-prepared airport Learjet as the source host captures it: render-
// enabled high-detail atomics, the host's own face classification and the
// generic aircraft kernel. The Dodo (legacy calibration) is the control.
#define DODO_MODEL_NO_MAIN
#include "test_dodomodel.cpp"
#undef DODO_MODEL_NO_MAIN
#include "../src/aircraftbodymodel.h"
#include <map>
#include <string>
namespace learjet {
using namespace aircraftbodymodel;
using BodySurface=aircraftbodymodel::Surface;
Vec3 V(CVector p){return {p.x,p.y,p.z};}
Vec3 Add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 Sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 Mul(Vec3 a,float s){return {a.x*s,a.y*s,a.z*s};}
Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float Length(Vec3 a){return std::sqrt(Dot(a,a));}
Vec3 Centre(const BodySurface &s){return Mul(Add(Add(s.points[0],s.points[1]),s.points[2]),1.f/3);}
float SurfaceArea(const BodySurface &s){return .5f*Length(Cross(Sub(s.points[1],s.points[0]),Sub(s.points[2],s.points[0])));}
struct Captured {std::vector<BodySurface> surfaces;std::vector<std::string> names;Definition definition;size_t renderOnly=0;};
// AircraftBody.cpp CaptureAtomic admission and Body::Initialize classification.
Captured Capture(Clump &clump,int model,Kind kind){
 Captured out;out.names.resize(clump.atomics.size());Vec3 lo{1e10f,1e10f,1e10f},hi{-1e10f,-1e10f,-1e10f};std::array<Vec3,4> wheelSum{};std::array<int,4> wheelCount{};
 for(size_t group=0;group<clump.atomics.size();++group){auto *a=clump.atomics[group];const char *name=a->frame->name;out.names[group]=name;
  if(!(a->flags&4)||!a->geometry)continue;if(std::strstr(name,"_vlo")||std::strstr(name,"_lo"))continue;
  if(RenderOnlyAtomic(name)){out.renderOnly+=a->geometry->triangles.size();continue;}
  for(const auto &t:a->geometry->triangles){BodySurface s;s.group=uint32_t(group);for(int k=0;k<3;++k){s.points[k]=V(ModelPoint(a->frame,a->geometry->vertices[t.v[k]]));lo={std::min(lo.x,s.points[k].x),std::min(lo.y,s.points[k].y),std::min(lo.z,s.points[k].z)};hi={std::max(hi.x,s.points[k].x),std::max(hi.y,s.points[k].y),std::max(hi.z,s.points[k].z)};}
   const auto c=Centre(s);s.role=ClassifyFace(name,c,kind,model);const auto *m=a->geometry->materials[t.material];
   const auto face=ClassifyMaterial(s.role,kind,name,c,Cross(Sub(s.points[1],s.points[0]),Sub(s.points[2],s.points[0])),m->color.a<255,m->color.a,m->texture?m->texture->name.c_str():nullptr);s.glass=face.glass;s.laminated=face.laminated;s.structural=face.structural;
   if(s.role==Role::Wheel){const int w=(c.x<0?0:2)+(c.y<0?1:0);wheelSum[w]=Add(wheelSum[w],c);++wheelCount[w];}out.surfaces.push_back(s);}
 }
 auto &d=out.definition;d.kind=kind;d.model=model;d.bounds={lo,hi};d.legacyDodo=model==181;d.armor=1;d.hasWheels=true;
 for(int i=0;i<4;++i)d.wheels[i]=wheelCount[i]?Mul(wheelSum[i],1.f/wheelCount[i]):Vec3{(i<2?lo.x:hi.x)*.8f,(i%2?lo.y:hi.y)*.65f,lo.z};
 if(wheelCount[0]==0&&wheelCount[2]>0)d.wheels[0]=d.wheels[2];
 return out;
}
float RoleArea(const Captured &c,Role role,float minAbsX){float area=0;for(const auto &s:c.surfaces)if(s.role==role&&s.structural&&std::fabs(Centre(s).x)>minAbsX)area+=SurfaceArea(s);return area;}
Vec3 BoxCentre(const mechanicalmodel::Box &b){return Mul(Add(b.lo,b.hi),.5f);}
// Largest |displacement| of the given role's rest vertices.
float RoleMotion(const Model &m,const Captured &c,Role role){float worst=0;for(const auto &s:c.surfaces)if(s.role==role)for(auto p:s.points)worst=std::max(worst,Length(Sub(m.Deform(p,s.group),p)));return worst;}
}

int main(){
 using namespace learjet;
 CHECK(airportjetmodel::Install());
 Clump donor=LoadCockpit();auto *source=Load();if(!source)return 1;
 Clump jet{static_cast<Frame *>(FrameCreate()),{}};std::strcpy(jet.root->name,"ap_learjet1_01");auto *a=static_cast<Atomic *>(AtomicCreate());a->geometry=source;a->frame=jet.root;jet.atomics.push_back(a);
 CHECK(airportjetmodel::Prepare(&jet,&donor));
 // The prepared jet shares the Dodo handling (2500 kg) under its own model slot.
 constexpr int kJetModel=6500;constexpr float kJetMass=2500;
 auto L=Capture(jet,kJetModel,Kind::Plane);
 std::printf("  learjet admitted %zu faces, %zu render-only\n",L.surfaces.size(),L.renderOnly);
 for(const char *name:{"jet_cabin","jet_cabin_rear","jet_dashboard","jet_lining","jet_window_trim","jet_steps"})CHECK(RenderOnlyAtomic(name));
 CHECK(!RenderOnlyAtomic("jet_hull")&&!RenderOnlyAtomic("dodo_cabin")&&!RenderOnlyAtomic("chassis_hi"));
 Model jetModel;const bool admitted=jetModel.Initialize(L.surfaces,kJetMass,L.definition);CHECK(admitted);
 // jet_hull carries the main wing; flaps alone are a small fraction of it.
 const float left=RoleArea(L,Role::WingLeft,1.2f),right=RoleArea(L,Role::WingRight,1.2f),fuselageOutboard=RoleArea(L,Role::Body,2.f);
 std::printf("  learjet wing area L/R %.1f/%.1f m2, outboard fuselage-role area %.1f m2\n",left,right,fuselageOutboard);
 CHECK(left>30&&right>30&&std::fabs(left-right)<.1f*left&&fuselageOutboard<2);
 if(admitted){
  const auto &layout=jetModel.Layout();
  // Twin engines are the rear fuselage pods, not the Dodo-style nose.
  const auto engine=layout.airframe[airframemodel::EngineL];const auto e=BoxCentre(engine);
  std::printf("  learjet engine box (%.2f %.2f %.2f)-(%.2f %.2f %.2f)\n",engine.lo.x,engine.lo.y,engine.lo.z,engine.hi.x,engine.hi.y,engine.hi.z);
  CHECK(layout.airframeEnabled[airframemodel::EngineL]&&e.y<-2.5f&&e.y>-6&&engine.lo.x<-1.4f&&engine.hi.x>1.4f&&engine.hi.y<-2.5f);
  // Wing tanks sit in the inboard main wing, mirrored left/right.
  const auto fl=BoxCentre(layout.airframe[airframemodel::FuelL]),fr=BoxCentre(layout.airframe[airframemodel::FuelR]);
  std::printf("  learjet fuel L (%.2f %.2f %.2f) R (%.2f %.2f %.2f)\n",fl.x,fl.y,fl.z,fr.x,fr.y,fr.z);
  CHECK(fl.x<-1.5f&&fl.x>-6&&fr.x>1.5f&&fr.x<6&&std::fabs(fl.x+fr.x)<.3f&&fl.y>-4.5f&&fl.y<.6f&&fr.y>-4.5f&&fr.y<.6f&&fl.z<-.3f&&fr.z<-.3f);
  // Parked self-weight (native 20 m/s2): an elastic droop, no plastic collapse,
  // and nothing left once the load is removed.
  Model parked=jetModel;for(int i=0;i<180;++i)parked.Tick(1.f/60,false,{0,0,-20});
  const float sag=std::max(RoleMotion(parked,L,Role::WingLeft),RoleMotion(parked,L,Role::WingRight));Model unloaded=parked;for(int i=0;i<180;++i)unloaded.Tick(1.f/60,false,{0,0,0});
  const float residual=std::max(RoleMotion(unloaded,L,Role::WingLeft),RoleMotion(unloaded,L,Role::WingRight));std::printf("  learjet parked wing droop %.4f m, residual after unloading %.4f m\n",sag,residual);CHECK(sag<.15f&&residual<.01f);
  // An 80 kJ leading-edge strike on the outboard right wing bends it.
  const BodySurface *edge=nullptr;float best=-1e9f;for(const auto &s:L.surfaces){if(s.role!=Role::WingRight||!s.structural)continue;const auto c=Centre(s);if(c.x<5||c.x>8)continue;const float score=c.y;if(score>best){best=score;edge=&s;}}
  CHECK(edge);if(edge){Model struck=jetModel;const auto c=Centre(*edge);const auto r=struck.ContactEnergy(c,{0,-1,0},80000,1000,1.f/60);for(int i=0;i<60;++i)struck.Tick(1.f/60,false,{0,0,-20});
   const float bend=RoleMotion(struck,L,Role::WingRight),other=RoleMotion(struck,L,Role::WingLeft);std::printf("  learjet 80kJ leading edge at (%.2f %.2f %.2f): valid=%d right wing %.3f m, left wing %.3f m\n",c.x,c.y,c.z,r.valid,bend,other);
   CHECK(r.valid&&bend>.1f&&bend<4&&other<sag+.01f);}
 }
 // The legacy-calibrated Dodo keeps its admission and classification.
 Clump dodo=LoadDodo();Clump cockpit=LoadCockpit();CHECK(airportjetmodel::Prepare(&dodo,&cockpit));auto D=Capture(dodo,181,Kind::Plane);Model dodoModel;
 CHECK(D.renderOnly==0&&D.surfaces.size()==1329&&dodoModel.Initialize(D.surfaces,2500,D.definition));
 std::printf("learjet body: failures=%d\n",failures);return failures?1:0;
}

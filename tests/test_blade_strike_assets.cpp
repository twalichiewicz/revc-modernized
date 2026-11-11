// Actual Maverick DFF -> source-host material admission rules -> shared blade
// strike planner (src/aircraftbodybridge.h) -> canonical material kernel. A
// native rotor-disc overlap must land on real blade material and break it.
#define VEHICLE_BODY_ASSETS_NO_MAIN
#include "test_vehicle_body_assets.cpp"
#include "../src/aircraftbodybridge.h"
#include <chrono>
namespace bladeasset {
using aircraftbodymodel::Kind;using aircraftbodymodel::Definition;using aircraftbodymodel::Model;using aircraftbodymodel::Role;using BodySurface=aircraftbodymodel::Surface;using namespace aircraftbodyblade;
// tools/host/AircraftBody.cpp: blur discs never become material (CaptureAtomic),
// bonnet/boot helicopter parts are the main/tail rotor (Body::Initialize).
std::vector<BodySurface> HostSurfaces(Clump &clump,int model,Definition &d,uint32_t &rotor,Vec3 &hub){
 std::vector<BodySurface> out;d={};d.kind=Kind::Helicopter;d.model=model;d.bounds={{1e9f,1e9f,1e9f},{-1e9f,-1e9f,-1e9f}};
 for(size_t g=0;g<clump.atomics.size();++g){auto *a=clump.atomics[g];const char *n=a->frame->name;if(!vehicleasset::Visible(*a)||!a->geometry)continue;
  if(model!=165&&(!strcmp(n,"toprotor")||!strcmp(n,"rearrotor")||!strcmp(n,"backrotor")||!strcmp(n,"windscreen_dummy")||!strcmp(n,"bump_rear_dummy")))continue;
  if(strstr(n,"bonnet")){rotor=uint32_t(g);const auto p=ModelPoint(a->frame,{0,0,0});hub={p.x,p.y,p.z};}
  for(const auto &t:a->geometry->triangles){BodySurface s;s.group=uint32_t(g);for(int k=0;k<3;++k){s.points[k]=vehicleasset::V(ModelPoint(a->frame,a->geometry->vertices[t.v[k]]));vehicleasset::Grow(d.bounds,s.points[k]);}
   s.role=strstr(n,"wheel")||strstr(n,"tire")?Role::Wheel:strstr(n,"door")?Role::Door:strstr(n,"bonnet")?Role::MainRotor:strstr(n,"boot")?Role::TailRotor:Role::Body;
   s.glass=s.role!=Role::MainRotor&&s.role!=Role::TailRotor&&a->geometry->materials[t.material]->color.a<255;s.structural=!s.glass&&s.role!=Role::Wheel&&s.role!=Role::Door;out.push_back(s);}
 }
 for(int i=0;i<4;++i)d.wheels[i]=Vec3{(i<2?d.bounds.lo.x:d.bounds.hi.x)*.8f,(i%2?d.bounds.lo.y:d.bounds.hi.y)*.65f,d.bounds.lo.z};return out;
}
bool MainRotor(const BodySurface &s){return s.role==Role::MainRotor;}
std::array<float,12> Spin(float a,Vec3 hub){const float c=std::cos(a),s=std::sin(a);const Vec3 h{c*hub.x-s*hub.y,s*hub.x+c*hub.y,hub.z};return {c,s,0,-s,c,0,0,0,1,hub.x-h.x,hub.y-h.y,hub.z-h.z};}
float RotorArea(const Model &m){float a=0;for(const auto &f:m.Fragments())if(f.attached&&MainRotor(m.Surfaces()[f.source]))for(size_t i=1;i+1<f.polygon.size();++i)a+=.5f*Length(Cross(Sub(f.polygon[i].position,f.polygon[0].position),Sub(f.polygon[i+1].position,f.polygon[0].position)));return a;}
struct Outcome {bool hit=false,valid=false,material=false,root=false;float work=0,released=0,rotor=1,ms=0;};
// A flat wall `ahead` metres in front of the hub, sampled exactly like the host:
// 48 rays from the drawn hub to the attached blade tip plane.
Outcome Strike(const Model &base,uint32_t group,Vec3 hub,float rotorAngle,float ahead,float spin){
 Outcome o;Model m=base;if(!m.SetGroupPose(group,Spin(rotorAngle,hub)))return o;
 const Vec3 axis{0,0,1},e1{std::cos(rotorAngle),std::sin(rotorAngle),0},e2{-std::sin(rotorAngle),std::cos(rotorAngle),0};
 float outer,inertia,axial;const auto samples=Samples(m,hub,axis,e1,e2,MainRotor,outer,inertia,axial);
 float best=1;int bestK=-1;for(int k=0;k<48;++k){const float a=k*6.28318531f/48;const Vec3 dir=Add(Mul(e1,std::cos(a)),Mul(e2,std::sin(a)));if(dir.y<=1e-6f)continue;const float t=ahead/(dir.y*outer);if(t<=1&&t<best){best=t;bestK=k;}}
 if(bestK<0)return o;o.hit=true;
 const auto start=std::chrono::steady_clock::now();
 const auto plan=Plan(m,samples,axis,outer,inertia,bestK*6.28318531f/48,best*outer,spin,{0,1,0},1000,0,MainRotor);
 o.ms=std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-start).count();
 o.valid=plan.valid;o.root=plan.root;o.work=plan.work;if(!plan.valid)return o;
 const float before=RotorArea(m);const auto r=m.ContactEnergy(plan.point,plan.inward,plan.work,1000,1.f/60);std::vector<aircraftconnectivity::Fragment> out;m.TransferDetached(out);
 for(const auto &f:out)if(MainRotor(m.Surfaces()[f.source]))for(size_t i=1;i+1<f.polygon.size();++i)o.released+=.5f*Length(Cross(Sub(f.polygon[i].position,f.polygon[0].position),Sub(f.polygon[i+1].position,f.polygon[0].position)));
 o.material=r.valid&&(RotorArea(m)<before-1e-4f||m.Effects().rotor<1);o.rotor=m.Effects().rotor;return o;
}
}
int main(){
 using namespace bladeasset;const char *env=std::getenv("GAMEDATA");const std::string data=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";
 vehicleasset::Catalog cat{217,"maverick","heli",Kind::Helicopter};auto clump=vehicleasset::Read(data,cat);Definition d;uint32_t group=~0u;Vec3 hub{};auto surfaces=HostSurfaces(clump,217,d,group,hub);
 Model base;CHECK(group!=~0u&&base.Initialize(surfaces,5000,d));if(failures){Destroy(clump);return 1;}
 float outer,inertia,axial;Samples(base,hub,{0,0,1},{1,0,0},{0,1,0},MainRotor,outer,inertia,axial);
 std::printf("Maverick attached blade radius %.3fm (native disc 6.15m), sheet rotor inertia %.1f kg m^2\n",outer,inertia);
 CHECK(outer>5&&outer<5.3f);CHECK(inertia>10);
 // Native overlap at the real tip region: every rotor angle reaches blade material.
 int hits=0,material=0,severed=0,fall=0;float worst=0,released=1e9f;
 for(int deg=0;deg<180;++deg){const auto o=Strike(base,group,hub,deg*3.14159265f/180,4.6f,.22f*50);hits+=o.hit;material+=o.material;severed+=o.root&&o.released>.5f;fall+=o.rotor<.78f;worst=std::max(worst,o.ms);if(o.root)released=std::min(released,o.released);}
 std::printf("building clip 0.6m inside the disc at full spin: %d/180 angles hit blade material, %d shed a blade (min released %.2fm2), %d leave too little rotor to hover; plan max %.2fms\n",material,severed,released,fall,worst);
 CHECK(hits==180&&material==180);CHECK(severed==180);CHECK(fall==180);
 // A slow, winding-down rotor touching the same wall chips the struck station only.
 int chipped=0,shed=0,flying=0;for(int deg=0;deg<180;deg+=15){const auto o=Strike(base,group,hub,deg*3.14159265f/180,4.6f,.03f*50);chipped+=o.material;shed+=o.released>0;flying+=o.rotor>.8f;}
 std::printf("slow rotor contact: %d/12 local material damage, %d shed, %d still flyable\n",chipped,shed,flying);
 CHECK(chipped==12&&shed==0&&flying==12);
 Destroy(clump);if(!failures)std::puts("blade strike actual Maverick: all passed");return failures?1:0;
}

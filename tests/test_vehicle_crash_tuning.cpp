// Crash and kerb damage calibration on the installed vehicle catalog: actual
// DFF geometry, handling.cfg masses and wheel templates instanced at the wheel
// dummies, as the native model loader does for a resident actor.
#define VEHICLE_BODY_ASSETS_NO_MAIN
#include "test_vehicle_body_assets.cpp"
#undef VEHICLE_BODY_ASSETS_NO_MAIN
#include "BodyContact.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace crashtuning {
using namespace vehicleasset;
using namespace aircraftbodymodel;
std::string Data(){const char *env=std::getenv("GAMEDATA");return env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";}
std::vector<std::string> Words(std::string line){std::replace(line.begin(),line.end(),',',' ');std::istringstream in(line);std::vector<std::string> out;for(std::string w;in>>w;)out.push_back(w);return out;}
std::map<std::string,float> Masses(){
 std::map<std::string,float> out;std::ifstream in(Data()+"/data/handling.cfg");
 for(std::string line;std::getline(in,line);){if(line.empty()||line[0]==';'||line[0]=='%'||line[0]=='!'||line[0]=='$')continue;auto w=Words(line);if(w.size()<30)continue;char *end=nullptr;const float mass=std::strtof(w[1].c_str(),&end);if(end&&*end==0&&mass>0)out[w[0]]=mass;}
 return out;
}
struct Ide {std::string handling;float wheelScale=.7f;};
std::map<int,Ide> Ides(){
 std::map<int,Ide> out;std::ifstream in(Data()+"/data/default.ide");bool cars=false;
 for(std::string line;std::getline(in,line);){if(!line.empty()&&line.back()=='\r')line.pop_back();if(line=="cars"){cars=true;continue;}if(!cars)continue;if(line=="end")break;auto w=Words(line);if(w.size()<6)continue;char *a=nullptr,*b=nullptr;const long id=std::strtol(w[0].c_str(),&a,10);const float scale=std::strtof(w.back().c_str(),&b);if(a&&*a==0&&b&&*b==0)out[int(id)]={w[4],scale};}
 return out;
}
struct Car {Catalog catalog;Definition definition;std::vector<BodySurface> surfaces;std::vector<std::string> groups;std::array<uint32_t,4> wheel{};float mass=0,radius=.35f;};
// Wheel templates: tread and both sidewalls at each dummy, Role::Wheel.
void AddWheels(Car &car){
 const float r=car.radius,hw=.12f;const int n=12;
 for(int w=0;w<4;++w){const Vec3 c=car.definition.wheels[w];const uint32_t group=uint32_t(car.groups.size());car.wheel[w]=group;car.groups.push_back(std::string("wheel_")+"LFLRRFRR"[w*2]+"LFLRRFRR"[w*2+1]);
  for(int i=0;i<n;++i){const float a0=6.2831853f*i/n,a1=6.2831853f*(i+1)/n;const Vec3 p00{c.x-hw,c.y+r*std::cos(a0),c.z+r*std::sin(a0)},p01{c.x-hw,c.y+r*std::cos(a1),c.z+r*std::sin(a1)},p10{c.x+hw,c.y+r*std::cos(a0),c.z+r*std::sin(a0)},p11{c.x+hw,c.y+r*std::cos(a1),c.z+r*std::sin(a1)};
   auto add=[&](Vec3 a,Vec3 b,Vec3 d){BodySurface s;s.group=group;s.role=Role::Wheel;s.glass=false;s.structural=false;s.points={a,b,d};car.surfaces.push_back(s);};
   add(p00,p01,p11);add(p00,p11,p10);add({c.x-hw,c.y,c.z},p01,p00);add({c.x+hw,c.y,c.z},p10,p11);}
 }
}
bool Load(int id,Car &car){
 static const auto catalogs=Catalogs(Data());static const auto masses=Masses();static const auto ides=Ides();
 bool found=false;for(const auto &c:catalogs)if(c.id==id){car.catalog=c;found=true;}
 auto ide=ides.find(id);if(!found||ide==ides.end())return false;auto mass=masses.find(ide->second.handling);if(mass==masses.end())return false;
 auto clump=Read(Data(),car.catalog);car.definition.kind=car.catalog.kind;car.definition.model=id;car.definition.armor=(id==157||id==158||id==162)?8.f:1.f;size_t all=0;car.surfaces=Surfaces(clump,car.definition,all,nullptr,car.catalog);
 for(auto a:clump.atomics)car.groups.push_back(a->frame->name);Destroy(clump);
 car.mass=mass->second;car.radius=ide->second.wheelScale*.5f;car.definition.hasWheels=car.catalog.kind==Kind::Automobile;
 if(car.definition.hasWheels)AddWheels(car);
 car.definition.bounds={{1e9f,1e9f,1e9f},{-1e9f,-1e9f,-1e9f}};for(const auto &s:car.surfaces)for(auto p:s.points)Grow(car.definition.bounds,p);
 return true;
}
// Native contact witnesses are committed material vertices, never tyres.
Vec3 Snap(const Car &car,Vec3 target){float best=1e30f;Vec3 out=target;for(const auto &s:car.surfaces)if(s.role!=Role::Wheel&&!s.glass)for(auto p:s.points){const float d=DotV(SubV(p,target),SubV(p,target));if(d<best){best=d;out=p;}}return out;}
float GroupArea(const Model &m,uint32_t group){return Area(m.Fragments(),group);}
struct Pose {std::vector<Vec3> points;};
Pose Sample(const Model &m){Pose out;for(const auto &s:m.Surfaces())for(auto p:s.points)out.points.push_back(m.Deform(p,s.group));return out;}
float MaxMove(const Pose &a,const Pose &b){float best=0;for(size_t i=0;i<a.points.size()&&i<b.points.size();++i){const auto d=SubV(a.points[i],b.points[i]);best=std::max(best,std::sqrt(DotV(d,d)));}return best;}
float MaxDisplacement(const Model &m){float best=0;for(const auto &s:m.Surfaces())for(auto p:s.points){const auto d=SubV(m.Deform(p,s.group),p);best=std::max(best,std::sqrt(DotV(d,d)));}return best;}
void Hits(Model &m,Vec3 point,Vec3 inward,float joules,int count,uint32_t &now){for(int k=0;k<count;++k){m.ContactEnergy(point,inward,joules,now,1.f/30);now+=400;for(int t=0;t<3;++t)m.Tick(1.f/30,true,{0,0,-20});}}
void PrintDoses(const char *label,const Model &m){std::printf("  %s:",label);const auto &d=m.Mechanical().damage;for(int i=0;i<mechanicalmodel::kModules;++i)if(d[i]>.005f)std::printf(" %s=%.2f",mechanicalmodel::Name(i),d[i]);std::printf("\n");}
float Median(std::vector<float> v){std::sort(v.begin(),v.end());return v.empty()?0:v[v.size()/2];}
}

#ifndef CRASH_TUNING_NO_MAIN
int main(){
 using namespace crashtuning;
 // Accepted-impulse policy: native GTA bodies have no tyre or suspension
 // compliance, so a bottomed or kerbed chassis receives a rigid impulse.
 {
  const float meff=500,closing=6,outward=.3f,impulse=meff*(closing+outward);
  CHECK(std::fabs(bodycontact::EffectiveMass(impulse,closing,outward)-meff)<1e-2f);
  const float raw=.5f*impulse*(closing-outward);
  CHECK(bodycontact::ChassisWork(impulse,closing,outward,false)==raw);
  CHECK(bodycontact::ChassisWork(impulse,closing,outward,true)==0);
  const float hard=meff*(14+.7f),severe=bodycontact::ChassisWork(hard,14,.7f,true),rigid=bodycontact::ChassisWork(hard,14,.7f,false);
  CHECK(severe>.6f*rigid&&severe<rigid);
  CHECK(bodycontact::ChassisWork(meff*(4.6f+4.5f),4.6f,4.5f,false)==0);// below the mass-scaled floor
  const Vec3 wheels[4]{{-.85f,1.38f,-.65f},{-.85f,-1.38f,-.65f},{.85f,1.38f,-.65f},{.85f,-1.38f,-.65f}};
  CHECK(bodycontact::Underbody({0,0,-.7f},{0,0,1},-.65f,.4f));
  CHECK(!bodycontact::Underbody({0,2.2f,-.5f},{0,-1,0},-.65f,.4f));
  CHECK(!bodycontact::Underbody({0,0,.4f},{0,0,1},-.65f,.4f));
  CHECK(bodycontact::Tyre({-.95f,1.5f,-.8f},wheels,4,.4f));
  CHECK(!bodycontact::Tyre({0,0,-.7f},wheels,4,.4f));
  bodycontact::Pass pass;int a=0,b=0;pass.Begin(&a,&b,7);CHECK(pass.Matches(&a,&b,7)&&!pass.Matches(&b,&a,7)&&!pass.Matches(&a,&b,8));
  CHECK(pass.Record(0,1000,2)==1&&pass.Record(1,1000,3)==2&&pass.Record(2,1000,3)==2&&pass.Record(3,1000,4)==3);
 }
 // Kerb-strength bumper hits must not kill a robust SUV engine.
 {
  Car car;CHECK(Load(130,car));Model m;CHECK(m.Initialize(car.surfaces,car.mass,car.definition));const auto b=car.definition.bounds;uint32_t now=1000;
  Hits(m,Snap(car,{0,b.hi.y,b.lo.z+.15f}),{0,-1,0},8000,3,now);PrintDoses("landstal 3x8kJ bumper",m);
  CHECK(m.Mechanical().damage[mechanicalmodel::kEngine]<.3f);
 }
 // Repeated moderate nose hits cannot fold a body half, and a later small
 // contact cannot re-aim accumulated plastic deformation.
 {
  Car car;CHECK(Load(159,car));Model m;CHECK(m.Initialize(car.surfaces,car.mass,car.definition));uint32_t now=1000;
  Hits(m,Snap(car,{.25f,2.86f,-.44f}),{0,-1,0},8000,2,now);
  std::printf("  banshee 2x8kJ: bend=%.3f rad max displacement=%.3f m\n",m.BendPlastic(0),MaxDisplacement(m));
  CHECK(m.BendPlastic(0)<.1f);CHECK(MaxDisplacement(m)<.15f);
  const auto before=Sample(m);m.ContactEnergy(Snap(car,{-.5f,1.5f,-.5f}),{0,.5f,-.8660254f},100,now,1.f/30);m.Tick(1.f/30,true,{0,0,-20});
  const float moved=MaxMove(before,Sample(m));std::printf("  banshee later 100J contact moves a vertex %.4f m\n",moved);CHECK(moved<.02f);
 }
 // A wing hit beside the wheel arch cannot release a wheel whose hub still
 // has its mounts.
 {
  Car car;CHECK(Load(130,car));Model m;CHECK(m.Initialize(car.surfaces,car.mass,car.definition));uint32_t now=1000;const float area=GroupArea(m,car.wheel[0]);
  Hits(m,Snap(car,{-1.f,.94f,-.42f}),{1,0,0},9000,1,now);std::printf("  landstal 9kJ wing: LF wheel area %.3f -> %.3f\n",area,GroupArea(m,car.wheel[0]));
  CHECK(GroupArea(m,car.wheel[0])>.95f*area);
 }
 // The same per-kilogram hit on 1-2 t cars spends comparable component work.
 {
  struct Site {const char *name;int index;};std::map<int,std::vector<float>> doses[2];std::vector<std::string> names;int cars=0,lost=0;
  for(const auto &c:Catalogs(Data())){if(c.kind!=Kind::Automobile)continue;Car car;if(!Load(c.id,car)||car.mass<1000||car.mass>2000)continue;++cars;
   for(int site=0;site<2;++site){Model m;CHECK(m.Initialize(car.surfaces,car.mass,car.definition));const auto b=car.definition.bounds;const auto wheel=car.definition.wheels[0];uint32_t now=1000;std::array<float,4> areas{};for(int w=0;w<4;++w)areas[w]=GroupArea(m,car.wheel[w]);
    if(site==0)Hits(m,Snap(car,{0,b.hi.y,b.lo.z+.15f}),{0,-1,0},6*car.mass,1,now);else Hits(m,Snap(car,{b.lo.x,wheel.y,wheel.z+car.radius*.9f}),{1,0,0},6*car.mass,1,now);
    for(int i=0;i<mechanicalmodel::kModules;++i)doses[site][i].push_back(m.Mechanical().damage[i]);
    for(int w=0;w<4;++w)if(GroupArea(m,car.wheel[w])<.9f*areas[w]){++lost;std::printf("  wheel %d lost: model %d %s site %d\n",w,c.id,c.name.c_str(),site);}
   }
  }
  CHECK(cars>=30);CHECK(lost==0);
  for(int site=0;site<2;++site)for(int i=0;i<mechanicalmodel::kModules;++i){const auto &v=doses[site][i];const float median=Median(v);float worst=0;for(float d:v)worst=std::max(worst,std::fabs(d-median));if(worst>.2f){std::printf("  spread site %d %s: median %.2f worst deviation %.2f\n",site,mechanicalmodel::Name(i),median,worst);++failures;}}
  std::printf("  roster: %d cars of 1-2 t\n",cars);
 }
 // A genuinely severe head-on still wrecks the front end and engine.
 for(int id:{130,159,150}){
  Car car;CHECK(Load(id,car));Model m;CHECK(m.Initialize(car.surfaces,car.mass,car.definition));const auto b=car.definition.bounds;uint32_t now=1000;float area=0;for(uint32_t g=0;g<car.groups.size();++g)area+=GroupArea(m,g);
  const auto r=m.ContactEnergy(Snap(car,{0,b.hi.y,b.lo.z+.15f}),{0,-1,0},.5f*car.mass*30*30,now,1.f/30);m.Tick(1.f/30,true,{0,0,-20});PrintDoses(car.catalog.name.c_str(),m);
  const auto &d=m.Mechanical().damage;float after=0;for(uint32_t g=0;g<car.groups.size();++g)after+=GroupArea(m,g);std::printf("  severe %s: material lost %.2f m2, displacement %.2f m\n",car.catalog.name.c_str(),area-after,MaxDisplacement(m));CHECK(r.valid&&r.holes>0&&area-after>.05f);CHECK(d[mechanicalmodel::kEngine]>.9f);CHECK(mechanicalmodel::Power(m.Mechanical())<.2f);
 }
 std::printf("vehicle crash tuning: failures=%d\n",failures);return failures?1:0;
}
#endif

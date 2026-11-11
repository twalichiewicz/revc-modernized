#include "aircraftbodymodel.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <new>

namespace aircraftbodymodel {namespace {
Vec3 Add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 Sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 Mul(Vec3 a,float b){return {a.x*b,a.y*b,a.z*b};}
float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float Length(Vec3 p){return std::sqrt(Dot(p,p));}
bool Finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
Vec3 Transform(const std::array<float,12>&m,Vec3 p){return {m[0]*p.x+m[3]*p.y+m[6]*p.z+m[9],m[1]*p.x+m[4]*p.y+m[7]*p.z+m[10],m[2]*p.x+m[5]*p.y+m[8]*p.z+m[11]};}
float Determinant(const std::array<float,12>&m){return Dot({m[0],m[1],m[2]},Cross({m[3],m[4],m[5]},{m[6],m[7],m[8]}));}
Vec3 InverseDirection(const std::array<float,12>&m,Vec3 p){const Vec3 r{m[0],m[1],m[2]},f{m[3],m[4],m[5]},u{m[6],m[7],m[8]};const float det=Determinant(m);return {Dot(Cross(f,u),p)/det,Dot(Cross(u,r),p)/det,Dot(Cross(r,f),p)/det};}
bool Control(int i){return i==airframemodel::Elevator||i==airframemodel::Rudder||i==airframemodel::AileronL||i==airframemodel::AileronR;}
bool InBox(Vec3 p,const mechanicalmodel::Box&b){return p.x>=b.lo.x&&p.x<=b.hi.x&&p.y>=b.lo.y&&p.y<=b.hi.y&&p.z>=b.lo.z&&p.z<=b.hi.z;}
bool ControlSurface(const Surface&s,int module){return module==airframemodel::Elevator?s.role==Role::Elevator:module==airframemodel::Rudder?s.role==Role::Rudder:module==airframemodel::AileronL?s.role==Role::WingLeft:(module==airframemodel::AileronR&&s.role==Role::WingRight)||(module==airframemodel::MainRotor&&s.role==Role::MainRotor)||(module==airframemodel::TailRotor&&s.role==Role::TailRotor);}
float Clamp(float v){return std::clamp(v,0.f,1.f);}
Vec3 Centre(const Surface&s){return Mul(Add(Add(s.points[0],s.points[1]),s.points[2]),1.f/3);}
float Area(const std::vector<skinmodel::Sample>&p,bool horizontal){Vec3 normal{};for(size_t i=1;i+1<p.size();++i)normal=Add(normal,Cross(Sub(p[i].position,p[0].position),Sub(p[i+1].position,p[0].position)));return .5f*(horizontal?std::fabs(normal.z):Length(normal));}
using Boxes=std::array<mechanicalmodel::Box,airframemodel::Count>;
Boxes Components(){
 using namespace airframemodel;Boxes b;for(auto&box:b)box={{-5.5f,-6.8f,-1.6f},{5.5f,3.5f,2}};
 // Measured Dodo model coordinates: cockpit ends at Y1.24; the nose engine is
 // forward of its firewall. Wet wing cells follow the actual elevated wing,
 // rather than fractions of the narrower, older COL envelope.
 b[EngineL]=b[EngineR]={{-.48f,1.65f,-.35f},{.48f,3.05f,.55f}};
 b[FuelL]={{-2.50f,-.08f,.965f},{-.82f,.78f,1.025f}};
 b[FuelR]={{.82f,-.08f,.965f},{2.50f,.78f,1.025f}};
 b[Electrical]={{-.40f,.73f,.20f},{.40f,1.10f,.61f}};
 b[Hydraulics]={{-.45f,-.45f,-.32f},{.45f,.65f,-.12f}};
 b[Elevator]={{-2.05f,-5.98f,-.11f},{2.05f,-4.65f,.05f}};
 b[Rudder]={{-.055f,-6.64f,.01f},{.055f,-5.38f,1.89f}};
 b[AileronL]={{-4.85f,-.78f,.91f},{-2.62f,-.04f,1.10f}};
 b[AileronR]={{2.62f,-.78f,.91f},{4.85f,-.04f,1.10f}};return b;
}
bool MechanicalKind(Kind kind){return kind==Kind::Automobile||kind==Kind::Bike||kind==Kind::Boat;}
mechanicalmodel::Box SurfaceBounds(const std::vector<Surface>&surfaces,Role role,bool &found){
 mechanicalmodel::Box b{{1e9f,1e9f,1e9f},{-1e9f,-1e9f,-1e9f}};found=false;
 for(const auto&s:surfaces)if(s.role==role&&!s.glass){found=true;for(auto p:s.points){b.lo={std::min(b.lo.x,p.x),std::min(b.lo.y,p.y),std::min(b.lo.z,p.z)};b.hi={std::max(b.hi.x,p.x),std::max(b.hi.y,p.y),std::max(b.hi.z,p.z)};}}return b;
}
mechanicalmodel::Box FractionBox(const mechanicalmodel::Box&b,Vec3 lo,Vec3 hi){const auto d=Sub(b.hi,b.lo);return {{b.lo.x+d.x*lo.x,b.lo.y+d.y*lo.y,b.lo.z+d.z*lo.z},{b.lo.x+d.x*hi.x,b.lo.y+d.y*hi.y,b.lo.z+d.z*hi.z}};}
mechanicalmodel::Box FitBox(mechanicalmodel::Box b,const mechanicalmodel::Box&bounds){
 float *lo[]={&b.lo.x,&b.lo.y,&b.lo.z},*hi[]={&b.hi.x,&b.hi.y,&b.hi.z};const float a[]={bounds.lo.x,bounds.lo.y,bounds.lo.z},z[]={bounds.hi.x,bounds.hi.y,bounds.hi.z};
 for(int axis=0;axis<3;++axis){*lo[axis]=std::clamp(*lo[axis],a[axis],z[axis]-.0001f);*hi[axis]=std::clamp(*hi[axis],*lo[axis]+.0001f,z[axis]);}return b;
}
bool BuildLayout(const Definition&d,const std::vector<Surface>&surfaces,VehicleLayout&layout,MaterialParameters&material){
 if(!materialmodel::ValidBox(d.bounds)||!std::isfinite(d.armor)||d.armor<1||d.armor>100||int(d.kind)<0||int(d.kind)>4)return false;
 const auto fullSize=Sub(d.bounds.hi,d.bounds.lo);if(std::max({fullSize.x,fullSize.y,fullSize.z})>200)return false;
 bool bodyFound=false;auto body=SurfaceBounds(surfaces,Role::Body,bodyFound);if(!bodyFound)body=d.bounds;
 // A source body may be a single sheet in focused fixtures. Only use its
 // measured nonzero dimensions; the complete mesh supplies the remaining axis.
 if(body.hi.x-body.lo.x<.001f){body.lo.x=d.bounds.lo.x;body.hi.x=d.bounds.hi.x;}if(body.hi.y-body.lo.y<.001f){body.lo.y=d.bounds.lo.y;body.hi.y=d.bounds.hi.y;}if(body.hi.z-body.lo.z<.001f){body.lo.z=d.bounds.lo.z;body.hi.z=d.bounds.hi.z;}body=FitBox(body,d.bounds);
 layout.longitudinalBody=MechanicalKind(d.kind);layout.mechanical.body=d.bounds;layout.mechanical.wheels=d.wheels;const auto size=Sub(body.hi,body.lo),centre=Mul(Add(body.lo,body.hi),.5f);
 const bool steel=d.kind==Kind::Automobile||d.kind==Kind::Bike;material=steel?MaterialParameters{200e9f,250e6f,d.kind==Kind::Bike?.0012f:.0009f,d.kind==Kind::Bike?9.4f:7.1f,50000,1}:MaterialParameters{};
 if(d.kind==Kind::Boat){material.thickness=.0015f;material.surfaceDensity=4.05f;material.yield=110e6f;}
 const float armor=std::max(d.armor,materialmodel::Armored(d.model)?4.f:1.f);material.thickness*=armor;material.surfaceDensity*=armor;material.skinResistance=armor;
 auto &v=layout.mechanical.volumes;for(auto&box:v)box=body;
 bool rear=d.kind==Kind::Boat||d.model==211||d.model==210||d.model==141||d.model==145||d.model==236;
 v[mechanicalmodel::kEngine]=FractionBox(body,{.23f,rear?.05f:.69f,.10f},{.77f,rear?.31f:.96f,.67f});
 if(d.kind==Kind::Bike)v[mechanicalmodel::kEngine]=FractionBox(body,{.2f,.34f,.05f},{.8f,.62f,.52f});
 if(d.kind==Kind::Helicopter)v[mechanicalmodel::kEngine]=FractionBox(body,{.2f,.4f,.52f},{.8f,.72f,.94f});
 v[mechanicalmodel::kFuel]=FractionBox(body,{.22f,rear?.62f:.06f,.08f},{.78f,rear?.85f:.26f,.38f});
 if(d.kind==Kind::Bike)v[mechanicalmodel::kFuel]=FractionBox(body,{.25f,.45f,.58f},{.75f,.68f,.85f});
 v[mechanicalmodel::kCooling]=FractionBox(body,{.17f,rear?.03f:.94f,.1f},{.83f,rear?.09f:.99f,.6f});
 const auto engineCentre=Mul(Add(v[mechanicalmodel::kEngine].lo,v[mechanicalmodel::kEngine].hi),.5f);
 const Vec3 ehalf{size.x*.11f,size.y*.07f,size.z*.16f};v[mechanicalmodel::kElectrical]=FitBox({Sub(Add(engineCentre,{size.x*.2f,0,size.z*.12f}),ehalf),Add(Add(engineCentre,{size.x*.2f,0,size.z*.12f}),ehalf)},body);
 v[mechanicalmodel::kDrivetrain]=FractionBox(body,{.3f,.3f,.02f},{.7f,.67f,.26f});v[mechanicalmodel::kSteering]=FractionBox(body,{.12f,.66f,.02f},{.88f,.76f,.22f});
 // Automobile drivetrain parts sit relative to the measured axles, not to
 // fractions of an envelope that also spans bumpers, roofs and spoilers.
 mechanicalmodel::Layout axles;if(d.kind==Kind::Automobile&&d.hasWheels&&mechanicalmodel::Build(d.model,d.bounds.lo,d.bounds.hi,d.wheels,axles))for(int module:{mechanicalmodel::kEngine,mechanicalmodel::kCooling,mechanicalmodel::kSteering,mechanicalmodel::kDrivetrain,mechanicalmodel::kFuel,mechanicalmodel::kElectrical})v[module]=FitBox(axles.volumes[module],d.bounds);
 for(int module:{mechanicalmodel::kEngine,mechanicalmodel::kCooling,mechanicalmodel::kDrivetrain,mechanicalmodel::kFuel,mechanicalmodel::kElectrical})layout.enabled[module]=bodyFound;
 layout.enabled[mechanicalmodel::kSteering]=bodyFound&&d.kind!=Kind::Boat;
 for(auto mapping:std::array<std::pair<Role,int>,3>{{{Role::Engine,mechanicalmodel::kEngine},{Role::Fuel,mechanicalmodel::kFuel},{Role::Electrical,mechanicalmodel::kElectrical}}}){bool found=false;auto b=SurfaceBounds(surfaces,mapping.first,found);if(found){const Vec3 pad{.025f,.025f,.025f};v[mapping.second]=FitBox({Sub(b.lo,pad),Add(b.hi,pad)},d.bounds);layout.enabled[mapping.second]=true;}}
 for(int i=0;i<4;++i){if(d.hasWheels&&!Finite(d.wheels[i]))return false;const Vec3 half{std::min(.3f,fullSize.x*.15f),std::min(.4f,fullSize.y*.1f),std::min(.4f,fullSize.z*.25f)};v[mechanicalmodel::kWheelLF+i]=FitBox({Sub(d.wheels[i],half),Add(d.wheels[i],half)},d.bounds);v[mechanicalmodel::kBrakeLF+i]=FitBox({Sub(d.wheels[i],Mul(half,.5f)),Add(d.wheels[i],Mul(half,.5f))},d.bounds);layout.enabled[mechanicalmodel::kWheelLF+i]=layout.enabled[mechanicalmodel::kBrakeLF+i]=d.hasWheels;}
 auto &a=layout.airframe;for(auto&box:a)box=body;
 a[airframemodel::EngineL]=a[airframemodel::EngineR]=v[mechanicalmodel::kEngine];a[airframemodel::Electrical]=v[mechanicalmodel::kElectrical];a[airframemodel::Hydraulics]=v[mechanicalmodel::kDrivetrain];
 a[airframemodel::FuelL]=v[mechanicalmodel::kFuel];a[airframemodel::FuelR]=v[mechanicalmodel::kFuel];a[airframemodel::FuelL].hi.x=centre.x;a[airframemodel::FuelR].lo.x=centre.x;
 for(int module:{airframemodel::EngineL,airframemodel::FuelL,airframemodel::FuelR,airframemodel::Electrical,airframemodel::Hydraulics})layout.airframeEnabled[module]=bodyFound;layout.airframeEnabled[airframemodel::EngineL]=layout.enabled[mechanicalmodel::kEngine];layout.airframeEnabled[airframemodel::FuelL]=layout.airframeEnabled[airframemodel::FuelR]=layout.enabled[mechanicalmodel::kFuel];layout.airframeEnabled[airframemodel::Electrical]=layout.enabled[mechanicalmodel::kElectrical];
 for(auto mapping:std::array<std::pair<Role,int>,6>{{{Role::WingLeft,airframemodel::AileronL},{Role::WingRight,airframemodel::AileronR},{Role::Elevator,airframemodel::Elevator},{Role::Rudder,airframemodel::Rudder},{Role::MainRotor,airframemodel::MainRotor},{Role::TailRotor,airframemodel::TailRotor}}}){bool found=false;auto b=SurfaceBounds(surfaces,mapping.first,found);if(!found)continue;const Vec3 pad{.001f,.001f,.001f};a[mapping.second]=FitBox({Sub(b.lo,pad),Add(b.hi,pad)},d.bounds);layout.airframeEnabled[mapping.second]=true;if(mapping.first==Role::WingLeft||mapping.first==Role::WingRight){const int side=mapping.first==Role::WingLeft?0:1;const auto wingSize=Sub(b.hi,b.lo);layout.beams[side]={Vec3{side?1.f:-1.f,0,0},side?b.lo.x:-b.hi.x,wingSize.x};a[side?airframemodel::FuelR:airframemodel::FuelL]=FractionBox(FitBox({Sub(b.lo,{0,0,.01f}),Add(b.hi,{0,0,.01f})},d.bounds),side?Vec3{.12f,.25f,0}:Vec3{.4f,.25f,0},side?Vec3{.6f,.75f,1}:Vec3{.88f,.75f,1});/* wing tanks span 12-60% of each wing from its root */}}
 float tailRoot=centre.y;for(int side=0;side<2;++side)if(layout.beams[side].span>0){bool found=false;const auto wing=SurfaceBounds(surfaces,side?Role::WingRight:Role::WingLeft,found);if(found)tailRoot=std::min(tailRoot,wing.lo.y);}
 layout.beams[2]={{0,-1,0},-tailRoot,std::max(0.f,tailRoot-d.bounds.lo.y)};if(layout.longitudinalBody){layout.beams[0]={{0,1,0},centre.y,body.hi.y-centre.y};layout.beams[1]={{0,-1,0},-centre.y,centre.y-body.lo.y};layout.beams[2].span=0;}return true;
}
Vec3 ClosestPoint(Vec3 p,const Surface&s){
 const auto a=s.points[0],b=s.points[1],c=s.points[2],ab=Sub(b,a),ac=Sub(c,a),ap=Sub(p,a);const float d1=Dot(ab,ap),d2=Dot(ac,ap);if(d1<=0&&d2<=0)return a;
 const auto bp=Sub(p,b);const float d3=Dot(ab,bp),d4=Dot(ac,bp);if(d3>=0&&d4<=d3)return b;const float vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0)return Add(a,Mul(ab,d1/(d1-d3)));
 const auto cp=Sub(p,c);const float d5=Dot(ab,cp),d6=Dot(ac,cp);if(d6>=0&&d5<=d6)return c;const float vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0)return Add(a,Mul(ac,d2/(d2-d6)));const float va=d3*d6-d5*d4;if(va<=0&&d4-d3>=0&&d5-d6>=0)return Add(b,Mul(Sub(c,b),(d4-d3)/(d4-d3+d5-d6)));const float sum=va+vb+vc;if(std::fabs(sum)<1e-15f)return a;return Add(a,Add(Mul(ab,vb/sum),Mul(ac,vc/sum)));
}
void InferAircraftRoles(std::vector<Surface>&surfaces,const Definition&d){
 // Named wing atomics (flaps, ailerons, classified hulls) take precedence only
 // while they carry the wing material; a few flaps must not leave the main
 // wing skin as fuselage.
 if(d.kind!=Kind::Plane)return;float explicitWing=0;for(const auto&s:surfaces)if((s.role==Role::WingLeft||s.role==Role::WingRight)&&s.structural)explicitWing+=Length(Cross(Sub(s.points[1],s.points[0]),Sub(s.points[2],s.points[0])));
 bool found=false;const auto b=SurfaceBounds(surfaces,Role::Body,found);if(!found)return;const auto centre=Mul(Add(b.lo,b.hi),.5f);const float length=b.hi.y-b.lo.y;if(length<.01f)return;
 std::array<float,9> widths{};for(size_t station=0;station<widths.size();++station){const float y=b.lo.y+length*float(station+1)/10;for(const auto&s:surfaces)if(s.role==Role::Body&&s.structural&&!s.glass)for(int edge=0;edge<3;++edge){const auto a=s.points[edge],c=s.points[(edge+1)%3];if((a.y<=y&&c.y>=y)||(a.y>=y&&c.y<=y)){const float span=c.y-a.y;if(std::fabs(span)>.00001f)widths[station]=std::max(widths[station],std::fabs(a.x+(c.x-a.x)*(y-a.y)/span-centre.x));}}}
 auto ordered=widths;std::sort(ordered.begin(),ordered.end());const float core=ordered[2],maximum=ordered.back();if(core<.01f||maximum<core*1.8f)return;float wingLo=b.hi.y,wingHi=b.lo.y;for(size_t i=0;i<widths.size();++i)if(widths[i]>core*1.5f){wingLo=std::min(wingLo,b.lo.y+length*float(i)/10);wingHi=std::max(wingHi,b.lo.y+length*float(i+2)/10);}
 float inferredWing=0;for(const auto&s:surfaces)if(s.role==Role::Body&&s.structural&&!s.glass){const auto c=Centre(s);if(c.y>=wingLo&&c.y<=wingHi&&std::fabs(c.x-centre.x)>core)inferredWing+=Length(Cross(Sub(s.points[1],s.points[0]),Sub(s.points[2],s.points[0])));}if(explicitWing>0&&explicitWing>=inferredWing)return;
 for(auto&s:surfaces)if(s.role==Role::Body&&s.structural&&!s.glass){const auto c=Centre(s);const float lateral=std::fabs(c.x-centre.x);if(c.y>=wingLo&&c.y<=wingHi&&lateral>core)s.role=c.x<centre.x?Role::WingLeft:Role::WingRight;else if(c.y<wingLo&&lateral>core*1.1f)s.role=Role::Elevator;else if(c.y<wingLo&&c.z>centre.z){const auto n=Cross(Sub(s.points[1],s.points[0]),Sub(s.points[2],s.points[0]));if(std::fabs(n.x)>Length(n)*.7f)s.role=Role::Rudder;}}
}
void BuildSupports(const std::vector<Surface>&surfaces,const aircraftconnectivity::Mesh&baseline,const VehicleLayout&layout,std::vector<aircraftconnectivity::Anchor>&roots,std::vector<aircraftconnectivity::Support>&supports){
 std::map<size_t,std::vector<size_t>> islands;std::map<size_t,float> bodyArea,totalArea;
 for(const auto&f:baseline.Fragments()){islands[f.component].push_back(f.source);const auto&s=surfaces[f.source];const float area=Length(Cross(Sub(s.points[1],s.points[0]),Sub(s.points[2],s.points[0])));totalArea[f.component]+=area;if(s.role==Role::Body&&s.structural&&!s.glass)bodyArea[f.component]+=area;}
 if(islands.empty())return;size_t core=islands.begin()->first;float biggest=-1;const auto&areas=bodyArea.empty()?totalArea:bodyArea;for(const auto&entry:areas)if(entry.second>biggest){biggest=entry.second;core=entry.first;}
 const auto centre=Mul(Add(layout.mechanical.body.lo,layout.mechanical.body.hi),.5f);std::vector<size_t> coreFaces;for(size_t face:islands[core])if(surfaces[face].role==Role::Body&&surfaces[face].structural&&!surfaces[face].glass)coreFaces.push_back(face);if(coreFaces.empty())coreFaces=islands[core];
 auto nearest=[&](const std::vector<size_t>&faces,Vec3 target){aircraftconnectivity::Anchor anchor{};float closest=std::numeric_limits<float>::infinity();for(size_t face:faces){const auto p=ClosestPoint(target,surfaces[face]);const float distance=Dot(Sub(p,target),Sub(p,target));if(distance<closest){closest=distance;anchor={face,p};}}return anchor;};
 // Mechanical panels and wheels are fastened along a mounting edge or hub,
 // not at one material point: up to two more mounts, spread across the part
 // material nearest the core. Brittle glazing keeps its single pane mount.
 auto mount=[&](const std::vector<size_t>&faces,aircraftconnectivity::Anchor part,aircraftconnectivity::Anchor parent){
  supports.push_back({part,parent});bool glass=true;for(size_t face:faces)glass&=surfaces[face].glass;if(!layout.longitudinalBody||glass)return;
  const float gap=Length(Sub(part.position,parent.position));std::vector<size_t> near;for(size_t face:coreFaces)if(Length(Sub(ClosestPoint(parent.position,surfaces[face]),parent.position))<1.f)near.push_back(face);if(near.empty())return;
  struct Candidate{aircraftconnectivity::Anchor part,parent;};std::vector<Candidate> candidates;
  for(size_t face:faces){const auto&s=surfaces[face];if(s.glass||Length(Cross(Sub(s.points[1],s.points[0]),Sub(s.points[2],s.points[0])))<1e-6f)continue;const auto c=Centre(s);if(Length(Sub(c,part.position))>1.f)continue;const auto p=nearest(near,c);if(Length(Sub(p.position,c))<=gap+.15f)candidates.push_back({{face,c},p});}
  std::vector<Vec3> chosen{part.position};for(int extra=0;extra<2;++extra){const Candidate*best=nullptr;float spread=.1f;for(const auto&k:candidates){float d=std::numeric_limits<float>::infinity();for(auto q:chosen)d=std::min(d,Length(Sub(k.part.position,q)));if(d>spread){spread=d;best=&k;}}if(!best)break;chosen.push_back(best->part.position);supports.push_back({best->part,best->parent});}
 };
 roots.push_back(nearest(coreFaces,centre));const auto size=Sub(layout.mechanical.body.hi,layout.mechanical.body.lo);for(auto offset:std::array<Vec3,6>{{{size.x*.12f,0,0},{-size.x*.12f,0,0},{0,size.y*.12f,0},{0,-size.y*.12f,0},{0,0,size.z*.12f},{0,0,-size.z*.12f}}})roots.push_back(nearest(coreFaces,Add(centre,offset)));
 for(const auto&entry:islands)if(entry.first!=core){std::vector<size_t>mountFaces;for(size_t face:entry.second)if(surfaces[face].structural&&!surfaces[face].glass&&surfaces[face].role!=Role::Interior)mountFaces.push_back(face);if(mountFaces.empty())for(size_t face:entry.second)if(!surfaces[face].glass&&surfaces[face].role!=Role::Interior)mountFaces.push_back(face);if(mountFaces.empty())mountFaces=entry.second;auto part=nearest(mountFaces,centre);auto parent=nearest(coreFaces,part.position);part=nearest(mountFaces,parent.position);parent=nearest(coreFaces,part.position);mount(mountFaces,part,parent);}
 // Wheels can coincide with chassis material in an export. Their axle mount
 // remains a separate material support after that coincident skin is cut.
 std::map<uint32_t,std::vector<size_t>> wheelGroups;for(size_t face:islands[core])if(surfaces[face].role==Role::Wheel)wheelGroups[surfaces[face].group].push_back(face);
 for(const auto&entry:wheelGroups){auto part=nearest(entry.second,centre),parent=nearest(coreFaces,part.position);part=nearest(entry.second,parent.position);mount(entry.second,part,parent);}
}
constexpr float kYoung=70e9f,kYield=100e6f,kThickness=.0008f,kChord=1.5f,kDepth=.14f,kSpan=4.5f;
constexpr float kInertia=2*kChord*kThickness*(kDepth*.5f)*(kDepth*.5f);
constexpr float kStiffness=kYoung*kInertia/kSpan,kYieldMoment=kYield*kInertia/(kDepth*.5f),kYieldAngle=kYieldMoment/kStiffness;
// Bounded, explicit two-sheet cantilever approximation. Plastic work per area
// assumes 12% accumulated sheet strain; this is a calibration, not a shell FEM.
constexpr float kTearWorkPerArea=kYield*kThickness*.12f;
// Effective work to create a through-thickness crack surface, J/m^2. Native
// contacts still use the separate sheet-perforation work above.
constexpr int kSectionCount=12;
constexpr float kSurfaceDensity=2.7f,kTailSpan=5.2f;
float BeamCoordinate(Vec3 p,int region,const VehicleLayout*layout=nullptr){return layout?Dot(p,layout->beams[region].axis)-layout->beams[region].root:region<2?(region==0?-p.x:p.x)-.85f:-p.y-1.5f;}
float BeamSpan(int region,const VehicleLayout*layout=nullptr){return layout?std::max(.001f,layout->beams[region].span):region<2?kSpan:kTailSpan;}
int SectionRegion(const Surface&s,const VehicleLayout*layout=nullptr){const auto c=Centre(s);if(layout&&layout->longitudinalBody)return s.role==Role::Body?(c.y>=layout->beams[0].root?0:1):-1;if(s.role==Role::WingLeft)return 0;if(s.role==Role::WingRight)return 1;if(layout)return (s.role==Role::Body||s.role==Role::Elevator||s.role==Role::Rudder||s.role==Role::TailRotor)&&BeamCoordinate(c,2,layout)>0?2:-1;return s.role==Role::Body&&std::fabs(c.x)>.85f&&c.y>-1.1f&&c.y<1.3f&&c.z>.65f&&c.z<1.4f?(c.x<0?0:1):c.y< -1.5f?2:-1;}
float Transverse(Vec3 p,int region,int axis,const VehicleLayout*layout=nullptr){return axis==1?p.z:layout?std::fabs(layout->beams[region].axis.x)>.5f?p.y:p.x:region<2?p.y:p.x;}
struct Ligament {Vec3 a,b,normal;uint32_t group=0;};
std::vector<Ligament> UnionSectionLines(const std::vector<Ligament>&input,bool keepGroups){
 std::vector<Ligament> out;out.reserve(input.size());
 for(auto candidate:input){
  // A new interval can bridge several old intervals. Restart after each
  // merge, so no overlap survives because it appeared earlier in the list.
  for(size_t i=0;i<out.size();){const auto&line=out[i];if(keepGroups&&line.group!=candidate.group){++i;continue;}auto direction=Sub(candidate.b,candidate.a);const float length=Length(direction);if(length<1e-6f){++i;continue;}direction=Mul(direction,1/length);const float a=Dot(Sub(line.a,candidate.a),direction),b=Dot(Sub(line.b,candidate.a),direction);if(Length(Sub(Sub(line.a,candidate.a),Mul(direction,a)))>1e-5f||Length(Sub(Sub(line.b,candidate.a),Mul(direction,b)))>1e-5f||std::min(length,std::max(a,b))-std::max(0.f,std::min(a,b))< -1e-5f){++i;continue;}const auto origin=candidate.a;candidate.a=Add(origin,Mul(direction,std::min(0.f,std::min(a,b))));candidate.b=Add(origin,Mul(direction,std::max(length,std::max(a,b))));out.erase(out.begin()+i);i=0;}
  out.push_back(candidate);
 }
 return out;
}
// A section is integrated from surviving longitudinal sheet intervals. Source
// triangulation and UV seams do not constitute additional structural material.
std::vector<Ligament> SectionLines(const std::vector<aircraftconnectivity::Fragment>&fragments,const std::vector<Surface>&surfaces,int region,float at,const VehicleLayout*layout=nullptr){
 std::vector<Ligament> lines;
 for(const auto&f:fragments){const auto&s=surfaces[f.source];if(!f.attached||!s.structural||s.glass||s.role==Role::Wheel||s.role==Role::Interior||s.role==Role::Door)continue;
  if(layout&&layout->longitudinalBody?(region>=2||s.role!=Role::Body):SectionRegion(s,layout)!=region)continue;
  std::vector<Vec3> points;for(size_t i=0;i<f.polygon.size();++i){const auto a=f.polygon[i].position,b=f.polygon[(i+1)%f.polygon.size()].position;const float da=BeamCoordinate(a,region,layout)-at,db=BeamCoordinate(b,region,layout)-at;if(std::fabs(da)<1e-6f)points.push_back(a);if((da<0&&db>0)||(da>0&&db<0))points.push_back(Add(a,Mul(Sub(b,a),da/(da-db))));}
  if(points.size()<2)continue;Vec3 a{},b{};float longest=0;for(size_t i=0;i<points.size();++i)for(size_t j=0;j<i;++j){const float length=Length(Sub(points[i],points[j]));if(length>longest){a=points[i];b=points[j];longest=length;}}if(longest<1e-5f)continue;
  auto normal=Cross(Sub(s.points[1],s.points[0]),Sub(s.points[2],s.points[0]));const float normalLength=Length(normal);if(normalLength<1e-7f)continue;normal=Mul(normal,1/normalLength);
  const float alignment=layout?std::fabs(Dot(normal,layout->beams[region].axis)):region<2?std::fabs(normal.x):std::fabs(normal.y);if(alignment>.98f)continue;
  lines.push_back({a,b,normal,f.group});
 }
 // Fracture targets retain the material group covering their whole interval.
 return UnionSectionLines(lines,true);
}
struct SectionGeometry {float area=0,mass=0,arm=0;std::array<float,2> inertia{},extent{};};
SectionGeometry MeasureSection(const std::vector<aircraftconnectivity::Fragment>&fragments,const std::vector<Surface>&surfaces,int region,float at,bool loads=true,const VehicleLayout*layout=nullptr,const MaterialParameters*material=nullptr){
 // Strength integrates the geometric union, including coincident UV seams in
 // different groups. These provenance-free intervals are never crack targets.
 const float thickness=material?material->thickness:kThickness,density=material?material->surfaceDensity:kSurfaceDensity;SectionGeometry out;const auto lines=UnionSectionLines(SectionLines(fragments,surfaces,region,at,layout),false);double area=0,first[2]{},second[2]{};
 for(const auto&line:lines){const double weight=thickness*Length(Sub(line.b,line.a));area+=weight;for(int axis=0;axis<2;++axis){const double a=Transverse(line.a,region,axis,layout),b=Transverse(line.b,region,axis,layout);first[axis]+=weight*(a+b)*.5;second[axis]+=weight*((a*a+a*b+b*b)/3+thickness*thickness/12);}}
 out.area=float(area);if(area>1e-12)for(int axis=0;axis<2;++axis){const double centroid=first[axis]/area;out.inertia[axis]=float(std::max(0.,second[axis]-first[axis]*centroid));for(const auto&line:lines)out.extent[axis]=std::max(out.extent[axis],float(std::max(std::fabs(Transverse(line.a,region,axis,layout)-centroid),std::fabs(Transverse(line.b,region,axis,layout)-centroid))+thickness*.5));}
 if(!loads)return out;const skinmodel::Cut cut=layout?skinmodel::Cut{Mul(layout->beams[region].axis,-1),-(at+layout->beams[region].root)}:region==0?skinmodel::Cut{{1,0,0},-(at+.85f)}:region==1?skinmodel::Cut{{-1,0,0},-(at+.85f)}:skinmodel::Cut{{0,1,0},-(at+1.5f)};
 for(const auto&f:fragments){const auto&s=surfaces[f.source];if(!f.attached||!s.structural||s.glass||s.role==Role::Wheel||s.role==Role::Interior||s.role==Role::Door)continue;if(layout&&layout->longitudinalBody?(region>=2||s.role!=Role::Body):SectionRegion(s,layout)!=region)continue;
  const auto polygon=skinmodel::Slice(f.polygon,cut);for(size_t i=1;i+1<polygon.size();++i){const auto a=polygon[0].position,b=polygon[i].position,c=polygon[i+1].position;const float mass=Length(Cross(Sub(b,a),Sub(c,a)))*.5f*density;out.mass+=mass;out.arm+=mass*std::max(0.f,BeamCoordinate(Mul(Add(Add(a,b),c),1.f/3),region,layout)-at);}
 }
 return out;
}
SectionGeometry MeasureRestSection(const std::vector<Surface>&surfaces,int region,float at,const VehicleLayout*layout=nullptr,const MaterialParameters*material=nullptr){
 std::vector<aircraftconnectivity::Fragment> original;original.reserve(surfaces.size());for(size_t i=0;i<surfaces.size();++i){const auto&s=surfaces[i];if(!s.structural||s.glass)continue;const float a=BeamCoordinate(s.points[0],region,layout),b=BeamCoordinate(s.points[1],region,layout),c=BeamCoordinate(s.points[2],region,layout);if(std::min({a,b,c})>at||std::max({a,b,c})<at)continue;original.push_back({i,s.group,{{s.points[0],{1,0,0}},{s.points[1],{0,1,0}},{s.points[2],{0,0,1}}},0,true});}return MeasureSection(original,surfaces,region,at,false,layout,material);
}
float Resistance(int i){static const float r[]={65,0,8,8,0,0,16,16,12,12,15,12,0,0};return r[i];}
// Accepted crash work crushes mechanical components in the order its local
// deformation front reaches them. Each needs its destruction energy before the
// remainder passes on. Joules for a 1500 kg reference car: projectile budgets
// keep their native damage units, contact work is SI and never borrows them.
constexpr float kCrushEnergy[]={12000,10000,10000,10000,10000,10000,5000,5000,5000,5000,8000,15000,8000,5000};
constexpr float kWingDesignLoad=4,kCrushMass=1500,kBendPerEpisode=.05f,kBodyCrushForce=150000,kRubberTearWork=40000;
std::array<float,airframemodel::Count> Crush(Vec3 from,Vec3 inward,float reach,float joules,const Boxes&volumes,const std::array<bool,airframemodel::Count>&present,const std::array<float,mechanicalmodel::kModules>&damage,float scale){
 std::array<float,airframemodel::Count> doses{};if(!std::isfinite(joules)||joules<=0||!std::isfinite(reach)||reach<=0||!std::isfinite(scale)||scale<=0)return doses;
 // The crushed region has width as well as depth.
 const Vec3 d=Mul(inward,reach);const float radius=std::clamp(reach*.5f,.05f,.4f);std::array<std::pair<float,int>,mechanicalmodel::kModules> order{};size_t count=0;
 for(int i=0;i<mechanicalmodel::kModules;++i){if(!present[i])continue;auto b=volumes[i];b.lo=Sub(b.lo,{radius,radius,radius});b.hi=Add(b.hi,{radius,radius,radius});float near=0,far=1;if(materialmodel::BoxHit(b,from,d,near,far))order[count++]={near,i};}
 std::sort(order.begin(),order.begin()+count);float remaining=joules;
 for(size_t k=0;k<count&&remaining>0;++k){const int i=order[k].second;const float capacity=kCrushEnergy[i]*scale,spent=std::min(remaining,capacity*std::max(0.f,1-damage[i]));if(spent<=0)continue;doses[i]=spent/capacity;remaining-=spent;}
 return doses;
}
bool Intersect(Vec3 a,Vec3 d,Vec3 x,Vec3 y,Vec3 z,float &t,Vec3 &weights,Vec3&normal){
 auto e=Sub(y,x),f=Sub(z,x),h=Cross(d,f);float det=Dot(e,h);if(!std::isfinite(det)||std::fabs(det)<1e-9f)return false;auto s=Sub(a,x);float u=Dot(s,h)/det;if(u<0||u>1)return false;auto q=Cross(s,e);float v=Dot(d,q)/det;if(v<0||u+v>1)return false;float at=Dot(f,q)/det;if(!std::isfinite(at)||at<0||at>=t)return false;normal=Cross(e,f);float n=Length(normal);if(n<1e-7f)return false;normal=Mul(normal,1/n);if(Dot(normal,d)>0)normal=Mul(normal,-1);weights={1-u-v,u,v};t=at;return true;
}
Vec3 Weighted(Vec3 a,Vec3 b,Vec3 c,Vec3 w){return Add(Add(Mul(a,w.x),Mul(b,w.y)),Mul(c,w.z));}
// A tempered pane: the connected tempered glass of one source part, joined by
// shared rest vertices, sorted by source face.
std::vector<size_t> TemperedPane(const std::vector<Surface>&surfaces,size_t seed){
 std::vector<size_t> pane{seed};std::vector<bool> taken(surfaces.size());taken[seed]=true;const auto&origin=surfaces[seed];
 for(size_t k=0;k<pane.size();++k){const auto&a=surfaces[pane[k]];for(size_t j=0;j<surfaces.size();++j){const auto&b=surfaces[j];if(taken[j]||!b.glass||b.laminated||b.group!=origin.group)continue;bool shared=false;for(auto p:a.points)for(auto q:b.points)shared|=Dot(Sub(p,q),Sub(p,q))<1e-6f;if(shared){taken[j]=true;pane.push_back(j);}}}
 std::sort(pane.begin(),pane.end());return pane;
}
}
Role ClassifyFace(const char *name,Vec3 c,Kind kind,int model){
 if(!name)return Role::Body;Role role=Role::Body;
 if(std::strstr(name,"tire")||std::strstr(name,"wheel"))role=Role::Wheel;
 else if(std::strstr(name,"cabin")||std::strstr(name,"dashboard")||std::strstr(name,"yoke"))role=Role::Interior;
 else if(std::strstr(name,"flap_l"))role=Role::WingLeft;else if(std::strstr(name,"flap_r"))role=Role::WingRight;
 else if(std::strstr(name,"stab")||std::strstr(name,"elevator"))role=Role::Elevator;else if(std::strstr(name,"rudder"))role=Role::Rudder;
 else if(std::strstr(name,"door"))role=Role::Door;
 // Prepared airport Learjet, measured: low main wing y -4.3..0.6 m outboard of
 // the 0.95 m fuselage, twin engine pods on the rear fuselage above it, T-tail.
 else if(!std::strcmp(name,"jet_hull")){const float x=std::fabs(c.x);if(x>.95f&&x<1.95f&&c.y>-5.6f&&c.y<-2.85f&&c.z>-.3f)role=Role::Engine;else if(x>.98f&&c.y>-4.5f&&c.y<.6f&&c.z<-.3f)role=c.x<0?Role::WingLeft:Role::WingRight;else if(c.y<-7.8f&&c.z>1.5f&&x>.12f)role=Role::Elevator;else if(c.y<-6.4f&&x<.35f&&c.z>.6f)role=Role::Rudder;}
 else if(!std::strcmp(name,"dodo_hull")){if(std::fabs(c.x)>.85f&&((c.y>-1&&c.y<1.2f&&c.z>.8f)||(c.y>.5f&&c.y<1.1f&&c.z>-.5f&&c.z<.8f)))role=c.x<0?Role::WingLeft:Role::WingRight;else if(c.y<-3.5f)role=c.z>.15f?Role::Rudder:Role::Elevator;}
 if(model!=181){
  if(kind==Kind::Helicopter&&(std::strstr(name,"bonnet")||std::strstr(name,"toprotor")))role=Role::MainRotor;
  else if(kind==Kind::Helicopter&&(std::strstr(name,"boot")||std::strstr(name,"backrotor")))role=Role::TailRotor;
  else if(std::strstr(name,"bonnet")||std::strstr(name,"boot"))role=Role::Door;
  else if(std::strstr(name,"engine"))role=Role::Engine;
  else if(std::strstr(name,"rotor")||std::strstr(name,"moving_rotor")||std::strstr(name,"static_rotor"))role=(std::strstr(name,"rear")||std::strstr(name,"tail"))?Role::TailRotor:Role::MainRotor;
  else if(role==Role::Body&&kind==Kind::Plane&&(std::strstr(name,"wing")||std::strstr(name,"aileron")))role=c.x<0?Role::WingLeft:Role::WingRight;
 }
 return role;
}
bool RenderOnlyAtomic(const char *name){
 if(!name||std::strncmp(name,"jet_",4))return false;
 // The Learjet's cabin furniture, lining, window trim and airstair are about
 // 3,100 of its 4,800 prepared faces; none is load-bearing airframe skin.
 for(const char *part:{"cabin","dashboard","lining","window_trim","step"})if(std::strstr(name+4,part))return true;return false;
}
FaceClass ClassifyMaterial(Role role,Kind kind,const char *name,Vec3 centre,Vec3 normal,bool translucent,uint8_t alpha,const char *texture){
 FaceClass out;out.glass=role!=Role::MainRotor&&role!=Role::TailRotor&&translucent;
 // Car badges, stripes, emergency decals and lamps also use alpha: only cabin
 // glazing is glass. The laminated windscreen keeps local perforations.
 if(out.glass&&kind==Kind::Automobile){const auto pane=CabinGlass(name,centre,normal,alpha==255?254:alpha,texture);out.glass=pane!=GlassPane::None;out.laminated=pane==GlassPane::Front;}
 out.structural=!out.glass&&role!=Role::Interior&&role!=Role::Wheel&&role!=Role::Door;return out;
}
bool Model::SetGroupPose(uint32_t group,const std::array<float,12>&affine){
 if(!initialized_)return false;bool known=false;for(const auto&s:surfaces_)if(s.group==group){known=true;break;}if(!known)return false;
 for(float value:affine)if(!std::isfinite(value))return false;const float det=Determinant(affine);if(!std::isfinite(det)||std::fabs(det)<1e-7f)return false;
 // Check all original vertices before committing even a finite affine: its
 // products must also remain finite for the renderer and native collision.
 for(const auto&s:surfaces_)if(s.group==group)for(auto p:s.points)if(!Finite(Transform(affine,p)))return false;
 for(auto&pose:poses_)if(pose.group==group){if(pose.affine==affine)return true;pose.affine=affine;++deformationRevision_;return true;}
 constexpr std::array<float,12> identity{1,0,0,0,1,0,0,0,1,0,0,0};if(affine==identity)return true;
 try{poses_.push_back({group,affine});++deformationRevision_;return true;}catch(const std::bad_alloc&){return false;}
}
bool Model::SetMass(float mass){if(!initialized_||!std::isfinite(mass)||mass<=0||mass>1e9f)return false;mass_=mass;return true;}
Vec3 Model::Pose(Vec3 point,uint32_t group)const{for(const auto&pose:poses_)if(pose.group==group)return Transform(pose.affine,point);return point;}
Vec3 Model::RestDirection(Vec3 direction,uint32_t group)const{for(const auto&pose:poses_)if(pose.group==group)return InverseDirection(pose.affine,direction);return direction;}
int Model::Side(const Surface&s)const{const int region=SectionRegion(s,generic_?&layout_:nullptr);return region<2?region:-1;}
int Model::Region(const Surface&s)const{return SectionRegion(s,generic_?&layout_:nullptr);}
bool Model::MechanicalKind()const{return generic_&&aircraftbodymodel::MechanicalKind(definition_.kind);}
bool Model::SheetComponent(int i)const{return !MechanicalKind()&&(Control(i)||(generic_&&(i==airframemodel::MainRotor||i==airframemodel::TailRotor)));}
float Model::ComponentResistance(int i)const{static const float mechanicalResistance[]={80,8,16,16,16,16,12,12,12,12,20,70,6,10};if(MechanicalKind())return layout_.enabled[i]?mechanicalResistance[i]:0;if(generic_&&!layout_.airframeEnabled[i])return 0;if(generic_&&(i==airframemodel::MainRotor||i==airframemodel::TailRotor))return 16;return Resistance(i);}
void Model::ApplyDoses(const std::array<float,airframemodel::Count>&doses,Vec3 inward,bool collision){
 if(MechanicalKind()){if(collision)mechanicalmodel::Apply(mechanical_,doses,inward);else mechanicalmodel::Apply(mechanical_,doses);damage_=mechanical_.damage;return;}
 for(int i=0;i<airframemodel::Count;++i)if(doses[i]>0)damage_[i]=Clamp(damage_[i]+doses[i]);damage_[airframemodel::EngineR]=damage_[airframemodel::EngineL];
 mechanical_.damage[mechanicalmodel::kEngine]=damage_[airframemodel::EngineL];mechanical_.damage[mechanicalmodel::kFuel]=std::max(damage_[airframemodel::FuelL],damage_[airframemodel::FuelR]);mechanical_.damage[mechanicalmodel::kElectrical]=damage_[airframemodel::Electrical];
}
bool Model::Initialize(const std::vector<Surface>&surfaces,float mass,const std::vector<aircraftconnectivity::Anchor>&mounts){return InitializeBody(surfaces,mass,mounts,nullptr);}
bool Model::Initialize(const std::vector<Surface>&surfaces,float mass,const Definition&definition,const std::vector<aircraftconnectivity::Anchor>&mounts){return InitializeBody(surfaces,mass,mounts,&definition);}
bool Model::InitializeBody(const std::vector<Surface>&input,float mass,const std::vector<aircraftconnectivity::Anchor>&mounts,const Definition*definition){
 if(input.empty()||input.size()>4096||!std::isfinite(mass)||mass<=0||mass>1e9f)return false;
 try{
  Model next;next.mass_=mass;next.surfaces_=input;auto&surfaces=next.surfaces_;if(definition&&!definition->legacyDodo)InferAircraftRoles(surfaces,*definition);if(definition){next.definition_=*definition;if(!BuildLayout(*definition,surfaces,next.layout_,next.material_))return false;next.generic_=!definition->legacyDodo;}if(!next.generic_){next.layout_.airframe=Components();next.layout_.mechanical.body={{-5.5f,-6.8f,-1.6f},{5.5f,3.5f,2}};next.material_={};}
  for(const auto&s:surfaces)if(s.role!=Role::Wheel&&s.role!=Role::Interior&&s.role!=Role::Door&&next.Region(s)==2)next.tailGroups_.push_back(s.group);std::sort(next.tailGroups_.begin(),next.tailGroups_.end());next.tailGroups_.erase(std::unique(next.tailGroups_.begin(),next.tailGroups_.end()),next.tailGroups_.end());
  for(const auto&s:surfaces)if(next.Side(s)>=0)next.deformGroups_[next.Side(s)].push_back(s.group);for(auto&groups:next.deformGroups_){std::sort(groups.begin(),groups.end());groups.erase(std::unique(groups.begin(),groups.end()),groups.end());}std::vector<aircraftconnectivity::Face> faces;faces.reserve(surfaces.size());
  for(const auto&s:surfaces){for(auto p:s.points)if(!Finite(p))return false;faces.push_back({s.points,s.group});}
  aircraftconnectivity::Mesh baseline;if(!baseline.Build(faces))return false;std::vector<aircraftconnectivity::Anchor> roots=mounts;
  std::map<size_t,std::pair<float,aircraftconnectivity::Anchor>> islands;
  for(const auto&f:baseline.Fragments()){
   const auto&s=surfaces[f.source];auto p=Centre(s);const auto centre=next.generic_?Mul(Add(next.layout_.mechanical.body.lo,next.layout_.mechanical.body.hi),.5f):Vec3{};float score=Dot(Sub(p,centre),Sub(p,centre));
   // Original exported islands have their own material-point mount. They are
   // preserved, while a later fragment must still connect to that same mount.
   if(next.Side(s)>=0)score=std::fabs(p.x)*10+std::fabs(p.y)+std::fabs(p.z-1);
   if(s.role==Role::Elevator||s.role==Role::Rudder)score=-p.y;
   if(s.role==Role::Wheel||s.role==Role::Door)score=-p.z;
   auto found=islands.find(f.component);if(found==islands.end()||score<found->second.first)islands[f.component]={score,{f.source,p}};
   if(s.structural&&!s.glass&&s.role==Role::Body&&(next.generic_?false:std::fabs(p.x)<.7f&&p.y> -1.5f&&p.y<1.4f))roots.push_back({f.source,p});
  }
  std::vector<aircraftconnectivity::Support>supports;if(next.generic_)BuildSupports(surfaces,baseline,next.layout_,roots,supports);else for(auto &island:islands)roots.push_back(island.second.second);
  for(const auto&s:surfaces)if(s.role!=Role::Interior&&(!s.structural||s.glass||s.role==Role::Door||s.role==Role::Wheel))next.partContacts_.push_back({s.group,{}});std::sort(next.partContacts_.begin(),next.partContacts_.end(),[](const PartEpisode&a,const PartEpisode&b){return a.group<b.group;});next.partContacts_.erase(std::unique(next.partContacts_.begin(),next.partContacts_.end(),[](const PartEpisode&a,const PartEpisode&b){return a.group==b.group;}),next.partContacts_.end());
  if(!next.mesh_.Build(faces,roots,{},supports))return false;next.initialized_=true;next.deformationRevision_=Revision()+1;
  for(int region=0;region<3;++region)for(int i=0;i<kSectionCount;++i)next.sections_[region][i].at.fill((i+.5f)*BeamSpan(region,next.generic_?&next.layout_:nullptr)/kSectionCount);
  next.RebuildSections();
  // A visual wing skin omits its spars and stringers. Each generic aircraft wing
  // station is reinforced, once, to carry kWingDesignLoad times its own vertical
  // weight moment; later damage still removes measured skin proportionally.
  if(next.generic_&&next.definition_.kind==Kind::Plane){bool sized=false;for(int region=0;region<2;++region)for(auto&section:next.sections_[region]){if(section.inertia[1]<=1e-12f||section.arm[1]<=0)continue;const float required=kWingDesignLoad*20*section.arm[1]*std::max(next.material_.thickness*.5f,section.extent[1])/next.material_.yield;if(required>section.inertia[1]){section.reinforcement[1]=required/section.inertia[1];sized=true;}}if(sized){next.sectionRevision_=0;next.RebuildSections();}}
  for(auto&region:next.sections_)for(auto&section:region)section.intactInertia=section.inertia;
  for(int side=0;side<2;++side)next.originalLift_[side]=next.AttachedLift(side);
  next.originalControls_={next.AttachedControl(Role::Elevator),next.AttachedControl(Role::Rudder)};
  for(int i=0;i<airframemodel::Count;++i)next.originalComponents_[i]=next.ComponentPresent(i);
  *this=std::move(next);return true;
 }catch(const std::bad_alloc&){return false;}
}
bool Model::GroupAttached(uint32_t group)const{for(const auto&f:mesh_.Fragments())if(f.group==group&&f.attached)return true;return false;}
bool Model::ComponentVolume(int module,mechanicalmodel::Box&box)const{
 box=MechanicalKind()?layout_.mechanical.volumes[module]:layout_.airframe[module];if(!generic_)return true;
 Role role=Role::Body;int wheel=-1;if(MechanicalKind()&&module>=mechanicalmodel::kWheelLF&&module<=mechanicalmodel::kBrakeRR){role=Role::Wheel;wheel=module<=mechanicalmodel::kWheelRR?module-mechanicalmodel::kWheelLF:module-mechanicalmodel::kBrakeLF;}else if(!MechanicalKind()&&(module==airframemodel::FuelL||module==airframemodel::FuelR)&&layout_.beams[module==airframemodel::FuelL?0:1].span>.001f)role=module==airframemodel::FuelL?Role::WingLeft:Role::WingRight;
 const Role explicitRole=MechanicalKind()?(module==mechanicalmodel::kEngine?Role::Engine:module==mechanicalmodel::kFuel?Role::Fuel:module==mechanicalmodel::kElectrical?Role::Electrical:Role::Body):(module==airframemodel::EngineL||module==airframemodel::EngineR?Role::Engine:module==airframemodel::FuelL||module==airframemodel::FuelR?Role::Fuel:module==airframemodel::Electrical?Role::Electrical:Role::Body);
 if(explicitRole!=Role::Body)for(const auto&s:surfaces_)if(s.role==explicitRole){role=explicitRole;break;}
 if(!MechanicalKind()&&SheetComponent(module))role=module==airframemodel::Elevator?Role::Elevator:module==airframemodel::Rudder?Role::Rudder:module==airframemodel::AileronL?Role::WingLeft:module==airframemodel::AileronR?Role::WingRight:module==airframemodel::MainRotor?Role::MainRotor:Role::TailRotor;
 mechanicalmodel::Box present{{1e9f,1e9f,1e9f},{-1e9f,-1e9f,-1e9f}};bool found=false;
 for(const auto&f:mesh_.Fragments())if(f.attached&&surfaces_[f.source].role==role&&!surfaces_[f.source].glass){if(wheel>=0&&definition_.hasWheels){const auto centre=Centre(surfaces_[f.source]);int nearest=0;float best=std::numeric_limits<float>::infinity();for(int i=0;i<4;++i){const float distance=Dot(Sub(centre,definition_.wheels[i]),Sub(centre,definition_.wheels[i]));if(distance<best){best=distance;nearest=i;}}if(nearest!=wheel)continue;}for(const auto&p:f.polygon){found=true;present.lo={std::min(present.lo.x,p.position.x),std::min(present.lo.y,p.position.y),std::min(present.lo.z,p.position.z)};present.hi={std::max(present.hi.x,p.position.x),std::max(present.hi.y,p.position.y),std::max(present.hi.z,p.position.z)};}}
 if(!found)return false;constexpr float margin=.025f;box.lo={std::max(box.lo.x,present.lo.x-margin),std::max(box.lo.y,present.lo.y-margin),std::max(box.lo.z,present.lo.z-margin)};box.hi={std::min(box.hi.x,present.hi.x+margin),std::min(box.hi.y,present.hi.y+margin),std::min(box.hi.z,present.hi.z+margin)};return materialmodel::ValidBox(box);
}
bool Model::ComponentPresent(int module)const{
 if(generic_&&ComponentResistance(module)<=0)return false;
 mechanicalmodel::Box b;if(!ComponentVolume(module,b))return false;const auto size=Sub(layout_.mechanical.body.hi,layout_.mechanical.body.lo);const float margin=generic_?std::min(.35f,std::max(.035f,std::min({size.x,size.y,size.z})*.3f)):module==airframemodel::FuelL||module==airframemodel::FuelR?.12f:.25f;
 for(const auto&f:mesh_.Fragments())if(f.attached&&(!generic_||(!surfaces_[f.source].glass&&surfaces_[f.source].role!=Role::Interior))){Vec3 lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};for(auto v:f.polygon){auto p=v.position;lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}if(lo.x<=b.hi.x+margin&&hi.x>=b.lo.x-margin&&lo.y<=b.hi.y+margin&&hi.y>=b.lo.y-margin&&lo.z<=b.hi.z+margin&&hi.z>=b.lo.z-margin){if(!generic_)return true;auto polygon=f.polygon;for(const auto&cut:std::array<skinmodel::Cut,6>{{{{1,0,0},b.hi.x+margin},{{-1,0,0},-b.lo.x+margin},{{0,1,0},b.hi.y+margin},{{0,-1,0},-b.lo.y+margin},{{0,0,1},b.hi.z+margin},{{0,0,-1},-b.lo.z+margin}}}){polygon=skinmodel::Slice(polygon,cut);if(polygon.empty())break;}if(Area(polygon,false)>1e-10f)return true;}}
 return false;
}
float Model::AttachedLift(int side)const{
 float area=0;for(const auto&f:mesh_.Fragments())if(f.attached&&Side(surfaces_[f.source])==side){auto polygon=f.polygon;for(auto&p:polygon)p.position=Deform(p.position,f.group);area+=Area(polygon,true);}return area;
}
float Model::AttachedControl(Role role)const{
 float area=0;for(const auto&f:mesh_.Fragments())if(f.attached&&surfaces_[f.source].role==role)area+=Area(f.polygon,false);return area;
}
Vec3 Model::Deform(Vec3 p,uint32_t group)const{
 if(!initialized_||!Finite(p))return p;Vec3 localDent{};for(const auto&dent:dents_)if(dent.group==group){const auto relative=Sub(p,dent.point);if(std::fabs(Dot(relative,dent.normal))>.08f)continue;const auto planar=Sub(relative,Mul(dent.normal,Dot(relative,dent.normal)));const float distance=Length(planar);if(distance>=dent.radius)continue;const float weight=1-distance/dent.radius;localDent=Add(localDent,Mul(dent.direction,dent.depth*weight*weight));}
 if(generic_){int region=-1;for(int side=0;side<2;++side)if(std::binary_search(deformGroups_[side].begin(),deformGroups_[side].end(),group)&&BeamCoordinate(p,side,&layout_)>0){region=side;break;}if(region<0&&std::binary_search(tailGroups_.begin(),tailGroups_.end(),group)&&BeamCoordinate(p,2,&layout_)>0)region=2;
  if(region>=0){const float coordinate=BeamCoordinate(p,region,&layout_),span=BeamSpan(region,&layout_);Vec3 shift{};if(region<2){const auto&b=bend_[region];const float distance=std::clamp(coordinate,0.f,span),lever=.5f*distance*distance/span;if(MechanicalKind())shift=Add(Mul(b.plasticVector,lever),Mul(b.direction,std::clamp(b.elastic,-.8f,.8f)*lever));else{const float angle=std::clamp(b.plastic+b.elastic,-.8f,.8f);shift=Mul(b.direction,.5f*angle*distance*distance/span);}}for(const auto&section:sections_[region]){const float transverse=section.flex[0].angle*std::max(0.f,coordinate-section.at[0]),vertical=section.flex[1].angle*std::max(0.f,coordinate-section.at[1]);if(std::fabs(layout_.beams[region].axis.x)>.5f)shift.y+=transverse;else shift.x+=transverse;shift.z+=vertical;}const float length=Length(shift);if(length>span*.75f)shift=Mul(shift,span*.75f/length);p=Add(p,shift);}return Pose(Add(p,localDent),group);}
 const Vec3 rest=p;const int side=rest.x<0?0:1;const auto&groups=deformGroups_[side];const bool wing=std::binary_search(groups.begin(),groups.end(),group)&&std::fabs(rest.x)>.85f&&rest.y>=-1.5f&&rest.y<=1.6f&&rest.z>=-.6f&&rest.z<=1.7f;
 const int region=wing?side:rest.y< -1.5f&&std::binary_search(tailGroups_.begin(),tailGroups_.end(),group)?2:-1;
 if(wing){const auto&b=bend_[side];const float distance=std::min(kSpan,std::fabs(rest.x)-.85f),angle=std::clamp(b.plastic+b.elastic,-.8f,.8f);p=Add(p,Mul(b.direction,.5f*angle*distance*distance/kSpan));}
 // Both fields are sampled in immutable material coordinates. Contact motion
 // cannot move a vertex across the beam mask or change its gravity lever arm.
 if(region>=0){const float coordinate=BeamCoordinate(rest,region);Vec3 shift{};for(const auto&section:sections_[region]){const float transverse=section.flex[0].angle*std::max(0.f,coordinate-section.at[0]),vertical=section.flex[1].angle*std::max(0.f,coordinate-section.at[1]);if(region<2)shift.y+=transverse;else shift.x+=transverse;shift.z+=vertical;}const float length=Length(shift),limit=BeamSpan(region)*.75f;if(length>limit)shift=Mul(shift,limit/length);p=Add(p,shift);}
 return Pose(Add(p,localDent),group);
}
void Model::RememberSection(Vec3 point,int region){
 if(region<0||region>2)return;const float at=BeamCoordinate(point,region,generic_?&layout_:nullptr);if(at<=0||at>=BeamSpan(region,generic_?&layout_:nullptr))return;const int index=std::clamp(int(at*kSectionCount/BeamSpan(region,generic_?&layout_:nullptr)),0,kSectionCount-1);auto&section=sections_[region][index];const auto next=MeasureSection(mesh_.Fragments(),surfaces_,region,at,false,generic_?&layout_:nullptr,generic_?&material_:nullptr);const auto previous0=MeasureSection(mesh_.Fragments(),surfaces_,region,section.at[0],false,generic_?&layout_:nullptr,generic_?&material_:nullptr),previous1=section.at[0]==section.at[1]?previous0:MeasureSection(mesh_.Fragments(),surfaces_,region,section.at[1],false,generic_?&layout_:nullptr,generic_?&material_:nullptr);const std::array<bool,2> replace{{next.inertia[0]<previous0.inertia[0],next.inertia[1]<previous1.inertia[1]}};
 if(replace[0]||replace[1]){const auto original=MeasureRestSection(surfaces_,region,at,generic_?&layout_:nullptr,generic_?&material_:nullptr);for(int axis=0;axis<2;++axis)if(replace[axis]){section.at[axis]=at;section.intactInertia[axis]=original.inertia[axis]*section.reinforcement[axis];}}sectionRevision_=0;
}
void Model::RebuildSections(){
 if(sectionRevision_==mesh_.Revision())return;
 for(int region=0;region<3;++region)for(auto&section:sections_[region]){if(generic_&&layout_.beams[region].span<=.001f)continue;const auto first=MeasureSection(mesh_.Fragments(),surfaces_,region,section.at[0],true,generic_?&layout_:nullptr,generic_?&material_:nullptr),second=section.at[0]==section.at[1]?first:MeasureSection(mesh_.Fragments(),surfaces_,region,section.at[1],true,generic_?&layout_:nullptr,generic_?&material_:nullptr);for(int axis=0;axis<2;++axis){const auto&geometry=axis==0?first:second;section.inertia[axis]=geometry.inertia[axis]*section.reinforcement[axis];section.extent[axis]=geometry.extent[axis];section.mass[axis]=geometry.mass;section.arm[axis]=geometry.arm;}}
 sectionRevision_=mesh_.Revision();
}
bool Model::AdvanceSections(float dt,Vec3 apparentGravity){
 RebuildSections();bool changed=false;
 for(int region=0;region<3;++region)for(auto&section:sections_[region])for(int axis=0;axis<2;++axis){auto&flex=section.flex[axis];const float stiffness=material_.young*section.inertia[axis]/(BeamSpan(region,generic_?&layout_:nullptr)/kSectionCount);if(stiffness<=1e-8f||section.mass[axis]<=1e-8f)continue;const float load=section.arm[axis]*Transverse(apparentGravity,region,axis,generic_?&layout_:nullptr),lever=section.arm[axis]/section.mass[axis];const float omega=std::clamp(std::sqrt(stiffness/std::max(.001f,section.mass[axis]*lever*lever)),.5f,100.f),target=flex.plastic+load/stiffness;const float x=flex.angle-target,a=flex.velocity+omega*x,decay=std::exp(-omega*dt);const float next=std::clamp(target+(x+a*dt)*decay,-.65f,.65f),velocity=(flex.velocity-omega*a*dt)*decay;changed|=std::fabs(next-flex.angle)>1e-6f;
  const float yieldMoment=material_.yield*section.inertia[axis]/std::max(material_.thickness*.5f,section.extent[axis]),elastic=next-flex.plastic,yieldAngle=yieldMoment/stiffness;if(std::fabs(elastic)>yieldAngle){const float plastic=elastic-std::copysign(yieldAngle,elastic);flex.plastic+=plastic;section.crackWork[axis]+=.35f*yieldMoment*std::fabs(plastic);}
  flex.angle=next;flex.velocity=std::fabs(next)>=.65f?0:velocity;
 }
 // Crack growth spends previously dissipated plastic work on actual openings.
 // Two local 12 cm advances per update bound topology work; there is no part
 // health, minimum-area release threshold, or disconnected second graph.
 int advances=0;for(int region=0;region<3&&advances<2;++region)for(auto&section:sections_[region])for(int axis=0;axis<2;++axis){if(advances==2)break;if(section.crackWork[axis]<material_.fractureEnergy*material_.thickness*.01f)continue;const auto lines=SectionLines(mesh_.Fragments(),surfaces_,region,section.at[axis],generic_?&layout_:nullptr);const Ligament*target=nullptr;float longest=0,total=0;for(const auto&line:lines){const float length=Length(Sub(line.b,line.a));total+=length;if(length>longest){longest=length;target=&line;}}if(!target)continue;float radius=std::min(.12f,section.crackWork[axis]/(2*material_.fractureEnergy*material_.thickness));
  for(int attempt=0;attempt<4&&radius>=.005f;++attempt,radius*=.5f){auto staged=mesh_;const auto revision=staged.Revision();if(!staged.ApplyHole({Mul(Add(target->a,target->b),.5f),target->normal,radius},target->group)||staged.Revision()==revision)break;float remaining=0;for(const auto&line:SectionLines(staged.Fragments(),surfaces_,region,section.at[axis],generic_?&layout_:nullptr))remaining+=Length(Sub(line.b,line.a));const float cost=material_.fractureEnergy*material_.thickness*std::max(0.f,total-remaining);if(cost>section.crackWork[axis]+1e-5f)continue;if(cost<=1e-6f)break;mesh_=std::move(staged);section.crackWork[axis]=std::max(0.f,section.crackWork[axis]-cost);sectionRevision_=0;changed=true;++advances;break;}
 }
 return changed;
}
bool Model::TraceComponent(Vec3 from,Vec3 to,float entry,float&closest,Vec3&normal)const{
 if(!initialized_||!Finite(from)||!Finite(to)||!std::isfinite(entry)||!std::isfinite(closest)||entry<0||entry>=closest||closest>1)return false;
 auto boxes=MechanicalKind()?layout_.mechanical.volumes:layout_.airframe;for(int i=0;i<airframemodel::Count;++i)ComponentVolume(i,boxes[i]);auto d=Sub(to,from);bool hit=false;const float p[]={from.x,from.y,from.z},v[]={d.x,d.y,d.z};
 for(int i=0;i<airframemodel::Count;++i){if(ComponentResistance(i)<=0||!ComponentPresent(i))continue;const auto&b=boxes[i];
  // Hinged control material is finite sheet geometry, so its current posed
  // triangles replace the neutral envelope for exposed-component queries.
  if(SheetComponent(i)){for(const auto&f:mesh_.Fragments())if(f.attached&&ControlSurface(surfaces_[f.source],i))for(size_t k=1;k+1<f.polygon.size();++k){const auto a=f.polygon[0],c=f.polygon[k],e=f.polygon[k+1];float t=closest;Vec3 w,n;if(Intersect(from,d,Deform(a.position,f.group),Deform(c.position,f.group),Deform(e.position,f.group),t,w,n)&&t>=entry&&InBox(Weighted(a.position,c.position,e.position,w),b)){closest=t;normal=n;hit=true;}}continue;}
  float near=entry,far=closest;Vec3 n{};bool valid=true;const float lo[]={b.lo.x,b.lo.y,b.lo.z},hi[]={b.hi.x,b.hi.y,b.hi.z};
  for(int axis=0;axis<3;++axis){if(std::fabs(v[axis])<1e-7f){if(p[axis]<lo[axis]||p[axis]>hi[axis])valid=false;continue;}float a=(lo[axis]-p[axis])/v[axis],z=(hi[axis]-p[axis])/v[axis],sign=-1;if(a>z){std::swap(a,z);sign=1;}if(a>near){near=a;n={float(axis==0)*sign,float(axis==1)*sign,float(axis==2)*sign};}far=std::min(far,z);if(near>=far)valid=false;}
  if(valid&&near<closest){closest=near;normal=Length(n)>.1f?n:Mul(d,-1/std::max(1e-6f,Length(d)));hit=true;}
 }return hit;
}
Result Model::Bullet(Vec3 from,Vec3 to,float budget,float limit){
 Result out;if(!initialized_||!Finite(from)||!Finite(to)||!std::isfinite(budget)||budget<=0||budget>10000||!std::isfinite(limit)||limit<=0||limit>1)return out;
 auto d=Sub(to,from);float length=Length(d);if(!std::isfinite(length)||length<1e-6f)return out;
 struct Crossing{float t,incidence;size_t source;Vec3 rest,normal;};std::vector<Crossing> hits;hits.reserve(32);
 for(const auto&f:mesh_.Fragments())if(f.attached)for(size_t k=1;k+1<f.polygon.size();++k){
  auto a=f.polygon[0],b=f.polygon[k],c=f.polygon[k+1];float t=limit;Vec3 w,n;if(!Intersect(from,d,Deform(a.position,f.group),Deform(b.position,f.group),Deform(c.position,f.group),t,w,n))continue;
  const auto rest=Weighted(a.position,b.position,c.position,w);bool duplicate=false;for(const auto&h:hits)if(std::fabs(t-h.t)*length<.001f&&surfaces_[h.source].glass==surfaces_[f.source].glass){duplicate=true;break;}
  if(!duplicate){if(hits.size()==projectilemodel::kLayers)return out;const auto raw=Cross(Sub(b.position,a.position),Sub(c.position,a.position));hits.push_back({t,std::clamp(std::fabs(Dot(n,d)/length),0.f,1.f),f.source,rest,Mul(raw,1/std::max(1e-6f,Length(raw)))});}
 }
 float entry=hits.empty()?limit:hits.front().t;for(const auto&h:hits)entry=std::min(entry,h.t);
 // Missing skin is an aperture, not a shield at the opposite wall. The
 // original surface supplies the aperture's entry coordinate, without charging
 // removed material. Current attached geometry still gates every internal box.
 // This also prevents a box extending slightly beyond its enclosing sheet from
 // absorbing before the projectile has actually entered that sheet.
 for(const auto&s:surfaces_){if(generic_&&!GroupAttached(s.group))continue;float t=entry;Vec3 w,n;if(Intersect(from,d,Deform(s.points[0],s.group),Deform(s.points[1],s.group),Deform(s.points[2],s.group),t,w,n))entry=t;}
 if(entry>=limit){out.valid=true;out.remaining=std::min(budget,1000.f);return out;}
 std::sort(hits.begin(),hits.end(),[](const Crossing&a,const Crossing&b){return a.t<b.t;});std::array<projectilemodel::Layer,projectilemodel::kLayers> layers{};
 for(size_t i=0;i<hits.size();++i){const auto&s=surfaces_[hits[i].source];auto kind=s.glass?projectilemodel::Material::Glass:s.role==Role::Wheel&&definition_.hasWheels?projectilemodel::Material::Rubber:generic_&&MechanicalKind()&&definition_.kind!=Kind::Boat?projectilemodel::Material::Body:projectilemodel::Material::Skin;if(generic_&&material_.skinResistance>1&&s.role!=Role::Wheel)kind=s.glass?projectilemodel::Material::ArmoredGlass:projectilemodel::Material::Armor;layers[i]={hits[i].t,hits[i].incidence,kind,generic_?material_.skinResistance:1};}
 auto volumes=MechanicalKind()?layout_.mechanical.volumes:layout_.airframe;for(int i=0;i<airframemodel::Count;++i){mechanicalmodel::Box box;if(ComponentVolume(i,box))volumes[i]=box;}float resistance[airframemodel::Count],capacity[]={140,140,45,45,160,160,90,90,55,55,55,45,100,60};static const float mechanicalCapacity[]={120,10,40,40,40,40,12,12,12,12,35,100,12,15};for(int i=0;i<airframemodel::Count;++i){if(MechanicalKind())capacity[i]=mechanicalCapacity[i];resistance[i]=!SheetComponent(i)&&ComponentPresent(i)?ComponentResistance(i):0;}
 if(!MechanicalKind()&&originalLift_[0]>0&&AttachedLift(0)<originalLift_[0]*.02f)resistance[airframemodel::FuelL]=resistance[airframemodel::AileronL]=0;
 if(!MechanicalKind()&&originalLift_[1]>0&&AttachedLift(1)<originalLift_[1]*.02f)resistance[airframemodel::FuelR]=resistance[airframemodel::AileronR]=0;
 projectilemodel::Ray ray{from,to,entry,limit,budget,layers.data(),hits.size()};auto absorbed=projectilemodel::Absorb(layout_.mechanical.body,volumes,resistance,capacity,ray);if(!absorbed.valid)return out;
 out.valid=true;out.deposited=absorbed.deposited+absorbed.surfaceLoss;out.remaining=absorbed.remaining;
 auto damage=damage_;for(int i=0;i<airframemodel::Count;++i)if(absorbed.doses[i]>0){damage[i]=Clamp(damage[i]+absorbed.doses[i]);out.changed=true;}
 // A control surface is actual finite skin, not its old broad envelope.
 // Spend only the sheet work already removed by Absorb at a current attached
 // crossing. The original aperture used for entry cannot lend missing material
 // another resistance volume, and posed controls retain their material work.
 for(size_t k=0;k<hits.size();++k)if(absorbed.layerLoss[k]>0&&!surfaces_[hits[k].source].glass)for(int i=0;i<airframemodel::Count;++i)if(SheetComponent(i)&&ControlSurface(surfaces_[hits[k].source],i)&&InBox(hits[k].rest,volumes[i])){const float before=damage[i];damage[i]=Clamp(damage[i]+absorbed.layerLoss[k]/capacity[i]);out.changed|=damage[i]>before;}
 if(!MechanicalKind())damage[airframemodel::EngineR]=damage[airframemodel::EngineL];
 std::array<float,mechanicalmodel::kModules>wheelDoses{};if(!MechanicalKind()&&definition_.hasWheels)for(size_t k=0;k<hits.size();++k)if(surfaces_[hits[k].source].role==Role::Wheel&&absorbed.layerLoss[k]>0){int wheel=0;float closest=std::numeric_limits<float>::infinity();for(int i=0;i<4;++i){const float distance=Dot(Sub(hits[k].rest,definition_.wheels[i]),Sub(hits[k].rest,definition_.wheels[i]));if(distance<closest){closest=distance;wheel=i;}}wheelDoses[mechanicalmodel::kWheelLF+wheel]+=absorbed.layerLoss[k]*.85f/40;wheelDoses[mechanicalmodel::kBrakeLF+wheel]+=absorbed.layerLoss[k]*.15f/12;out.changed=true;}
 auto material=mesh_;const auto revision=mesh_.Revision();for(size_t i=0;i<hits.size();++i)if(absorbed.layerLoss[i]>0&&(!generic_||absorbed.layerRemaining[i]>0)){
   // A penetrating round shatters a car's tempered side/rear pane whole.
   const auto&struck=surfaces_[hits[i].source];if(generic_&&definition_.kind==Kind::Automobile&&struck.glass&&!struck.laminated){if(!material.RemoveSources(TemperedPane(surfaces_,hits[i].source)))return {};++out.holes;continue;}
  // Perforation scales with the original native round budget, with a bounded
  // .075 m radius. Repeated rounds alter the same material rather than a count.
  const float radius=.015f+.0003f*std::min(budget,200.f);const auto&h=hits[i];if(!material.ApplyHole({h.rest,h.normal,radius},surfaces_[h.source].group))return {};++out.holes;
 }
 out.changed|=material.Revision()!=revision;mesh_=std::move(material);std::array<float,airframemodel::Count>doses{};for(int i=0;i<airframemodel::Count;++i)doses[i]=std::max(0.f,damage[i]-damage_[i]);ApplyDoses(doses);if(!MechanicalKind()&&definition_.hasWheels)mechanicalmodel::Apply(mechanical_,wheelDoses);if(mesh_.Revision()!=revision)for(const auto&hit:hits){const int side=Side(surfaces_[hit.source]);RememberSection(hit.rest,generic_?Region(surfaces_[hit.source]):side>=0?side:hit.rest.y< -1.5f?2:-1);}return out;
}
// A yield patch takes a point near its rest centre or near where that centre
// has been pushed: the host reports struck vertices where they are now.
bool Model::YieldPatch(const Dent&d,uint32_t group,Vec3 point){if(!d.yield||d.group!=group)return false;const float r=d.radius*.5f;return Length(Sub(d.point,point))<r||Length(Sub(Add(d.point,Mul(d.direction,d.depth)),point))<r;}
long Model::NearestSurface(Vec3 point)const{
 if(!initialized_||!Finite(point))return -1;
 const aircraftconnectivity::Fragment*nearest=nullptr;Vec3 rest{},normal{};float closest=.35f*.35f;
 for(const auto&f:mesh_.Fragments())if(f.attached&&surfaces_[f.source].role!=Role::Interior)for(size_t k=1;k+1<f.polygon.size();++k){
  auto a=f.polygon[0],b=f.polygon[k],c=f.polygon[k+1];auto x=Deform(a.position,f.group),y=Deform(b.position,f.group),z=Deform(c.position,f.group),e=Sub(y,x),v=Sub(z,x),n=Cross(e,v);float nl=Length(n);if(nl<1e-7f)continue;n=Mul(n,1/nl);float distance=Dot(Sub(point,x),n);auto projected=Sub(point,Mul(n,distance)),q=Sub(projected,x);float ee=Dot(e,e),vv=Dot(v,v),ev=Dot(e,v),denom=ee*vv-ev*ev;if(denom<1e-10f)continue;float u=(Dot(q,e)*vv-Dot(q,v)*ev)/denom,w=(Dot(q,v)*ee-Dot(q,e)*ev)/denom;if(u<-.01f||w<-.01f||u+w>1.01f)continue;if(distance*distance<closest){closest=distance*distance;nearest=&f;rest=Weighted(a.position,b.position,c.position,{1-u-w,u,w});auto rn=Cross(Sub(b.position,a.position),Sub(c.position,a.position));normal=Mul(rn,1/std::max(1e-6f,Length(rn)));}
 }
 return nearest?long(nearest->source):-1;
}
float Model::YieldForce(size_t surface,Vec3 point)const{
 if(!initialized_||surface>=surfaces_.size()||!Finite(point))return 0;const auto&s=surfaces_[surface];
 const float sheet=8*material_.yield*material_.thickness*material_.thickness*material_.skinResistance/.65f,structure=kBodyCrushForce*std::clamp(mass_/kCrushMass,.4f,8.f)*material_.skinResistance/.65f;
 for(const auto&dent:dents_)if(YieldPatch(dent,s.group,point)&&dent.depth>=.55f*dent.radius)return structure;
 // Bumper beams sit at or above axle height; material over a quarter tyre
 // radius below the wheel-centre plane is lower trim with no beam behind it.
 float wheel=0;for(auto w:definition_.wheels)wheel+=w.z*.25f;const float radius=std::max(.1f,wheel-definition_.bounds.lo.z);
 const bool lowerEdge=definition_.hasWheels&&(definition_.kind==Kind::Automobile||definition_.kind==Kind::Bike)&&point.z<wheel-.25f*radius;
 return !s.structural||s.glass||s.role==Role::Door||s.role==Role::Wheel||lowerEdge?sheet:structure;
}
int Model::YieldZone(size_t surface,Vec3 point)const{
 if(!initialized_||surface>=surfaces_.size()||!Finite(point))return -1;const auto&s=surfaces_[surface];int zone=0;
 for(const auto&dent:dents_)if(YieldPatch(dent,s.group,point)&&dent.depth>=.55f*dent.radius)zone|=2;
 float wheel=0;for(auto w:definition_.wheels)wheel+=w.z*.25f;const float radius=std::max(.1f,wheel-definition_.bounds.lo.z);
 const bool lower=definition_.hasWheels&&(definition_.kind==Kind::Automobile||definition_.kind==Kind::Bike)&&point.z<wheel-.25f*radius;if(lower)zone|=4;
 if(!(!s.structural||s.glass||s.role==Role::Door||s.role==Role::Wheel||lower))zone|=1;return zone;
}
float Model::YieldForce(Vec3 point)const{const long surface=NearestSurface(point);return surface<0?0:YieldForce(size_t(surface),point);}
Result Model::Yield(size_t surface,Vec3 point,Vec3 inward,float displacement,float force,uint32_t now){
 Result out;if(!initialized_||surface>=surfaces_.size()||!Finite(point)||!Finite(inward)||!std::isfinite(displacement)||displacement<=0||displacement>.5f||!std::isfinite(force)||force<=0)return out;const float l=Length(inward);if(l<1e-6f)return out;
 const auto&s=surfaces_[surface];inward=RestDirection(Mul(inward,1/l),s.group);out.valid=true;
 Dent*patch=nullptr;for(auto&dent:dents_)if(YieldPatch(dent,s.group,point)){patch=&dent;break;}
 if(!patch){if(dents_.size()>=128)return out;auto n=Cross(Sub(s.points[1],s.points[0]),Sub(s.points[2],s.points[0]));const float nl=Length(n);n=nl>1e-9f?Mul(n,1/nl):inward;
  try{dents_.push_back({point,n,inward,.25f,0,s.group});}catch(const std::bad_alloc&){return out;}patch=&dents_.back();patch->yield=true;patch->yieldAt=now+1;}
 float push=displacement;if(patch->yieldAt==now){push=std::max(0.f,displacement-patch->yieldPush);patch->yieldPush=std::max(patch->yieldPush,displacement);}else{patch->yieldAt=now;patch->yieldPush=displacement;}
 const float room=std::max(0.f,.6f*patch->radius-patch->depth),applied=std::min(push,room);if(applied<=0)return out;
 const auto moved=Add(Mul(patch->direction,patch->depth),Mul(inward,applied));const float length=Length(moved);patch->depth=std::min(.6f*patch->radius,length);if(length>1e-7f)patch->direction=Mul(moved,1/length);
 out.changed=true;out.deposited=force*applied;++deformationRevision_;return out;
}
Result Model::Yield(Vec3 point,Vec3 inward,float displacement,float force,uint32_t now){const long surface=NearestSurface(point);return surface<0?Result{}:Yield(size_t(surface),point,inward,displacement,force,now);}
Result Model::ContactEnergy(Vec3 point,Vec3 inward,float joules,uint32_t now,float dt){
 if(!initialized_||!std::isfinite(joules)||joules<=0)return {};const float energy=std::min(100000.f,joules);
 // Equivalent reference speed affects only the <=1 m local crushing reach.
 // All material work comes from the accepted native energy, not body deltaV.
 // Mechanical bodies crush as deep as this vehicle's own equivalent impact
 // speed, from the uncapped accepted work, so equal work per kilogram
 // reaches equally far into light and heavy cars.
 if(MechanicalKind()){const float uncapped=std::min(joules,2e6f),equivalent=std::sqrt(2*uncapped/std::max(1.f,mass_));return SpendContact(point,inward,energy,std::min(1.f,.08f+equivalent*.035f),now,dt,uncapped/energy);}
 const float effectiveMass=std::min(120.f,mass_*.035f),speed=std::sqrt(2*energy/(std::max(1.f,effectiveMass)*.55f));
 return SpendContact(point,inward,energy,std::min(1.f,.08f+speed*.035f),now,dt);
}
Result Model::Contact(Vec3 point,Vec3 inward,float deltaV,uint32_t now,float dt){
 if(!initialized_||!std::isfinite(deltaV)||deltaV<=0||deltaV>500)return {};const float effectiveMass=std::min(120.f,mass_*.035f),energy=std::min(100000.f,.5f*effectiveMass*deltaV*deltaV*.55f);
 return SpendContact(point,inward,energy,std::min(1.f,.08f+deltaV*.035f),now,dt);
}
Result Model::SpendContact(Vec3 point,Vec3 inward,float energy,float reach,uint32_t now,float dt,float crush){
 Result out;if(!initialized_||!Finite(point)||!Finite(inward)||!std::isfinite(energy)||energy<=0||energy>100000||!std::isfinite(reach)||reach<=0||reach>1||!std::isfinite(dt)||dt<=0||dt>1||!std::isfinite(crush)||crush<1)return out;float directionLength=Length(inward);if(directionLength<1e-6f)return out;inward=Mul(inward,1/directionLength);
 const aircraftconnectivity::Fragment*nearest=nullptr;Vec3 rest{},normal{};float closest=.35f*.35f;
 for(const auto&f:mesh_.Fragments())if(f.attached&&surfaces_[f.source].role!=Role::Interior)for(size_t k=1;k+1<f.polygon.size();++k){
  auto a=f.polygon[0],b=f.polygon[k],c=f.polygon[k+1];auto x=Deform(a.position,f.group),y=Deform(b.position,f.group),z=Deform(c.position,f.group),e=Sub(y,x),v=Sub(z,x),n=Cross(e,v);float nl=Length(n);if(nl<1e-7f)continue;n=Mul(n,1/nl);float distance=Dot(Sub(point,x),n);auto projected=Sub(point,Mul(n,distance)),q=Sub(projected,x);float ee=Dot(e,e),vv=Dot(v,v),ev=Dot(e,v),denom=ee*vv-ev*ev;if(denom<1e-10f)continue;float u=(Dot(q,e)*vv-Dot(q,v)*ev)/denom,w=(Dot(q,v)*ee-Dot(q,e)*ev)/denom;if(u<-.01f||w<-.01f||u+w>1.01f)continue;if(distance*distance<closest){closest=distance*distance;nearest=&f;rest=Weighted(a.position,b.position,c.position,{1-u-w,u,w});auto rn=Cross(Sub(b.position,a.position),Sub(c.position,a.position));normal=Mul(rn,1/std::max(1e-6f,Length(rn)));}
 }
 if(!nearest)return out;out.valid=true;const auto surface=surfaces_[nearest->source];inward=RestDirection(inward,surface.group);inward=Mul(inward,1/std::max(1e-6f,Length(inward)));const int side=Side(surface);const auto centre=Mul(Add(layout_.mechanical.body.lo,layout_.mechanical.body.hi),.5f);const int region=side>=0?side:generic_?(rest.y<centre.y?2:3):rest.y< -3?2:rest.y>1.5f?3:rest.x<0?4:5;const bool materialPatch=!surface.structural||surface.glass||surface.role==Role::Door||surface.role==Role::Wheel;auto part=materialPatch?std::lower_bound(partContacts_.begin(),partContacts_.end(),surface.group,[](const PartEpisode&e,uint32_t group){return e.group<group;}):partContacts_.end();auto&episode=part!=partContacts_.end()&&part->group==surface.group?part->episode:contacts_[region];
 if(!episode.seen||now<episode.last||now-episode.last>350){episode={point,now,0,true};}episode.last=now;
 const float work=std::max(0.f,energy-episode.peak);episode.peak=std::max(episode.peak,energy);if(work<=0)return out;out.deposited=work;
 // Native impulse work has one bounded allocation: 80% shell bending/tear,
 // 20% local internal crushing. Work is never copied into every module.
 const float shellWork=work*.8f;
 auto volumes=MechanicalKind()?layout_.mechanical.volumes:layout_.airframe;for(int i=0;i<airframemodel::Count;++i){mechanicalmodel::Box box;if(ComponentVolume(i,box))volumes[i]=box;}float resistance[airframemodel::Count],capacity[]={140,140,45,45,160,160,90,90,55,55,55,45,100,60};static const float mechanicalCapacity[]={120,10,40,40,40,40,12,12,12,12,35,100,12,15};for(int i=0;i<airframemodel::Count;++i){if(MechanicalKind())capacity[i]=mechanicalCapacity[i];resistance[i]=ComponentPresent(i)?ComponentResistance(i):0;}
 projectilemodel::Ray load{rest,Add(rest,Mul(inward,reach)),0,1,work*.2f,nullptr,0,4};if(surface.role==Role::Wheel&&!MechanicalKind()&&definition_.hasWheels){int wheel=0;float closest=std::numeric_limits<float>::infinity();for(int i=0;i<4;++i){const float distance=Dot(Sub(rest,definition_.wheels[i]),Sub(rest,definition_.wheels[i]));if(distance<closest){closest=distance;wheel=i;}}const float accepted=std::min(1000.f,work*.2f);std::array<float,mechanicalmodel::kModules>doses{};doses[mechanicalmodel::kWheelLF+wheel]=accepted*.85f/40;doses[mechanicalmodel::kBrakeLF+wheel]=accepted*.15f/12;mechanicalmodel::Apply(mechanical_,doses,inward);out.changed|=accepted>0;}else if(MechanicalKind()){std::array<bool,airframemodel::Count> present{};for(int i=0;i<airframemodel::Count;++i)present[i]=resistance[i]>0;const auto doses=Crush(rest,inward,reach,work*.2f*crush,volumes,present,mechanical_.damage,std::clamp(mass_/kCrushMass,.4f,8.f)*material_.skinResistance);for(auto dose:doses)out.changed|=dose>0;ApplyDoses(doses,inward,true);}else{auto absorbed=projectilemodel::Absorb(layout_.mechanical.body,volumes,resistance,capacity,load);if(absorbed.valid){for(auto dose:absorbed.doses)out.changed|=dose>0;ApplyDoses(absorbed.doses,inward,true);}}
 if(materialPatch){
  // Nonstructural sheets spend their own finite local work. Glass fractures
  // brittly, reinforced rubber punctures, and metal divides work between
  // plastic plate bending and tearing. None supplies chassis/wing stiffness.
  const bool glass=surface.glass,rubber=surface.role==Role::Wheel;const float armor=glass?material_.skinResistance:rubber?1:material_.skinResistance;
  const float initiation=(glass?3.f:rubber?12.f:8.f)*armor,areaWork=glass?1500.f*armor:rubber?(MechanicalKind()?kRubberTearWork:6000.f):material_.yield*material_.thickness*.12f,fracture=std::max(0.f,shellWork*(glass||rubber?1.f:.35f)-initiation),radius=std::min(1.2f,std::sqrt(fracture/(3.14159265359f*areaWork)));
  if(radius>.005f){std::vector<size_t>sources;try{for(size_t i=0;i<surfaces_.size();++i){const auto&s=surfaces_[i];if(s.group==surface.group&&s.role!=Role::Interior&&s.glass==glass&&(s.role==Role::Wheel)==rubber&&(glass||rubber||s.structural==surface.structural))sources.push_back(i);}}catch(const std::bad_alloc&){return out;}const auto revision=mesh_.Revision();if(mesh_.ApplyHole({rest,normal,radius},surface.group,sources)&&mesh_.Revision()!=revision){out.changed=true;out.holes=1;
    // The tear inserts actual source-interpolated rim vertices. Their local
    // plastic plate field is published by the same Deform path to RW and COL.
    if(!glass&&!rubber){const float patchRadius=std::clamp(reach*.75f+radius*2,.08f,.5f),depth=std::min(.6f*patchRadius,shellWork*.65f/std::max(.001f,8*material_.yield*material_.thickness*material_.thickness));Dent*patch=nullptr;for(auto&dent:dents_)if(dent.group==surface.group&&Length(Sub(dent.point,rest))<dent.radius*.5f&&Dot(dent.normal,normal)>.9f){patch=&dent;break;}if(!patch&&dents_.size()<128){try{dents_.push_back({rest,normal,inward,patchRadius,0,surface.group});patch=&dents_.back();}catch(const std::bad_alloc&){}}
     if(patch){const auto displacement=Add(Mul(patch->direction,patch->depth),Mul(inward,depth));const float length=Length(displacement);patch->radius=std::max(patch->radius,patchRadius);patch->depth=std::min(.6f*patch->radius,length);if(length>1e-7f)patch->direction=Mul(displacement,1/length);++deformationRevision_;}
    }
   }}return out;
 }
 float tear=0;if(side>=0&&MechanicalKind()){
  // A body half rotates about its root: the intact root section, not the
  // weakest torn station at the contact, carries this whole-half bend.
  RebuildSections();const int axis=std::fabs(inward.z)>std::fabs(inward.y)?1:0;const auto&root=sections_[side][0];const float span=BeamSpan(side,&layout_),inertia=std::max(1e-9f,root.intactInertia[axis]),stiffness=std::max(.001f,material_.young*inertia/span),yieldMoment=std::max(.001f,material_.yield*inertia/std::max(material_.thickness*.5f,root.extent[axis])),yieldAngle=yieldMoment/stiffness;auto&bend=bend_[side];
  const float existing=.5f*stiffness*bend.elastic*bend.elastic,total=existing+shellWork,yieldWork=.5f*stiffness*yieldAngle*yieldAngle;const Vec3 elastic=Add(Mul(bend.direction,bend.elastic),Mul(inward,std::max(0.f,std::min(yieldAngle,std::sqrt(2*total/stiffness))-std::fabs(bend.elastic))));const float magnitude=Length(elastic);if(magnitude>1e-9f){bend.direction=Mul(elastic,1/magnitude);bend.elastic=std::min(yieldAngle,magnitude);}bend.velocity=0;
  const float plasticWork=std::max(0.f,total-yieldWork),plastic=std::min(std::max(0.f,kBendPerEpisode-episode.bend),plasticWork*.65f/yieldMoment);episode.bend+=plastic;bend.plasticVector=Add(bend.plasticVector,Mul(inward,plastic));const float length=Length(bend.plasticVector);if(length>.65f)bend.plasticVector=Mul(bend.plasticVector,.65f/length);bend.plastic=std::min(.65f,length);
  // Work the bounded whole-half bend cannot take crumples the struck sheet.
  const float crushed=std::max(0.f,plasticWork-plastic*yieldMoment/.65f);if(crushed>0){const float patchRadius=std::clamp(reach*.75f,.08f,.5f),depth=std::min(.6f*patchRadius,crushed*.65f/(kBodyCrushForce*std::clamp(mass_/kCrushMass,.4f,8.f)*material_.skinResistance));Dent*patch=nullptr;for(auto&dent:dents_)if(dent.group==surface.group&&Length(Sub(dent.point,rest))<dent.radius*.5f&&Dot(dent.normal,normal)>.9f){patch=&dent;break;}if(!patch&&dents_.size()<128){try{dents_.push_back({rest,normal,inward,patchRadius,0,surface.group});patch=&dents_.back();}catch(const std::bad_alloc&){}}
   if(patch){const auto displacement=Add(Mul(patch->direction,patch->depth),Mul(inward,depth));const float shift=Length(displacement);patch->radius=std::max(patch->radius,patchRadius);patch->depth=std::min(.6f*patch->radius,shift);if(shift>1e-7f)patch->direction=Mul(displacement,1/shift);}}
  tear=plasticWork*.35f;out.changed=true;++deformationRevision_;
 }else if(side>=0){RebuildSections();float strength=1;const int axis=std::fabs(inward.z)>std::fabs(inward.y)?1:0;for(const auto&section:sections_[side])if(section.mass[axis]>1e-6f&&section.intactInertia[axis]>1e-12f)strength=std::min(strength,section.inertia[axis]/section.intactInertia[axis]);strength=std::clamp(strength,.00001f,1.f);const auto&localSection=sections_[side][std::clamp(int(BeamCoordinate(rest,side,generic_?&layout_:nullptr)*kSectionCount/BeamSpan(side,generic_?&layout_:nullptr)),0,kSectionCount-1)];const float stiffness=generic_?std::max(.001f,material_.young*localSection.inertia[axis]/BeamSpan(side,&layout_)):kStiffness*strength,yieldMoment=generic_?std::max(.001f,material_.yield*localSection.inertia[axis]/std::max(material_.thickness*.5f,localSection.extent[axis])):kYieldMoment*strength,yieldAngle=generic_?yieldMoment/stiffness:kYieldAngle;auto&bend=bend_[side];bend.direction=inward;const float existing=.5f*stiffness*bend.elastic*bend.elastic,total=existing+shellWork,yieldWork=.5f*stiffness*yieldAngle*yieldAngle;
  bend.elastic=std::min(yieldAngle,std::sqrt(2*total/stiffness));bend.velocity=0;const float plasticWork=std::max(0.f,total-yieldWork);bend.plastic=std::min(.65f,bend.plastic+plasticWork*.65f/yieldMoment);tear=plasticWork*.35f;out.changed=true;++deformationRevision_;
 }else tear=std::max(0.f,shellWork-1500);
 // Local tearing only: remove an actual material opening. Connectivity decides
 // what releases; neither a health threshold nor a half-plane removes a part.
 if(tear>0){const float radius=std::min(1.2f,std::sqrt(tear/(3.14159265359f*(generic_?material_.yield*material_.thickness*.12f:kTearWorkPerArea))));if(radius>.015f){auto before=mesh_.Revision();if(mesh_.ApplyHole({rest,normal,radius},surface.group)&&mesh_.Revision()!=before){out.changed=true;out.holes=1;RememberSection(rest,generic_?Region(surface):side>=0?side:rest.y< -1.5f?2:-1);}}}
 return out;
}
void Model::Tick(float dt,bool running,Vec3 apparentGravity){
 if(!initialized_||!std::isfinite(dt)||dt<=0||dt>1||!Finite(apparentGravity)||Length(apparentGravity)>2000)return;const float omega=std::sqrt(kStiffness/(std::max(1.f,mass_*.035f)*kSpan*kSpan/3)),gamma=omega*.35f,frequency=omega*std::sqrt(1-.35f*.35f),decay=std::exp(-gamma*dt),c=std::cos(frequency*dt),s=std::sin(frequency*dt);
 for(size_t index=0;index<bend_.size();++index){auto&b=bend_[index];const float x=b.elastic,v=b.velocity;if(std::fabs(x)+std::fabs(v)<1e-7f)continue;float w=omega,g=gamma,f=frequency,de=decay,co=c,si=s;if(generic_){const auto&section=sections_[index][0];const float span=BeamSpan(int(index),&layout_),stiffness=material_.young*std::max(section.intactInertia[0],section.intactInertia[1])/span;w=std::clamp(std::sqrt(stiffness/std::max(.001f,section.mass[0]*span*span/3)),.5f,100.f);g=w*.35f;f=w*std::sqrt(1-.35f*.35f);de=std::exp(-g*dt);co=std::cos(f*dt);si=std::sin(f*dt);}const float q=(v+g*x)/f;b.elastic=de*(x*co+q*si);b.velocity=de*((-g*x+f*q)*co+(-g*q-f*x)*si);++deformationRevision_;}
 if(AdvanceSections(dt,apparentGravity))++deformationRevision_;
 if(MechanicalKind()){mechanicalmodel::Tick(mechanical_,dt,running,running?1:0,0);fuel_=mechanical_.fuel;heat_=mechanical_.heat;return;}
 fuel_=std::max(0.f,fuel_-dt*(std::max(damage_[airframemodel::FuelL],damage_[airframemodel::FuelR])*.02f+(running?.00012f:0)));const float target=running?.4f+damage_[airframemodel::EngineL]:.2f;heat_+=(target-heat_)*(1-std::exp(-dt*.08f));mechanical_.fuel=fuel_;mechanical_.heat=heat_;
}
bool Model::TransferDetached(std::vector<aircraftconnectivity::Fragment>&out){
 try{std::vector<aircraftconnectivity::Fragment> released;for(const auto&f:mesh_.Fragments())if(!f.attached)released.push_back(f);if(released.empty()){out.clear();return true;}auto staged=mesh_;if(!staged.DiscardDetached())return false;mesh_=std::move(staged);out.swap(released);return true;}catch(const std::bad_alloc&){return false;}
}
void Model::Burn(float dt){if(!initialized_||!std::isfinite(dt)||dt<=0||dt>1)return;if(MechanicalKind()){std::array<float,airframemodel::Count>doses{};for(int i:{mechanicalmodel::kEngine,mechanicalmodel::kFuel,mechanicalmodel::kElectrical})doses[i]=.02f*dt;ApplyDoses(doses);return;}std::array<float,airframemodel::Count>doses{};for(int i:{airframemodel::EngineL,airframemodel::EngineR,airframemodel::FuelL,airframemodel::FuelR,airframemodel::Electrical})doses[i]=.02f*dt;ApplyDoses(doses);}
airframemodel::Effects Model::Effects()const{
 airframemodel::Effects e;if(!initialized_)return e;e.managed=true;if(MechanicalKind()){e.power=mechanicalmodel::Power(mechanical_);return e;}const bool engineAttached=!originalComponents_[airframemodel::EngineL]||ComponentPresent(airframemodel::EngineL);e.power=fuel_>0&&engineAttached?1-damage_[airframemodel::EngineL]:0;float lift[2];for(int side=0;side<2;++side)lift[side]=originalLift_[side]>1e-7f?Clamp(AttachedLift(side)/originalLift_[side]):1;
 e.leftLift=lift[0];e.rightLift=lift[1];const float hydraulic=1-.9f*damage_[airframemodel::Hydraulics];const float elevator=originalControls_[0]>1e-7f?Clamp(AttachedControl(Role::Elevator)/originalControls_[0]):1,rudder=originalControls_[1]>1e-7f?Clamp(AttachedControl(Role::Rudder)/originalControls_[1]):1;e.pitch=elevator*(1-damage_[airframemodel::Elevator])*hydraulic;e.yaw=rudder*(1-damage_[airframemodel::Rudder])*hydraulic;e.roll=(lift[0]+lift[1])*.5f*(1-(damage_[airframemodel::AileronL]+damage_[airframemodel::AileronR])*.5f)*hydraulic;
 if(generic_&&definition_.kind==Kind::Helicopter){auto rotorArea=[&](Role role){float area=0;for(const auto&s:surfaces_)if(s.role==role)area+=Length(Cross(Sub(s.points[1],s.points[0]),Sub(s.points[2],s.points[0])))*.5f;return area>1e-7f?Clamp(AttachedControl(role)/area):1;};e.rotor=rotorArea(Role::MainRotor)*((layout_.airframeEnabled[airframemodel::MainRotor]&&!ComponentPresent(airframemodel::MainRotor))?0:1-damage_[airframemodel::MainRotor]);e.antiTorque=rotorArea(Role::TailRotor)*((layout_.airframeEnabled[airframemodel::TailRotor]&&!ComponentPresent(airframemodel::TailRotor))?0:1-damage_[airframemodel::TailRotor]);}
 e.drag=std::min(1.f,(2-lift[0]-lift[1])*.5f+.12f*(std::fabs(bend_[0].plastic+bend_[0].elastic)+std::fabs(bend_[1].plastic+bend_[1].elastic)));return e;
}
Vec3 Model::FireSource()const{
 if(!initialized_)return {};int module=airframemodel::EngineL;if(std::max(damage_[airframemodel::FuelL],damage_[airframemodel::FuelR])>=.3f)module=damage_[airframemodel::FuelL]>damage_[airframemodel::FuelR]?airframemodel::FuelL:airframemodel::FuelR;
 if(MechanicalKind())module=mechanical_.damage[mechanicalmodel::kFuel]>=.3f?mechanicalmodel::kFuel:mechanicalmodel::kEngine;const auto box=MechanicalKind()?layout_.mechanical.volumes[module]:layout_.airframe[module];const Vec3 source=Mul(Add(box.lo,box.hi),.5f);float closest=std::numeric_limits<float>::infinity();uint32_t group=0;bool found=false;
 for(const auto&f:mesh_.Fragments())if(f.attached&&surfaces_[f.source].structural&&!surfaces_[f.source].glass){Vec3 centre{};for(const auto&p:f.polygon)centre=Add(centre,p.position);centre=Mul(centre,1.f/float(f.polygon.size()));const float distance=Dot(Sub(centre,source),Sub(centre,source));if(distance<closest){closest=distance;group=f.group;found=true;}}
 return found?Deform(source,group):source;
}
firemodel::Input Model::Fire(bool running)const{firemodel::Input f;if(MechanicalKind()){f.fuel=mechanical_.fuel;f.heat=mechanical_.heat;f.engine=mechanical_.damage[mechanicalmodel::kEngine];f.electrical=mechanical_.damage[mechanicalmodel::kElectrical];f.breach=mechanical_.damage[mechanicalmodel::kFuel];f.running=running;return f;}f.fuel=fuel_;f.heat=heat_;f.engine=damage_[airframemodel::EngineL];f.electrical=damage_[airframemodel::Electrical];f.breach=std::max(damage_[airframemodel::FuelL],damage_[airframemodel::FuelR]);f.running=running&&(!originalComponents_[airframemodel::EngineL]||ComponentPresent(airframemodel::EngineL));return f;}
}

// Original Dodo DFF -> prepared native-shaped source triangles -> canonical
// material model. Every cut below is a ray through surviving actual material.
#define DODO_MODEL_NO_MAIN
#include "test_dodomodel.cpp"
#include "../src/aircraftbodymodel.h"
#include <array>
#include <chrono>
#include <string>

namespace bodyasset {
using aircraftbodymodel::Model;
using aircraftbodymodel::Role;
using aircraftbodymodel::Surface;
Vec3 V(CVector p){return {p.x,p.y,p.z};}
Vec3 AddV(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 SubV(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 MulV(Vec3 a,float t){return {a.x*t,a.y*t,a.z*t};}
float DotV(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3 CrossV(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float LengthV(Vec3 a){return std::sqrt(DotV(a,a));}
struct Asset {
 std::vector<Surface> surfaces;
 std::vector<std::string> names;
 std::array<uint32_t,3> wheels{};
};
Role Classify(const char *name,const std::array<Vec3,3> &p){
 if(std::strstr(name,"tire"))return Role::Wheel;
 if(std::strstr(name,"cabin")||std::strstr(name,"dashboard")||std::strstr(name,"yoke"))return Role::Interior;
 if(std::strstr(name,"flap_l"))return Role::WingLeft;
 if(std::strstr(name,"flap_r"))return Role::WingRight;
 if(std::strstr(name,"stab")||std::strstr(name,"elevator"))return Role::Elevator;
 if(std::strstr(name,"rudder"))return Role::Rudder;
 if(std::strcmp(name,"dodo_hull"))return Role::Body;
 Vec3 c=MulV(AddV(AddV(p[0],p[1]),p[2]),1.f/3);const float x=std::fabs(c.x);
 const bool skin=x>.85f&&c.y> -1&&c.y<1.2f&&c.z>.8f;
 const bool brace=x>.85f&&c.y>.5f&&c.y<1.1f&&c.z>-.5f&&c.z<.8f;
 if(skin||brace)return c.x<0?Role::WingLeft:Role::WingRight;
 if(c.y< -3.5f)return c.z>.15f?Role::Rudder:Role::Elevator;
 return Role::Body;
}
Asset Read(Clump &clump){
 Asset out;out.names.resize(clump.atomics.size());
 for(size_t group=0;group<clump.atomics.size();++group){auto *a=clump.atomics[group];if(!(a->flags&4))continue;const char *name=a->frame->name;out.names[group]=name;auto *g=a->geometry;
  for(const auto &t:g->triangles){Surface f;f.group=uint32_t(group);for(int i=0;i<3;++i)f.points[i]=V(ModelPoint(a->frame,g->vertices[t.v[i]]));f.role=Classify(name,f.points);f.glass=g->materials[t.material]->color.a<255;f.structural=!f.glass&&f.role!=Role::Interior&&f.role!=Role::Wheel;out.surfaces.push_back(f);}
  for(int wheel=0;wheel<3;++wheel)if(!std::strcmp(name,wheel==0?"dodo_tire_n":(wheel==1?"dodo_tire_l":"dodo_tire_r")))out.wheels[wheel]=uint32_t(group);
 }return out;
}
void Bounds(const Model &model){
 CHECK(model.Fragments().size()<=16384);size_t vertices=0;for(const auto &f:model.Fragments()){CHECK(f.source<model.Surfaces().size());CHECK(f.group==model.Surfaces()[f.source].group);CHECK(f.polygon.size()>=3&&f.polygon.size()<=32);vertices+=f.polygon.size();for(auto p:f.polygon){CHECK(std::isfinite(p.position.x)&&std::isfinite(p.position.y)&&std::isfinite(p.position.z));CHECK(std::fabs(p.weights.x+p.weights.y+p.weights.z-1)<.0001f);}}CHECK(vertices<=65535);
}
float Area(const std::vector<skinmodel::Sample> &p){float area=0;for(size_t i=1;i+1<p.size();++i)area+=LengthV(CrossV(SubV(p[i].position,p[0].position),SubV(p[i+1].position,p[0].position)))*.5f;return area;}
float RegionArea(const Model &model,const skinmodel::Cut &region,bool attachedOnly=true){float area=0;for(const auto &f:model.Fragments())if(!attachedOnly||f.attached)area+=Area(skinmodel::Slice(f.polygon,region));return area;}
float WheelArea(const Model &model,uint32_t group){float area=0;for(const auto &f:model.Fragments())if(f.group==group&&f.attached)area+=Area(f.polygon);return area;}
void WheelsSame(const Model &model,const Asset &asset,const std::array<float,3> &before){for(int i=0;i<3;++i){CHECK(std::fabs(WheelArea(model,asset.wheels[i])-before[i])<.00001f);for(const auto &s:asset.surfaces)if(s.group==asset.wheels[i])for(auto p:s.points)CHECK(LengthV(SubV(model.Deform(p,s.group),p))<.000001f);}}
std::array<float,3> Wheels(const Model &model,const Asset &asset){return {WheelArea(model,asset.wheels[0]),WheelArea(model,asset.wheels[1]),WheelArea(model,asset.wheels[2])};}
struct Shot {Vec3 centre,normal;};
std::vector<Shot> Band(const Asset &asset,int axis,float coordinate){
 std::vector<Shot> shots;for(const auto &f:asset.surfaces){if(!f.structural)continue;std::vector<Vec3> cross;for(int i=0;i<3;++i){const auto a=f.points[i],b=f.points[(i+1)%3];const float ca=axis==0?a.x:a.y,cb=axis==0?b.x:b.y;if((ca<coordinate&&cb>coordinate)||(ca>coordinate&&cb<coordinate))cross.push_back(AddV(a,MulV(SubV(b,a),(coordinate-ca)/(cb-ca))));}if(cross.size()!=2)continue;
  auto normal=CrossV(SubV(f.points[1],f.points[0]),SubV(f.points[2],f.points[0]));const auto length=LengthV(normal);if(length<.00000001f)continue;normal=MulV(normal,1/length);const int steps=std::max(1,int(std::ceil(LengthV(SubV(cross[1],cross[0]))/.05f)));
  for(int step=0;step<=steps;++step){const auto centre=AddV(cross[0],MulV(SubV(cross[1],cross[0]),float(step)/steps));bool duplicate=false;for(auto s:shots)duplicate|=LengthV(SubV(s.centre,centre))<.02f&&std::fabs(DotV(s.normal,normal))>.99f;if(!duplicate)shots.push_back({centre,normal});}
 }return shots;
}
void Fire(Model &model,const std::vector<Shot> &shots){for(const auto &s:shots){const auto result=model.Bullet(AddV(s.centre,MulV(s.normal,.3f)),AddV(s.centre,MulV(s.normal,-.3f)),200);CHECK(result.valid);}Bounds(model);}
// Rounds aimed into earlier holes pass through them. Close a cut by aiming at
// the surviving cross-section material, as an aimed stream of rounds would.
// The loop bound detects a failed fixture; release still requires disconnection.
bool SealBand(Model &model,int axis,float coordinate,const skinmodel::Cut &released,bool root=false){
 for(int attempt=0;attempt<4096&&RegionArea(model,released)>.000001f;++attempt){
  bool found=false;Shot target{};float longest=.00002f;
  for(const auto &fragment:model.Fragments()){
   if(!fragment.attached||!model.Surfaces()[fragment.source].structural)continue;std::vector<Vec3> intersections;Vec3 centre{},normal{};
   for(size_t i=0;i<fragment.polygon.size();++i){auto a=fragment.polygon[i].position,b=fragment.polygon[(i+1)%fragment.polygon.size()].position;centre=AddV(centre,a);const float ca=axis==0?a.x:a.y,cb=axis==0?b.x:b.y;if(std::fabs(ca-coordinate)<.000001f)intersections.push_back(a);if((ca<coordinate&&cb>coordinate)||(ca>coordinate&&cb<coordinate))intersections.push_back(AddV(a,MulV(SubV(b,a),(coordinate-ca)/(cb-ca))));}
   if(intersections.size()<2)continue;centre=MulV(centre,1.f/fragment.polygon.size());for(size_t i=1;i+1<fragment.polygon.size();++i)normal=AddV(normal,CrossV(SubV(fragment.polygon[i].position,fragment.polygon[0].position),SubV(fragment.polygon[i+1].position,fragment.polygon[0].position)));const auto length=LengthV(normal);if(length<.000000001f)continue;normal=MulV(normal,1/length);
   for(size_t i=0;i<intersections.size();++i)for(size_t j=0;j<i;++j){const auto span=LengthV(SubV(intersections[i],intersections[j]));if(span<=longest)continue;auto aim=MulV(AddV(intersections[i],intersections[j]),.5f);if(root&&(aim.y< -1||aim.y>1.2f||aim.z<-.55f))continue;aim=AddV(MulV(aim,.98f),MulV(centre,.02f));target={aim,normal};longest=span;found=true;}
  }
  if(!found)return false;const auto before=model.Revision();const auto hit=model.Bullet(AddV(target.centre,MulV(target.normal,.3f)),AddV(target.centre,MulV(target.normal,-.3f)),200);if(!hit.valid||model.Revision()==before)return false;
 }
 Bounds(model);return RegionArea(model,released)<.000001f;
}
void Contact(const Asset &asset){
 Model model;const bool initialized=model.Initialize(asset.surfaces,800);CHECK(initialized);if(!initialized)return;const auto wheels=Wheels(model,asset);const auto result=model.Contact({2.58f,.43f,1.04f},{0,-1,0},9.44f,1000,1.f/60);CHECK(result.valid&&result.changed);
 float tipMotion=0;for(const auto &s:asset.surfaces)if(s.role==Role::WingRight)for(auto p:s.points)if(p.x>4.5f)tipMotion=std::max(tipMotion,LengthV(SubV(model.Deform(p,s.group),p)));CHECK(tipMotion>.01f);WheelsSame(model,asset,wheels);Bounds(model);const auto effects=model.Effects();CHECK(std::isfinite(effects.rightLift)&&effects.rightLift>0);std::printf("Canonical actual Dodo contact: outboard motion %.6fm\n",tipMotion);
}
void WingBand(const Asset &asset){
 Model model;const bool initialized=model.Initialize(asset.surfaces,800);CHECK(initialized);if(!initialized)return;const auto wheels=Wheels(model,asset);const skinmodel::Cut tip{{-1,0,0},-4.8f};const float before=RegionArea(model,tip);CHECK(before>.01f);const auto shots=Band(asset,0,3);std::vector<Shot> incomplete,bridge;for(auto s:shots)(s.centre.y>.1f&&s.centre.y<.5f?bridge:incomplete).push_back(s);
 Fire(model,incomplete);CHECK(RegionArea(model,tip)>.99f*before);WheelsSame(model,asset,wheels);Fire(model,bridge);CHECK(SealBand(model,0,3,tip));CHECK(RegionArea(model,tip)<.000001f);CHECK(RegionArea(model,tip,false)>.99f*before);WheelsSame(model,asset,wheels);CHECK(model.Effects().rightLift<model.Effects().leftLift);
 std::vector<aircraftconnectivity::Fragment> released;const auto sourceCount=model.Surfaces().size();CHECK(model.TransferDetached(released));CHECK(!released.empty());float transferredTip=0;for(const auto &f:released){CHECK(!f.attached&&f.source<sourceCount&&f.group==asset.surfaces[f.source].group);transferredTip+=Area(skinmodel::Slice(f.polygon,tip));}CHECK(transferredTip>.99f*before);CHECK(model.Surfaces().size()==sourceCount);CHECK(RegionArea(model,tip,false)<.000001f);for(const auto &f:model.Fragments())CHECK(f.attached);CHECK(model.TransferDetached(released)&&released.empty());const auto throughReleased=model.Bullet({5.1f,.4f,.5f},{5.1f,.4f,1.4f},200);CHECK(throughReleased.valid&&throughReleased.deposited==0&&throughReleased.holes==0);WheelsSame(model,asset,wheels);Bounds(model);
 std::printf("Canonical actual Dodo wing: complete geometric band detaches %.6fm2 tip region\n",before);
}
void RootAndSupport(const Asset &asset){
 Model model;const bool initialized=model.Initialize(asset.surfaces,800);CHECK(initialized);if(!initialized)return;const auto wheels=Wheels(model,asset);const skinmodel::Cut tip{{-1,0,0},-4.8f};const float before=RegionArea(model,tip);std::vector<Shot> shell,brace;for(auto s:Band(asset,0,1)){if(s.centre.y< -1||s.centre.y>1.2f||s.centre.z<-.55f)continue;(s.centre.z>.8f?shell:brace).push_back(s);}
 Fire(model,shell);CHECK(RegionArea(model,tip)>.99f*before);WheelsSame(model,asset,wheels);Fire(model,brace);CHECK(SealBand(model,0,1,tip,true));CHECK(RegionArea(model,tip)<.000001f);WheelsSame(model,asset,wheels);
}
void TailBand(const Asset &asset){
 Model model;const bool initialized=model.Initialize(asset.surfaces,800);CHECK(initialized);if(!initialized)return;const auto wheels=Wheels(model,asset);const skinmodel::Cut tail{{0,1,0},-5.8f},wing{{-1,0,0},-4.8f};const float tailBefore=RegionArea(model,tail),wingBefore=RegionArea(model,wing);std::vector<Shot> incomplete,bridge;for(auto s:Band(asset,1,-4))(s.centre.z<-.1f&&std::fabs(s.centre.x)<.15f?bridge:incomplete).push_back(s);
 Fire(model,incomplete);CHECK(RegionArea(model,tail)>.99f*tailBefore);Fire(model,bridge);CHECK(SealBand(model,1,-4,tail));CHECK(RegionArea(model,tail)<.000001f);CHECK(RegionArea(model,wing)>.99f*wingBefore);WheelsSame(model,asset,wheels);CHECK(model.Effects().pitch<1);std::printf("Canonical actual Dodo tail: separated tail preserves wings and independent wheels\n");
}
float DownwardMotion(const Model &model,const Asset &asset,bool tail=false){
 float motion=0;for(const auto&s:asset.surfaces){if(!s.structural||s.glass)continue;for(auto p:s.points)if(tail?p.y< -5.8f:(s.role==Role::WingRight&&p.x>4.8f))motion=std::max(motion,p.z-model.Deform(p,s.group).z);}return motion;
}
void GravityFlex(const Asset &asset){
 Model intact,wing,tail;CHECK(intact.Initialize(asset.surfaces,2400)&&wing.Initialize(asset.surfaces,2400)&&tail.Initialize(asset.surfaces,2400));const auto wheels=Wheels(wing,asset);
 std::vector<Shot> wingCuts,tailCuts;for(auto s:Band(asset,0,3))if(!(s.centre.y>.1f&&s.centre.y<.5f))wingCuts.push_back(s);for(auto s:Band(asset,1,-4))if(!(s.centre.z<-.1f&&std::fabs(s.centre.x)<.15f))tailCuts.push_back(s);
 Fire(wing,wingCuts);Fire(tail,tailCuts);const skinmodel::Cut tip{{-1,0,0},-4.8f},aft{{0,1,0},-5.8f};CHECK(RegionArea(wing,tip)>.99f*RegionArea(intact,tip));CHECK(RegionArea(tail,aft)>.99f*RegionArea(intact,aft));
 double peakMs=0,totalMs=0;for(int step=0;step<180;++step)for(auto model:{&intact,&wing,&tail}){const auto start=std::chrono::steady_clock::now();model->Tick(1.f/60,false);const double milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();peakMs=std::max(peakMs,milliseconds);totalMs+=milliseconds;}
 const float wholeWing=DownwardMotion(intact,asset),cutWing=DownwardMotion(wing,asset),wholeTail=DownwardMotion(intact,asset,true),cutTail=DownwardMotion(tail,asset,true);
 std::printf("Actual Dodo gravity flex: wing intact/cut %.7f/%.7fm; tail intact/cut %.7f/%.7fm\n",wholeWing,cutWing,wholeTail,cutTail);
 std::printf("Actual Dodo section Tick: mean %.6fms, max %.6fms (includes dirty-section rebuild)\n",totalMs/540,peakMs);
 CHECK(cutWing>.001f&&cutWing>wholeWing*1.25f);CHECK(cutTail>.001f&&cutTail>wholeTail*1.25f);WheelsSame(wing,asset,wheels);WheelsSame(tail,asset,wheels);CHECK(wing.Fire(false).engine==0&&wing.Fire(false).breach==0);CHECK(tail.Fire(false).engine==0&&tail.Fire(false).breach==0);Bounds(wing);Bounds(tail);
}
float RemainingSection(const Model&model,int axis,float coordinate){
 float total=0;for(const auto&f:model.Fragments())if(f.attached&&model.Surfaces()[f.source].structural){std::vector<Vec3> points;for(size_t i=0;i<f.polygon.size();++i){const auto a=f.polygon[i].position,b=f.polygon[(i+1)%f.polygon.size()].position;const float da=(axis==0?a.x:a.y)-coordinate,db=(axis==0?b.x:b.y)-coordinate;if(std::fabs(da)<1e-6f)points.push_back(a);if((da<0&&db>0)||(da>0&&db<0))points.push_back(AddV(a,MulV(SubV(b,a),da/(da-db))));}float longest=0;for(auto a:points)for(auto b:points)longest=std::max(longest,LengthV(SubV(a,b)));total+=longest;}return total;
}
void LoadedLigament(const Asset&asset){
 Model weak,strong,unloaded,tail;CHECK(weak.Initialize(asset.surfaces,2400)&&strong.Initialize(asset.surfaces,2400)&&unloaded.Initialize(asset.surfaces,2400)&&tail.Initialize(asset.surfaces,2400));
 std::vector<Shot> weakCuts,strongCuts,tailCuts;for(auto s:Band(asset,0,3)){if(!(s.centre.y>.1f&&s.centre.y<.5f))weakCuts.push_back(s);if(!(s.centre.y>-.1f&&s.centre.y<.7f))strongCuts.push_back(s);}for(auto s:Band(asset,1,-4))if(!(s.centre.z<-.1f&&std::fabs(s.centre.x)<.15f))tailCuts.push_back(s);
 Fire(weak,weakCuts);Fire(unloaded,weakCuts);Fire(strong,strongCuts);Fire(tail,tailCuts);const skinmodel::Cut tip{{-1,0,0},-4.8f},aft{{0,1,0},-5.8f};const float before=RegionArea(weak,tip),tailBefore=RegionArea(tail,aft);CHECK(RemainingSection(weak,0,3)>.001f&&RemainingSection(tail,1,-4)>.001f);const auto wheels=Wheels(weak,asset);
 for(int i=0;i<180;++i){weak.Tick(1.f/60,false);strong.Tick(1.f/60,false);unloaded.Tick(1.f/60,false,{0,0,0});}CHECK(DownwardMotion(weak,asset)>DownwardMotion(strong,asset)*1.1f);CHECK(DownwardMotion(unloaded,asset)<.000001f);CHECK(RegionArea(unloaded,tip)>.99f*before);
 const float materialBefore=RegionArea(weak,tip,false);for(int i=0;i<600;++i){weak.Tick(1.f/120,false,{0,0,-1000});tail.Tick(1.f/120,false,{0,0,-1000});}std::printf("Actual Dodo finite ligament: wing attached %.6f/%.6fm2; tail %.6f/%.6fm2, remaining sections %.6f/%.6fm, sag %.6f/%.6fm\n",RegionArea(weak,tip),before,RegionArea(tail,aft),tailBefore,RemainingSection(weak,0,3),RemainingSection(tail,1,-4),DownwardMotion(weak,asset),DownwardMotion(tail,asset,true));
 CHECK(RegionArea(weak,tip)<.1f*before);CHECK(RegionArea(tail,aft)<.1f*tailBefore);CHECK(RegionArea(weak,tip,false)>.99f*materialBefore);WheelsSame(weak,asset,wheels);WheelsSame(tail,asset,wheels);CHECK(tail.Fire(false).engine==0&&tail.Fire(false).breach==0);Bounds(weak);Bounds(tail);
 std::vector<aircraftconnectivity::Fragment> released;CHECK(weak.TransferDetached(released)&&!released.empty());CHECK(weak.TransferDetached(released)&&released.empty());
}
void WeakenedContact(const Asset&asset){
 Model whole,cut;CHECK(whole.Initialize(asset.surfaces,2400)&&cut.Initialize(asset.surfaces,2400));std::vector<Shot> cuts;for(auto s:Band(asset,0,3))if(!(s.centre.y>.1f&&s.centre.y<.5f))cuts.push_back(s);Fire(cut,cuts);
 const auto a=whole.ContactEnergy({4.5f,.4f,1.04f},{0,-1,0},100,1000,.01f),b=cut.ContactEnergy({4.5f,.4f,1.04f},{0,-1,0},100,1000,.01f);CHECK(a.valid&&b.valid);float wholeMotion=0,cutMotion=0;for(const auto&s:asset.surfaces)if(s.role==Role::WingRight)for(auto p:s.points)if(p.x>4.8f){wholeMotion=std::max(wholeMotion,std::fabs(whole.Deform(p,s.group).y-p.y));cutMotion=std::max(cutMotion,std::fabs(cut.Deform(p,s.group).y-p.y));}std::printf("Actual Dodo equal-work bend: intact %.6f / weakened %.6fm\n",wholeMotion,cutMotion);CHECK(cutMotion>wholeMotion*1.25f);
}
void OwnWeightLigament(const Asset&asset){
 Model weak;CHECK(weak.Initialize(asset.surfaces,2400));
 // Short accepted ray segments isolate the individual sheets; a through ray
 // would perforate both flanges and could not leave just this lower ligament.
 for(const auto&s:Band(asset,0,3))if(!(s.centre.z<1.f&&s.centre.y>.1f&&s.centre.y<.5f)){const auto hit=weak.Bullet(AddV(s.centre,MulV(s.normal,.025f)),AddV(s.centre,MulV(s.normal,-.025f)),200);CHECK(hit.valid);}
 const skinmodel::Cut tip{{-1,0,0},-4.8f};const float before=RegionArea(weak,tip);CHECK(before>.1f&&RemainingSection(weak,0,3)>.001f);Model weightless=weak,overload=weak;float peak=0;bool released=false;for(int i=0;i<300;++i){weak.Tick(1.f/60,false);weightless.Tick(1.f/60,false,{0,0,0});peak=std::max(peak,DownwardMotion(weak,asset));released|=RegionArea(weak,tip)<before*.1f;}for(int i=0;i<1200;++i)overload.Tick(1.f/120,false,{0,0,-2000});CHECK(RegionArea(overload,tip)<before*.1f);
 std::printf("Actual Dodo lower-sheet ligament under own weight: %.6fm peak, released=%d\n",peak,int(released));CHECK(peak>.03f);CHECK(DownwardMotion(weightless,asset)<.000001f&&RegionArea(weightless,tip)>.99f*before);Bounds(weak);Bounds(weightless);
}
void SideWindow(const Asset &asset){
 for(const auto cabinRay:std::array<Vec3,2>{{{-2,-.9f,.65f},{-2,1.1f,.8f}}}){Model model;const bool initialized=model.Initialize(asset.surfaces,800);CHECK(initialized);if(!initialized)return;auto end=cabinRay;end.x=2;const auto hit=model.Bullet(cabinRay,end,200);CHECK(hit.valid&&hit.changed);CHECK(model.Effects().power==1);const auto fire=model.Fire(false);CHECK(fire.engine==0&&fire.breach==0&&fire.electrical==0);firemodel::State state;for(int i=0;i<300;++i)firemodel::Tick(state,fire,.1f);CHECK(!state.lit);Bounds(model);}

}
}
int main(){
 auto dodo=LoadDodo(),donor=LoadCockpit();std::vector<std::pair<Geo *,uint64_t>> originals;for(auto *a:dodo.atomics)originals.push_back({a->geometry,GeometryFingerprint(*a->geometry)});CHECK(airportjetmodel::Install()&&airportjetmodel::Prepare(&dodo,&donor));const auto asset=bodyasset::Read(dodo);CHECK(!asset.surfaces.empty());
 bodyasset::Contact(asset);bodyasset::WingBand(asset);bodyasset::RootAndSupport(asset);bodyasset::TailBand(asset);bodyasset::GravityFlex(asset);bodyasset::LoadedLigament(asset);bodyasset::WeakenedContact(asset);bodyasset::OwnWeightLigament(asset);bodyasset::SideWindow(asset);for(const auto &p:originals)CHECK(GeometryFingerprint(*p.first)==p.second);Destroy(dodo);Destroy(donor);CHECK(liveGeometry==0&&liveMaterial==0&&liveFrame==0&&liveAtomic==0);if(!failures)std::puts("Canonical Dodo actual-asset material behavior: all passed");return failures?1:0;
}

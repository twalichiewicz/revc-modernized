// Historical red control for the legacy fallback; expected to fail.
// The canonical source Dodo owner intentionally bypasses this path.
// Original Dodo -> real preparation -> real material owner. Actor/RenderWare
// allocation is native-shaped; the model vertices and production writers are real.
#define main GlassFixtureChecks
#include "test_carglass.cpp"
#undef main
#define DODO_MODEL_NO_MAIN
#include "test_dodomodel.cpp"
#include "airframemodel.h"
#include <memory>

int main(){
 using fixture::Check;
 fixture::entries["_ZN11CAutomobile13SetDoorDamageEi6eDoorsb"]=reinterpret_cast<void *>(fixture::Door);
 fixture::entries["_ZN11CAutomobile15SetBumperDamageEi7ePanelsb"]=reinterpret_cast<void *>(fixture::Bumper);
 fixture::entries["_ZN11CAutomobile3FixEv"]=reinterpret_cast<void *>(fixture::Fix);
 fixture::entries["_ZN7CEntity14DeleteRwObjectEv"]=reinterpret_cast<void *>(fixture::Delete);
 game::api.PoolsGetVehicle=fixture::Car;gConfig.carGlass=gConfig.carDeformation=true;Check(carglass::Install(),"material bindings install");fixture::Reset();
 auto actor=LoadDodo(),donor=LoadCockpit();CHECK(airportjetmodel::Install()&&airportjetmodel::Prepare(&actor,&donor));
 auto vehicle=fixture::cars.data();*reinterpret_cast<int16_t *>(vehicle+0x6a)=181;*reinterpret_cast<int *>(vehicle+0x348)=4;
 auto &clump=fixture::clumps[0];clump.atomics.clear();
 std::vector<std::unique_ptr<fixture::Frame>> frames;std::vector<std::unique_ptr<fixture::Atomic>> atoms;
 for(auto original:actor.atomics)if(original->flags&4){
  frames.emplace_back(new fixture::Frame);auto &frame=*frames.back();fixture::Identity(frame.m.data());frame.parent=&clump.root;frame.name=original->frame->name;
  atoms.emplace_back(new fixture::Atomic);auto &atom=*atoms.back();atom.frame=&frame;auto source=original->geometry;
  auto geometry=static_cast<fixture::Geo *>(fixture::CreateGeo(source->vertices.size(),source->triangles.size(),source->flags));
  geometry->mats={&fixture::glass,&fixture::metal};geometry->vertices=source->vertices;geometry->normals=source->normals;
  for(size_t i=0;i<source->vertices.size();++i){geometry->vertices[i]=ModelPoint(original->frame,source->vertices[i]);geometry->uv[i]={source->uv[i].x,source->uv[i].y};}
  for(size_t i=0;i<source->triangles.size();++i){auto t=source->triangles[i];geometry->triangles[i]={{t.v[0],t.v[1],t.v[2]},uint16_t(source->materials[t.material]->color.a<255?0:1)};}
  fixture::SetGeometry(&atom,geometry,0);fixture::DestroyGeo(geometry);clump.atomics.push_back(&atom);
 }
 auto named=[&](const char *name){for(auto a:clump.atomics)if(a->frame->name==name)return a;return static_cast<fixture::Atomic *>(nullptr);};
 auto hull=named("dodo_hull"),wheel=named("dodo_tire_r");Check(hull&&wheel,"actual prepared hull and wheel available");
 auto baseline=hull->geo->vertices;dentmodel::Impact hit;Check(dentmodel::Make({2.58f,.43f,1.04f},{0,-1,0},9.44f,hit)&&carglass::Dent(vehicle,hit),"recorded user's wing contact reaches actual material writer");
 float tipMotion=0;for(size_t i=0;i<baseline.size();++i)if(baseline[i].x>4.5f){auto a=baseline[i],b=hull->geo->vertices[i];tipMotion=std::max(tipMotion,std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z)));}
 Check(tipMotion>.01f,"registered wing strike bends the connected outboard wing instead of only a small local patch");
 const auto wheelBefore=wheel->geo->triangles.size();
 airframemodel::Layout layout;airframemodel::Build(181,4,{-5.5f,-5.6f,-1.3f},{5.5f,4,2.6f},layout);auto cut=airframemodel::SectionCut(layout,1);
 Check(carglass::SeverSection(vehicle,cut.normal,cut.offset,false),"existing structural separation callback commits");
 Check(wheel->geo->triangles.size()==wheelBefore,"wing separation preserves the independently attached right landing wheel");
 std::printf("Dodo user reproduction: tip_motion=%.6f wheel_faces_before=%zu after=%zu failures=%d\n",tipMotion,wheelBefore,wheel->geo->triangles.size(),fixture::failed);
 carglass::Forget(vehicle,true);for(auto &atom:atoms)if(atom->geo){fixture::DestroyGeo(atom->geo);atom->geo=nullptr;}clump.atomics.clear();Destroy(actor);Destroy(donor);
 return fixture::failed||failures?1:0;
}

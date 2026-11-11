// Cabin glazing on the installed car catalog with the source host's own face
// classification: decals and lamps are sheet, tempered side/rear panes go out
// whole on one penetrating round, the laminated windscreen keeps perforations.
#define VEHICLE_BODY_ASSETS_NO_MAIN
#include "test_vehicle_body_assets.cpp"
#undef VEHICLE_BODY_ASSETS_NO_MAIN
#include <set>

namespace glassstopgap {
using namespace vehicleasset;
using namespace aircraftbodymodel;
std::string Data(){const char *env=std::getenv("GAMEDATA");return env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";}
Vec3 Centre(const BodySurface &s){return MulV(AddV(AddV(s.points[0],s.points[1]),s.points[2]),1.f/3);}
Vec3 Normal(const BodySurface &s){return CrossV(SubV(s.points[1],s.points[0]),SubV(s.points[2],s.points[0]));}
float SurfaceArea(const BodySurface &s){const auto n=Normal(s);return .5f*std::sqrt(DotV(n,n));}
struct Car {std::vector<BodySurface> surfaces;std::vector<std::string> groups;Definition definition;std::vector<std::string> textures;std::vector<bool> translucent;};
// AircraftBody.cpp Body::Initialize: ClassifyFace and ClassifyMaterial.
bool Load(int id,Car &car){
 const auto catalogs=Catalogs(Data());const Catalog *catalog=nullptr;for(const auto &c:catalogs)if(c.id==id)catalog=&c;if(!catalog)return false;
 auto clump=Read(Data(),*catalog);car.definition.kind=catalog->kind;car.definition.model=id;car.definition.bounds={{1e9f,1e9f,1e9f},{-1e9f,-1e9f,-1e9f}};
 for(size_t group=0;group<clump.atomics.size();++group){const auto *a=clump.atomics[group];car.groups.push_back(a->frame->name);if(!Visible(*a)||!a->geometry||RenderOnlyAtomic(a->frame->name))continue;const auto *g=a->geometry;
  for(const auto &t:g->triangles){if(t.material>=g->materials.size())continue;BodySurface s;s.group=uint32_t(group);for(int i=0;i<3;++i){s.points[i]=V(ModelPoint(a->frame,g->vertices[t.v[i]]));Grow(car.definition.bounds,s.points[i]);}
   const auto *m=g->materials[t.material];const char *texture=m->texture?m->texture->name.c_str():nullptr;s.role=ClassifyFace(a->frame->name,Centre(s),catalog->kind,id);
   const auto face=ClassifyMaterial(s.role,catalog->kind,a->frame->name,Centre(s),Normal(s),m->color.a<255,m->color.a,texture);s.glass=face.glass;s.laminated=face.laminated;s.structural=face.structural;
   car.surfaces.push_back(s);car.textures.push_back(texture?texture:"");car.translucent.push_back(m->color.a<255);}}
 Destroy(clump);return !car.surfaces.empty();
}
// Connected glass of one source part, by shared rest vertices.
std::vector<size_t> Pane(const Car &car,size_t seed){
 std::vector<size_t> out{seed};std::set<size_t> seen{seed};
 for(size_t k=0;k<out.size();++k){const auto &a=car.surfaces[out[k]];for(size_t j=0;j<car.surfaces.size();++j){const auto &b=car.surfaces[j];if(seen.count(j)||!b.glass||b.group!=a.group)continue;bool shared=false;for(auto p:a.points)for(auto q:b.points)shared|=DotV(SubV(p,q),SubV(p,q))<1e-6f;if(shared){seen.insert(j);out.push_back(j);}}}
 return out;
}
float AttachedArea(const Model &m,const std::vector<size_t> &sources){std::set<size_t> wanted(sources.begin(),sources.end());float area=0;for(const auto &f:m.Fragments())if(f.attached&&wanted.count(f.source))for(size_t i=1;i+1<f.polygon.size();++i){const auto c=CrossV(SubV(f.polygon[i].position,f.polygon[0].position),SubV(f.polygon[i+1].position,f.polygon[0].position));area+=.5f*std::sqrt(DotV(c,c));}return area;}
// Largest glass face on one side whose normal faces sideways.
size_t SideGlass(const Car &car,bool left){size_t best=car.surfaces.size();float area=0;for(size_t i=0;i<car.surfaces.size();++i){const auto &s=car.surfaces[i];if(!s.glass||s.laminated)continue;const auto c=Centre(s),n=Normal(s);if((left?c.x>0:c.x<0)||std::fabs(n.x)<.7f*std::sqrt(DotV(n,n)))continue;if(SurfaceArea(s)>area){area=SurfaceArea(s);best=i;}}return best;}
}

int main(){
 using namespace glassstopgap;
 // A police car's translucent decals and light-bar lenses are body sheet.
 {
  Car police;CHECK(Load(156,police));size_t translucent=0,glass=0,decals=0;for(size_t i=0;i<police.surfaces.size();++i){if(!police.translucent[i])continue;++translucent;if(police.surfaces[i].glass)++glass;else if(police.surfaces[i].structural||police.surfaces[i].role==Role::Door)++decals;}
  std::printf("  police: %zu translucent faces, %zu glazing, %zu decal/lamp sheet\n",translucent,glass,decals);
  CHECK(glass>0&&decals>0&&glass<translucent);
  for(size_t i=0;i<police.surfaces.size();++i)if(police.translucent[i]&&!police.textures[i].empty()&&police.textures[i].find("police")!=std::string::npos)CHECK(!police.surfaces[i].glass);
 }
 // Admiral: one pistol round removes the struck tempered side pane, only it.
 {
  Car admiral;CHECK(Load(175,admiral));Model m;CHECK(m.Initialize(admiral.surfaces,1550,admiral.definition));
  const size_t left=SideGlass(admiral,true),right=SideGlass(admiral,false);CHECK(left<admiral.surfaces.size()&&right<admiral.surfaces.size());
  if(left<admiral.surfaces.size()&&right<admiral.surfaces.size()){
   const auto pane=Pane(admiral,left),other=Pane(admiral,right);const float before=AttachedArea(m,pane),otherBefore=AttachedArea(m,other);
   const auto c=Centre(admiral.surfaces[left]);auto n=Normal(admiral.surfaces[left]);n=MulV(n,1/std::sqrt(DotV(n,n)));if(n.x>0)n=MulV(n,-1);// outward on the left
   const auto r=m.Bullet(AddV(c,MulV(n,.5f)),SubV(c,MulV(n,.5f)),25);const float after=AttachedArea(m,pane);
   std::printf("  admiral %s side pane: %zu faces %.3f -> %.3f m2 (holes %zu), right pane %.3f -> %.3f m2\n",admiral.groups[admiral.surfaces[left].group].c_str(),pane.size(),before,after,r.holes,otherBefore,AttachedArea(m,other));
   CHECK(r.valid&&r.changed&&before>.1f&&after<.01f*before&&std::fabs(AttachedArea(m,other)-otherBefore)<1e-5f);
  }
 }
 // The laminated windscreen keeps a local perforation.
 {
  Car admiral;CHECK(Load(175,admiral));Model m;CHECK(m.Initialize(admiral.surfaces,1550,admiral.definition));size_t seed=admiral.surfaces.size();float area=0;
  for(size_t i=0;i<admiral.surfaces.size();++i){const auto &s=admiral.surfaces[i];if(s.glass&&s.laminated&&SurfaceArea(s)>area){area=SurfaceArea(s);seed=i;}}
  CHECK(seed<admiral.surfaces.size());if(seed<admiral.surfaces.size()){std::vector<size_t> screen;for(size_t i=0;i<admiral.surfaces.size();++i)if(admiral.surfaces[i].glass&&admiral.surfaces[i].laminated)screen.push_back(i);
   const float before=AttachedArea(m,screen);const auto c=Centre(admiral.surfaces[seed]);auto n=Normal(admiral.surfaces[seed]);n=MulV(n,1/std::sqrt(DotV(n,n)));if(n.y<0)n=MulV(n,-1);
   const auto r=m.Bullet(AddV(c,MulV(n,.5f)),SubV(c,MulV(n,.5f)),25);const float after=AttachedArea(m,screen);std::printf("  admiral windscreen %.3f -> %.3f m2 (holes %zu)\n",before,after,r.holes);
   CHECK(r.valid&&r.holes>0&&after<before&&after>.9f*before);}
 }
 std::printf("vehicle glass stopgap: failures=%d\n",failures);return failures?1:0;
}

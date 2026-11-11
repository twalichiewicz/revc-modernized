// Actual vehicle DFFs -> material body -> pose-only publication premises.
// 1) Posing a part changes only that part's model-space collision vertices,
//    on intact and damaged (holed, dented, flexed) bodies.
// 2) A rigid part pose leaves its local render vertices unchanged, so the
//    animation path may keep the committed RW mesh and patch only COL.
#define VEHICLE_BODY_ASSETS_NO_MAIN
#include "test_vehicle_body_assets.cpp"
#include "BodyAffine.h"
#include "BodyPublication.h"
#include <random>
namespace publicationassets {
using namespace vehicleasset;
bool Degenerate(Vec3 a,Vec3 b,Vec3 c){const Vec3 u{b.x-a.x,b.y-a.y,b.z-a.z},v{c.x-a.x,c.y-a.y,c.z-a.z};const Vec3 n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};return n.x*n.x+n.y*n.y+n.z*n.z<1e-14f;}
// The host's publication order: attached fragments, fan triangles, degenerate skipped.
bodypublication::Cache Publish(const Model &m){bodypublication::Cache c;c.valid=true;for(const auto &f:m.Fragments()){if(!f.attached)continue;for(size_t i=1;i+1<f.polygon.size();++i){bodypublication::Triangle t;t.group=f.group;t.source=f.source;t.rest={{f.polygon[0].position,f.polygon[i].position,f.polygon[i+1].position}};const auto role=m.Surfaces()[f.source].role;t.sweep=role!=Role::Wheel&&role!=Role::Interior;std::array<Vec3,3> p;for(int k=0;k<3;++k)p[k]=m.Deform(t.rest[k],t.group);if(!Degenerate(p[0],p[1],p[2])){t.col=int32_t(c.positions.size()/3);for(auto q:p)c.positions.push_back(q);}c.triangles.push_back(t);}}return c;}
Vec3 Apply(const bodyaffine::Matrix &a,Vec3 p){return {a[0]*p.x+a[3]*p.y+a[6]*p.z+a[9],a[1]*p.x+a[4]*p.y+a[7]*p.z+a[10],a[2]*p.x+a[5]*p.y+a[8]*p.z+a[11]};}
// Rotation about an axis through the part centre, optionally scaled (native wheel frames scale).
bodyaffine::Matrix Pose(Vec3 centre,int axis,float angle,float scale){const float c=std::cos(angle)*scale,s=std::sin(angle)*scale;bodyaffine::Matrix r{{scale,0,0,0,scale,0,0,0,scale,0,0,0}};
 if(axis==0){r[4]=c;r[5]=s;r[7]=-s;r[8]=c;}else if(axis==2){r[0]=c;r[1]=s;r[3]=-s;r[4]=c;}else{r[0]=c;r[2]=-s;r[6]=s;r[8]=c;}
 const Vec3 moved=Apply(r,centre);r[9]=centre.x-moved.x;r[10]=centre.y-moved.y+.01f;r[11]=centre.z-moved.z;return r;}
void Exercise(const Catalog &catalog,const std::vector<BodySurface> &surfaces,const Definition &definition,int shots){
 Model m;if(!m.Initialize(surfaces,1500,definition)){std::printf("FAIL init model=%d\n",catalog.id);++failures;return;}
 std::mt19937 rng(11);std::vector<const BodySurface*> outer;for(const auto &s:surfaces)if(s.role!=Role::Wheel&&s.role!=Role::Interior)outer.push_back(&s);
 for(int i=0;i<shots&&!outer.empty();++i){auto &s=*outer[rng()%outer.size()];auto p=MulV(AddV(AddV(s.points[0],s.points[1]),s.points[2]),1.f/3);auto n=CrossV(SubV(s.points[1],s.points[0]),SubV(s.points[2],s.points[0]));const float l=std::sqrt(DotV(n,n));if(l<1e-6f)continue;n=MulV(n,1/l);m.Bullet(AddV(p,MulV(n,.3f)),AddV(p,MulV(n,-.3f)),40);}
 if(shots&&!outer.empty()){auto &s=*outer[0];auto p=MulV(AddV(AddV(s.points[0],s.points[1]),s.points[2]),1.f/3);m.ContactEnergy(p,{1,0,0},800,1000,.02f);for(int i=0;i<4;++i)m.Tick(1.f/30,true,{0,2,-20});}
 std::vector<aircraftconnectivity::Fragment> released;CHECK(m.TransferDetached(released));
 const auto cache=Publish(m);uint32_t groups=0;for(const auto &s:surfaces)groups=std::max(groups,s.group+1);std::vector<uint8_t> posed(groups);std::vector<Vec3> centre(groups);std::vector<int> count(groups);
 for(const auto &s:surfaces){centre[s.group]=AddV(centre[s.group],MulV(AddV(AddV(s.points[0],s.points[1]),s.points[2]),1.f/3));++count[s.group];if(s.role==Role::Wheel||s.role==Role::Door||s.role==Role::MainRotor||s.role==Role::TailRotor)posed[s.group]=1;}
 size_t chosen=0;for(auto p:posed)chosen+=p;if(!chosen)for(uint32_t g=0;g<groups&&chosen<2;++g)if(count[g]){posed[g]=1;++chosen;}
 Model intact=m;std::vector<bodyaffine::Matrix> poses(groups);
 for(uint32_t g=0;g<groups;++g)if(posed[g]&&count[g]){centre[g]=MulV(centre[g],1.f/count[g]);poses[g]=Pose(centre[g],g%3,.7f+.1f*g,g%2?1.f:1.25f);if(!m.SetGroupPose(g,poses[g])){std::printf("FAIL pose model=%d group=%u\n",catalog.id,g);++failures;posed[g]=0;}}
 std::vector<Vec3> next;bool sweep=false;const auto patch=bodypublication::PoseOnly(cache,posed,[&](Vec3 p,uint32_t g){return m.Deform(p,g);},Degenerate,next,sweep);
 if(patch==bodypublication::Patch::Rebuild){std::printf("note model=%d: degeneracy changed; host falls back to full publication\n",catalog.id);return;}
 // Premise 1: the patched collision equals recomputing every vertex from the posed body.
 size_t mismatched=0,moved=0;for(const auto &t:cache.triangles){if(t.col<0)continue;for(int k=0;k<3;++k){const auto full=m.Deform(t.rest[k],t.group),&patched=next[size_t(t.col)*3+k];if(full.x!=patched.x||full.y!=patched.y||full.z!=patched.z)++mismatched;if(posed[t.group])++moved;}}
 if(mismatched){std::printf("FAIL model=%d pose-only patch differs from full recompute at %zu vertices\n",catalog.id,mismatched);++failures;}
 // Premise 2: local render vertices (inverse current pose * posed vertex) are pose-invariant.
 float worst=0;for(const auto &t:cache.triangles){if(!posed[t.group])continue;bodyaffine::Matrix inverse;if(!bodyaffine::Inverse(poses[t.group],inverse)){++failures;std::printf("FAIL inverse model=%d\n",catalog.id);break;}for(int k=0;k<3;++k){const auto local=Apply(inverse,m.Deform(t.rest[k],t.group)),rest=intact.Deform(t.rest[k],t.group);const auto d=SubV(local,rest);worst=std::max(worst,std::sqrt(DotV(d,d))/(1+std::sqrt(DotV(rest,rest))));}}
 if(worst>2e-6f){std::printf("FAIL model=%d posed part local render vertices drift %.3g\n",catalog.id,worst);++failures;}
 std::printf("model %3d %-9s shots=%3d triangles=%zu posed_groups=%zu moved_vertices=%zu sweep_rebuild=%d local_drift=%.2g\n",catalog.id,catalog.name.c_str(),shots,cache.triangles.size(),chosen,moved,sweep,worst);
}
}
int main(){
 using namespace publicationassets;const char *env=std::getenv("GAMEDATA");const std::string data=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";
 const std::set<std::string> names{"landstal","admiral","stinger","rumpo","pcj600","speeder","maverick","rcbaron","flatbed"};int exercised=0;
 for(const auto &c:Catalogs(data)){if(!names.count(c.name))continue;auto clump=Read(data,c);Definition definition;definition.kind=c.kind;definition.model=c.id;definition.armor=1;size_t all=0;auto surfaces=Surfaces(clump,definition,all,nullptr,c);for(int shots:{0,60})Exercise(c,surfaces,definition,shots);Destroy(clump);++exercised;}
 CHECK(exercised==int(names.size()));CHECK(liveGeometry==0&&liveMaterial==0&&liveFrame==0&&liveAtomic==0);
 if(!failures)std::puts("body publication actual assets: all passed");return failures?1:0;
}

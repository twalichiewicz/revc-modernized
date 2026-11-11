// Publication policy for source-host material bodies: capacity hard reject,
// bounded transient retry, pose-only collision patch
// and the ordered sweep de-duplication used by the committed collision.
#include "BodyPublication.h"
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <vector>
using namespace bodypublication;
namespace {
int failed=0;
void Check(bool ok,const char *name){if(!ok){++failed;std::printf("FAIL %s\n",name);}}
bool Same(const std::vector<Vec3> &a,const std::vector<Vec3> &b){if(a.size()!=b.size())return false;for(size_t i=0;i<a.size();++i)if(a[i].x!=b[i].x||a[i].y!=b[i].y||a[i].z!=b[i].z)return false;return true;}
struct Incident{int holes=1;};
// One world-frame transaction as the host performs it: replay queued incidents
// on a staged copy, then publish or apply the failure policy.
struct World {
 int committed=0,capacity=10,replayed=0;bool policy=true;Gate gate;std::vector<Incident> events;
 bool Shoot(){if(policy&&!gate.AcceptsHoles())return false;if(events.size()>=256)return false;events.push_back({});return true;}
 void Frame(){
  if(events.empty())return;int candidate=committed;for(auto &e:events){candidate+=e.holes;++replayed;}
  if(candidate>capacity){if(policy)Retire(gate,Failure::Capacity,events);return;}
  committed=candidate;events.clear();gate.Committed();
 }
};
}
int main(){
 {Gate g;auto v=g.Failed(Failure::Capacity,true);Check(v.drop&&v.report&&g.saturated&&!g.AcceptsHoles(),"capacity retires incidents, reports and saturates");
  v=g.Failed(Failure::Capacity,true);Check(v.drop&&!v.report,"repeated capacity failure is not re-reported");
  g.Committed();Check(!g.reported&&g.saturated,"commit opens a new report episode but capacity stays sticky");
  v=g.Failed(Failure::Capacity,true);Check(v.drop&&!v.report,"capacity is reported once per actor, even after later commits");v=g.Failed(Failure::Transient,true);Check(v.report&&!v.drop,"a later resource failure is still reported once");}
 {Gate g;auto v=g.Failed(Failure::Invalid,true);Check(v.drop&&v.report&&!g.saturated,"invalid candidate retires incidents without saturating");}
 {Gate g;int drops=0,reports=0;for(uint32_t i=1;i<=Gate::kRetryFrames;++i){auto v=g.Failed(Failure::Transient,true);drops+=v.drop;reports+=v.report;if(i<Gate::kRetryFrames)Check(!v.drop,"transient failure keeps incidents while retrying");}
  Check(drops==1&&reports==2&&!g.saturated,"transient retry is bounded: one first report, one retirement report");
  auto v=g.Failed(Failure::Transient,false);Check(!v.drop&&!v.report,"failure without incidents never drops or re-reports");}
 {Gate g;std::vector<Incident> events(3);auto v=Retire(g,Failure::Capacity,events);Check(v.drop&&events.empty(),"retire clears the queued incidents");
  std::vector<Incident> kept(2);Retire(g,Failure::Transient,kept);Check(kept.size()==2,"transient retire keeps incidents for retry");}
 // Stuck retry: incidents that can never publish must not be replayed forever.
 {World legacy;legacy.policy=false;World fixed;for(int frame=0;frame<120;++frame){legacy.Shoot();fixed.Shoot();legacy.Frame();fixed.Frame();}
  Check(legacy.events.size()>100&&legacy.replayed>5000,"legacy policy replays an ever-growing queue (reproduces the stuck body)");
  Check(fixed.events.empty()&&fixed.gate.saturated&&fixed.committed==10&&fixed.replayed<=12,"fixed policy retires the failing incident once and refuses further holes");
  std::printf("stuck retry over 120 frames: legacy replayed=%d queued=%zu; fixed replayed=%d queued=%zu committed=%d\n",legacy.replayed,legacy.events.size(),fixed.replayed,fixed.events.size(),fixed.committed);}
 // Pose-only collision patch over three groups: only posed groups move.
 {Cache c;c.valid=true;auto tri=[&](uint32_t g,float x,bool sweep,bool col){Triangle t;t.group=g;t.rest={{Vec3{x,0,0},Vec3{x+1,0,0},Vec3{x,1,0}}};t.sweep=sweep;t.col=col?int32_t(c.positions.size()/3):-1;if(col)for(auto p:t.rest)c.positions.push_back(p);c.triangles.push_back(t);};
  tri(0,0,true,true);tri(1,5,false,true);tri(1,7,false,false);tri(2,9,true,true);
  auto shift=[](Vec3 p,uint32_t g){return g==1?Vec3{p.x,p.y,p.z+1}:p;};auto degenerate=[](Vec3 a,Vec3 b,Vec3 c){const Vec3 u{b.x-a.x,b.y-a.y,b.z-a.z},v{c.x-a.x,c.y-a.y,c.z-a.z};const Vec3 n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};return n.x*n.x+n.y*n.y+n.z*n.z<1e-14f;};
  std::vector<Vec3> next;bool sweep=true;std::vector<uint8_t> posed{0,1,0};
  // Group 1's uncollided triangle stays degenerate-free, so it must not flip.
  c.triangles[2].col=-1;auto result=PoseOnly(c,posed,shift,[&](Vec3 a,Vec3 b,Vec3 d){return c.triangles[2].rest[0].x==a.x?true:degenerate(a,b,d);},next,sweep);
  Check(result==Patch::Moved&&!sweep,"posed non-sweep group moves without a sweep rebuild");
  Check(next.size()==c.positions.size()&&next[3].z==1&&next[4].z==1&&next[5].z==1&&next[0].z==0&&next[6].z==0,"only the posed group's collision vertices move");
  result=PoseOnly(c,posed,shift,degenerate,next,sweep);Check(result==Patch::Rebuild,"a degeneracy change requires a full publication");
  posed={1,0,0};result=PoseOnly(c,posed,[](Vec3 p,uint32_t){return p;},[&](Vec3 a,Vec3 b,Vec3 d){return c.triangles[2].rest[0].x==a.x?true:degenerate(a,b,d);},next,sweep);Check(result==Patch::Moved&&sweep,"a posed sweep group requests a sweep rebuild");
  posed={0,0,0};result=PoseOnly(c,posed,shift,degenerate,next,sweep);Check(result==Patch::Unchanged&&Same(next,c.positions),"no posed group leaves the committed collision untouched");}
 // Ordered de-duplication must equal std::map insertion (last write wins).
 {std::mt19937 rng(5);std::uniform_int_distribution<int> key(0,40);for(int round=0;round<50;++round){std::map<std::array<int,3>,int> map;std::vector<std::pair<std::array<int,3>,int>> entries;for(int i=0;i<300;++i){std::array<int,3> k{{key(rng),key(rng)%3,key(rng)%2}};map[k]=i;entries.push_back({k,i});}
   std::vector<int> ordered;LastByKey(entries,ordered);std::vector<int> expected;for(auto &item:map)expected.push_back(item.second);if(ordered!=expected){Check(false,"ordered sweep de-duplication equals std::map");break;}}}
 {Hash a,b,c;float x[3]={1,2,3},y[3]={1,2,3.0000002f};a.Add(x,sizeof x);b.Add(x,sizeof x);c.Add(y,sizeof y);Check(a.value==b.value&&a.value!=c.value,"render part hash is exact over bytes");}
 // Native sector registration clamps only the upper index (World.h GetSector).
 // VC's grid is x [-2400,1600), y [-2000,2000): the skyline airtrain flies at
 // y=-2145, and re-adding it there wrote before CWorld::ms_aSectors.
 {const float minX=-2400,minY=-2000,maxX=1600,maxY=2000;
  Check(InsideGrid(-670.8f,-2148.9f,-670.7f,-2149.0f,minX,minY,maxX,maxY)==false,"airtrain debris south of the grid is outside");
  Check(InsideGrid(-700,-1100,-680,-1130,minX,minY,maxX,maxY),"airport vehicle is inside the grid");
  Check(!InsideGrid(-2401,10,-2390,0,minX,minY,maxX,maxY)&&!InsideGrid(1590,10,1600,0,minX,minY,maxX,maxY)&&!InsideGrid(0,2000,10,1990,minX,minY,maxX,maxY),"any edge past the grid is outside; the upper bound is exclusive");
  Check(!InsideGrid(NAN,10,0,0,minX,minY,maxX,maxY)&&!InsideGrid(0,INFINITY,0,0,minX,minY,maxX,maxY),"non-finite bounds are outside");}
 // Outside the grid nothing may be re-registered, but queued incidents are
 // the actor's: keep them for its return, report once, never count retries.
 {Gate g;std::vector<int> events{1,2};bool reports=false;for(int i=0;i<100;++i)reports|=Retire(g,Failure::Outside,events).report&&i>0;
  Check(events.size()==2&&g.deferred==0&&!g.saturated&&!reports&&std::string(Name(Failure::Outside))=="outside-world","outside-world deferral keeps incidents, reports once and never drops");}
 if(!failed)std::puts("body publication policy: all passed");return failed?1:0;
}

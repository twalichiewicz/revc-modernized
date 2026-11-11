#pragma once
// Publication policy for per-actor material bodies. Pure state and geometry
// bookkeeping; the source host owns RW meshes, instance COL and world lists.
#include "vec3.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
namespace bodypublication {
// Native RW render meshes and instance COL accept at most this many triangles.
constexpr size_t kMaxTriangles=16000;
// Capacity: the candidate exceeds a fixed native mesh limit and never will fit.
// Invalid: this candidate can never publish. Transient: resources may return.
// Outside: the actor is beyond the native sector grid, where nothing may be
// re-registered; its incidents wait for its return instead of being retired.
enum class Failure:uint8_t{None,Capacity,Invalid,Transient,Outside};
inline const char *Name(Failure f){return f==Failure::Capacity?"capacity":f==Failure::Invalid?"invalid":f==Failure::Transient?"resources":f==Failure::Outside?"outside-world":"none";}
// Native sector registration (CPhysical::Add) clamps only the upper index, so
// a bound rect past any grid edge writes outside CWorld::ms_aSectors. CRect
// order: left=x min, bottom=y max, right=x max, top=y min; max is exclusive.
inline bool InsideGrid(float left,float bottom,float right,float top,float minX,float minY,float maxX,float maxY){return std::isfinite(left)&&std::isfinite(bottom)&&std::isfinite(right)&&std::isfinite(top)&&left>=minX&&top>=minY&&right<maxX&&bottom<maxY;}
struct Verdict{bool drop=false,report=false;};
// One body's publication health. Incidents that cannot publish are retired
// instead of replayed every world frame. Capacity is sticky for the actor (a
// repaired or new body starts clear): further holes are refused at the
// source, while native damage and non-perforating material work continue.
struct Gate{
 static constexpr uint32_t kRetryFrames=30;
 bool saturated=false,reported=false;uint32_t deferred=0;
 bool AcceptsHoles()const{return !saturated;}
 // Capacity is reported once per actor; other failures once per episode,
 // which ends at the next committed publication.
 Verdict Failed(Failure f,bool incidents){
  Verdict v;if(f==Failure::Capacity){v.report=!saturated;saturated=true;}else{v.report=!reported;reported=true;}
  if(f==Failure::Outside)return v;
  if(f==Failure::Transient){if(++deferred>=kRetryFrames){deferred=0;v.drop=incidents;v.report|=incidents;}}else{deferred=0;v.drop=incidents;}
  return v;
 }
 void Committed(){reported=false;deferred=0;}
};
template<class Events> Verdict Retire(Gate &gate,Failure failure,Events &events){const auto v=gate.Failed(failure,!events.empty());if(v.drop)events.clear();return v;}
// Committed parent collision in publication order. Every attached fan triangle
// is recorded; col<0 marks one skipped as degenerate. positions holds three
// model-space vertices per collision triangle, surfaces its native surface.
struct Triangle{std::array<Vec3,3> rest{};uint32_t group=0;size_t source=0;int32_t col=-1;bool sweep=false;};
struct Cache{bool valid=false;uint64_t revision=0;std::vector<Triangle> triangles;std::vector<Vec3> positions;std::vector<uint8_t> surfaces,render;std::vector<uint64_t> partHash;std::vector<size_t> partCount;};
enum class Patch:uint8_t{Unchanged,Moved,Rebuild};
// Rigid part poses change only posed groups' model-space vertices; each
// group's material deformation is independent of other groups' poses. A
// degeneracy change would alter the published triangle set: Rebuild.
template<class Deform,class Degenerate> Patch PoseOnly(const Cache &cache,const std::vector<uint8_t> &posed,Deform deform,Degenerate degenerate,std::vector<Vec3> &next,bool &sweep){
 next=cache.positions;sweep=false;bool moved=false;
 for(const auto &t:cache.triangles){
  if(t.group>=posed.size()||!posed[t.group])continue;
  const std::array<Vec3,3> p{{deform(t.rest[0],t.group),deform(t.rest[1],t.group),deform(t.rest[2],t.group)}};
  if(degenerate(p[0],p[1],p[2])!=(t.col<0))return Patch::Rebuild;if(t.col<0)continue;
  if(size_t(t.col)*3+2>=next.size())return Patch::Rebuild;for(size_t k=0;k<3;++k)next[size_t(t.col)*3+k]=p[k];moved=true;sweep|=t.sweep;
 }
 return moved?Patch::Moved:Patch::Unchanged;
}
// Same result as sequential std::map<Key,Value>::operator[] assignment: one
// value per key (the last written), in ascending key order.
template<class Key,class Value> void LastByKey(std::vector<std::pair<Key,Value>> &entries,std::vector<Value> &out){
 std::stable_sort(entries.begin(),entries.end(),[](const std::pair<Key,Value> &a,const std::pair<Key,Value> &b){return a.first<b.first;});
 out.clear();out.reserve(entries.size());for(size_t i=0;i<entries.size();++i)if(i+1==entries.size()||entries[i].first<entries[i+1].first)out.push_back(entries[i].second);
}
// Exact byte digest of a part's local render triangles. Equal digest and
// count means the native mesh built from them would be identical.
struct Hash{uint64_t value=1469598103934665603ull;void Add(const void *data,size_t bytes){auto p=static_cast<const unsigned char*>(data);while(bytes--){value^=*p++;value*=1099511628211ull;}}};
}

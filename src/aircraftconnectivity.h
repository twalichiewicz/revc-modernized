#pragma once
#include "skinmodel.h"
#include "boundsindex.h"
#include <array>
#include <cstdint>
#include <limits>
#include <new>
#include <numeric>
#include <stdexcept>
#include <utility>

// Rest-space material topology. Native atomics, physics, damage and rendering
// consume this state; none of those systems owns a second connectivity model.
namespace aircraftconnectivity {
constexpr uint32_t kAllGroups=std::numeric_limits<uint32_t>::max();
struct Face {std::array<Vec3,3> positions{};uint32_t group=0;};
struct Anchor {size_t face=0;Vec3 position{};};
// A source atomic can have a real mechanical mount across an exported mesh
// gap. The child material point is supported only while its parent point is
// rooted. Both points are tested against current surviving polygons.
struct Support {Anchor part{},support{};};
struct Limits {
 size_t maxFaces=4096,maxFragments=16384,maxVertices=65535,maxNeighbors=65536,maxContacts=262144,maxHoles=8192;
 // Positive-length contacts smaller than this tolerance do not carry material.
 // This is a geometric precision limit, independent of impact dose or count.
 double tolerance=.00001;
};
struct Fragment {size_t source=0;uint32_t group=0;std::vector<skinmodel::Sample> polygon;size_t component=0;bool attached=false;};
class Mesh {
 using Polygon=std::vector<skinmodel::Sample>;
 struct Point {double x,y,z;};
 struct Contact {size_t a,b;};
 struct Neighbor {size_t a,b;std::vector<Contact> contacts;};
 struct Applied {skinmodel::Hole hole;uint32_t group;std::vector<size_t> sources;};
 struct Union {
  std::vector<size_t> p;
  explicit Union(size_t n):p(n){std::iota(p.begin(),p.end(),0);}
  size_t Find(size_t i){size_t root=i;while(p[root]!=root)root=p[root];while(p[i]!=i){const auto next=p[i];p[i]=root;i=next;}return root;}
  void Join(size_t a,size_t b){a=Find(a);b=Find(b);if(a!=b)p[std::max(a,b)]=std::min(a,b);}
 };
 std::vector<Face> faces_;
 std::vector<Anchor> anchors_;
 std::vector<Support> supports_;
 std::vector<std::vector<Polygon>> polygons_;
 std::vector<Neighbor> neighbors_;
 std::vector<bool> independent_;
 std::vector<Fragment> fragments_;
 std::vector<Applied> holes_;
 Limits limits_{};
 uint64_t revision_=0;
 static Point P(Vec3 p){return {p.x,p.y,p.z};}
 static Point Sub(Point a,Point b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
 static Point Add(Point a,Point b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
 static Point Mul(Point a,double t){return {a.x*t,a.y*t,a.z*t};}
 static double Dot(Point a,Point b){return a.x*b.x+a.y*b.y+a.z*b.z;}
 static Point Cross(Point a,Point b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
 static double Length(Point p){return std::sqrt(Dot(p,p));}
 static bool Finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
 static Point AreaNormal(const Polygon &p){Point n{};if(p.empty())return n;const auto origin=P(p[0].position);for(size_t i=1;i+1<p.size();++i)n=Add(n,Cross(Sub(P(p[i].position),origin),Sub(P(p[i+1].position),origin)));return n;}
 void Clean(Polygon &p)const{
  Polygon out;out.reserve(p.size());for(const auto &v:p)if(out.empty()||Length(Sub(P(v.position),P(out.back().position)))>limits_.tolerance)out.push_back(v);
  if(out.size()>1&&Length(Sub(P(out[0].position),P(out.back().position)))<=limits_.tolerance)out.pop_back();
  p=std::move(out);if(p.size()<3||Length(AreaNormal(p))<=limits_.tolerance*limits_.tolerance)p.clear();
 }
 bool BoxTouches(const Polygon &a,const Polygon &b)const{
  if(a.empty()||b.empty())return false;auto alo=P(a[0].position),ahi=alo,blo=P(b[0].position),bhi=blo;
  auto bounds=[](const Polygon &p,Point &lo,Point &hi){for(const auto &v:p){auto q=P(v.position);lo={std::min(lo.x,q.x),std::min(lo.y,q.y),std::min(lo.z,q.z)};hi={std::max(hi.x,q.x),std::max(hi.y,q.y),std::max(hi.z,q.z)};}};bounds(a,alo,ahi);bounds(b,blo,bhi);const auto e=limits_.tolerance;
  return alo.x<=bhi.x+e&&blo.x<=ahi.x+e&&alo.y<=bhi.y+e&&blo.y<=ahi.y+e&&alo.z<=bhi.z+e&&blo.z<=ahi.z+e;
 }
 bool EdgeOverlap(Point a,Point b,Point c,Point d)const{
  auto edge=Sub(b,a);const double length=Length(edge),e=limits_.tolerance;if(length<=e)return false;auto u=Mul(edge,1/length);const double cAt=Dot(Sub(c,a),u),dAt=Dot(Sub(d,a),u);
  if(Length(Sub(Sub(c,a),Mul(u,cAt)))>e||Length(Sub(Sub(d,a),Mul(u,dAt)))>e)return false;
  return std::min(length,std::max(cAt,dAt))-std::max(0.,std::min(cAt,dAt))>e;
 }
 bool Span(const Polygon &p,Point origin,Point normal,Point axis,double &lo,double &hi)const{
  lo=std::numeric_limits<double>::infinity();hi=-lo;auto include=[&](Point q){const auto t=Dot(q,axis);lo=std::min(lo,t);hi=std::max(hi,t);};
  for(size_t i=0;i<p.size();++i){auto a=P(p[i].position),b=P(p[(i+1)%p.size()].position);const double da=Dot(Sub(a,origin),normal),db=Dot(Sub(b,origin),normal);if(std::fabs(da)<=limits_.tolerance)include(a);if((da<0&&db>0)||(da>0&&db<0))include(Add(a,Mul(Sub(b,a),da/(da-db))));}
  return hi-lo>limits_.tolerance;
 }
 bool Touch(const Polygon &a,const Polygon &b)const{
  if(!BoxTouches(a,b))return false;auto na=AreaNormal(a),nb=AreaNormal(b);const double la=Length(na),lb=Length(nb);if(la<=limits_.tolerance*limits_.tolerance||lb<=limits_.tolerance*limits_.tolerance)return false;na=Mul(na,1/la);nb=Mul(nb,1/lb);auto axis=Cross(na,nb);const auto length=Length(axis);
  if(length>1e-7){axis=Mul(axis,1/length);double alo,ahi,blo,bhi;return Span(a,P(b[0].position),nb,axis,alo,ahi)&&Span(b,P(a[0].position),na,axis,blo,bhi)&&std::min(ahi,bhi)-std::max(alo,blo)>limits_.tolerance;}
  if(std::fabs(Dot(Sub(P(a[0].position),P(b[0].position)),nb))>limits_.tolerance)return false;
  // Edge intervals handle UV seams, opposite winding and T-junctions. A point
  // contact alone never joins two material fragments.
  for(size_t i=0;i<a.size();++i)for(size_t j=0;j<b.size();++j)if(EdgeOverlap(P(a[i].position),P(a[(i+1)%a.size()].position),P(b[j].position),P(b[(j+1)%b.size()].position)))return true;
  // Interior overlap also joins coplanar source faces without matching edges.
  std::vector<Point> clipped;clipped.reserve(a.size()+b.size());for(auto p:a)clipped.push_back(P(p.position));
  for(size_t i=0;i<b.size()&&!clipped.empty();++i){const auto origin=P(b[i].position),edge=Sub(P(b[(i+1)%b.size()].position),origin);std::vector<Point> next;next.reserve(clipped.size()+1);for(size_t j=0;j<clipped.size();++j){const auto p=clipped[j],q=clipped[(j+1)%clipped.size()];const double dp=Dot(Cross(edge,Sub(p,origin)),nb),dq=Dot(Cross(edge,Sub(q,origin)),nb);if(dp>=0)next.push_back(p);if((dp<0&&dq>0)||(dp>0&&dq<0))next.push_back(Add(p,Mul(Sub(q,p),dp/(dp-dq))));}clipped=std::move(next);}
  Point area{};for(size_t i=1;i+1<clipped.size();++i)area=Add(area,Cross(Sub(clipped[i],clipped[0]),Sub(clipped[i+1],clipped[0])));return Length(area)>limits_.tolerance*limits_.tolerance;
 }
 bool Contains(const Polygon &p,Vec3 value)const{
  if(p.size()<3)return false;auto n=AreaNormal(p);const double length=Length(n);if(length<=limits_.tolerance*limits_.tolerance)return false;n=Mul(n,1/length);const auto q=P(value);if(std::fabs(Dot(Sub(q,P(p[0].position)),n))>limits_.tolerance)return false;
  for(size_t i=0;i<p.size();++i){auto edge=Sub(P(p[(i+1)%p.size()].position),P(p[i].position));if(Dot(Cross(edge,Sub(q,P(p[i].position))),n)<-limits_.tolerance*Length(edge))return false;}return true;
 }
 bool Capacity(const std::vector<std::vector<Polygon>> &polygons)const{
  size_t count=0,vertices=0;for(const auto &face:polygons)for(const auto &p:face){if(++count>limits_.maxFragments||p.size()>32||p.size()>limits_.maxVertices-std::min(vertices,limits_.maxVertices))return false;vertices+=p.size();}return true;
 }
 bool Contacts(Neighbor &n,const std::vector<std::vector<Polygon>> &polygons,size_t &count)const{
  n.contacts.clear();for(size_t i=0;i<polygons[n.a].size();++i)for(size_t j=n.a==n.b?i+1:0;j<polygons[n.b].size();++j)if(Touch(polygons[n.a][i],polygons[n.b][j])){if(count==limits_.maxContacts)return false;++count;n.contacts.push_back({i,j});}return true;
 }
 bool Resolve(const std::vector<std::vector<Polygon>> &polygons,const std::vector<Neighbor> &neighbors,std::vector<Fragment> &out)const{
  std::vector<size_t> starts(polygons.size()+1);for(size_t i=0;i<polygons.size();++i)starts[i+1]=starts[i]+polygons[i].size();Union components(starts.back());
  for(const auto &n:neighbors)for(auto edge:n.contacts)components.Join(starts[n.a]+edge.a,starts[n.b]+edge.b);
  std::vector<bool> rooted(starts.back());for(const auto &anchor:anchors_)for(size_t j=0;j<polygons[anchor.face].size();++j)if(Contains(polygons[anchor.face][j],anchor.position))rooted[components.Find(starts[anchor.face]+j)]=true;
  // Propagate only from rooted surviving material; a cycle cannot support
  // itself. At most one new dependency layer is reached on each pass.
  auto componentAt=[&](const Anchor&a){for(size_t j=0;j<polygons[a.face].size();++j)if(Contains(polygons[a.face][j],a.position))return components.Find(starts[a.face]+j);return starts.back();};
  for(size_t pass=0;pass<supports_.size();++pass){bool changed=false;for(const auto&mount:supports_){const auto parent=componentAt(mount.support),part=componentAt(mount.part);if(parent<rooted.size()&&part<rooted.size()&&rooted[parent]&&!rooted[part]){rooted[part]=true;changed=true;}}if(!changed)break;}
  out.clear();out.reserve(starts.back());for(size_t i=0;i<polygons.size();++i)for(size_t j=0;j<polygons[i].size();++j){const auto component=components.Find(starts[i]+j);out.push_back({i,faces_[i].group,polygons[i][j],component,independent_[i]||rooted[component]});}return true;
 }
 static bool Same(Vec3 a,Vec3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
 static bool Same(const skinmodel::Hole &a,const skinmodel::Hole &b){return Same(a.centre,b.centre)&&Same(a.normal,b.normal)&&a.radius==b.radius;}
 bool BuildState(const std::vector<Face> &faces,const std::vector<Anchor> &anchors,Limits limits,const std::vector<Support>&supports){
  if(faces.empty()||faces.size()>limits.maxFaces||!std::isfinite(limits.tolerance)||limits.tolerance<=0)return false;if(supports.size()>faces.size())return false;limits_=limits;faces_=faces;anchors_=anchors;supports_=supports;polygons_.resize(faces.size());
  for(size_t i=0;i<faces.size();++i){const auto &f=faces[i];if(f.group==kAllGroups)return false;for(auto p:f.positions)if(!Finite(p))return false;Polygon p{{f.positions[0],{1,0,0}},{f.positions[1],{0,1,0}},{f.positions[2],{0,0,1}}};Clean(p);if(!p.empty())polygons_[i].push_back(std::move(p));}
  if(!Capacity(polygons_))return false;for(auto a:anchors_)if(a.face>=faces_.size()||!Finite(a.position)||polygons_[a.face].empty()||!Contains(polygons_[a.face][0],a.position))return false;
  for(const auto&mount:supports_)for(const auto&a:{mount.part,mount.support})if(a.face>=faces_.size()||!Finite(a.position)||polygons_[a.face].empty()||!Contains(polygons_[a.face][0],a.position))return false;
  // Conservative broad phase only. The existing exact polygon predicate
  // remains authoritative; sorted source IDs preserve all union/contact order.
  std::vector<boundsindex::Box> bounds;bounds.reserve(faces.size());
  const float padding=float(limits_.tolerance),infinity=std::numeric_limits<float>::infinity();
  for(const auto&face:faces){auto lo=face.positions[0],hi=lo;for(auto p:face.positions){lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}bounds.push_back({{std::nextafter(lo.x-padding,-infinity),std::nextafter(lo.y-padding,-infinity),std::nextafter(lo.z-padding,-infinity)},{std::nextafter(hi.x+padding,infinity),std::nextafter(hi.y+padding,infinity),std::nextafter(hi.z+padding,infinity)}});}
  const boundsindex::Index index(bounds);Union baseline(faces.size());for(size_t i=0;i<faces.size();++i){if(neighbors_.size()==limits_.maxNeighbors)return false;neighbors_.push_back({i,i,{}});std::vector<size_t> candidates;index.Query(bounds[i],[&](size_t j){if(j<i)candidates.push_back(j);});std::sort(candidates.begin(),candidates.end());for(size_t j:candidates)if(!polygons_[i].empty()&&!polygons_[j].empty()&&Touch(polygons_[i][0],polygons_[j][0])){if(neighbors_.size()==limits_.maxNeighbors)return false;neighbors_.push_back({j,i,{{0,0}}});baseline.Join(j,i);}}
  std::vector<bool> hasAnchor(faces.size());for(auto a:anchors_)hasAnchor[baseline.Find(a.face)]=true;for(const auto&mount:supports_){hasAnchor[baseline.Find(mount.part.face)]=true;hasAnchor[baseline.Find(mount.support.face)]=true;}independent_.resize(faces.size());for(size_t i=0;i<faces.size();++i)independent_[i]=!hasAnchor[baseline.Find(i)];size_t contacts=0;for(const auto &n:neighbors_)contacts+=n.contacts.size();if(contacts>limits_.maxContacts)return false;return Resolve(polygons_,neighbors_,fragments_);
 }
public:
 // Roots are material points, so several distributed roots can describe a
 // retained body. Baseline components without a root remain independent; they
 // are never removed merely because an artist exported disconnected islands.
 bool Build(const std::vector<Face> &faces,const std::vector<Anchor> &anchors={},Limits limits={},const std::vector<Support>&supports={}){
  try{Mesh next;if(!next.BuildState(faces,anchors,limits,supports))return false;next.revision_=revision_+1;*this=std::move(next);return true;}catch(const std::bad_alloc &){return false;}catch(const std::length_error &){return false;}
 }
 // A sorted optional source set keeps mixed-material atomic neighbors out
 // of a local fracture transaction. Empty retains the original all-source API.
 bool ApplyHole(const skinmodel::Hole &hole,uint32_t group=kAllGroups,const std::vector<size_t>&sources={}){
  if(faces_.empty()||!Finite(hole.centre)||!Finite(hole.normal)||!std::isfinite(hole.radius)||hole.radius<=0||Length(P(hole.normal))<1e-6)return false;
  if(!std::is_sorted(sources.begin(),sources.end())||std::adjacent_find(sources.begin(),sources.end())!=sources.end()||(!sources.empty()&&sources.back()>=faces_.size()))return false;
  for(const auto &old:holes_)if((old.group==group||old.group==kAllGroups)&&Same(old.hole,hole)&&(old.sources.empty()||old.sources==sources))return true;
  try{
   auto polygons=polygons_;std::vector<bool> changed(faces_.size());bool any=false;
   for(size_t i=0;i<faces_.size();++i){if((group!=kAllGroups&&faces_[i].group!=group)||(!sources.empty()&&!std::binary_search(sources.begin(),sources.end(),i)))continue;std::vector<Polygon> next;for(const auto &p:polygons_[i])if(!skinmodel::Subtract(p,hole,next))return false;for(auto &p:next)Clean(p);next.erase(std::remove_if(next.begin(),next.end(),[](const Polygon &p){return p.empty();}),next.end());
    bool same=next.size()==polygons_[i].size();for(size_t j=0;same&&j<next.size();++j){same=next[j].size()==polygons_[i][j].size();for(size_t k=0;same&&k<next[j].size();++k)same=Same(next[j][k].position,polygons_[i][j][k].position)&&Same(next[j][k].weights,polygons_[i][j][k].weights);}if(!same){polygons[i]=std::move(next);changed[i]=true;any=true;}
   }
   if(!any)return true;if(holes_.size()==limits_.maxHoles||!Capacity(polygons))return false;
   auto neighbors=neighbors_;size_t contacts=0;for(auto &n:neighbors)if(changed[n.a]||changed[n.b]){if(!Contacts(n,polygons,contacts))return false;}else{if(n.contacts.size()>limits_.maxContacts-std::min(contacts,limits_.maxContacts))return false;contacts+=n.contacts.size();}
   std::vector<Fragment> fragments;if(!Resolve(polygons,neighbors,fragments))return false;auto holes=holes_;holes.push_back({hole,group,sources});polygons_=std::move(polygons);neighbors_=std::move(neighbors);fragments_=std::move(fragments);holes_=std::move(holes);++revision_;return true;
  }catch(const std::bad_alloc &){return false;}catch(const std::length_error &){return false;}
 }
 // Transfer removes released material from the parent without compacting any
 // source face IDs. Callers can stage a Mesh copy until debris allocation and
 // its native collision geometry have succeeded.
 bool DiscardDetached(){
  bool any=false;for(const auto &f:fragments_)any|=!f.attached;if(!any)return true;
  try{
   auto polygons=polygons_;std::vector<bool> changed(faces_.size());size_t cursor=0;
   for(size_t source=0;source<polygons.size();++source){std::vector<Polygon> kept;kept.reserve(polygons[source].size());for(auto &p:polygons[source]){if(fragments_[cursor++].attached)kept.push_back(std::move(p));else changed[source]=true;}polygons[source]=std::move(kept);}
   auto neighbors=neighbors_;size_t contacts=0;for(auto &n:neighbors)if(changed[n.a]||changed[n.b]){if(!Contacts(n,polygons,contacts))return false;}else{if(n.contacts.size()>limits_.maxContacts-std::min(contacts,limits_.maxContacts))return false;contacts+=n.contacts.size();}
   std::vector<Fragment> fragments;if(!Resolve(polygons,neighbors,fragments))return false;polygons_=std::move(polygons);neighbors_=std::move(neighbors);fragments_=std::move(fragments);++revision_;return true;
  }catch(const std::bad_alloc &){return false;}catch(const std::length_error &){return false;}
 }
 // Removes every surviving polygon of these sorted source faces at once: a
 // shattered tempered pane leaves no rim. Source face IDs stay stable.
 bool RemoveSources(const std::vector<size_t> &sources){
  if(!std::is_sorted(sources.begin(),sources.end())||std::adjacent_find(sources.begin(),sources.end())!=sources.end()||(!sources.empty()&&sources.back()>=faces_.size()))return false;
  try{
   auto polygons=polygons_;std::vector<bool> changed(faces_.size());bool any=false;for(size_t source:sources)if(!polygons[source].empty()){polygons[source].clear();changed[source]=true;any=true;}if(!any)return true;
   auto neighbors=neighbors_;size_t contacts=0;for(auto &n:neighbors)if(changed[n.a]||changed[n.b]){if(!Contacts(n,polygons,contacts))return false;}else{if(n.contacts.size()>limits_.maxContacts-std::min(contacts,limits_.maxContacts))return false;contacts+=n.contacts.size();}
   std::vector<Fragment> fragments;if(!Resolve(polygons,neighbors,fragments))return false;polygons_=std::move(polygons);neighbors_=std::move(neighbors);fragments_=std::move(fragments);++revision_;return true;
  }catch(const std::bad_alloc &){return false;}catch(const std::length_error &){return false;}
 }
 const std::vector<Face> &Faces()const{return faces_;}
 const std::vector<Fragment> &Fragments()const{return fragments_;}
 uint64_t Revision()const{return revision_;}
 size_t NeighborCount()const{return neighbors_.size();}
 bool Connected(size_t faceA,Vec3 pointA,size_t faceB,Vec3 pointB)const{
  for(const auto &a:fragments_)if(a.source==faceA&&Contains(a.polygon,pointA))for(const auto &b:fragments_)if(b.source==faceB&&a.component==b.component&&Contains(b.polygon,pointB))return true;return false;
 }
};
}

#pragma once
#include "vec3.h"
#include <algorithm>
#include <numeric>
#include <vector>

// Conservative lookup only: leaves still use the engine's triangle routines.
// Rebuilt from the current COL for each pair, so no streaming pointer survives.
namespace boundsindex {
struct Box {Vec3 lo,hi;};
inline bool Overlap(const Box&a,const Box&b){return a.lo.x<=b.hi.x&&a.hi.x>=b.lo.x&&a.lo.y<=b.hi.y&&a.hi.y>=b.lo.y&&a.lo.z<=b.hi.z&&a.hi.z>=b.lo.z;}
inline Box Union(Box a,Box b){return {{std::min(a.lo.x,b.lo.x),std::min(a.lo.y,b.lo.y),std::min(a.lo.z,b.lo.z)},{std::max(a.hi.x,b.hi.x),std::max(a.hi.y,b.hi.y),std::max(a.hi.z,b.hi.z)}};}
class Index {
 struct Node {Box bounds;size_t begin,end,left=0,right=0;};
 std::vector<Box> boxes;std::vector<size_t> order;std::vector<Node> nodes;
 size_t Build(size_t begin,size_t end){
  size_t id=nodes.size();Box box=boxes[order[begin]];for(size_t i=begin+1;i<end;++i)box=Union(box,boxes[order[i]]);nodes.push_back({box,begin,end});
  if(end-begin>8){Vec3 span{box.hi.x-box.lo.x,box.hi.y-box.lo.y,box.hi.z-box.lo.z};int axis=span.x>=span.y&&span.x>=span.z?0:span.y>=span.z?1:2;size_t middle=(begin+end)/2;
   auto center=[&](size_t i){auto &b=boxes[i];return axis==0?b.lo.x+b.hi.x:axis==1?b.lo.y+b.hi.y:b.lo.z+b.hi.z;};
   std::nth_element(order.begin()+begin,order.begin()+middle,order.begin()+end,[&](size_t a,size_t b){return center(a)<center(b);});
   size_t left=Build(begin,middle),right=Build(middle,end);nodes[id].left=left;nodes[id].right=right;
  }return id;
 }
 template<class F>void Visit(size_t id,const Box &box,F &f)const{auto &n=nodes[id];if(!Overlap(n.bounds,box))return;if(n.right){Visit(n.left,box,f);Visit(n.right,box,f);}else for(size_t i=n.begin;i<n.end;++i)if(Overlap(boxes[order[i]],box))f(order[i]);}
public:
 explicit Index(std::vector<Box> input):boxes(std::move(input)),order(boxes.size()){std::iota(order.begin(),order.end(),0);nodes.reserve(boxes.size()*2);if(!boxes.empty())Build(0,boxes.size());}
 template<class F>void Query(const Box &box,F f)const{if(!nodes.empty())Visit(0,box,f);}
};
}

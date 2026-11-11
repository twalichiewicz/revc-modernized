#include "../src/aircraftconnectivity.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>
namespace {long failAllocationAfter=-1;size_t allocationCalls=0;}
void *operator new(std::size_t size){++allocationCalls;if(failAllocationAfter==0)throw std::bad_alloc();if(failAllocationAfter>0)--failAllocationAfter;if(void *p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void *p)noexcept{std::free(p);}
void operator delete(void *p,std::size_t)noexcept{std::free(p);}
using namespace aircraftconnectivity;
namespace {
int failures=0;
#define CHECK(c) do {if(!(c)){std::printf("FAIL %d: %s\n",__LINE__,#c);++failures;}} while(0)
Face Triangle(Vec3 a,Vec3 b,Vec3 c,uint32_t group=0){return {{a,b,c},group};}
std::vector<Face> Panel(){return {Triangle({0,0,0},{4,0,0},{4,2,0}),Triangle({0,0,0},{4,2,0},{0,2,0})};}
std::vector<Anchor> Roots(){return {{1,{.2f,1,0}}};}
void Band(Mesh &mesh,float end,uint32_t group=0){for(int i=0;i<=int(std::round(end/.08f));++i)CHECK(mesh.ApplyHole({{2,i*.08f,0},{0,0,1},.1f},group));}
void PanelCuts(){
 Mesh m;CHECK(m.Build(Panel(),Roots()));CHECK(m.Fragments().size()==2);CHECK(m.Connected(1,{.2f,1,0},0,{3.8f,1,0}));
 CHECK(m.ApplyHole({{2,1,0},{0,0,1},.15f},0));CHECK(m.Connected(1,{.2f,1,0},0,{3.8f,1,0}));const auto revision=m.Revision();const auto count=m.Fragments().size();
 CHECK(m.ApplyHole({{2,1,0},{0,0,1},.15f},0));CHECK(m.Revision()==revision&&m.Fragments().size()==count);
 CHECK(m.ApplyHole({{100,100,0},{0,0,1},.15f},0));CHECK(m.Revision()==revision);
 CHECK(m.Build(Panel(),Roots()));Band(m,1.44f);CHECK(m.Connected(1,{.2f,1,0},0,{3.8f,1,0}));
 for(int i=19;i<=25;++i)CHECK(m.ApplyHole({{2,i*.08f,0},{0,0,1},.1f},0));CHECK(!m.Connected(1,{.2f,1,0},0,{3.8f,1,0}));
 bool detached=false,attached=false;for(const auto &f:m.Fragments()){detached|=!f.attached;attached|=f.attached;CHECK(f.source<2&&f.group==0&&f.polygon.size()>=3);for(const auto &p:f.polygon)CHECK(std::fabs(p.weights.x+p.weights.y+p.weights.z-1)<.00001f);}CHECK(detached&&attached);
}
void TJunctionAndIslands(){
 std::vector<Face> f={Triangle({0,0,0},{1,0,0},{1,2,0}),Triangle({0,0,0},{1,2,0},{0,2,0}),Triangle({1,0,0},{2,0,0},{1,1,0}),Triangle({2,0,0},{2,1,0},{1,1,0}),Triangle({1,1,0},{2,1,0},{1,2,0}),Triangle({2,1,0},{2,2,0},{1,2,0}),Triangle({10,0,0},{11,0,0},{10,1,0},7)};
 Mesh m;CHECK(m.Build(f,{{1,{.2f,1,0}}}));CHECK(m.Connected(1,{.2f,1,0},5,{1.8f,1.8f,0}));CHECK(!m.Connected(1,{.2f,1,0},6,{10.1f,.1f,0}));CHECK(m.Fragments().back().attached);CHECK(m.ApplyHole({{2,1,0},{0,0,1},.1f},0));CHECK(m.Fragments().back().group==7&&m.Fragments().back().attached);
}
void InteriorSupport(){
 auto faces=Panel();faces.push_back(Triangle({.5f,1,-.5f},{3.5f,1,-.5f},{2,1,.5f},1));Mesh m;CHECK(m.Build(faces,Roots()));Band(m,2);CHECK(m.Connected(1,{.2f,1,0},0,{3.8f,1,0}));
 CHECK(m.ApplyHole({{2,1,0},{0,1,0},.75f},1));CHECK(!m.Connected(1,{.2f,1,0},0,{3.8f,1,0}));
}
void DegenerateSource(){
 auto f=Panel();f.push_back(Triangle({100,0,0},{101,0,0},{100,0,0}));Mesh m;CHECK(m.Build(f,Roots()));CHECK(m.Faces().size()==3&&m.Fragments().size()==2);CHECK(m.Connected(1,{.2f,1,0},0,{3.8f,1,0}));
}
void CoplanarOverlap(){
 std::vector<Face> f={Triangle({0,0,0},{3,0,0},{0,3,0}),Triangle({1,1,0},{4,1,0},{4,4,0})};Mesh m;CHECK(m.Build(f,{{0,{.1f,.1f,0}}}));CHECK(m.Connected(0,{.1f,.1f,0},1,{3.5f,1.2f,0}));
}
void DetachedTransfer(){
 Mesh m;CHECK(m.Build(Panel(),Roots()));CHECK(m.ApplyHole({{2,1,0},{0,0,1},1.25f}));size_t detached=0;for(const auto &f:m.Fragments())detached+=!f.attached;CHECK(detached>0);const auto revision=m.Revision();CHECK(m.DiscardDetached());CHECK(m.Revision()==revision+1&&m.Faces().size()==2);for(const auto &f:m.Fragments())CHECK(f.attached);CHECK(!m.Connected(1,{.2f,1,0},0,{3.8f,1,0}));const auto removed=m.Revision();CHECK(m.DiscardDetached()&&m.Revision()==removed);CHECK(m.ApplyHole({{3.8f,1,0},{0,0,1},.1f}));CHECK(m.Revision()==removed);
 Mesh probe;CHECK(probe.Build(Panel(),Roots()));CHECK(probe.ApplyHole({{2,1,0},{0,0,1},1.25f}));const auto before=allocationCalls;CHECK(probe.DiscardDetached());const auto attempts=allocationCalls-before;CHECK(attempts>0);
 for(size_t i=0;i<attempts;++i){Mesh candidate;CHECK(candidate.Build(Panel(),Roots()));CHECK(candidate.ApplyHole({{2,1,0},{0,0,1},1.25f}));const auto revision=candidate.Revision();const auto count=candidate.Fragments().size();failAllocationAfter=long(i);const bool ok=candidate.DiscardDetached();failAllocationAfter=-1;CHECK(!ok);CHECK(candidate.Revision()==revision&&candidate.Fragments().size()==count);size_t remains=0;for(const auto &f:candidate.Fragments())remains+=!f.attached;CHECK(remains>0);}
}
void AllocationTransactions(){
 Mesh sample;CHECK(sample.Build(Panel(),Roots()));const auto before=allocationCalls;CHECK(sample.ApplyHole({{2,1,0},{0,0,1},.15f},0));const auto attempts=allocationCalls-before;CHECK(attempts>0);
 for(size_t i=0;i<attempts;++i){Mesh m;CHECK(m.Build(Panel(),Roots()));const auto revision=m.Revision();failAllocationAfter=long(i);const bool ok=m.ApplyHole({{2,1,0},{0,0,1},.15f},0);failAllocationAfter=-1;CHECK(!ok);CHECK(m.Revision()==revision&&m.Fragments().size()==2&&m.Connected(1,{.2f,1,0},0,{3.8f,1,0}));}
}
void TransactionLimits(){
 Mesh m;Limits limits;limits.maxVertices=6;CHECK(m.Build(Panel(),Roots(),limits));const auto revision=m.Revision();CHECK(!m.ApplyHole({{2,1,0},{0,0,1},.15f},0));CHECK(m.Revision()==revision&&m.Fragments().size()==2&&m.Connected(1,{.2f,1,0},0,{3.8f,1,0}));
 auto invalid=Panel();invalid[0].positions[0].x=std::nanf("");CHECK(!m.Build(invalid,Roots()));CHECK(m.Revision()==revision&&m.Fragments().size()==2);
 limits.maxFaces=1;CHECK(!m.Build(Panel(),Roots(),limits));CHECK(m.Revision()==revision);CHECK(!m.ApplyHole({{2,1,0},{0,0,0},.1f}));CHECK(m.Revision()==revision);
}
}
int main(){PanelCuts();TJunctionAndIslands();InteriorSupport();DegenerateSource();CoplanarOverlap();TransactionLimits();AllocationTransactions();DetachedTransfer();if(!failures)std::puts("Aircraft surviving polygon connectivity: all passed");return failures?1:0;}

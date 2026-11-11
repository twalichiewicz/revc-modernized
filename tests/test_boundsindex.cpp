#include "boundsindex.h"
#include <cstdio>
#include <random>
#include <set>
int main(){std::mt19937 random(83);std::uniform_real_distribution<float> value(-100,100),extent(0,8);std::vector<boundsindex::Box> boxes;for(int i=0;i<3000;++i){Vec3 a{value(random),value(random),value(random)};boxes.push_back({a,{a.x+extent(random),a.y+extent(random),a.z+extent(random)}});}boundsindex::Index tree(boxes);for(int i=0;i<1000;++i){Vec3 p{value(random),value(random),value(random)};boundsindex::Box q{p,{p.x+extent(random),p.y+extent(random),p.z+extent(random)}};std::set<size_t> expected,found;for(size_t j=0;j<boxes.size();++j)if(boundsindex::Overlap(q,boxes[j]))expected.insert(j);tree.Query(q,[&](size_t j){found.insert(j);});if(found!=expected){std::puts("FAIL conservative bounds query");return 1;}}boundsindex::Index empty({});empty.Query({{0,0,0},{0,0,0}},[](size_t){std::abort();});std::puts("Bounds index matches brute force for 1000 queries over 3000 boxes");}

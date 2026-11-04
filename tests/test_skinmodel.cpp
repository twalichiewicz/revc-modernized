#include "skinmodel.h"
#include <cstdio>
#include <cmath>
int main(){using namespace skinmodel;int bad=0;auto check=[&](bool b,const char *s){if(!b){printf("FAIL: %s\n",s);++bad;}};
 std::vector<Sample> triangle{{{-1,-1,0},{1,0,0}},{{1,-1,0},{0,1,0}},{{0,1,0},{0,0,1}}};
 Hole hole{{0,0,0},{0,0,1},.15f};std::vector<std::vector<Sample>> pieces;
 check(Subtract(triangle,hole,pieces),"bounded polygon clipping succeeds");float area=0;bool centre=false;
 for(const auto &p:pieces)for(size_t i=1;i+1<p.size();i++){
  const auto a=p[0].position,b=p[i].position,c=p[i+1].position;area+=std::fabs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))*.5f;
  const float x=(a.x+b.x+c.x)/3,y=(a.y+b.y+c.y)/3;centre|=x*x+y*y<.01f;
  for(auto v:{p[0],p[i],p[i+1]})check(std::fabs(v.weights.x+v.weights.y+v.weights.z-1)<1e-5f,"cut vertices retain normalized original barycentrics");
 }
 check(area<1.95f&&area>1.90f&&!centre,"real octagonal hole removes local area without deleting the coarse original triangle");
 hole.centre.z=2;pieces.clear();check(Subtract(triangle,hole,pieces)&&pieces.size()==1&&pieces[0].size()==3,"one skin impact does not remove the far side of the airframe");
 printf("skinmodel: %s\n",bad?"FAILED":"all passed");return bad?1:0;
}

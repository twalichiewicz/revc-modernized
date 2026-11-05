#include "materialmodel.h"
#include "projectilemodel.h"
#include <cstdio>
#include <cmath>
int main(){int bad=0;auto check=[&](bool ok,const char *s){if(!ok){std::printf("FAIL: %s\n",s);++bad;}};
 mechanicalmodel::Layout layout;layout.body={{-2,-3,-1},{2,3,2}};for(auto &v:layout.volumes)v={{10,10,10},{11,11,11}};
 layout.volumes[mechanicalmodel::kEngine]={{-.5f,1,-.4f},{.5f,2.5f,.6f}};
 const auto impact=materialmodel::Contact(layout,{0,2.7f,.1f},{0,-1,0},18);
 check(impact.valid&&impact.doses[0]>0,"contact deposits work into intersected material");
 const auto load=materialmodel::ContactRay({0,2.7f,.1f},{0,-1,0},18);
 check(impact.deposited+impact.surfaceLoss+impact.remaining<=load.budget+.001f,"contact shares one conserved work budget across components");
 check(mechanicalmodel::Doses(layout,{{0,2.7f,.1f},{0,-1,0},18,3})==impact.doses,"collision adapter uses the common material deposition result");
 mechanicalmodel::State state;mechanicalmodel::Apply(state,impact.doses);const float dented=state.damage[0];
 materialmodel::Ray bullet{{0,3,.1f},{0,0,.1f},0,1,30,nullptr,0};const auto round=projectilemodel::Doses(layout,bullet);mechanicalmodel::Apply(state,round.doses);
 check(dented>0&&state.damage[0]>dented,"bullet and collision work accumulate in the same component integrity");
 materialmodel::Layer sheet{.5f,1,materialmodel::Material::Body};materialmodel::Ray outside{{-3,0,5},{3,0,5},.5f,1,20,&sheet,1};const auto outer=materialmodel::Trace(layout,outside);
 check(outer.valid&&outer.surfaceLoss==12&&outer.layerRemaining[0]==8&&outer.deposited==0,"real exterior sheets absorb material work even outside coarse body bounds");
 materialmodel::Layer repeated[]={{.5f,1,materialmodel::Material::Skin,20},{.5f,1,materialmodel::Material::Skin,20}};materialmodel::Ray patch{{-3,0,5},{3,0,5},.5f,1,100,repeated,2};const auto crushing=materialmodel::Trace(layout,patch);
 check(crushing.valid&&crushing.surfaceLoss==60&&crushing.remaining==40&&crushing.layerLoss[0]+crushing.layerLoss[1]==60,"broad contact sheet charges and exposes its work once even across duplicate faces");
 repeated[0].resistanceScale=NAN;check(!materialmodel::Trace(layout,patch).valid,"invalid patch resistance cannot introduce unbounded work");
 check(!materialmodel::Contact(layout,{NAN,0,0},{0,1,0},20).valid&&!materialmodel::Contact(layout,{0,0,0},{0,0,0},20).valid,"invalid contact cannot inject work");
 std::printf("material model: %d failures\n",bad);return bad?1:0;
}

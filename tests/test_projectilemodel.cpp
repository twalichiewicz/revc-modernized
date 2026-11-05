#if __has_include("projectilemodel.h")
#include "projectilemodel.h"
#include <cstdio>
#include <cmath>
using namespace projectilemodel;
int main(){int failures=0;auto check=[&](bool ok,const char *why){if(!ok){printf("FAIL: %s\n",why);++failures;}};
 std::array<Vec3,4>w{{{-1,1.4f,-.35f},{-1,-1.4f,-.35f},{1,1.4f,-.35f},{1,-1.4f,-.35f}}};
 mechanicalmodel::Layout front,rear;check(mechanicalmodel::Build(150,{-1.2f,-2.5f,-.7f},{1.2f,2.5f,1.2f},w,front)&&mechanicalmodel::Build(211,{-1.2f,-2.5f,-.7f},{1.2f,2.5f,1.2f},w,rear),"layout fixture");
 Layer panel{1.f/12,1,Material::Body};Ray ray{{0,3,.2f},{0,-3,.2f},1.f/12,1,20,&panel,1};
 const auto body=Doses(front,ray);check(body.valid&&body.doses[mechanicalmodel::kEngine]>0&&body.doses[mechanicalmodel::kCooling]>0,"body round reaches front cooling and engine");
 panel.fraction=0;ray.entry=0;const auto outside=Doses(front,ray);check(std::fabs(outside.doses[mechanicalmodel::kEngine]-body.doses[mechanicalmodel::kEngine])<1e-6f,"visible panel just outside original body bounds still consumes its surface budget");panel.fraction=ray.entry=1.f/12;
 auto backwards=ray;backwards.from={0,-3,.2f};backwards.to={0,3,.2f};check(Doses(rear,backwards).doses[mechanicalmodel::kEngine]>0&&Doses(rear,ray).doses[mechanicalmodel::kEngine]==0,"point path respects rear engine location");
 panel.material=Material::Glass;const auto glass=Doses(front,ray);check(glass.doses[mechanicalmodel::kEngine]>body.doses[mechanicalmodel::kEngine],"glass consumes less budget than body material");
 panel.material=Material::Body;panel.incidence=.1f;check(Doses(front,ray).doses[mechanicalmodel::kEngine]==0,"oblique body surface stops a weak round");panel.incidence=1;
 ray.budget=8;const auto stopped=Doses(front,ray);check(stopped.valid&&stopped.deposited==0&&stopped.remaining==0,"insufficient budget cannot injure modules through the panel");ray.budget=80;
 const auto strong=Doses(front,ray);check(strong.doses[mechanicalmodel::kEngine]>body.doses[mechanicalmodel::kEngine],"more budget can injure a deeper robust component");
 check(std::fabs(strong.remaining+strong.deposited+strong.surfaceLoss-80)<.001f,"surface and module absorption conserve budget");
 Layer layers[2]{{1.f/12,1,Material::Body},{.1f,1,Material::Body}};ray.layers=layers;ray.count=2;ray.budget=20;
 check(Doses(front,ray).doses[mechanicalmodel::kEngine]==0,"intervening second sheet stops downstream engine damage");
 layers[1]=layers[0];check(std::fabs(Doses(front,ray).doses[mechanicalmodel::kEngine]-body.doses[mechanicalmodel::kEngine])<1e-6f,"coincident mesh triangles do not charge material twice");
 ray.layers=&panel;ray.count=1;ray.limit=.1f;check(Doses(front,ray).doses[mechanicalmodel::kEngine]==0,"nearer external cover clips internal penetration");ray.limit=1;
 ray.from={1.2f,0,.6f};ray.to={-1.2f,0,.6f};ray.entry=0;panel.fraction=0;const auto cabin=Doses(front,ray);check(cabin.doses[mechanicalmodel::kEngine]==0&&cabin.doses[mechanicalmodel::kCooling]==0,"door/cabin hit does not damage engine through collision spread");
 ray.from={-1.5f,1.4f,-.35f};ray.to={0,1.4f,-.35f};panel.material=Material::Rubber;ray.budget=40;const auto corner=Doses(front,ray);
 check(corner.doses[mechanicalmodel::kWheelLF]>0&&corner.doses[mechanicalmodel::kBrakeLF]>0&&corner.doses[mechanicalmodel::kWheelRF]==0,"local wheel/caliper path leaves opposite assembly intact");
 check(std::fabs(corner.remaining+corner.deposited+corner.surfaceLoss-40)<.001f,"overlapping module volumes share absorption without creating energy");
 ray.count=65;check(!Doses(front,ray).valid,"bounded path rejects layer overflow");ray.count=1;ray.budget=NAN;check(!Doses(front,ray).valid,"invalid budget rejected");ray.budget=20;ray.to=ray.from;check(!Doses(front,ray).valid,"degenerate ray rejected");
 {mechanicalmodel::Box body{{-1,-1,-1},{1,1,1}};std::array<mechanicalmodel::Box,mechanicalmodel::kModules> systems;systems.fill({{.5f,-.2f,-.2f},{.8f,.2f,.2f}});float r[mechanicalmodel::kModules]{},c[mechanicalmodel::kModules];std::fill(c,c+mechanicalmodel::kModules,100.f);r[0]=20;
 Structure spars;spars.volumes.fill({{-.2f,-.2f,-.2f},{.2f,.2f,.2f}});spars.resistance={80,0,0};spars.capacity={100,100,100};Layer skin{.25f,1,Material::Skin};Ray sparShot{{-2,0,0},{2,0,0},.25f,1,20,&skin,1};auto stopped=Absorb(body,systems,r,c,sparShot,&spars);
 check(stopped.valid&&stopped.structuralDoses[0]>0&&stopped.doses[0]==0&&stopped.remaining==0,"real structural member consumes the shared bullet energy before a farther functional system");check(std::fabs(stopped.surfaceLoss+stopped.deposited+stopped.remaining-20)<1e-5f,"skin, structural and functional injury conserve one projectile budget");sparShot.limit=.4f;auto cover=Absorb(body,systems,r,c,sparShot,&spars);check(cover.structuralDoses[0]==0,"nearer cover prevents structural damage beyond its accepted contact");
 }
 printf("projectilemodel: %s\n",failures?"FAILED":"all passed");return failures?1:0;
}
#else
#include <cstdio>
int main(){puts("FAIL: material-limited module projectile model is missing");return 1;}
#endif

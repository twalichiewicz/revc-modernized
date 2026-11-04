#include "airframemodel.h"
#include <cstdio>
#include <cmath>
int main(){using namespace airframemodel;int bad=0;auto check=[&](bool b,const char *s){if(!b){printf("FAIL: %s\n",s);++bad;}};Layout jet;check(Build(180,4,{-10,-10,-2},{10,10,2},jet),"airframe layout from original bounds");
 State cold;projectilemodel::Layer skin{0,1,projectilemodel::Material::Skin};projectilemodel::Ray ray{{-12,0,0},{0,0,0},0,1,100,&skin,1};
 for(int i=0;i<20;i++)Projectile(cold,jet,ray);auto e=Read(cold,jet);check(e.leftLift<.5f&&e.rightLift==1,"localized wing hits reduce only the struck lifting surface");check(cold.damage[EngineR]==0&&cold.damage[TailRotor]==0,"wing path does not injure unrelated propulsion or helicopter parts");
 State engine;const auto box=jet.volumes[EngineL];Vec3 p{(box.lo.x+box.hi.x)*.5f,(box.lo.y+box.hi.y)*.5f,(box.lo.z+box.hi.z)*.5f};Impact(engine,jet,p,10);check(Read(engine,jet).power<1&&Read(engine,jet).rightLift==1,"engine collision independently affects propulsion");
 State tail;Impact(tail,jet,{0,-9,0},10);check(Read(tail,jet).pitch<1&&Read(tail,jet).power==1,"tail damage affects controls without converting hull health to engine damage");
 Layout heli;Build(217,0,{-3,-7,-2},{3,5,3},heli);State rotor;Impact(rotor,heli,{0,0,2.7f},10);check(Read(rotor,heli).rotor<1,"rotor volume is separate from fixed-wing components");
 State leaking;leaking.damage[FuelL]=.8f;leaking.damage[Electrical]=.9f;Tick(leaking,1,true);auto f=Fire(leaking,true);check(f.breach>.5f&&f.electrical>.5f&&leaking.fuel<1,"leak and short expose real progressive fire sources");
 printf("airframemodel: %s\n",bad?"FAILED":"all passed");return bad?1:0;
}

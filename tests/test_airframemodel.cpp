#include "airframemodel.h"
#include "materialmodel.h"
#include <cstdio>
#include <cmath>
int main(){using namespace airframemodel;int bad=0;auto check=[&](bool b,const char *s){if(!b){printf("FAIL: %s\n",s);++bad;}};Layout jet;check(Build(180,4,{-10,-10,-2},{10,10,2},jet),"airframe layout from original bounds");
 State cold;projectilemodel::Layer skin{0,1,projectilemodel::Material::Skin};projectilemodel::Ray ray{{-12,0,0},{0,0,0},0,1,100,&skin,1};
 for(int i=0;i<20;i++)Projectile(cold,jet,ray);auto e=Read(cold,jet);check(e.leftLift<.5f&&e.rightLift==1,"localized wing hits reduce only the struck lifting surface");check(cold.damage[EngineR]==0&&cold.damage[TailRotor]==0,"wing path does not injure unrelated propulsion or helicopter parts");
 State engine;const auto box=jet.volumes[EngineL];Vec3 p{(box.lo.x+box.hi.x)*.5f,(box.lo.y+box.hi.y)*.5f,(box.lo.z+box.hi.z)*.5f};Impact(engine,jet,p,10);check(Read(engine,jet).power<1&&Read(engine,jet).rightLift==1,"engine collision independently affects propulsion");
 State tail;Impact(tail,jet,{0,-9,0},10);check(Read(tail,jet).pitch<1&&Read(tail,jet).power==1,"tail damage affects controls without converting hull health to engine damage");
 Layout heli;Build(217,0,{-3,-7,-2},{3,5,3},heli);State rotor;Impact(rotor,heli,{0,0,2.7f},10);check(Read(rotor,heli).rotor<1,"rotor volume is separate from fixed-wing components");
 Layout maverick;Build(227,0,{-1.1647055f,-7.520007f,-.6669215f},{1.1580596f,3.580109f,2.1271734f},maverick);
 State cabin;projectilemodel::Ray openCabin{{-.8f,.5f,1.6f},{.8f,.5f,1.6f},0,1,80,nullptr,0};auto emptyCabin=Projectile(cabin,maverick,openCabin);
 check(emptyCabin.deposited==0&&emptyCabin.remaining==80&&cabin.structure==State{}.structure,"empty cabin between existing openings cannot absorb work as invisible cage metal");
 State piercedBoom;projectilemodel::Ray acrossBoom{{-2,-6.5f,1.4f},{2,-6.5f,1.4f},.3f,1,80,nullptr,0};
 for(int i=0;i<40;i++)Projectile(piercedBoom,maverick,acrossBoom);
 check(piercedBoom.structure[2]>0&&Read(piercedBoom,maverick).antiTorque<1,"transverse shots through the actual Maverick tail boom damage its load-bearing material and tail function");
 State crash;Impact(crash,maverick,{-.11f,3.48f,.13f},29.30f,{0,-1,0});Impact(crash,maverick,{-.61f,-7.12f,1.43f},13.43f,{0,1,0});
 check(crash.structure[0]+crash.structure[1]+crash.structure[2]>0,"recorded Maverick nose and tail impacts load the airframe structure");
 State leaking;leaking.damage[FuelL]=.8f;leaking.damage[Electrical]=.9f;Tick(leaking,1,true);auto f=Fire(leaking,true);check(f.breach>.5f&&f.electrical>.5f&&leaking.fuel<1,"leak and short expose real progressive fire sources");
 State wingBreak;projectilemodel::Layer wingSkin{.25f,1,projectilemodel::Material::Skin};projectilemodel::Ray rootHit{{-2.4f,0,4},{-2.4f,0,-4},.25f,1,80,&wingSkin,1};for(int i=0;i<12;i++)Projectile(wingBreak,jet,rootHit);check(wingBreak.structure[0]==1&&wingBreak.structure[1]==0&&Read(wingBreak,jet).leftLift==0,"localized structural wing failure removes only that section's lift");
 State tip;projectilemodel::Ray tipHit{{-8,0,4},{-8,0,-4},.25f,1,80,&wingSkin,1};for(int i=0;i<12;i++)Projectile(tip,jet,tipHit);check(tip.structure[0]==0,"wing-tip skin damage cannot pretend to break an unstruck root spar");
 wingBreak.separated|=1;projectilemodel::Ray missingWing{{-12,0,0},{-4,0,0},.25f,1,80,nullptr,0};const auto emptyPath=Projectile(wingBreak,jet,missingWing);check(emptyPath.deposited==0&&emptyPath.remaining==80,"a separated wing and engine pod cannot absorb bullets as phantom material");check(wingBreak.damage[EngineL]==1&&wingBreak.damage[EngineR]==0,"engine pod removed with its wing loses that engine's function only");
 State boom;projectilemodel::Layer boomSkin{.2f,1,projectilemodel::Material::Skin};projectilemodel::Ray boomHit{{0,-10,1.6f},{0,5,1.6f},.2f,1,80,&boomSkin,1};for(int i=0;i<12;i++)Projectile(boom,heli,boomHit);check(boom.structure[2]==1&&Read(boom,heli).antiTorque==0&&Read(boom,heli).rotor==1,"helicopter tail structure failure takes tail function while preserving main rotor");
 check(Read(boom,heli).pitch==1&&Read(boom,heli).roll==1,"tail separation preserves helicopter cyclic controls instead of inventing fixed-wing surface failures");
 State contact,energy;const Vec3 contactPoint{-8,0,0},contactNormal{1,0,0};const auto work=materialmodel::ContactRay(contactPoint,contactNormal,20);
 Impact(contact,jet,contactPoint,20,contactNormal);Projectile(energy,jet,work);
 check(contact.damage==energy.damage&&contact.structure==energy.structure,"aircraft collision and projectile adapters use the same material work solver and integrity");
 printf("airframemodel: %s\n",bad?"FAILED":"all passed");return bad?1:0;
}

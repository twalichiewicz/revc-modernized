#include "../src/log.h"
#define main ExistingShotTests
#include "test_shotcollision.cpp"
#undef main
static void *ControlSymbol(void *,const char *);
#define dlsym ControlSymbol
#include "../src/aircraftcontrols.cpp"
#undef dlsym
static void *ControlSymbol(void *h,const char *s){return fixture::Lookup(h,s);}
int main(){using namespace fixture;Check(aircraftcontrols::Install(),"aircraft control bindings install");
 struct Case {const char *name;int model;V seat;};for(const auto c:{Case{"polmav",227,{-.417f,1.476f,-.0995f}},Case{"maverick",217,{-.4166f,1.4765f,-.0808f}},Case{"sparrow",199,{-.417f,.953f,-.0686f}},Case{"hunter",155,{0,3.1463f,-.1063f}}}){
  Setup();Check(LoadAircraftParts(c.name),"stock cockpit loads");*reinterpret_cast<int16_t *>(cars.data()+0x6a)=int16_t(c.model);auto *ped=peds.data();*reinterpret_cast<V *>(ped+0x38)=c.seat;ped[0x4c0]=1;*reinterpret_cast<void **>(ped+0x4b8)=cars.data();wheelgrip::Grip left,right;
  const bool found=aircraftcontrols::Grips(cars.data(),ped,left,right);Check(found,"actual stock control supplies two hand tracking points");
  if(found){Check(std::isfinite(right.hand.pos.x)&&right.hand.pos.y>c.seat.y&&right.hand.pos.y<c.seat.y+.9f,"stick hand tracks in front of the seated pilot");const auto before=right.hand.pos;*reinterpret_cast<float *>(cars.data()+0x38)=10;*reinterpret_cast<float *>(ped+0x38)+=10;Check(aircraftcontrols::Grips(cars.data(),ped,left,right)&&std::fabs(right.hand.pos.x-before.x-10)<1e-4f,"control anchor follows vehicle pose, not fixed world positions");}
 }
 Setup();Check(LoadAircraftParts("polmav"),"Learjet control donor loads");for(auto &a:clump.atomics)for(auto &p:a.geo.verts)p={p.x*.76f,5+(p.y-1.476f)*.68f,-.65f+(p.z+.09952587f)*.85f};*reinterpret_cast<int16_t *>(cars.data()+0x6a)=248;*reinterpret_cast<int *>(cars.data()+0x348)=4;*reinterpret_cast<V *>(peds.data()+0x38)={-.334f,5,-.65f};wheelgrip::Grip left,right;Check(aircraftcontrols::Grips(cars.data(),peds.data(),left,right),"actual transformed Learjet cockpit donor retains control tracking points");
 std::printf("aircraft control assets: %d failures\n",fail);return fail?1:0;}

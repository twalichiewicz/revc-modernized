// Real CPlane -> fixed-wing adapter -> three wheels -> native effective-mass
// friction consumer. Retain actual DEADDODO handling and world orientation.
#define JETWHEELS_NATIVE_FLIGHT
#define main ExistingWheelChecks
#include "test_jetwheels.cpp"
#undef main
#define dlsym WheelSymbol
#ifndef PLANEFLIGHT_SOURCE
#define PLANEFLIGHT_SOURCE "../src/planeflight.cpp"
#endif
#include PLANEFLIGHT_SOURCE
#undef dlsym
namespace {
alignas(16) uint8_t groundInfo[0x258]{},groundCollision[0x68]{};
uint8_t *groundModels[6500]{};
int16_t Pitch(uint8_t *){return 0;}int16_t Throttle(uint8_t *){return int16_t(gas*255);}bool Look(uint8_t *){return false;}
void *FlightSymbol(const char *name){
 if(std::strstr(name,"GetSteeringUpDown")||std::strstr(name,"GetCarGun"))return reinterpret_cast<void *>(Pitch);
 if(std::strstr(name,"GetAccelerate"))return reinterpret_cast<void *>(Throttle);
 if(std::strstr(name,"GetLook"))return reinterpret_cast<void *>(Look);return nullptr;
}
void FlightNative(uint8_t *p)
{
 ++nativeCalls;V(p,0x84).z-=.008f*step;
 const float resistance=std::pow(1/std::fabs(1+F(p,0xd8)*.5f*DotW(V(p,0x84),V(p,0x84))),step);
 V(p,0x84)=Times(V(p,0x84),resistance);V(p,0x90)=Times(V(p,0x90),.99f);
 Check(planeflight::Apply(p,5),"genuine owned CPlane reaches fixed-wing controller before its native wheel contacts");
}
void PrepareGround()
{
 Reset();F(v,0xd8)=.00016f;F(v,0x28c)=.5f;gas=0;
 groundModels[248]=groundInfo;*reinterpret_cast<uint8_t **>(groundInfo+0x20)=groundCollision;
 V(groundCollision,0x10)={-10,-9,jetwheels::kTireBottom};game::api.modelInfoPtrs=groundModels;
}
float Beta(){return std::atan2(DotW(V(v,0x84),V(v,8)),DotW(V(v,0x84),V(v,0x18)));}
}
int main()
{
 control=FlightNative;gWheelExtraSymbol=FlightSymbol;
 game::api.FindPlayerPed=Player;game::api.GetPad=Pad;game::api.PoolsGetVehicle=Vehicle;game::api.ProcessVerticalLine=Ray;game::api.timeStep=&step;game::api.timeMs=&clockMs;
 *reinterpret_cast<uint8_t **>(pool)=v;*reinterpret_cast<uint8_t **>(pool+8)=flags;*reinterpret_cast<int32_t *>(pool+0x10)=1;
 planeflight::Install();planeflight::SetAirportOwner(airportjets::Owns);Check(jetwheels::Install(),"full fixed-wing/wheel chain installs");
 for(float tick:{.5f,1.0f,1.6666667f})for(float direction:{-1.0f,1.0f}){
  PrepareGround();step=tick;V(v,0x84)={.04f*direction,.2f,0};
  for(int i=0;i<int(1/(step/50));++i)Step();
  Check(std::fabs(Beta())<.03f,"native runway adhesion removes a2m/s lateral gust without fabricated lateral forces");
  PrepareGround();step=tick;V(v,0x84).y=.12f;steer=direction*32;
  for(int i=0;i<int(6/(step/50));++i)Step();
  const float heading=std::atan2(V(v,0x18).x,V(v,0x18).y),beta=Beta();
  std::printf("ground %.0fHz steer%+.0f heading%+.3f beta%+.3f speed%.2f\n",50/tick,direction,heading,beta,std::sqrt(DotW(V(v,0x84),V(v,0x84)))*50);
  Check(heading*direction>.08f&&std::fabs(beta)<.1f,"actual contact steering changes the aircraft path with bounded tire slip");
  Check(std::fabs(V(v,0x38).z+jetwheels::kTireBottom)<.1f,"rolling turn retains three-gear support without body collision contacts");
 }
 for(float tick:{50/30.0f,50/120.0f}){
  float final[2]{};
  for(int braking=0;braking<2;++braking){
   PrepareGround();step=tick;V(v,0x84).y=.5f;brake=braking?255:0;
   for(int i=0;i<int(8/(step/50));++i)Step();
   final[braking]=std::sqrt(DotW(V(v,0x84),V(v,0x84)))*50;
  }
  std::printf("ground brake %.0fHz coast%.2f brake%.2fm/s\n",50/tick,final[0],final[1]);
  Check(final[1]<final[0]-3&&final[1]<18,"native tire braking remains effective after jet clean-flight profile change");
 }
#if __has_feature(address_sanitizer)
 __asan_unpoison_memory_region(v,sizeof v);
#endif
 if(!fails)puts("jet ground native: all passed");return fails?1:0;
}

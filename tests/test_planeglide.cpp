// Full fixed-wing consumer: airborne wheel brake must not add a drag device.
#define VCMM_PLANE_ENGINE_NO_MAIN
#include "test_planeengine.cpp"
#undef VCMM_PLANE_ENGINE_NO_MAIN
namespace {
struct Glide {CVector position,speed;};
Glide RunGlide(int model,float dt,int brake)
{
 PrepareEngine(model);gThrottle=0;gBrake=brake;gBoat[0x295]&=~uint8_t(16);
 V(gBoat,0x38)={0,0,1000};V(gBoat,0x84)={0,1.2f,-.12f};
 for(int frame=0;frame<int(std::lround(8/dt));++frame)EngineStep(dt,false);
 return {V(gBoat,0x38),Times(V(gBoat,0x84),50)};
}
}
int main()
{
 gExtraSymbol=EngineSymbol;Reset();planeflight::Install();CHECK(planeflight::gForcesReady);
 for(int model:{190,181,248})for(float dt:{1/30.0f,1/120.0f}){
  const auto coast=RunGlide(model,dt,0),brake=RunGlide(model,dt,255);
  std::printf("glide model%d %.0fHz coast %.2fm/s %.1fm; brake %.2fm/s %.1fm\n",model,1/dt,
      Magnitude(coast.speed),coast.position.y,Magnitude(brake.speed),brake.position.y);
  CHECK(Magnitude(Plus(brake.speed,Times(coast.speed,-1)))<.001f);
  CHECK(Magnitude(Plus(brake.position,Times(coast.position,-1)))<.001f);
  // Lift may trade initial airspeed for height. The aircraft retains forward
  // flight while normal drag dissipates its total mechanical energy.
  CHECK(coast.position.y>200&&Magnitude(coast.speed)>20&&Magnitude(coast.speed)<60);
  const float energy=20*coast.position.z+.5f*Magnitude(coast.speed)*Magnitude(coast.speed);
  CHECK(coast.position.z>700&&energy<21818&&energy>20000);
 }
 if(!failures)std::puts("plane glide native consumer: all passed");return failures?1:0;
}

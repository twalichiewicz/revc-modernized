// Actual native fixed-wing force consumer, including pool identity and clock.
#define main ExistingPlaneChecks
#include "test_planeflight.cpp"
#undef main
#include <fstream>
#include <sstream>
#include <string>
namespace {
alignas(16) uint8_t enginePool[0x20]{};
uint8_t engineFlags[1]{1},*enginePoolPtr=enginePool;
uint32_t engineClock=1000;
uint8_t *EngineVehicle(int ref){return ref==engineFlags[0]&&!(engineFlags[0]&128)?gBoat:nullptr;}
void *EngineSymbol(const char *name){return std::strstr(name,"ms_pVehiclePool")?&enginePoolPtr:nullptr;}
bool EngineOwner(const uint8_t *p){return p==gBoat&&game::EntityModel(p)>=240&&game::EntityModel(p)<=249;}
void PrepareEngine(int model)
{
 Reset();++engineFlags[0];engineClock+=1000;game::api.timeMs=&engineClock;game::api.PoolsGetVehicle=EngineVehicle;
 *reinterpret_cast<uint8_t **>(enginePool)=gBoat;*reinterpret_cast<uint8_t **>(enginePool+8)=engineFlags;*reinterpret_cast<int32_t *>(enginePool+16)=1;
 *reinterpret_cast<void **>(gBoat)=reinterpret_cast<void *>(uintptr_t(0x1000));
 *reinterpret_cast<int16_t *>(gBoat+0x6a)=int16_t(model);*reinterpret_cast<int32_t *>(gBoat+0x348)=model==190?1:4;
 planeflight::gPlane={};if(model==181)planeflight::gPlane={int(engineFlags[0]),gBoat,*reinterpret_cast<void **>(gBoat),0,0};
 planeflight::SetAirportOwner(EngineOwner);planeflight::SetAirportWheelControl(nullptr);
 V(gBoat,0x38)={0,0,1.4f};V(gBoat,0x84)={};
}
void EngineStep(float dt,bool support=true)
{
 gStep=dt*50;V(gBoat,0x84).z-=.008f*gStep;
 const float resistance=std::pow(1/std::fabs(1+*reinterpret_cast<float *>(gBoat+0xd8)*.5f*planeflight::Dot(V(gBoat,0x84),V(gBoat,0x84))),gStep);
 V(gBoat,0x84)=Times(V(gBoat,0x84),resistance);V(gBoat,0x90)=Times(V(gBoat,0x90),.99f);
 if(support&&V(gBoat,0x38).z<=1.4f){V(gBoat,0x38).z=1.4f;V(gBoat,0x84).z=std::max(0.0f,V(gBoat,0x84).z);}
 CHECK(planeflight::Apply(gBoat,game::EntityModel(gBoat)==190?3:5));
 V(gBoat,0x38)=Plus(V(gBoat,0x38),Times(V(gBoat,0x84),gStep));
 const CVector angular=Times(V(gBoat,0x90),gStep);
 V(gBoat,0x18)=Normal(Plus(V(gBoat,0x18),Cross(angular,V(gBoat,0x18))));
 V(gBoat,0x28)=Normal(Plus(V(gBoat,0x28),Cross(angular,V(gBoat,0x28))));
 V(gBoat,8)=Normal(Cross(V(gBoat,0x18),V(gBoat,0x28)));V(gBoat,0x28)=Normal(Cross(V(gBoat,8),V(gBoat,0x18)));
 engineClock+=uint32_t(std::lround(dt*1000));
}
float ForwardSpeed(){return 50*planeflight::Dot(V(gBoat,0x84),V(gBoat,0x18));}
[[maybe_unused]] void Profile(int model,float dt)
{
 PrepareEngine(model);float speed[3]{};int captured=0;
 for(int frame=1;frame<=int(std::lround(5/dt));++frame){EngineStep(dt);if(captured<3&&frame==int(std::lround((1+captured*2)/dt)))speed[captured++]=ForwardSpeed();}
 std::printf("engine model%d %.0fHz v1/3/5s %.2f %.2f %.2fm/s\n",model,1/dt,speed[0],speed[1],speed[2]);
 CHECK(captured==3&&speed[0]>.5f&&speed[0]<4&&speed[1]>8&&speed[1]<18&&speed[2]>20&&speed[2]<31);
 float peak=ForwardSpeed();gPitch=20;for(int frame=0;frame<int(35/dt);++frame){EngineStep(dt);peak=std::max(peak,ForwardSpeed());}
 std::printf("  takeoff height%.1f speed%.1fm/s peakForward%.1f\n",V(gBoat,0x38).z,Magnitude(V(gBoat,0x84))*50,peak);
 CHECK(V(gBoat,0x38).z>100&&peak>40);
}
struct Level {float speed,height,vertical,maxSlip;};
[[maybe_unused]] Level LevelFlight(int model,float dt,float height=500,float health=1000,float side=0,float initialSpeed=60)
{
 PrepareEngine(model);*reinterpret_cast<float *>(gBoat+0x2a0)=health;
 V(gBoat,0x38).z=height;V(gBoat,0x84)={side/50,initialSpeed/50,0};
 float integral=0,sum=0,maxSlip=0;int samples=0;
 for(int frame=0;frame<int(std::lround(300/dt));++frame){
  const float error=height-V(gBoat,0x38).z,vertical=50*V(gBoat,0x84).z;
  integral=std::clamp(integral+error*.06f*dt,-90.0f,90.0f);
  gPitch=int(std::lround(std::clamp(error*.6f-vertical*5+integral,-110.0f,110.0f)));
  EngineStep(dt,false);
  const float slip=std::atan2(planeflight::Dot(V(gBoat,0x84),V(gBoat,8)),planeflight::Dot(V(gBoat,0x84),V(gBoat,0x18)));
  maxSlip=std::max(maxSlip,std::fabs(slip));
  if(frame>int(270/dt)){sum+=Magnitude(V(gBoat,0x84))*50;++samples;}
 }
 return {sum/std::max(1,samples),V(gBoat,0x38).z,50*V(gBoat,0x84).z,maxSlip};
}
[[maybe_unused]] float RoadCarSpeed()
{
 const char *env=std::getenv("GAMEDATA");std::ifstream file(std::string(env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData")+"/data/handling.cfg");
 CHECK(bool(file));float maximum=0;int found=0;
 for(std::string line;std::getline(file,line);){std::istringstream row(line);std::string name;row>>name;if(name!="INFERNUS"&&name!="CHEETAH")continue;
  float value=0;for(int field=0;field<13;++field)row>>value;
  CHECK(row&&value==300);maximum=std::max(maximum,value/3.6f);++found;
 }
 CHECK(found==2);return maximum;
}
[[maybe_unused]] float FirstImpulse(){gImpulse={};CHECK(planeflight::Apply(gBoat,game::EntityModel(gBoat)==190?3:5));return gImpulse.y/5000*2500;}
}
#ifndef VCMM_PLANE_ENGINE_NO_MAIN
int main()
{
 gExtraSymbol=EngineSymbol;Reset();planeflight::Install();
 for(int model:{190,181,248})for(float dt:{1/30.0f,1/120.0f})Profile(model,dt);
 const float road=RoadCarSpeed();
 for(float dt:{1/30.0f,1/120.0f}){
  const auto jet=LevelFlight(248,dt);
  std::printf("level jet %.0fHz %.2fm/s height%.2f vz%.3f slip%.3f; road%.2fm/s\n",1/dt,jet.speed,jet.height,jet.vertical,jet.maxSlip,road);
  CHECK(jet.speed>road*1.25f&&jet.speed>=105&&jet.speed<=120);
  CHECK(std::fabs(jet.height-500)<5&&std::fabs(jet.vertical)<1);
 }
 for(float dt:{1/30.0f,1/120.0f}){
  for(float height:{300.0f,1500.0f,3000.0f}){
   const auto result=LevelFlight(248,dt,height);
   std::printf("jet envelope %.0fHz altitude%.0f speed%.2f height%.2f vz%.3f\n",1/dt,height,result.speed,result.height,result.vertical);
   CHECK(result.speed>=105&&result.speed<=120&&std::fabs(result.height-height)<5&&std::fabs(result.vertical)<1);
  }
  const auto injured=LevelFlight(248,dt,500,425);
  std::printf("jet injured %.0fHz halfpower speed%.2f\n",1/dt,injured.speed);CHECK(injured.speed>65&&injured.speed<95);
  for(int model:{190,181}){
   const auto legacy=LevelFlight(model,dt);CHECK(legacy.speed>72&&legacy.speed<76);
   std::printf("legacy%d level %.0fHz speed%.2f\n",model,1/dt,legacy.speed);
  }
  const auto remapped=LevelFlight(247,dt);CHECK(remapped.speed>=105&&remapped.speed<=120);
  for(float side:{-8.0f,8.0f}){
   const auto gust=LevelFlight(248,dt,500,1000,side,115);
   CHECK(gust.speed>=105&&gust.speed<=120&&gust.maxSlip<.1f&&std::fabs(gust.height-500)<5);
  }
  PrepareEngine(248);V(gBoat,0x38).z=1000;V(gBoat,0x84).y=.9f;
  V(gBoat,0x18)={0,std::cos(.8f),std::sin(.8f)};V(gBoat,0x28)={0,-std::sin(.8f),std::cos(.8f)};
  for(int i=0;i<int(12/dt);++i){gPitch=i<int(3/dt)?-32:0;EngineStep(dt,false);}
  const auto speed=Times(V(gBoat,0x84),50);
  const float alpha=std::atan2(-planeflight::Dot(speed,V(gBoat,0x28)),planeflight::Dot(speed,V(gBoat,0x18)));
  std::printf("jet stall recovery %.0fHz alpha%.3f speed%.2f altitude%.2f\n",1/dt,alpha,Magnitude(speed),V(gBoat,0x38).z);
  CHECK(std::isfinite(alpha)&&std::fabs(alpha)<.3f&&Magnitude(speed)>25&&Magnitude(speed)<120&&V(gBoat,0x38).z>700);
 }
 // The true rolling controller keeps the original force curve even at high
 // runway speed; the clean profile cannot undo tire braking after landing.
 for(float speed:{10.0f,20.0f,35.0f,110.0f}){
  PrepareEngine(248);V(gBoat,0x84).y=speed/50;
  planeflight::SetAirportWheelControl([](const uint8_t *v){return v==gBoat;});const float jet=FirstImpulse();
  PrepareEngine(181);V(gBoat,0x84).y=speed/50;const float legacy=FirstImpulse();
  CHECK(std::fabs(jet-legacy)<.0001f);
 }
 // Exhaust/jetwash reads the SAME spool used by the force consumer, not
 // raw full-throttle input. Health and engine state constrain it immediately.
 PrepareEngine(248);CHECK(planeflight::EnginePower(gBoat)==0);EngineStep(.02f);
 CHECK(planeflight::EnginePower(gBoat)>0&&planeflight::EnginePower(gBoat)<.02f);
 for(int i=1;i<50;++i)EngineStep(.02f);
 const float spool=planeflight::EnginePower(gBoat);CHECK(spool>.38f&&spool<.41f);
 gThrottle=0;CHECK(planeflight::EnginePower(gBoat)==spool);EngineStep(.02f);
 const float coasting=planeflight::EnginePower(gBoat);CHECK(coasting<spool&&coasting>.35f);
 *reinterpret_cast<float *>(gBoat+0x2a0)=425;CHECK(std::fabs(planeflight::EnginePower(gBoat)-coasting*.5f)<.0001f);
 CHECK(planeflight::EnginePower(nullptr)==0);
 for(int invalid=0;invalid<11;++invalid){
  PrepareEngine(248);for(int i=0;i<100;++i)EngineStep(.02f);CHECK(planeflight::EnginePower(gBoat)>.6f);
  if(invalid==0)++engineFlags[0];
  if(invalid==1)*reinterpret_cast<void **>(gBoat)=reinterpret_cast<void *>(uintptr_t(0x3000));
  if(invalid==2)*reinterpret_cast<int16_t *>(gBoat+0x6a)=249;
  if(invalid==3)engineClock=1;
  if(invalid==4)engineClock+=251;
  if(invalid==5)gBoat[0x295]=0;
  if(invalid==6)*reinterpret_cast<uint32_t *>(gBoat+0x60)=2|(5<<3);
  if(invalid==7)*reinterpret_cast<float *>(gBoat+0x2a0)=NAN;
  if(invalid==8)*reinterpret_cast<uint8_t **>(gBoat+0x218)=nullptr;
  if(invalid==9)planeflight::SetAirportOwner(nullptr);
  if(invalid==10)*reinterpret_cast<int32_t *>(enginePool+16)=2;
  CHECK(planeflight::EnginePower(gBoat)==0);
 }
 PrepareEngine(190);for(int i=0;i<100;++i)EngineStep(.02f);CHECK(planeflight::EnginePower(gBoat)==0);
 PrepareEngine(181);for(int i=0;i<100;++i)EngineStep(.02f);CHECK(planeflight::EnginePower(gBoat)>.6f);
 // Fresh airborne actors retain the existing steady flight envelope.
 PrepareEngine(248);V(gBoat,0x38).z=1500;V(gBoat,0x84).y=1.2f;CHECK(FirstImpulse()>1);
 // Recycled native slots and rewind cannot inherit a previously running engine.
 PrepareEngine(248);for(int i=0;i<400;++i)EngineStep(.02f);++engineFlags[0];V(gBoat,0x84)={};V(gBoat,0x38).z=1.4f;CHECK(FirstImpulse()<.2f);
 for(int i=0;i<400;++i)EngineStep(.02f);engineClock=1;V(gBoat,0x84)={};V(gBoat,0x38).z=1.4f;CHECK(FirstImpulse()<.2f);
 // Engine-off and wrong-driver callers do not obtain thrust through a cache.
 PrepareEngine(248);gBoat[0x295]=0;CHECK(FirstImpulse()==0);gBoat[0x295]=16;
 *reinterpret_cast<uint8_t **>(gBoat+0x218)=nullptr;CHECK(!planeflight::Apply(gBoat,5));
 if(!failures)puts("plane engine native: all passed");return failures?1:0;
}
#endif

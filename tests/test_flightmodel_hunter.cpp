// Actual rotor-control -> native NPC mission branch -> Hunter update boundary.
// The reused fixture supplies the real installed flight/plane hook consumers.
#define main FlightFixturesMain
#include "test_flightmodelhook.cpp"
#undef main
#include "../src/hunter.cpp"

namespace game {
float CameraHeading(){return 0;}
CVector HeadingForward(float heading){return {std::sin(heading),std::cos(heading),0};}
}
namespace wanted {
bool Active(){return false;}
bool OnHim(){return false;}
CVector Target(){return {};}
}
namespace {
alignas(16) uint8_t army[0x900]{},wantedLevel[0x80]{};
int area=0,aiCalls=0,physicalCalls=0,releases=0;
uint8_t *Army(int ref){return ref==25?army:nullptr;}
void Release(uint8_t *){++releases;}
void NpcNativeAuto(uint8_t *v)
{
 // Binary 0x24a0a0: mission20/22 on status2/3 bypasses CPhysical.
 // SteerAIHeliTowardsTargetCoors additionally increments rotor speed, then
 // directly translates the matrix. Rotor<.15 returns before its translation;
 // it still leaves skipPhysics true, so a zero rotor alone cannot fall.
 if((v[0x60]>>3)==3&&v[0x19e]==20){
  ++aiCalls;if(Scalar(v,0x5f0)<.22f)Scalar(v,0x5f0)+=.001f;
  if(Scalar(v,0x5f0)>=.15f)Row(v,0x38).z+=.0015f*step;
  return;
 }
 ++physicalCalls;Row(v,0x84).z-=.008f*step;
 Row(v,0x38).z+=Row(v,0x84).z*step;
}
void NpcHunter()
{
 Reset();std::memset(army,0,sizeof army);aiCalls=physicalCalls=releases=0;
 auto *v=vehicles[0];*reinterpret_cast<int16_t *>(v+0x6a)=155;v[0x60]=2|(3<<3);v[0x19e]=20;
 player[0x4c0]=0;*reinterpret_cast<uint8_t **>(v+0x218)=army;
 *reinterpret_cast<float *>(army+0x44c)=100;army[0x4c0]=1;game::SetPedState(army,50);
 *reinterpret_cast<uint8_t **>(army+0x4b8)=v;
 *reinterpret_cast<uint8_t **>(player+0x778)=wantedLevel;*reinterpret_cast<int32_t *>(wantedLevel+0x20)=6;
 game::api.PoolsGetPed=Army;
 hunter::gReady=true;hunter::gArea=&area;hunter::gHunter=Ref(v);hunter::gSoldier=25;
 hunter::gNotInteresting=Release;hunter::gLastTime=100;hunter::gLeaving=false;
 flightmodel::gControl=NpcNativeAuto;
}
}
int main()
{
 if(FlightFixturesMain())return 1;
 NpcHunter();auto *v=vehicles[0];Row(v,0x38)={0,-30,25};
 const CVector target{13,17,50};Row(v,0x1b4)=target;
 meshFrames[0]=&mainFrame;CVector from{20,-30,27},to{-20,-30,27};
 uint32_t weapon=26;alignas(16) uint8_t point[0x30]{};
 for(int i=0;i<3;++i)impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
 Check(flightmodel::RotorFailed(v),"actual NPC Hunter main rotor contacts latch failure");
 automobile(v);hunter::Update(101);
 Check(aiCalls==0&&physicalCalls==1&&Row(v,0x84).z<0&&Row(v,0x38).z<25,"failed NPC Hunter stops native direct AI before physics and falls under gravity");
 Check(v[0x19e]==0&&hunter::gHunter<0&&releases==1&&Row(v,0x1b4).x==target.x&&Row(v,0x1b4).z==target.z,"Hunter update releases stopped flight owner instead of retargeting failed aircraft");
 Check(game::Health(army)==100&&*reinterpret_cast<uint8_t **>(v+0x218)==army,"rotor failure does not invent pilot damage or remove native driver ownership");
 // The exact same failed native slot becomes a mission's/foreign actor's.
 // Rotor state and AI mission must then be left to that owner.
 NpcHunter();v=vehicles[0];meshFrames[0]=&mainFrame;
 for(int i=0;i<3;++i)impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
 v[0x294]=2;automobile(v);hunter::Update(101);
 Check(v[0x19e]==20&&aiCalls==1&&physicalCalls==0,"mission takeover preserves foreign direct-flight controller even after old rotor failure");
 if(!fails)std::puts("flightmodel Hunter integration: all passed");return fails?1:0;
}

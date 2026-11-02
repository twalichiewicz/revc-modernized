// Actual native control/contact/world hooks: accepted AI navigation remains
// native even when a car blocks the normal component of a running velocity.
#define main original_reaction_tests
#include "test_reactionhook.cpp"
#undef main

namespace {
int failures,worldPasses;
float normalSpeed,tangentSpeed;
bool leavingVehicle;
void Check(bool good,const char *message){if(!good){std::printf("FAIL: %s\n",message);++failures;}}
void Run(uint8_t *ped)
{
 if(leavingVehicle){game::SetPedState(ped,game::kPedIdle);ped[0x4c0]=0;game::EntityFlags(ped)|=game::kEntityUsesCollision;leavingVehicle=false;}
 game::EntitySpeed(ped)={normalSpeed/50,tangentSpeed/50,0};
}
void CarSide(uint8_t *ped)
{
 ++worldPasses;
 const CVector before=game::EntityPos(ped);
 alignas(16) uint8_t points[sizeof(reactcontact::NativePoint)]{};
 gContactPoints[0]={};gContactPoints[0].point={before.x+.25f,before.y,1.4f};
 gContactPoints[0].normal={-1,0,0};gContactPoints[0].pieceA=2;gContactPoints[0].depth=.02f;
 reactions::Hook_Collision(ped,gCar,points);
 // The native world pass accepts the slide around the parked vehicle.
 *reinterpret_cast<CVector *>(ped+0x38)={.01f,before.y+game::EntitySpeed(ped).y*.8f,1.04f};
}
void Setup()
{
 gNow+=1000;reactions::gLastFrame=gNow;reactions::gPeople.clear();reactions::gContacts={};gLive=gPed;
 std::memset(gPed,0,sizeof gPed);std::memset(gPlayer,0,sizeof gPlayer);std::memset(gCar,0,sizeof gCar);
 game::EntityFlags(gPed)=3|game::kEntityUsesCollision;game::EntityFlags(gCar)=2|game::kEntityUsesCollision;
 game::SetPedState(gPed,game::kPedIdle);game::PedFlags(gPed)=game::kPedStanding;
 *reinterpret_cast<CVector *>(gPed+0x38)={0,0,1.04f};*reinterpret_cast<uint8_t **>(gPed+0x58)=gContactClump;
 *reinterpret_cast<float *>(gPed+0x44c)=100;
 game::api.FindPlayerPed=Player;game::api.PoolsGetPed=Lookup;game::api.timeMs=&gNow;
 game::api.UpdateRpHAnim=CapturedIdleMatrices;game::api.GetAnimHierarchyFromSkinClump=ContactHierarchy;
 game::api.HAnimMatrixArray=ContactMatrices;game::api.HAnimIDGetIndex=ContactBone;
 reactions::gRef=Ref;reactions::gInstalled=true;reactions::gCutscene=nullptr;reactions::gReplay=nullptr;gConfig.npcReactions=true;
 reactions::gUpdateMatrix=reactions::gUpdateFrame=reactions::gReindex=Noop;
 reactions::gSetFall=Fall;reactions::gGetUp=GetUp;reactions::orig_Control=Run;
 reactions::orig_Collision=SupportedContacts;reactions::orig_WorldCollision=CarSide;
 static uint8_t *ignored=nullptr;game::api.ignoreEntity=&ignored;
 Check(reactcontact::Install(),"native navigation fixture binds real contact adapter");
 worldPasses=gFalls=0;
}
}
int main()
{
 for(bool exit:{false,true})for(float normal:{1.f,2.5f,3.f,4.f})for(float tangent:{0.f,3.f}) {
  Setup();normalSpeed=normal;tangentSpeed=tangent;leavingVehicle=exit;
  if(exit){game::SetPedState(gPed,0x3c);gPed[0x4c0]=1;game::EntityFlags(gPed)&=~game::kEntityUsesCollision;}
  bool stable=true;
  for(int frame=0;frame<120;frame++) {
   gNow+=16;reactions::Hook_Control(gPed);reactions::Hook_WorldCollision(gPed);
   const auto *p=reactions::Find(gPed);
   stable &= !p || (p->physical.State()==reactphysics::Mode::Balanced&&!reactions::OwnsRoot(p));
  }
  Check(stable&&gFalls==0,"routine car-side contact cannot knock down a native-controlled healthy NPC");
  Check(worldPasses==120,"bracing cannot suppress subsequent native car collision passes");
  Check(std::fabs(game::EntityPos(gPed).y-tangent*1.92f)<.0001f&&std::fabs(game::EntityPos(gPed).z-1.04f)<.0001f,
        "paramedic exit and cop cover run retain the native accepted slide and standing root");
  Check(game::Health(gPed)==100,"routine bracing preserves native health");
 }
 for(int prior:{0,1,2})for(float tangent:{0.f,3.f}) {
  Setup();normalSpeed=3;tangentSpeed=tangent;leavingVehicle=false;
  auto *tracked=reactions::Track(gPed);Check(tracked!=nullptr,"prior physical response fixture is tracked");
  if(!tracked)continue;
  if(prior==0)tracked->physical.Momentum({});
  if(prior==1)tracked->physical.Momentum({.02f,0,0});
  if(prior==2)tracked->physical.Impulse(reactphysics::Chest,{.02f,0,0});
  bool stable=true;
  for(int frame=0;frame<120;frame++) {
   gNow+=16;reactions::Hook_Control(gPed);reactions::Hook_WorldCollision(gPed);
   const auto *current=reactions::Find(gPed);stable &= !current||current->physical.State()==reactphysics::Mode::Balanced;
  }
  Check(stable&&gFalls==0&&worldPasses==120,"prior small/zero force cannot turn ordinary car navigation into a new fall");
 }
 {
  reactphysics::Body body;body.Reset(Standing());body.Momentum({.2f,0,0});
  const Vec3 before=body.CenterVelocity();
  body.ContactResponse(reactphysics::ShoulderL,{-3,0,0},{-1,0,0},{3,0,0},reactphysics::ContactControl::Native);
  const Vec3 after=body.CenterVelocity();
  Check(std::fabs(before.x-after.x)<1e-6f,"native contact observation preserves an existing external physical impulse");
 }
 Setup();normalSpeed=3;tangentSpeed=1;leavingVehicle=false;
 gNow+=16;reactions::Hook_Control(gPed);reactions::Hook_WorldCollision(gPed);
 auto *p=reactions::Find(gPed);Check(p!=nullptr,"contact still creates a protective pose");
 if(p){p->physical.Impulse(reactphysics::Chest,{6,0,0});Check(reactions::OwnsRoot(p),"a genuine external impact still takes physical fall ownership after bracing");}
 std::printf("reactnavigation: %d failures\n",failures);return failures?1:0;
}

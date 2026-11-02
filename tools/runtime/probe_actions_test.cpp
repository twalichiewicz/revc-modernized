#include "probe_actions.h"
#include <cassert>
#include <cmath>
#include <cstdio>
static uint8_t player[0x8d0]{},npc[0x8d0]{},obstacle[0x8d0]{},replacement[0x8d0]{};
static uint8_t *livePlayer=player,*liveNpc=npc,*hitEntity=npc;
static int switched=-1, switchCalls,queryCalls,bulletCalls;
static bool replaceDuringQuery;
static reactcontact::NativePoint nativePoint;
static uintptr_t windowStyle;
static int toggles;
static uintptr_t Style(void *) { return windowStyle; }
static void Toggle(void *) { ++toggles; }

static uint8_t *Lookup(int ref) { return ref==11?livePlayer:ref==22?liveNpc:nullptr; }
static void Select(uint8_t *ped,int slot) { assert(ped==player);switched=slot;switchCalls++;ped[0x64c]=uint8_t(slot); }
static bool Line(const CVector &,const CVector &,uint8_t *point,uint8_t *&entity,
                 bool buildings,bool vehicles,bool peds,bool objects,bool dummies,bool see,bool some,bool shoot)
{
 assert(buildings&&vehicles&&peds&&objects&&dummies&&!see&&!some&&!shoot);
 queryCalls++;entity=hitEntity;std::memcpy(point,&nativePoint,sizeof nativePoint);
 if(replaceDuringQuery)liveNpc=replacement;
 return entity!=nullptr;
}
static void Bullet(uint8_t *weapon,uint8_t *shooter,uint8_t *victim,CVector *from,CVector *to,uint8_t *point,CVector2D)
{
 assert(weapon==game::PedWeapon(player,9)&&shooter==player&&victim==npc);
 assert(from->x==-2&&to->z==.15f);
 assert(!std::memcmp(point,&nativePoint,sizeof nativePoint));
 bulletCalls++;*reinterpret_cast<float *>(victim+0x44c)-=10;
}
static uint8_t car[0x6f0]{},rwMatrix[64]{};
static int playerHits,matrixUpdates,frameUpdates;
static uint8_t *Current(){return livePlayer;}
static uint8_t *CarLookup(int ref){return ref==33?car:nullptr;}
static bool DamagePlayer(uint8_t *ped,uint8_t *by,int weapon,float amount,int piece,uint8_t direction){
 assert(ped==player&&!by&&weapon==17&&piece==3&&direction==0&&amount==10);++playerHits;
 *reinterpret_cast<float *>(ped+0x450)-=amount*.33f;return false;
}
static void UpdateMatrix(void *matrix){assert(matrix==car+8);++matrixUpdates;}
static void UpdateFrame(uint8_t *entity){assert(entity==car);++frameUpdates;}
static void CameraCommands(){
 uint8_t camera[0x100]{},pad[0x100]{};camera[0x63]=1;assert(probe::PlayerDiagnosticContext(camera,pad,false));assert(!probe::PlayerDiagnosticContext(camera,pad,true));camera[0x75]=1;assert(!probe::PlayerDiagnosticContext(camera,pad,false));camera[0x75]=0;*reinterpret_cast<uint16_t *>(pad+0xf0)=1;assert(!probe::PlayerDiagnosticContext(camera,pad,false));
 std::string error;probe::PlayerHitResult hit;*reinterpret_cast<float *>(player+0x44c)=100;*reinterpret_cast<float *>(player+0x450)=35;game::SetPedState(player,game::kPedIdle);
 assert(probe::HitPlayer(player,11,10,Current,Lookup,DamagePlayer,hit,error));assert(playerHits==1&&!hit.native&&hit.healthAfter==100&&hit.armourAfter<hit.armourBefore);
 assert(!probe::HitPlayer(player,11,0,Current,Lookup,DamagePlayer,hit,error));assert(!probe::HitPlayer(player,11,11,Current,Lookup,DamagePlayer,hit,error));
 *reinterpret_cast<float *>(player+0x44c)=50;assert(!probe::HitPlayer(player,11,10,Current,Lookup,DamagePlayer,hit,error));*reinterpret_cast<float *>(player+0x44c)=100;
 livePlayer=replacement;assert(!probe::HitPlayer(player,11,10,Current,Lookup,DamagePlayer,hit,error));livePlayer=player;assert(playerHits==1);
 player[0x4c0]=1;*reinterpret_cast<uint8_t **>(player+0x4b8)=car;game::SetPedState(player,game::kPedDriving);
 *reinterpret_cast<uint8_t **>(car+0x218)=player;*reinterpret_cast<float *>(car+0x2a0)=1000;*reinterpret_cast<uint8_t **>(car+0x58)=rwMatrix;*reinterpret_cast<uint8_t **>(car+0x48)=rwMatrix;
 *reinterpret_cast<CVector *>(car+8)={1,0,0};*reinterpret_cast<CVector *>(car+0x18)={0,1,0};*reinterpret_cast<CVector *>(car+0x28)={0,0,1};*reinterpret_cast<CVector *>(car+0x38)={7,8,9};*reinterpret_cast<uint32_t *>(car+0x14)=0x40020003;*reinterpret_cast<uint32_t *>(car+0x24)=0x12345678;*reinterpret_cast<uint32_t *>(car+0x34)=0x13572468;*reinterpret_cast<uint32_t *>(car+0x44)=0x87654321;game::EntitySpeed(car)={.001f,.002f,.003f};
 assert(probe::RollPlayerCar(player,11,car,33,true,Current,Lookup,CarLookup,UpdateMatrix,UpdateFrame,error));assert(matrixUpdates==1&&frameUpdates==1);assert(reinterpret_cast<CVector *>(car+8)->x==-1&&reinterpret_cast<CVector *>(car+0x28)->z==-1&&game::EntityForward(car).y==1&&game::EntityPos(car).z==9&&*reinterpret_cast<uint32_t *>(car+0x14)==0x40000000);
 assert(*reinterpret_cast<uint32_t *>(car+0x24)==0x12345678&&*reinterpret_cast<uint32_t *>(car+0x34)==0x13572468&&*reinterpret_cast<uint32_t *>(car+0x44)==0x87654321&&game::EntitySpeed(car).x==.001f&&game::EntitySpeed(car).z==.003f);
 assert(probe::RollPlayerCar(player,11,car,33,true,Current,Lookup,CarLookup,UpdateMatrix,UpdateFrame,error)&&frameUpdates==1);
 assert(!probe::RollPlayerCar(player,11,car,34,false,Current,Lookup,CarLookup,UpdateMatrix,UpdateFrame,error)&&frameUpdates==1);
 *reinterpret_cast<uint8_t **>(car+0x218)=npc;assert(!probe::RollPlayerCar(player,11,car,33,false,Current,Lookup,CarLookup,UpdateMatrix,UpdateFrame,error));*reinterpret_cast<uint8_t **>(car+0x218)=player;
 assert(probe::RollPlayerCar(player,11,car,33,false,Current,Lookup,CarLookup,UpdateMatrix,UpdateFrame,error)&&frameUpdates==2&&reinterpret_cast<CVector *>(car+0x28)->z==1);
 for(int model:{155,217}){*reinterpret_cast<int16_t *>(car+0x6a)=int16_t(model);assert(!probe::RollPlayerCar(player,11,car,33,true,Current,Lookup,CarLookup,UpdateMatrix,UpdateFrame,error));assert(frameUpdates==2&&matrixUpdates==2&&reinterpret_cast<CVector *>(car+0x28)->z==1);}*reinterpret_cast<int16_t *>(car+0x6a)=0;
 player[0x4c0]=0;game::SetPedState(player,game::kPedIdle);
}
int main()
{
 CameraCommands();
 *reinterpret_cast<int32_t *>(game::PedWeapon(player,2))=17;
 *reinterpret_cast<int32_t *>(game::PedWeapon(player,9))=32;
 *reinterpret_cast<int32_t *>(game::PedWeapon(player,9)+0xc)=25;
 player[0x64c]=2;*reinterpret_cast<float *>(npc+0x44c)=100;
 *reinterpret_cast<CVector *>(npc+0x38)={0,0,1};
 probe::WeaponSnapshot baseline;std::string error;
 assert(probe::SelectOwnedWeapon(player,11,32,Lookup,Select,baseline,error));
 assert(switched==9&&switchCalls==1&&baseline.slot==2&&baseline.type==17);
 assert(!probe::SelectOwnedWeapon(player,11,29,Lookup,Select,baseline,error)&&switchCalls==1);
 assert(probe::RestoreWeapon(player,Lookup,Select,baseline,error)&&switched==2&&switchCalls==2);
 assert(probe::SelectOwnedWeapon(player,11,32,Lookup,Select,baseline,error));
 livePlayer=replacement;
 assert(!probe::RestoreWeapon(player,Lookup,Select,baseline,error)&&switchCalls==3);
 livePlayer=player;assert(probe::CaptureWeapon(player,11,Lookup,baseline));
 *reinterpret_cast<int32_t *>(game::PedWeapon(player,9))=33;
 assert(!probe::RestoreWeapon(player,Lookup,Select,baseline,error)&&switchCalls==3);
 *reinterpret_cast<int32_t *>(game::PedWeapon(player,9))=32;
 nativePoint.point={-.1f,0,.17f};nativePoint.normal={-1,0,0};nativePoint.pieceB=4;nativePoint.surfaceB=5;
 probe::ShotResult shot;
 assert(probe::FireNpcShot(player,11,npc,22,{-2,0,1},{-.12f,0,.15f},Lookup,Line,Bullet,shot,error));
 assert(bulletCalls==1&&queryCalls==1&&game::Health(npc)==90&&shot.weapon==32&&shot.piece==4&&shot.point.z==.17f);
 hitEntity=obstacle;
 assert(!probe::FireNpcShot(player,11,npc,22,{-2,0,1},{-.12f,0,.15f},Lookup,Line,Bullet,shot,error)&&bulletCalls==1);
 hitEntity=nullptr;
 assert(!probe::FireNpcShot(player,11,npc,22,{-2,0,1},{-.12f,0,.15f},Lookup,Line,Bullet,shot,error)&&bulletCalls==1);
 hitEntity=npc;replaceDuringQuery=true;
 assert(!probe::FireNpcShot(player,11,npc,22,{-2,0,1},{-.12f,0,.15f},Lookup,Line,Bullet,shot,error)&&bulletCalls==1);
 replaceDuringQuery=false;liveNpc=npc;const int queries=queryCalls;
 assert(!probe::FireNpcShot(player,11,npc,22,{-31,0,1},{-.12f,0,.15f},Lookup,Line,Bullet,shot,error)&&queryCalls==queries);
 assert(!probe::FireNpcShot(player,11,npc,22,{NAN,0,1},{-.12f,0,.15f},Lookup,Line,Bullet,shot,error)&&queryCalls==queries);
 *reinterpret_cast<int32_t *>(game::PedWeapon(player,9)+0xc)=0;
 assert(!probe::FireNpcShot(player,11,npc,22,{-2,0,1},{-.12f,0,.15f},Lookup,Line,Bullet,shot,error)&&queryCalls==queries);
 *reinterpret_cast<int32_t *>(game::PedWeapon(player,9)+0xc)=25;
 *reinterpret_cast<int32_t *>(game::PedWeapon(player,9))=12;
 assert(!probe::FireNpcShot(player,11,npc,22,{-2,0,1},{-.12f,0,.15f},Lookup,Line,Bullet,shot,error)&&queryCalls==queries);
 assert(!probe::PhysicalRoot(true,reactphysics::Mode::Balanced));
 assert(probe::PhysicalRoot(true,reactphysics::Mode::Falling));
 assert(probe::PhysicalRoot(true,reactphysics::Mode::Recovering));
 assert(probe::PhysicalRoot(true,reactphysics::Mode::Settled));
 assert(!probe::PhysicalRoot(false,reactphysics::Mode::Falling));
 probe::WindowTransition transition;
 assert(probe::NativeWindow(false,npc,1,1,Style,Toggle,transition,error)&&toggles==0);
 assert(probe::NativeWindow(true,npc,1,2,Style,Toggle,transition,error)&&toggles==1);
 assert(probe::NativeWindow(true,npc,1,3,Style,Toggle,transition,error)&&toggles==1);
 assert(!probe::NativeWindow(false,npc,1,4,Style,Toggle,transition,error)&&toggles==1);
 windowStyle=uintptr_t(1)<<14;
 assert(probe::NativeWindow(true,npc,1,5,Style,Toggle,transition,error)&&toggles==1);
 assert(probe::NativeWindow(false,npc,1,6,Style,Toggle,transition,error)&&toggles==2);
 windowStyle=0;
 assert(!probe::NativeWindow(true,npc,0,7,Style,Toggle,transition,error)&&toggles==2);
 assert(!probe::NativeWindow(true,nullptr,1,8,Style,Toggle,transition,error)&&toggles==2);
 assert(probe::NativeWindow(true,npc,1,9,Style,Toggle,transition,error)&&toggles==3);
 assert(probe::NativeWindow(true,npc,1,3000000010ULL,Style,Toggle,transition,error)&&toggles==4);
 puts("probe actions: bounded player hit/car roll, weapon/native LOS/root/window ownership passed");
}

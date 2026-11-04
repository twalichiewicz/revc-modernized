#define main ExistingFireChecks
#include "test_carfire.cpp"
#undef main
#include "../src/combat.cpp"
int main(){if(ExistingFireChecks())return 1;using namespace fixture;
 Reset();carmechanics::sample.running=true;carmechanics::sample.heat=1.3f;carmechanics::sample.breach=.3f;for(int i=0;i<27;i++)Step();
 combat::gVehiclePool=reinterpret_cast<uint8_t **>(&poolPointer);combat::Dangers(now);
 Check(F(cars[0],0x2a0)>390&&game::OnFire(cars[0])&&combat::Fire({0,0,0},nullptr),"combat recognizes system-ignited fire even before aggregate health is low");
 Extinguish()(*reinterpret_cast<uint8_t **>(cars[0]+0x280));combat::Dangers(++now);Check(!game::OnFire(cars[0])&&!combat::Fire({0,0,0},nullptr),"water clears managed fire from combat and police awareness");
 printf("carfire awareness: %s\n",failures?"FAILED":"all passed");return failures?1:0;
}

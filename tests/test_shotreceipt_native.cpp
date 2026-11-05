// Actual FireInstantHit LOS and impact dispatch, whose camera ray source can
// differ from its muzzle source. Keep the native stack/vector/call ABI intact.
#define main ExistingShotChecks
#include "test_shotcollision.cpp"
#undef main
#include "nativejetshadowarm.h"
namespace receiptfixture {
using namespace fixture;using namespace nativejetshadowtest;
void *query,*impact;
__attribute__((naked)) void Exit(){__asm__ volatile("br x28\n");}
__attribute__((naked)) void Dispatch(void *,void *,uint8_t *,uint8_t *,CVector *,CVector *,CVector *){__asm__ volatile(
 "stp x29,x30,[sp,#-0x60]!\n stp x19,x20,[sp,#0x10]\n stp x21,x22,[sp,#0x20]\n stp x23,x24,[sp,#0x30]\n stp x27,x28,[sp,#0x40]\n sub sp,sp,#0x200\n add x29,sp,#0x140\n mov x23,x0\n mov x24,x1\n mov x21,x2\n mov x20,x3\n mov x19,x4\n ldr d0,[x5]\n ldr s1,[x5,#8]\n stur d0,[x29,#-0x8c]\n stur s1,[x29,#-0x84]\n ldr d0,[x6]\n ldr s1,[x6,#8]\n stur d0,[x29,#-0x98]\n stur s1,[x29,#-0x90]\n str xzr,[sp,#0x78]\n adr x28,1f\n blr x23\n 1: adr x28,2f\n blr x24\n 2: add sp,sp,#0x200\n ldp x27,x28,[sp,#0x40]\n ldp x23,x24,[sp,#0x30]\n ldp x21,x22,[sp,#0x20]\n ldp x19,x20,[sp,#0x10]\n ldp x29,x30,[sp],#0x60\n ret\n");}
bool Instant(uint8_t *w,uint8_t *by,CVector *muzzle){Dispatch(query,impact,w,by,muzzle,&from,&to);return true;}
void Finish(Code &code,uint64_t pc){const auto at=size_t(static_cast<uint8_t *>(code.Address(pc))-code.code),end=code.Thunk(reinterpret_cast<void *>(Exit));Store(code.code+at,uint32_t(0x14000000)|(uint32_t((int64_t(end)-int64_t(at))/4)&0x3ffffff));}
}
int main(){if(ExistingShotChecks(1,nullptr))return 1;using namespace receiptfixture;
 Code code;code.Add(0x100288cc0,0x100288cf4);code.Add(0x100289c38,0x100289c58);code.Add(0x10027ea40,0x10027eabc);code.bindings={{0x10012c554,entry.at("_ZN6CWorld18ProcessLineOfSightERK7CVectorS2_R9CColPointRP7CEntitybbbbbbbb")},{0x10028ea70,entry.at("_ZN7CWeapon14DoBulletImpactEP7CEntityS1_P7CVectorS3_P9CColPoint9CVector2D")}};code.Finish();pthread_jit_write_protect_np(0);Finish(code,0x100288cf0);Finish(code,0x100289c54);sys_icache_invalidate(code.code,0x10000);pthread_jit_write_protect_np(1);query=code.Address(0x100288cc0);impact=code.Address(0x100289c38);shotcollision::gInstant=Instant;
 for(int aircraftModel:{0,199,180}){
  Setup();Panel(-.8f,255,"chassis_hi");*reinterpret_cast<void **>(cars.data()+0x218)=nullptr;
  const bool helicopter=aircraftModel!=0;
  if(helicopter){*reinterpret_cast<int16_t *>(cars.data()+0x6a)=int16_t(aircraftModel);if(aircraftModel==180)*reinterpret_cast<int *>(cars.data()+0x348)=4;airframedamage::enabled=true;}else carmechanics::enabled=true;
  CVector muzzle=from;muzzle.y-=.2f;uint8_t w[32]{};std::memcpy(w,&weaponType,4);
  reinterpret_cast<Fire>(entry.at("_ZN7CWeapon14FireInstantHitEP7CEntityP7CVector"))(w,peds.data()+2*0x8d0,&muzzle);
  Check(helicopter?airframedamage::hits==1&&carglass::perforations==1&&*reinterpret_cast<float *>(cars.data()+0x2a0)==1000:carmechanics::deliveries==1,"actual native camera-ray/muzzle dispatch must deliver localized damage and helicopter skin perforation");
  if(helicopter){
   for(int i=0;i<100;i++)reinterpret_cast<Fire>(entry.at("_ZN7CWeapon14FireInstantHitEP7CEntityP7CVector"))(w,peds.data()+2*0x8d0,&muzzle);
   auto canEnter=reinterpret_cast<bool (*)(uint8_t *)>(code.Address(0x10027ea40));
   Check(*reinterpret_cast<float *>(cars.data()+0x2a0)==1000&&canEnter(cars.data()),"repeated certified skin shots keep native stationary helicopter entry available");
   *reinterpret_cast<float *>(cars.data()+0x84)=.3f;Check(!canEnter(cars.data()),"native moving vehicle entry rejection remains in force");
  }

 }
 printf("native shot receipt: %d failures\n",fail);return fail?1:0;
}

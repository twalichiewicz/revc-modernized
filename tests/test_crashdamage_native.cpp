#define main CrashChecks
#include "test_crashdamage.cpp"
#undef main
#include "nativejetshadowarm.h"
namespace nativecrash {
using namespace fixture;
int deaths=0;alignas(16) uint8_t stats[0x40]{};
uint8_t *Player(){return peds[0];}uint8_t *Car(){return car;}
bool IsPlayer(uint8_t *p){return p==peds[0];}
uintptr_t Zero(...){return 0;}int Type(...){return 2;}int Appearance(...){return 3;}
void Die(uint8_t *p,int,float,float){++deaths;*reinterpret_cast<int *>(p+0x2fc)=54;}
void Setup(){fixture::Setup();deaths=0;*reinterpret_cast<float *>(stats+0x2c)=1;for(auto &p:peds){*reinterpret_cast<int *>(p+0x2fc)=50;*reinterpret_cast<void **>(p+0x500)=stats;p[0x7d1]=1;}}
}
int main(){if(CrashChecks())return 1;using namespace nativejetshadowtest;using namespace fixture;Code code;code.Add(0x1001c7898,0x1001c872c);
 for(auto pc=uint64_t(0x1001c7898);pc<0x1001c872c;pc+=4){auto w=code.image.Word(pc);if((w&0xfc000000)==0x94000000){auto target=uint64_t(int64_t(pc)+(int64_t(int32_t(w<<6))>>4));code.bindings[target]=reinterpret_cast<void *>(nativecrash::Zero);}}
 code.bindings[0x10010c0f4]=reinterpret_cast<void *>(nativecrash::Player);code.bindings[0x10010c0b4]=reinterpret_cast<void *>(nativecrash::Car);code.bindings[0x10018e610]=reinterpret_cast<void *>(nativecrash::IsPlayer);code.bindings[0x10016f5a0]=reinterpret_cast<void *>(nativecrash::Type);code.bindings[0x10027dc48]=reinterpret_cast<void *>(nativecrash::Appearance);code.bindings[0x100195d70]=reinterpret_cast<void *>(nativecrash::Die);code.Finish();
 crashdamage::hurt=reinterpret_cast<decltype(crashdamage::hurt)>(code.Address(0x1001c7898));
 nativecrash::Setup();crashdamage::Contact(car,20);Flush();Check(game::Health(peds[0])<100&&game::Health(peds[0])>0&&game::Health(peds[1])<100,"actual seated CPed damage consumer accepts a nonfatal acceleration injury");
 nativecrash::Setup();crashdamage::Contact(car,29.3f);Flush();Check(game::PedState(peds[0])==54&&game::PedState(peds[1])==54&&nativecrash::deaths==2,"actual native seated player/passenger death path runs after a severe helicopter contact");
 nativecrash::Setup();peds[0][0x7d1]=0;crashdamage::Contact(car,29.3f);Flush();Check(game::Health(peds[0])==100&&game::PedState(peds[1])==54,"actual native player damage permission remains authoritative");
 printf("crash damage native: %d failures\n",bad);return bad?1:0;
}

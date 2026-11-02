#include "config.h"
#include "game.h"
#include "hooks.h"
#include "shotimpact.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <limits>

namespace {
using Native=void (*)(uint8_t *,uint8_t *,uint8_t *,CVector *,CVector *,uint8_t *,CVector2D);
std::array<uint8_t,0x900> ped{},other{},shooter{};
int generation=11,otherGeneration=22,nativeCalls,heliCalls,fail;
bool stalePool=false,chainFails=false;
int chainCalls;
enum class Action { Normal,Nested,InvalidNested,StaleGeneration,StaleClump,MutatePoint } action;
shotimpact::Event observed{};bool captured;
Native installed=nullptr,prior=nullptr;
int Ref(uint8_t *p){return p==ped.data()?generation:p==other.data()?otherGeneration:-1;}
uint8_t *Pool(int ref){if(stalePool)return nullptr;return ref==generation?ped.data():ref==otherGeneration?other.data():nullptr;}
void Check(bool ok,const char *message){if(!ok){printf("FAIL: %s\n",message);++fail;}}
void *Lookup(void *,const char *name){return !strcmp(name,"_ZN6CPools9GetPedRefEP4CPed")?reinterpret_cast<void *>(Ref):nullptr;}
void NativeImpact(uint8_t *,uint8_t *,uint8_t *,CVector *,CVector *,uint8_t *,CVector2D);
void HeliImpact(uint8_t *w,uint8_t *s,uint8_t *p,CVector *a,CVector *b,uint8_t *c,CVector2D d){++heliCalls;NativeImpact(w,s,p,a,b,c,d);}
}
Config gConfig;
namespace game { Api api; }
namespace hooks {
void *Chain(const char *,void *replacement){++chainCalls;if(chainFails)return nullptr;installed=reinterpret_cast<Native>(replacement);return reinterpret_cast<void *>(prior);}
}
#define dlsym Lookup
#include "../src/shotimpact.cpp"
#undef dlsym
namespace {
void InitPed(std::array<uint8_t,0x900> &p){p={};*reinterpret_cast<uint32_t *>(p.data()+0x60)=3;*reinterpret_cast<uint8_t **>(p.data()+0x58)=reinterpret_cast<uint8_t *>(0x1234);*reinterpret_cast<CVector *>(p.data()+0x38)={0,0,1};}
void Setup(bool helicopter=false,bool install=true)
{
	InitPed(ped);InitPed(other);InitPed(shooter);generation=11;otherGeneration=22;stalePool=chainFails=false;chainCalls=0;
	nativeCalls=heliCalls=0;action=Action::Normal;captured=false;observed={};
	shotimpact::gReady=false;shotimpact::gNext=nullptr;shotimpact::gRef=nullptr;
	game::api={};game::api.PoolsGetPed=Pool;gConfig={};gConfig.heliDamage=helicopter;
	installed=nullptr;prior=helicopter?HeliImpact:NativeImpact;
	if(install)Check(shotimpact::Install(),"impact observer installs independently of helicopter features");
}
void Fire(uint8_t *victim=ped.data())
{
	alignas(16) uint8_t weapon[0x60]{},point[0x30]{};*reinterpret_cast<int *>(weapon)=17;
	*reinterpret_cast<CVector *>(point)={.2f,0,.1f};CVector from{-5,0,1},to{5,2,1};
	installed(weapon,shooter.data(),victim,&from,&to,point,{.25f,.75f});
}
void NativeImpact(uint8_t *weapon,uint8_t *by,uint8_t *victim,CVector *from,CVector *to,uint8_t *point,CVector2D ahead)
{
	++nativeCalls;
	Check(by==shooter.data() && ahead.x==.25f && ahead.y==.75f,"original hook receives unchanged native arguments");
	if(action==Action::StaleGeneration)++generation;
	if(action==Action::StaleClump)*reinterpret_cast<uint8_t **>(victim+0x58)=reinterpret_cast<uint8_t *>(0x5678);
	if(action==Action::MutatePoint)*reinterpret_cast<CVector *>(point)={99,99,99};
	shotimpact::Event e;const bool got=shotimpact::Current(victim,17,e);
	if(victim==ped.data()){captured=got;observed=e;}
	if(action==Action::Nested && victim==ped.data()) {
		Check(got,"outer native damage can read its exact impact");Fire(other.data());
		Check(shotimpact::Current(ped.data(),17,e),"outer context restored after nested shot");
	} else if(action==Action::Nested) {
		Check(got,"nested native damage can read its own exact impact");
		Check(!shotimpact::Current(ped.data(),17,e),"nested shot cannot consume another victim's outer point");
	}
	if(action==Action::InvalidNested && victim==ped.data()) {
		installed(weapon,by,other.data(),from,to,nullptr,ahead);
		Check(shotimpact::Current(ped.data(),17,e),"outer context survives invalid nested impact");
	} else if(action==Action::InvalidNested) {
		Check(!got && !shotimpact::Current(ped.data(),17,e),"invalid nested event hides outer context");
	}
	Check(!shotimpact::Current(victim,18,e),"another weapon cannot consume current impact");
	(void)weapon;(void)from;(void)to;
}
}
int main()
{
	Setup();Fire();
	Check(captured && std::fabs(observed.position.x-.2f)<1e-6f,"native damage sees copied actual contact point");
	Check(captured && std::fabs(observed.direction.x-10/std::sqrt(104.0f))<1e-5f && std::fabs(observed.direction.y-2/std::sqrt(104.0f))<1e-5f,"force direction follows actual ray, independently of attacker root");
	shotimpact::Event e;Check(!shotimpact::Current(ped.data(),17,e),"impact expires at native call return");
	Check(nativeCalls==1,"native processing runs exactly once");
	Setup(true);Fire();Check(captured && heliCalls==1 && nativeCalls==1,"existing helicopter handler remains in call chain");
	Setup();action=Action::Nested;Fire();Check(nativeCalls==2,"nested impacts preserve original invocation count");
	Setup();action=Action::InvalidNested;Fire();Check(nativeCalls==2,"invalid nested impact preserves native call count");
	Setup();Fire(nullptr);Check(!captured && nativeCalls==1,"native miss has no victim impact context");
	Setup();action=Action::StaleGeneration;Fire();Check(!captured,"reused victim generation must reject old context");
	Setup();action=Action::StaleClump;Fire();Check(!captured,"replaced victim rig must reject old context");
	Setup();action=Action::MutatePoint;Fire();Check(captured && std::fabs(observed.position.x-.2f)<1e-6f,"native buffer reuse cannot mutate captured point");
	Setup();stalePool=true;Fire();Check(!captured && nativeCalls==1,"invalid pool identity falls back while retaining native impact");
	Setup();Check(shotimpact::Install() && chainCalls==1,"repeated install must not chain to itself");
	Setup(false,false);chainFails=true;Check(!shotimpact::Install() && !installed,"failed hook installation leaves native target alone");
	chainFails=false;Check(shotimpact::Install() && chainCalls==2,"failed installation can retry");Fire();Check(captured && nativeCalls==1,"retry preserves native flow and exact capture");
	return fail?1:(puts("shotimpact: all passed"),0);
}

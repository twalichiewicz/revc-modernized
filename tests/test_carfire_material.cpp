// Source-host material vehicles: native health is a function of canonical
// material state (native collision HP stays restored), so smoke, scripts and
// the aircraft crash fire see real damage while ordinary bumps change nothing.
#define VC_SOURCE_UUID_BYTES 1
#include <dlfcn.h>
#include <cstdint>
#include <cstring>
static void *MaterialSymbol(void *,const char *);
#define dlsym MaterialSymbol
#include "../src/aircraftbodyclient.h"
#undef dlsym
#define main LegacyCarFireChecks
#include "test_carfire.cpp"
#undef main
// The source-host build links its relocation table; this fixture keeps the
// Metal image-relative identity used by the legacy checks above.
namespace hooks {uint32_t ReturnOffset(const void *address){const auto base=ImageBase();if(!base||!address)return UINT32_MAX;const uintptr_t offset=reinterpret_cast<uintptr_t>(address)-reinterpret_cast<uintptr_t>(base);return offset<=UINT32_MAX?uint32_t(offset):UINT32_MAX;}}
namespace material {
bool owned=false;AircraftBodySnapshot body;int burns=0;
bool Owned(const void *v){return owned&&v==fixture::cars[0];}
bool Read(const void *v,AircraftBodySnapshot *s){if(!Owned(v)||!s)return false;*s=body;return true;}
void Burn(void *,float){++burns;}
}
static void *MaterialSymbol(void *,const char *n){
 if(!std::strcmp(n,"VCSourceAircraftBodyOwned"))return reinterpret_cast<void *>(material::Owned);
 if(!std::strcmp(n,"VCSourceAircraftBodyRead"))return reinterpret_cast<void *>(material::Read);
 if(!std::strcmp(n,"VCSourceAircraftBodyBurn"))return reinterpret_cast<void *>(material::Burn);
 return nullptr;
}
namespace {
using namespace fixture;
float Health(){return F(cars[0],0x2a0);}
void Material(AircraftBodySnapshot b={}){material::body=b;}
void Heli(){*reinterpret_cast<int16_t *>(cars[0]+0x6a)=217;F(cars[0],0xcc)=5000;}
// Accepted native contact impulse recorded by the previous world pass.
void Impact(float deltaV){F(cars[0],0x13c)=deltaV*F(cars[0],0xcc)/50;Step();F(cars[0],0x13c)=0;}
}
int main(){
 if(LegacyCarFireChecks())return 1;using namespace fixture;failures=0;material::owned=true;
 AircraftBodySnapshot b;
 Reset();b={};b.engine=.03f;b.breach=.02f;Material(b);for(int i=0;i<20;i++)Step();Check(Health()==1000,"bump-level component damage leaves native health unchanged");
 Reset();b={};b.engine=1;Material(b);Step();Check(Health()<250&&Health()>0&&!game::Wrecked(cars[0]),"destroyed material engine places native health in the fire range");
 for(int i=0;i<120;i++)Step();Check(!carfire::Burning(cars[0],false)&&blasts==0,"low health alone never ignites or blasts a car (issue 44)");
 Reset();b={};b.breach=1;Material(b);Step();Check(Health()<250&&Health()>0,"destroyed material fuel system places native health in the fire range");
 Reset();b={};b.engine=.6f;Material(b);Step();const float smoking=Health();Check(smoking>390&&smoking<650,"partially destroyed engine maps into the native smoke range");
 Hit()(cars[0],nullptr,17,100,{});Step();Check(std::fabs(Health()-smoking)<.01f,"an accepted shot under a lower material ceiling is not charged twice");
 Hit()(cars[0],nullptr,17,500,{});Step();Check(std::fabs(Health()-400)<.01f,"native weapon damage below the material ceiling still lowers health");
 Reset();Material();Step();F(cars[0],0x2a0)=2000;Step();b={};b.engine=1;Material(b);Step();Check(Health()>1000&&Health()<1300,"script-raised health keeps its margin over material damage");
 F(cars[0],0x2a0)=700;Step();Check(std::fabs(Health()-700)<.01f,"script-written health remains authoritative over the native baseline");
 Fix()(cars[0]);b={};b.generation=7;Material(b);Step();Check(Health()==1000,"repaired material body restores full native health");
 Reset();Material();F(cars[0],0xcc)=1500;Impact(30);Check(Health()==1000&&blasts==0,"a car impact maps only through its component state");
 Reset();Heli();Material();Impact(4);Check(Health()==1000,"an ordinary helicopter landing changes nothing");
 Reset();Heli();Material();Impact(10);Check(Health()>600&&Health()<800&&!carfire::Burning(cars[0],false),"a hard landing injures the airframe without fire");
 Reset();Heli();b={};b.rotor=.5f;Material(b);Step();Check(Health()<650&&Health()>390,"shed rotor blades smoke");
 Reset();Heli();Material();Impact(30);Check(Health()<250,"a crash beyond airframe crashworthiness destroys the aircraft");
 for(int i=0;i<10;i++)Step();Check(carfire::Burning(cars[0],false)&&blasts==0,"crashed aircraft burns from its breached fuel");
 for(int i=0;i<45;i++)Step();Check(blasts>=1&&game::Wrecked(cars[0]),"the crash fire reaches the fuel and blows the aircraft up 5s after the crash");
 Reset();Heli();Material();Impact(30);Step();Extinguish()(*reinterpret_cast<uint8_t **>(cars[0]+0x280));for(int i=0;i<60;i++)Step();Check(blasts==0,"a quenched crash fire cannot explode");
 Reset();Heli();b={};b.fuel=0;Material(b);Impact(30);for(int i=0;i<60;i++)Step();Check(blasts==0&&!carfire::Burning(cars[0],false),"a dry aircraft crash cannot become a fuel fire");
 material::owned=false;Reset();Heli();Material();Impact(30);Check(Health()==1000,"unowned vehicles retain their existing health owner");
 if(!failures)std::puts("material vehicle health: all passed");return failures?1:0;
}

// Source-host material helicopter: the native blade overlap is material work on
// the actual struck blade, never the legacy binary rotor failure, and lift
// follows the surviving material rotor. Legacy behavior is rerun unchanged.
#define VC_SOURCE_UUID_BYTES 1
#include <dlfcn.h>
#include <cstdint>
#include <cstring>
static void *MaterialSymbol(void *,const char *);
#define dlsym MaterialSymbol
#include "../src/aircraftbodyclient.h"
#undef dlsym
#define main LegacyFlightChecks
#include "test_flightmodelhook.cpp"
#undef main
// Source-only Dodo presentation consumers compiled into the shared plane owner.
namespace jetwheels {bool Snapshot(const uint8_t *,std::array<WheelPose,3> &p){p={};return true;}}
namespace airportjetmodel {void Animate(void *,const Pose &){}}
namespace material {
bool owned=false;int strikes=0,nativeCalls=0;float spin=0,radius=5.18f,nativeRadius=0;airframemodel::Effects effects;
bool Owned(const void *v){return owned&&v==vehicles[0];}
bool Radius(const void *v,float *r){if(!Owned(v)||!r)return false;*r=radius;return true;}
bool Strike(void *v,float s,uint32_t){if(!Owned(v))return false;++strikes;spin=s;return true;}
airframemodel::Effects Effects(const uint8_t *){auto e=effects;e.managed=true;return e;}
bool Blade(uint8_t *v,CVector at,uint8_t *m,int16_t t,float r,float d){++nativeCalls;nativeRadius=r;return NativeBlade(v,at,m,t,r,d);}
}
static void *MaterialSymbol(void *,const char *n){
 if(!std::strcmp(n,"VCSourceAircraftBodyOwned"))return reinterpret_cast<void *>(material::Owned);
 if(!std::strcmp(n,"VCSourceAircraftBodyBladeRadius"))return reinterpret_cast<void *>(material::Radius);
 if(!std::strcmp(n,"VCSourceAircraftBodyBladeStrike"))return reinterpret_cast<void *>(material::Strike);
 return nullptr;
}
int main(){
 if(LegacyFlightChecks())return 1;fails=0;
 using namespace material;owned=true;game::api.AirframeEffects=Effects;flightmodel::gBlade=material::Blade;
 Reset();strike=true;Check(!blade(vehicles[0],{},nullptr,3,6.15f,1),"native false rotor return preserved for a material rotor");
 Check(nativeCalls==1&&std::fabs(nativeRadius-5.18f)<1e-6f,"native disc test is clamped to the attached material blade radius");
 Check(!flightmodel::RotorFailed(vehicles[0])&&std::fabs(Scalar(vehicles[0],0x5f0)-.15f)<1e-6f,"material strike keeps native rotor slowdown and never sets the legacy failure");
 Check(strikes==1&&std::fabs(spin-.22f)<1e-6f,"accepted native blade overlap queues material work with the pre-strike spin");
 strike=false;automobile(vehicles[0]);automobile(vehicles[0]);
 Check(Scalar(vehicles[0],0x5f0)>.15f&&(atomics[0].flags&4)&&(atomics[1].flags&4),"material rotor respools and is never hidden by the legacy failure path");
 flightmodel::Frame(1000);Check((atomics[1].flags&4),"world frame never hides material blades");
 Reset();strike=false;blade(vehicles[0],{},nullptr,3,6.15f,1);Check(strikes==1,"overlap-free native blade pass queues no material work");
 Reset();radius=0;nativeCalls=0;strike=true;Check(!blade(vehicles[0],{},nullptr,3,6.15f,1)&&nativeCalls==0&&Scalar(vehicles[0],0x5f0)==.22f,"no attached blade material leaves no native blade disc");radius=5.18f;
 // Lift: thrust follows the surviving rotor material (tip-weighted, cubic).
 Reset();effects=airframemodel::Effects{};Run(10);Check(std::fabs(Row(vehicles[0],0x38).z-2500)<2,"intact material rotor hovers at neutral collective");
 for(float rotor:{1.f,.9f}){Reset();effects=airframemodel::Effects{};effects.rotor=rotor;throttle=255;Run(10);Check(Row(vehicles[0],0x38).z>2520,"intact or lightly damaged material rotor retains climb authority");}
 for(float rotor:{.5f,0.f}){Reset();effects=airframemodel::Effects{};effects.rotor=rotor;throttle=255;Run(5);Check(Row(vehicles[0],0x38).z<2480,"a shed blade leaves too little thrust even at full collective and the helicopter falls");}
 Reset();effects=airframemodel::Effects{};owned=false;strike=true;blade(vehicles[0],{},nullptr,3,6.15f,1);Check(flightmodel::RotorFailed(vehicles[0]),"unowned legacy rotor keeps its existing failure contract");
 if(!fails)std::puts("heli material rotor: all passed");return fails?1:0;
}

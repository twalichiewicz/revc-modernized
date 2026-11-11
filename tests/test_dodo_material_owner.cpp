#define VC_SOURCE_UUID_BYTES 1
#define PLANEROSTER_NO_MAIN
#include "test_planeflight_roster.cpp"
static airportjetmodel::Pose captured;
static bool geometryOwned=true;
static bool bodyOwned=false;
static int controlQueues=0;
namespace jetwheels {bool Snapshot(const uint8_t*,std::array<WheelPose,3>&p){p={};return true;}}
namespace airportjetmodel {void Animate(void*,const Pose&p){captured=p;}}
static void NoRender(uint8_t*){}
static void *SourceSymbol(const char *name){
 if(std::strstr(name,"VCSourceAircraftBodyOwned"))return reinterpret_cast<void*>(+[](const void*){return bodyOwned;});
 if(std::strstr(name,"VCSourceAircraftBodyControls"))return reinterpret_cast<void*>(+[](void*,float,float,float,float){++controlQueues;});
 if(std::strstr(name,"VCSourceWingGeometryOwned"))return reinterpret_cast<void*>(+[](const void*){return geometryOwned;});
 if(std::strstr(name,"VCSourceWingOwns"))return reinterpret_cast<void*>(+[](const void*){return false;});
 if(std::strstr(name,"VCSourceWingIntegrity"))return reinterpret_cast<void*>(+[](const void*){return 1.0;});
 return RosterSymbol(name);
}
int main(){
 RosterReset();gStreaming[181*0x20+0x10]=1;planeflight::Frame(3000);CHECK(gAllocated);
 Objective(gPlayer,18,gDodo);Warp(gPlayer,gDodo);gExtraSymbol=SourceSymbol;planeflight::orig_PreRender=NoRender;
 planeflight::Hook_PreRender(gDodo);CHECK(captured.materialWing); // attached plastic state or pending restoration, integrity1
 geometryOwned=false;planeflight::Hook_PreRender(gDodo);CHECK(!captured.materialWing);
 bodyOwned=true;planeflight::Hook_PreRender(gDodo);CHECK(captured.bodyOwned&&controlQueues==1);
 bodyOwned=false;planeflight::Hook_PreRender(gDodo);CHECK(!captured.bodyOwned&&controlQueues==1);
 if(!failures)puts("Dodo material animation authority: all passed");return failures?1:0;
}

#define AIRPORTJETS_NO_MAIN
#include "test_airportjets.cpp"
static void *HeliSymbol(void *,const char *);
#define dlsym HeliSymbol
#include "../src/airportheli.cpp"
#undef dlsym
namespace {
alignas(16) uint8_t heliInfo[6][0x258]{},padInfo[0x80]{},padCol[0x68]{};
void *autoVtable[2]={nullptr,reinterpret_cast<void *>(DestroyJet)};
int choices=0,autoAllocated=0;const int heliModels[]={155,177,199,217,218,227};
void *AllocateAuto(size_t bytes){CHECK(bytes==0x6f0);for(int i=0;i<14;i++)if(vehicleFlags[i]&128){vehicleFlags[i]=uint8_t(i+5);++autoAllocated;return jetVehicles[i];}return nullptr;}
void ConstructAuto(uint8_t *v,int model,uint8_t origin){std::memset(v,0,0x6f0);*reinterpret_cast<void **>(v)=autoVtable;*reinterpret_cast<void **>(v+0x58)=&clumpMarker;*reinterpret_cast<int16_t *>(v+0x6a)=int16_t(model);*reinterpret_cast<unsigned *>(v+0x60)=2;*reinterpret_cast<float *>(v+0x2a0)=1000;v[0x294]=origin;v[0x295]=16;v[0x297]=64;++*reinterpret_cast<uint16_t *>(models[model]+0x2c);}
uint32_t RandomHeli(){return uint32_t(choices++);}
}
static void *HeliSymbol(void *handle,const char *s){
 if(std::strstr(s,"CAutomobileC1"))return reinterpret_cast<void *>(ConstructAuto);
 if(std::strstr(s,"CAutomobileD0"))return nullptr;
 if(std::strstr(s,"CVehiclenw"))return reinterpret_cast<void *>(AllocateAuto);
 if(std::strstr(s,"GetRandomNumber"))return reinterpret_cast<void *>(RandomHeli);
 return JetSymbol(handle,s);
}
int main(){Setup();CHECK(airportheli::Install());airportheli::gRandom=RandomHeli;*reinterpret_cast<int16_t *>(buildings[0]+0x6a)=876;V(buildings[0],0x38)={-1359.9109f,-1251.8196f,15.8923f};
 models[876]=padInfo;*reinterpret_cast<void **>(padInfo+0x20)=padCol;V(padCol,0x10)={-8,-8,-.1f};V(padCol,0x1c)={8,8,.1f};
 for(int i=0;i<6;i++){models[heliModels[i]]=heliInfo[i];heliInfo[i][0x1d]=6;*reinterpret_cast<void **>(heliInfo[i]+0x20)=col;*reinterpret_cast<void **>(heliInfo[i]+0x30)=&clumpMarker;streamInfo[heliModels[i]*32+0x10]=0;}
 V(gPlayer,0x38)=V(buildings[0],0x38);gPlayer[0x4c0]=1;*reinterpret_cast<uint8_t **>(gPlayer+0x4b8)=gBoat;
 lastRequest=-1;airportheli::Frame(1000);CHECK(std::find(heliModels,heliModels+6,lastRequest)!=heliModels+6);const int first=lastRequest;
 if(first>=0){streamInfo[first*32+0x10]=1;airportheli::Frame(2000);}
 CHECK(autoAllocated==1&&airportheli::Owns(jetVehicles[0]));CHECK(!(jetVehicles[0][0x295]&16));CHECK(std::fabs(V(jetVehicles[0],0x38).x+1359.9109f-10.02f)<1);
 airportheli::Frame(2100);CHECK(autoAllocated==1&&choices==1);
 *reinterpret_cast<uint8_t **>(jetVehicles[0]+0x218)=gPlayer;V(gPlayer,0x38)={0,0,30};airportheli::Frame(3000);CHECK(airportheli::Owns(jetVehicles[0]));
 *reinterpret_cast<uint8_t **>(jetVehicles[0]+0x218)=nullptr;airportheli::Frame(4000);CHECK(!airportheli::Owns(jetVehicles[0]));
 V(gPlayer,0x38)=V(buildings[0],0x38);airportheli::Frame(5000);CHECK(choices==2&&lastRequest!=first);if(lastRequest>=0){streamInfo[lastRequest*32+0x10]=1;airportheli::Frame(6000);}CHECK(autoAllocated==2);
 const int oldDestroyed=destroyed;++vehicleFlags[0];*reinterpret_cast<int16_t *>(jetVehicles[0]+0x6a)=140;airportheli::Frame(6100);CHECK(destroyed==oldDestroyed);
 gConfig.planes=false;airportheli::Frame(7000);CHECK(!airportheli::Owns(jetVehicles[1]));
 // Native unloaded requests overwrite flags and recursively request the TXD.
 // Keep only our own bit, and return it on cancellation or successful spawn.
 auto pending=[&](uint8_t modelFlags,uint8_t txdFlags){
  airportheli::Reset();gConfig.planes=true;choices=0;V(gPlayer,0x38)=V(buildings[0],0x38);
  streamInfo[155*32+0x10]=streamInfo[(6500+17)*32+0x10]=0;
  streamInfo[155*32+0x11]=modelFlags;streamInfo[(6500+17)*32+0x11]=txdFlags;
  *reinterpret_cast<int16_t *>(heliInfo[0]+0x2e)=17;
  airportheli::Frame(8000);
 };
 pending(4,4);CHECK(streamInfo[155*32+0x11]==5&&streamInfo[(6500+17)*32+0x11]==5);
 gConfig.planes=false;airportheli::Frame(8100);CHECK(streamInfo[155*32+0x11]==4&&streamInfo[(6500+17)*32+0x11]==4);
 pending(1,3);gConfig.planes=false;airportheli::Frame(8100);CHECK(streamInfo[155*32+0x11]==1&&streamInfo[(6500+17)*32+0x11]==3);
 pending(0,2);streamInfo[155*32+0x10]=1;const int beforeSpawn=autoAllocated;airportheli::Frame(9000);CHECK(autoAllocated==beforeSpawn+1&&streamInfo[155*32+0x11]==0&&streamInfo[(6500+17)*32+0x11]==2);
 std::printf("airport helipad: %d failures\n",failures);return failures?1:0;}

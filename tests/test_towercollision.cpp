// Actual installed collision hooks, original-first contact consumer, and native
// COL-shaped data. No running game, render geometry or fabricated health.
#include "../src/game.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <cstdarg>
[[maybe_unused]] static void *TowerSymbol(void *,const char *);
#ifndef TOWERCOLLISION_BASELINE
#define dlsym TowerSymbol
#include "../src/towercollision.cpp"
#undef dlsym
#else
namespace towercollision {bool Install(){return true;}}
#endif
namespace game {Api api{};}
void Log(const char *,...){}
namespace {
using Models=int (*)(const uint8_t *,uint8_t *,const uint8_t *,uint8_t *,uint8_t *,uint8_t *,float *);
using Physical=int (*)(uint8_t *,uint8_t *,uint8_t *);
alignas(16) uint8_t vehicles[2][0x6f0]{},buildings[2][0x78]{},vehiclePool[32]{},buildingPool[32]{},carInfo[0x258]{},towerInfo[0x60]{},carCol[0x68]{},originalCol[0x68]{},replacementCol[0x68]{},points[64*40]{};
uint8_t *vehiclePoolPointer=vehiclePool,*buildingPoolPointer=buildingPool,*modelTable[6500]{},vehicleFlags[2]{17,18},buildingFlags[2]{7,8};
uint8_t *admitted=vehicles[0];bool allow=true,rejectModelsOnce=true,wrongMatrix=false,wrongPoints=false,wrongWheelPoints=false,wrongWheelDistances=false,specialCol=false,throwOriginal=false,recycleVehicle=false,recycleBuilding=false,replaceCol=false,nested=false,restartDuring=false;
int failures=0,nativeResult=0,bodyConsumers=0,supplements=0,clears=0,restarts=0,shutdowns=0;std::array<uint8_t *,4> cacheOwners{};uint8_t cacheBytes[4][32]{};
Models models;Physical physical,automobile;void (*restart)(),(*shutdown)();
void Check(bool ok,const char *what){if(!ok){++failures;std::printf("FAIL: %s\n",what);}}
CVector &V(uint8_t *p,int at){return *reinterpret_cast<CVector *>(p+at);}float &F(uint8_t *p,int at){return *reinterpret_cast<float *>(p+at);}
CVector Add(CVector a,CVector b){return {a.x+b.x,a.y+b.y,a.z+b.z};}CVector Sub(CVector a,CVector b){return {a.x-b.x,a.y-b.y,a.z-b.z};}CVector Mul(CVector a,float f){return {a.x*f,a.y*f,a.z*f};}float Dot(CVector a,CVector b){return a.x*b.x+a.y*b.y+a.z*b.z;}
CVector Nearest(CVector p,CVector a,CVector b,CVector c){
 const CVector ab=Sub(b,a),ac=Sub(c,a),ap=Sub(p,a);const float d1=Dot(ab,ap),d2=Dot(ac,ap);if(d1<=0&&d2<=0)return a;
 const CVector bp=Sub(p,b);const float d3=Dot(ab,bp),d4=Dot(ac,bp);if(d3>=0&&d4<=d3)return b;const float vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0)return Add(a,Mul(ab,d1/(d1-d3)));
 const CVector cp=Sub(p,c);const float d5=Dot(ab,cp),d6=Dot(ac,cp);if(d6>=0&&d5<=d6)return c;const float vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0)return Add(a,Mul(ac,d2/(d2-d6)));
 const float va=d3*d6-d5*d4;if(va<=0&&d4-d3>=0&&d5-d6>=0)return Add(b,Mul(Sub(c,b),(d4-d3)/((d4-d3)+(d5-d6))));const float inverse=1/(va+vb+vc);return Add(a,Add(Mul(ab,vb*inverse),Mul(ac,vc*inverse)));
}
uint8_t *Lookup(int ref){const int i=ref>>8;return i>=0&&i<2&&!(vehicleFlags[i]&128)&&uint8_t(ref)==vehicleFlags[i]?vehicles[i]:nullptr;}
int NativeModels(const uint8_t *a,uint8_t *ac,const uint8_t *b,uint8_t *col,uint8_t *out,uint8_t *wheelPoints,float *wheelDistances){
 const int lineCount=ac[0x2e];if(lineCount){Check(lineCount<=4&&wheelPoints&&wheelDistances,"native line consumer requires bounded nonnull wheel outputs");if(lineCount>4||!wheelPoints||!wheelDistances)return 0;
  const bool upper=col!=originalCol&&col!=replacementCol;if(upper){Check(ac==carCol,"supplement preserves original A COL identity");Check(wheelPoints!=vehicles[0]+0x4e0&&wheelDistances!=reinterpret_cast<float *>(vehicles[0]+0x580),"supplement wheel outputs use private scratch storage");Check(reinterpret_cast<uintptr_t>(wheelPoints)%16==0,"native scratch wheel CColPoints retain alignment");}
  for(int i=0;i<lineCount;++i){if(upper)Check(wheelDistances[i]==1,"scratch wheel fractions initialize to native full segment");std::memset(wheelPoints+i*40,(upper?0xa0:0x40)+i,40);wheelDistances[i]=(upper?.05f:.25f)+i*.1f;}
 }
 if(col==originalCol||col==replacementCol){
  if(nested){nested=false;auto *old=admitted;admitted=vehicles[1];(*reinterpret_cast<int32_t *>(vehicles[1]+0x348)==1?automobile:physical)(vehicles[1],buildings[1],points+32*40);admitted=old;}
  if(recycleVehicle){recycleVehicle=false;++vehicleFlags[0];}if(recycleBuilding){recycleBuilding=false;++buildingFlags[0];}if(replaceCol){replaceCol=false;*reinterpret_cast<uint8_t **>(towerInfo+0x20)=replacementCol;}
  if(restartDuring){restartDuring=false;restart();}if(throwOriginal){throwOriginal=false;throw std::runtime_error("native contact callback");}if(nativeResult)F(out,0)=777;return nativeResult;
 }
 ++supplements;int slot=-1;for(int i=0;i<4;++i)if(cacheOwners[i]==col)slot=i;
 if(!*reinterpret_cast<void **>(col+0x60)){if(slot<0)for(int i=0;i<4;++i)if(!cacheOwners[i]){cacheOwners[i]=col;slot=i;break;}Check(slot>=0,"persistent private native cache has bounded model storage");if(slot>=0)*reinterpret_cast<void **>(col+0x60)=cacheBytes[slot];}
 const auto *vertices=*reinterpret_cast<CVector *const *>(col+0x50);const auto *faces=*reinterpret_cast<uint16_t *const *>(col+0x58);const int count=*reinterpret_cast<int16_t *>(col+0x2c);
 Check(count>0&&count<600&&vertices&&faces,"supplement supplies actual bounded native triangle geometry");const CVector center=Sub(*reinterpret_cast<const CVector *>(a+0x30),*reinterpret_cast<const CVector *>(b+0x30));
 for(int i=0;i<count;++i){const auto *face=faces+i*4;const CVector nearest=Nearest(center,vertices[face[0]],vertices[face[1]],vertices[face[2]]),delta=Sub(center,nearest);if(Dot(delta,delta)<1){V(out,0)=Add(nearest,*reinterpret_cast<const CVector *>(b+0x30));V(out,0x10)=Mul(delta,1/std::max(.001f,std::sqrt(Dot(delta,delta))));F(out,0x24)=1-std::sqrt(Dot(delta,delta));return 1;}}
 return 0;
}
int NativePhysical(uint8_t *v,uint8_t *building,uint8_t *out){auto *ca=*reinterpret_cast<uint8_t **>(carInfo+0x20),*cb=*reinterpret_cast<uint8_t **>(towerInfo+0x20);const int count=models(v+8+(wrongMatrix?4:0),ca,building+8,cb,out+(wrongPoints?40:0),nullptr,nullptr);if(count>0){++bodyConsumers;*reinterpret_cast<uint32_t *>(v+0x60)|=1<<19;}return count;}
int NativeAutomobile(uint8_t *v,uint8_t *building,uint8_t *out){auto *ca=*reinterpret_cast<uint8_t **>(carInfo+0x20),*cb=*reinterpret_cast<uint8_t **>(towerInfo+0x20);const int count=models(v+8+(wrongMatrix?4:0),specialCol?replacementCol:ca,building+8,cb,out+(wrongPoints?40:0),v+0x4e0+(wrongWheelPoints?40:0),reinterpret_cast<float *>(v+0x580+(wrongWheelDistances?4:0)));if(count>0){++bodyConsumers;*reinterpret_cast<uint32_t *>(v+0x60)|=1<<19;}return count;}
void RemovePlanes(uint8_t *col){Check(*reinterpret_cast<void **>(col+0x60)!=nullptr,"only live native cache entries removed");*reinterpret_cast<void **>(col+0x60)=nullptr;++clears;}
void NativeRestart(){++restarts;for(auto *p:cacheOwners)if(p)Check(!*reinterpret_cast<void **>(p+0x60),"private cache cleared before original restart");}
void NativeShutdown(){++shutdowns;for(auto *p:cacheOwners)if(p)Check(!*reinterpret_cast<void **>(p+0x60),"private cache cleared before original shutdown");}
void Setup(){
 allow=true;admitted=vehicles[0];wrongMatrix=wrongPoints=wrongWheelPoints=wrongWheelDistances=specialCol=throwOriginal=recycleVehicle=recycleBuilding=replaceCol=nested=restartDuring=false;nativeResult=bodyConsumers=supplements=0;
 std::memset(vehicles,0,sizeof vehicles);std::memset(buildings,0,sizeof buildings);for(auto &flag:vehicleFlags)flag=uint8_t((flag+1)&127);for(auto &flag:buildingFlags)flag=uint8_t((flag+1)&127);
 *reinterpret_cast<uint8_t **>(vehiclePool)=vehicles[0];*reinterpret_cast<uint8_t **>(vehiclePool+8)=vehicleFlags;*reinterpret_cast<int *>(vehiclePool+16)=2;
 *reinterpret_cast<uint8_t **>(buildingPool)=buildings[0];*reinterpret_cast<uint8_t **>(buildingPool+8)=buildingFlags;*reinterpret_cast<int *>(buildingPool+16)=2;
 *reinterpret_cast<int16_t *>(carCol+0x28)=1;carCol[0x2e]=0;modelTable[240]=carInfo;modelTable[190]=carInfo;modelTable[1868]=towerInfo;carInfo[0x1d]=6;towerInfo[0x1d]=1;std::strcpy(reinterpret_cast<char *>(towerInfo+8),"doontoon22");*reinterpret_cast<uint8_t **>(carInfo+0x20)=carCol;*reinterpret_cast<uint8_t **>(towerInfo+0x20)=originalCol;
 for(int i=0;i<2;++i){auto *v=vehicles[i],*b=buildings[i];*reinterpret_cast<void **>(v)=carInfo;*reinterpret_cast<void **>(b)=towerInfo;*reinterpret_cast<int32_t *>(v+0x60)=2|0x100;*reinterpret_cast<int32_t *>(b+0x60)=1|0x100;*reinterpret_cast<int16_t *>(v+0x6a)=240;*reinterpret_cast<int16_t *>(b+0x6a)=1868;*reinterpret_cast<int32_t *>(v+0x348)=4;F(v,0x2a0)=1000;
  V(v,8)=V(b,8)={1,0,0};V(v,0x18)=V(b,0x18)={0,1,0};V(v,0x28)=V(b,0x28)={0,0,1};V(v,0x38)={-563.4f,952.746f,250};V(b,0x38)={-481.516f,952.746f,117.542f};
 }
 game::api.modelInfoPtrs=modelTable;game::api.PoolsGetVehicle=Lookup;
}
void Skimmer(){Setup();for(auto &vehicle:vehicles){*reinterpret_cast<int16_t *>(vehicle+0x6a)=190;*reinterpret_cast<int32_t *>(vehicle+0x348)=1;}carCol[0x2e]=4;}
bool WheelsOriginal(int actor=0){for(int i=0;i<4;++i){for(int j=0;j<40;++j)if(vehicles[actor][0x4e0+i*40+j]!=0x40+i)return false;if(F(vehicles[actor],0x580+i*4)!=.25f+i*.1f)return false;}return true;}
}
namespace airworld {bool OwnsFlightView(const uint8_t *v){return allow&&v==admitted;}}
namespace hooks {void *Chain(const char *name,void *hook){
 if(std::strstr(name,"CPhysical22ProcessEntityCollision")){auto old=physical;if(reinterpret_cast<void *>(old)==hook)return nullptr;physical=reinterpret_cast<Physical>(hook);return reinterpret_cast<void *>(old);}
 if(std::strstr(name,"CAutomobile22ProcessEntityCollision")){auto old=automobile;if(reinterpret_cast<void *>(old)==hook)return nullptr;automobile=reinterpret_cast<Physical>(hook);return reinterpret_cast<void *>(old);}
 if(std::strstr(name,"ProcessColModels")){if(rejectModelsOnce){rejectModelsOnce=false;return nullptr;}auto old=models;if(reinterpret_cast<void *>(old)==hook)return nullptr;models=reinterpret_cast<Models>(hook);return reinterpret_cast<void *>(old);}
 if(std::strstr(name,"ClearForRestart")){auto old=restart;if(reinterpret_cast<void *>(old)==hook)return nullptr;restart=reinterpret_cast<void (*)()>(hook);return reinterpret_cast<void *>(old);}
 if(std::strstr(name,"CModelInfo8ShutDown")){auto old=shutdown;if(reinterpret_cast<void *>(old)==hook)return nullptr;shutdown=reinterpret_cast<void (*)()>(hook);return reinterpret_cast<void *>(old);}return nullptr;
}}
static void *TowerSymbol(void *,const char *name){if(std::strstr(name,"ms_pVehiclePool"))return &vehiclePoolPointer;if(std::strstr(name,"ms_pBuildingPool"))return &buildingPoolPointer;if(std::strstr(name,"RemoveTrianglePlanes"))return reinterpret_cast<void *>(RemovePlanes);return nullptr;}
int main(){physical=NativePhysical;automobile=NativeAutomobile;models=NativeModels;restart=NativeRestart;shutdown=NativeShutdown;Setup();
#ifndef TOWERCOLLISION_BASELINE
 Check(!towercollision::Install(),"partial native hook failure is reported");
#endif
 Check(towercollision::Install(),"partial install retry preserves installed predecessors");
 Check(physical(vehicles[0],buildings[0],points)>0&&bodyConsumers==1,"owned aircraft upper-facade contact reaches original physical consumer");Check(supplements==1,"missing original contact uses actual upper triangle solver");Check(F(points,0)<-562&&F(points,0)>-564&&F(points,8)>249,"native-shaped contact is on the reported upper facade");
 nativeResult=1;const int before=supplements;Check(physical(vehicles[0],buildings[0],points)==1&&F(points,0)==777&&supplements==before,"positive original contacts and native output remain exact");
 Skimmer();Check(automobile(vehicles[0],buildings[0],points)>0&&bodyConsumers==1&&supplements==1,"real Skimmer automobile caller receives upper body contacts");Check(WheelsOriginal(),"supplement preserves original wheel CColPoints and fractions byte-for-byte");
 Skimmer();nativeResult=1;Check(automobile(vehicles[0],buildings[0],points)==1&&F(points,0)==777&&supplements==0&&WheelsOriginal(),"positive original Skimmer body/wheel outputs stay exact");
 Skimmer();nested=true;Check(automobile(vehicles[0],buildings[0],points)>0&&supplements==2&&WheelsOriginal()&&WheelsOriginal(1),"nested Skimmer contexts restore caller and keep both original wheel arrays");
 Skimmer();recycleVehicle=true;Check(automobile(vehicles[0],buildings[0],points)==0&&supplements==0&&WheelsOriginal(),"Skimmer callback generation change cannot inherit collision admission");
 for(int gate=0;gate<9;++gate){Skimmer();if(gate==0)allow=false;if(gate==1)*reinterpret_cast<int16_t *>(vehicles[0]+0x6a)=240;if(gate==2)*reinterpret_cast<int32_t *>(vehicles[0]+0x348)=4;if(gate==3)wrongWheelPoints=true;if(gate==4)wrongWheelDistances=true;if(gate==5)wrongPoints=true;if(gate==6)wrongMatrix=true;if(gate==7)specialCol=true;if(gate==8)carCol[0x2e]=3;Check(automobile(vehicles[0],buildings[0],points)==0&&supplements==0,"Skimmer context rejects foreign actor/COL/matrix/body/wheel buffers or line count");}
 for(int gate=0;gate<11;++gate){Setup();if(gate==0)allow=false;if(gate==1)vehicleFlags[0]|=128;if(gate==2)buildingFlags[0]|=128;if(gate==3)*reinterpret_cast<int16_t *>(buildings[0]+0x6a)=1869;if(gate==4)wrongMatrix=true;if(gate==5)*reinterpret_cast<int32_t *>(vehicles[0]+0x348)=0;if(gate==6)vehicles[0][0x60]=2|(5<<3);if(gate==7)F(vehicles[0],0x2a0)=0;if(gate==8)vehicles[0][0x61]&=~uint8_t(1);if(gate==9)buildings[0][0x61]&=~uint8_t(1);if(gate==10)wrongPoints=true;Check(physical(vehicles[0],buildings[0],points)==0&&supplements==0,"ordinary/foreign/free/retyped/wrecked or mismatched output context stays native");}
 for(int change=0;change<3;++change){Setup();recycleVehicle=change==0;recycleBuilding=change==1;replaceCol=change==2;Check(physical(vehicles[0],buildings[0],points)==0&&supplements==0,"native callback cannot transfer admission to recycled actors or replaced streamed COL");if(change==2)Check(physical(vehicles[0],buildings[0],points)>0,"fresh streamed COL generation can independently reacquire contact");}
 Setup();nested=true;Check(physical(vehicles[0],buildings[0],points)>0&&supplements==2,"nested aircraft contexts restore outer admission and stable private data");
 Setup();throwOriginal=true;try{physical(vehicles[0],buildings[0],points);Check(false,"fixture callback throws");}catch(const std::runtime_error &){}Check(models(vehicles[0]+8,carCol,buildings[0]+8,originalCol,points,nullptr,nullptr)==0&&supplements==0,"exception restores context before an unrelated collision call");
 Setup();restartDuring=true;Check(physical(vehicles[0],buildings[0],points)==0&&supplements==0,"restart inside native callback invalidates the old collision epoch");Setup();Check(physical(vehicles[0],buildings[0],points)>0,"cache remains usable after rejected callbacks");restart();Check(restarts==2&&clears>0,"restart drops native plane-cache links before world restart");const int old=clears;restart();Check(clears==old,"repeated restart cannot free native plane cache twice");Check(physical(vehicles[0],buildings[0],points)>0,"post-restart private cache lazily rebuilds");shutdown();Check(shutdowns==1&&clears==old+1,"shutdown releases rebuilt native cache before model destruction");
 if(!failures)std::puts("tower collision hook: all passed");return failures?1:0;
}

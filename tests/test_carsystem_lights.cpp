// Production custom/native lamp paths, with only the external electrical
// supply and renderer/camera boundaries replaced by fixture consumers.
#include "../src/carlights.cpp"
#include <cstdio>
#include <stdexcept>
namespace game {Api api{};}
void Im2DCanvas::SetTexture(void *){}
void Im2DCanvas::SetDepthMode(DepthMode){}
void Im2DCanvas::Tri(const Vtx &,const Vtx &,const Vtx &,CRGBA,CRGBA,CRGBA){}
void Im2DCanvas::Flush(){}
Config gConfig;void Log(const char *,...){}
namespace {
alignas(16) uint8_t cars[2][0x6f0]{};
float supplies[2]={1,1};uint32_t now=1000;int fails=0,coronas=0,points=0;float pointR=0;uint8_t alpha=0;
bool nested=false,throwRender=false,seated=false;uint8_t *seatCar=nullptr;
void Check(bool b,const char *s){if(!b){printf("FAIL: %s\n",s);++fails;}}
void Corona(uint32_t,uint8_t,uint8_t,uint8_t,uint8_t a,const CVector &,float,float,uint8_t,int8_t,uint8_t,uint8_t,uint8_t,float,bool,float){++coronas;alpha=a;}
void Point(uint8_t,CVector,CVector,float,float r,float,float,uint8_t,bool){++points;pointR=r;}
void Emit(uint8_t *v){carlights::Hook_RegisterCorona(uint32_t(uintptr_t(v))+6,255,200,100,200,{0,0,0},1,50,0,0,0,0,0,0,false,0);carlights::Hook_AddLight(0,{},{},10,1,.5f,.2f,0,false);}
void PreRender(uint8_t *v){
 if(throwRender){throwRender=false;throw std::runtime_error("render interruption");}
 if(nested&&v==cars[0]){nested=false;carlights::Hook_PreRender(cars[1]);}
 Emit(v);
}
}
namespace carmechanics {float LightingPower(const uint8_t *v){return v==cars[0]?supplies[0]:v==cars[1]?supplies[1]:1;}}
namespace camera {bool InSeat(){return seated;}uint8_t *SeatVehicle(){return seatCar;}}
namespace shaders {CVector CameraPosition(){return {};}}
namespace hooks {const void *ImageBase(){return nullptr;}}
int main(){
 using namespace carlights;game::api.timeMs=&now;rw.LightStatus=[](const void *,int){return 0u;};
 orig_PreRender=PreRender;orig_RegisterCorona=Corona;orig_AddLight=Point;
 cars[0][kVehicleFlags]=cars[1][kVehicleFlags]=kEngineOn|kLightsOn;
 *reinterpret_cast<float *>(cars[0]+kBrakePedal)=1;
 auto healthy=LightsOf(cars[0]);Check(healthy.head[0]==kHeadOn&&healthy.tail[0]==kBraking,"healthy supply preserves custom lamp strengths");
 supplies[0]=.5f;auto dim=LightsOf(cars[0]);Check(dim.head[0]==healthy.head[0]*.5f&&dim.tail[1]==healthy.tail[1]*.5f,"custom head/brake lamps follow electrical supply");
 Hook_PreRender(cars[0]);Check(alpha==100&&pointR==.5f,"native car corona alpha and point-light brightness follow supply");
 supplies[0]=0;auto failed=LightsOf(cars[0]);Check(!failed.Any(),"failed electrical supply removes custom lamps");
 int wasC=coronas,wasP=points;Hook_PreRender(cars[0]);Check(coronas==wasC&&points==wasP,"failed car emits no native lamp lights");
 Emit(cars[0]);Check(alpha==200&&pointR==1,"unscoped world emissions retain native brightness");
 supplies[0]=.5f;supplies[1]=1;nested=true;Hook_PreRender(cars[0]);Check(alpha==100&&pointR==.5f&&gPreRendering==nullptr,"nested car rendering restores outer electrical owner");
 Hook_PreRender(cars[1]);Check(alpha==200&&pointR==1,"uninjured actor remains fully lit");
 throwRender=true;try{Hook_PreRender(cars[0]);}catch(const std::runtime_error &){}Check(gPreRendering==nullptr,"render exception restores electrical owner");
 puts(fails?"car system lights: FAILED":"car system lights: all passed");return fails?1:0;
}

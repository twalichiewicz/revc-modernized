// Runs native conversion, gear selection and wheel/physical force consumers.
// Contacts are prescribed; this is not the complete controller/map simulation.
#define main HandlingAssetChecks
#include "test_handling_assets.cpp"
#undef main
#include "nativejetshadowarm.h"
#include "../src/rainvehicles.cpp"
namespace game {Api api{};} Config gConfig;void Log(const char *,...){}
static rainmodel::Weather testWeather;
namespace weather {const rainmodel::Weather &Now(){return testWeather;}bool Install(){return true;}}
namespace minimap {const mapdata::Map *RainMap(){return nullptr;}}
namespace shaders {float Seconds(){return 0;}}
namespace hooks {void *Chain(const char *,void *){return nullptr;}}
namespace {
CVector Cross(const CVector &a,const CVector &b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
CVector Multiply(const float *m,const CVector &v){return {m[0]*v.x+m[4]*v.y+m[8]*v.z,m[1]*v.x+m[5]*v.y+m[9]*v.z,m[2]*v.x+m[6]*v.y+m[10]*v.z};}
void Normalise(CVector *v){const float d=std::sqrt(v->x*v->x+v->y*v->y+v->z*v->z);if(d>1e-8f){v->x/=d;v->y/=d;v->z/=d;}}
int Rand(){return 0x5a5a;}
float &F(uint8_t *v,int at){return *reinterpret_cast<float *>(v+at);}
struct RunResult {float launch,speed,distance;};
int Model(const std::string &name){
 std::ifstream file(std::string(std::getenv("GAMEDATA"))+"/data/default.ide");
 for(std::string row;std::getline(file,row);){std::replace(row.begin(),row.end(),',',' ');std::istringstream in(row);int id;std::string model,texture,type,handling;
  if(in>>id>>model>>texture>>type>>handling&&type=="car"&&handling==name)return id;}
 return -1;
}
RunResult Run(nativejetshadowtest::Code &code,const Line &line,bool tuned,uint8_t surface,float wetness,float step=1,float grade=0,float grip=1,bool backwards=false,bool mixed=false,float power=1){
 // Reset the transmission/wheel scratch BSS between independent cars.
 std::memset(code.Page(0x1007f4598),0,16);
 Record r=line.raw;r.transmissionFlags=uint8_t(r.flags); // native LoadHandlingData+0xb10
 r.id=reinterpret_cast<int (*)(void *,const char *)>(code.Address(0x100272db4))(nullptr,line.name.c_str());
 nativejetshadowtest::Require(r.id>=0&&r.id<106,"native handling identifier");if(tuned)BeforeConversion(r,KindOf(line));reinterpret_cast<void (*)(void *,Record *)>(code.Address(0x100272c1c))(nullptr,&r);if(tuned)AfterConversion(r);
 alignas(16) uint8_t car[0x6f0]{};*reinterpret_cast<int16_t *>(car+0x6a)=int16_t(Model(line.name));*reinterpret_cast<Record **>(car+0x160)=&r;F(car,8)=F(car,0x1c)=F(car,0x30)=1;F(car,0xcc)=r.mass;F(car,0xd0)=r.turnMass;F(car,0xd4)=1;F(car,0x28c)=backwards?-1:1;car[0x2a4]=backwards?0:1;
 for(int w=0;w<4;w++)car[0x4e0+w*0x28+0x22]=mixed&&w>=2?1:surface;
 testWeather.wetness=wetness;testWeather.puddles=0;nativejetshadowtest::Store(code.Page(0x10079f31c),wetness);game::api.timeStep=&step;gConfig.rainVehicles=true;nativejetshadowtest::Store(code.Page(0x10058d674),step);
 auto calc=reinterpret_cast<float (*)(uint8_t *,const float &,uint8_t &,float &,const float &,bool)>(code.Address(0x100279a1c));
 rainvehicles::orig_ProcessWheel=reinterpret_cast<decltype(rainvehicles::orig_ProcessWheel)>(code.Address(0x10027ca40));
 int32_t states[4]{};float distance=0,launch=0;
 for(int frame=0;frame<int(500/step);frame++){
  const float thrust=calc(reinterpret_cast<uint8_t *>(&r)+0x34,F(car,0x28c),car[0x2a4],F(car,0x2a8),F(car,0x88),false);
  float contact[3]={F(car,0x84),F(car,0x88),0};
  for(int w=0;w<4;w++){
   float f[3]={0,1,0},right[3]={1,0,0},point[3]={0,0,0};
   const bool driven=r.driveType=='4'||(r.driveType=='F'?w%2==0:w%2!=0);
   // Native producer: .004 * handling grip /4 /frequency, surface table,
   // wet multiplier and player axle bias (0x24ae84..0x24b318).
   const uint8_t ground=car[0x4e0+w*0x28+0x22];uint8_t collision[40]{};collision[0x20]=29;collision[0x22]=ground;
   const float surfaceGrip=reinterpret_cast<float (*)(void *)>(code.Address(0x100129ba0))(collision);
   const float wet=reinterpret_cast<float (*)(uint8_t)>(code.Address(0x100129ae0))(ground);
   const float bias=2*(w%2?1-r.tractionBias:r.tractionBias);
   const float adhesion=.001f*r.tractionMultiplier*surfaceGrip*wet*bias*grip;
   rainvehicles::Hook_ProcessWheel(car,f,right,contact,point,4,driven?thrust*power:0,0,adhesion,int8_t(w),&F(car,0x5ec+4*w),&states[w],0);
  }
  F(car,0x88)-=grade*.008f*step;
  if(frame==0)launch=F(car,0x88)*2500/step;distance+=F(car,0x88)*step;
 }
 return {launch,F(car,0x88)*50,distance};
}
}
int main(){
 if(!Load())return 2;
 using namespace nativejetshadowtest;Code code;code.Add(0x100129ae0,0x100129c2c);code.Add(0x100272c1c,0x100272db4);code.Add(0x100272db4,0x100272e0c);code.Add(0x100279918,0x1002799d4);code.Add(0x100279a1c,0x100279da0);code.Add(0x10027ca40,0x10027cf68);code.Add(0x1001564c0,0x1001565ac);
 code.bindings={{0x1002f5910,reinterpret_cast<void *>(std::strncmp)},{0x100172e94,reinterpret_cast<void *>(Cross)},{0x100172ec0,reinterpret_cast<void *>(Multiply)},{0x100172e50,reinterpret_cast<void *>(Normalise)},{0x100143f28,reinterpret_cast<void *>(Rand)}};code.Finish();Store(code.Page(0x10058d674),1.0f);
 // SurfaceTable::Initialise stores this symmetric6x6 table without scaling.
 std::ifstream surfaces(std::string(std::getenv("GAMEDATA"))+"/data/surface.dat");int row=0;
 for(std::string text;std::getline(surfaces,text);){std::istringstream in(text);std::string name;if(!(in>>name)||name[0]==';')continue;
  Require(row<6,"surface table has six groups");for(int column=0;column<=row;column++){float value=0;Require(bool(in>>value),"surface matrix value");Store(code.Page(0x10058d5d0+24*row+4*column),value);Store(code.Page(0x10058d5d0+24*column+4*row),value);}++row;
 }
 Require(row==6,"actual surface table loaded");
 int fail=0;
 for(const char *name:{"INFERNUS","LANDSTAL","RANCHER","PATRIOT","BOBCAT","LINERUN","BARRACKS","MULE"})for(const auto &l:gLines)if(l.name==name){
  const auto stock=Run(code,l,false,1,0),dry=Run(code,l,true,1,0),grass=Run(code,l,true,2,1),beach=Run(code,l,true,33,1);
  printf("%-9s launch stock %.2f tuned %.2f m/s2; 10s wet grass %.3f m/s %.2fm, beach %.3f m/s %.2fm\n",name,stock.launch,dry.launch,grass.speed,grass.distance,beach.speed,beach.distance);
  if(KindOf(l)==Kind::OffRoad||KindOf(l)==Kind::HeavyTruck||KindOf(l)==Kind::LightTruck){
   if(grass.speed<1||grass.distance<5){printf("FAIL: healthy %s cannot traverse soaked grass\n",name);++fail;}
   if(dry.launch<stock.launch){printf("FAIL: %s lost its low-speed utility pull\n",name);++fail;}
  }
 }
 const auto line=[&](const char *name)->const Line &{for(const auto &l:gLines)if(l.name==name)return l;nativejetshadowtest::Require(false,"fixture handling line exists");return gLines.front();};
 const auto &semi=line("LINERUN"),&suv=line("LANDSTAL"),&sport=line("INFERNUS");
 auto check=[&](bool value,const char *message){if(!value){printf("FAIL: %s\n",message);++fail;}};
 for(const Line *l:{&semi,&suv}){
  const auto flat=Run(code,*l,true,2,1),hill=Run(code,*l,true,2,1,1,.05f),back=Run(code,*l,true,2,1,1,0,1,true);
  const auto fast=Run(code,*l,true,2,1,.5f),slow=Run(code,*l,true,2,1,2),mixed=Run(code,*l,true,2,1,1,0,1,false,true);
  const auto lowGrip=Run(code,*l,true,2,1,1,0,.25f),broken=Run(code,*l,true,2,1,1,0,1,false,false,0);
  printf("%s hill %.3f, reverse %.3f, 100/50/25Hz %.3f/%.3f/%.3f, mixed %.3f, low grip %.3f, broken %.3f m/s\n",l->name.c_str(),hill.speed,back.speed,fast.speed,flat.speed,slow.speed,mixed.speed,lowGrip.speed,broken.speed);
  check(hill.distance>5&&hill.speed>1,"utility crosses wet grass against a prescribed5% uphill force");
  check(back.distance< -5&&back.speed< -1,"utility can reverse out of wet grass");
  check(std::fabs(fast.distance-flat.distance)<flat.distance*.12f&&std::fabs(slow.distance-flat.distance)<flat.distance*.12f,"terrain progress remains consistent across25/50/100Hz");
  check(mixed.distance>flat.distance,"only actual soft-ground contacts apply terrain resistance");
  check(lowGrip.distance<flat.distance&&lowGrip.speed<flat.speed,"native tyre grip still limits utility progress");
  check(broken.distance==0&&broken.speed==0,"terrain capability does not create drive when upstream mechanics remove torque");
 }
 const auto sportsRoad=Run(code,sport,true,1,0),sportsSand=Run(code,sport,true,33,1),suvRoad=Run(code,suv,true,1,0),suvSand=Run(code,suv,true,33,1);
 check(suvSand.distance/suvRoad.distance>sportsSand.distance/sportsRoad.distance,"offroad vehicle retains more of its road performance on soaked sand than sports tyres");
 printf("utility native: %s\n",fail?"FAILED":"all passed");return fail?1:0;
}

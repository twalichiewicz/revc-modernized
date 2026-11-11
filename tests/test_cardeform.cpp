#include "game.h"
#include "config.h"
#include "hooks.h"
#include "dentmodel.h"
#include <array>
#include <cstring>
#include <vector>
#include <cstdio>
#include "nativejetshadowarm.h"
static void *Lookup(void *,const char *);
#define dlsym Lookup
#include "../src/carimpact.cpp"
#undef dlsym
namespace game {Api api{};}
Config gConfig;void Log(const char *,...){}
namespace fixture {
static void *(*extraLookup)(const char *)=nullptr;
using Point=carimpact::Point;
alignas(16) uint8_t cars[2][0x6f0]{},pool[24]{},wall[0x70]{};
uint8_t flags[2]={1,1};void *poolPointer=pool;uint32_t now=1000;
using Control=void (*)(uint8_t *);using Damage=void (*)(uint8_t *,float,uint16_t);
using Pair=bool (*)(uint8_t *,uint8_t *,Point &,float &,float &);
using Single=bool (*)(uint8_t *,Point &,float &);
using Alt=bool (*)(uint8_t *,uint8_t *,Point &,float &,CVector &,CVector &);
Control control,fix,deleteCar;Damage damage;Pair pair;Single single;Alt alt;
Single actualSingle=nullptr;
bool accepted=true,collision=true,nested=false,script=false,repair=false,recycle=false;int failures=0,nativeCalls=0;
struct Dent {uint8_t *v;dentmodel::Impact hit;};std::vector<Dent> dents;
std::vector<carimpact::Collision> mechanicalHits;
float &F(uint8_t *v,size_t at){return *reinterpret_cast<float *>(v+at);}
uint8_t *Car(int ref){int i=ref>>8;return i>=0&&i<2&&!(flags[i]&0x80)&&ref==((i<<8)|flags[i])?cars[i]:nullptr;}
void Check(bool b,const char *s){if(!b){printf("FAIL: %s\n",s);++failures;}}
void DamageNative(uint8_t *v,float amount,uint16_t){
 ++nativeCalls;if(script&&amount==0){script=false;damage(v,200,0);return;}
 if(repair&&amount==0){repair=false;fix(v);}
 if(accepted)F(v,0x2a0)-=10;
}
void ControlNative(uint8_t *v){if(nested&&v==cars[0]){nested=false;control(cars[1]);}damage(v,0,0);}
void FixNative(uint8_t *v){F(v,0x2a0)=1000;}
void DeleteNative(uint8_t *){}
bool PairNative(uint8_t *,uint8_t *,Point &,float &a,float &b){++nativeCalls;a=200;b=300;if(recycle){recycle=false;++flags[0];}return collision;}
bool SingleNative(uint8_t *v,Point &p,float &a){++nativeCalls;if(actualSingle){F(v,0xd0)=1000;F(v,0xdc)=.04f;F(v,0x88)=.2f;return actualSingle(v,p,a);}a=200;return collision;}
bool AltNative(uint8_t *,uint8_t *,Point &,float &a,CVector &b,CVector &c){++nativeCalls;a=200;b={1,2,3};c={4,5,6};return collision;}
void Record(uint8_t *v,uint8_t *other,const Point &p,float force,bool second=false){
 F(v,0x13c)=force;*reinterpret_cast<uint8_t **>(v+0x140)=other;
 memcpy(v+0x148,&p.n,12);*reinterpret_cast<uint16_t *>(v+0x154)=second?p.pieceB:p.pieceA;
}
void Reset(){
 for(auto &v:cars)fix(v);memset(cars,0,sizeof cars);dents.clear();mechanicalHits.clear();now+=1000;
 accepted=collision=true;nested=script=repair=recycle=false;
 for(int i=0;i<2;i++){
  flags[i]=uint8_t((flags[i]+1)&0x7f);auto v=cars[i];*reinterpret_cast<uint32_t *>(v+0x60)=2|0x20;v[0x297]=0x40;
  *reinterpret_cast<int16_t *>(v+0x6a)=211;*reinterpret_cast<void **>(v+0x58)=reinterpret_cast<void *>(uintptr_t(0x1000+i));
  F(v,8)=F(v,0x1c)=F(v,0x30)=1;F(v,0x38)=100;F(v,0x3c)=200+i*4;F(v,0xcc)=1000;F(v,0x2a0)=1000;
 }
}
Point Contact(){Point p{};p.p={100,202,.2f};p.n={0,-1,0};p.pieceA=p.pieceB=3;p.depth=.05f;return p;}
void Hit(int kind=0){auto p=Contact();float a=-1,b=-2;CVector x{},y{};
 if(kind==0){bool ok=pair(cars[0],wall,p,a,b);Check(ok==collision&&a==200&&b==300,"pair preserves native result and impulse references");}
 if(kind==1){bool ok=single(cars[0],p,a);Check(ok==collision&&std::fabs(a-200)<.01f,"single preserves native result and impulse reference");}
 if(kind==2){bool ok=alt(cars[0],wall,p,a,x,y);Check(ok==collision&&a==200&&x.y==2&&y.z==6,"alternate collision preserves native vector outputs");}
 Record(cars[0],kind==1?nullptr:wall,p,a);
}
}
namespace carglass {bool Install(){return true;}bool Dent(uint8_t *v,const dentmodel::Impact &hit){fixture::dents.push_back({v,hit});return true;}}
namespace carmechanics {void Collision(uint8_t *,const carimpact::Collision &hit){fixture::mechanicalHits.push_back(hit);}}
namespace crashdamage {int calls=0;float last=0;void Contact(uint8_t *,float speed){++calls;last=speed;}}
namespace hooks {void *Chain(const char *s,void *replacement){using namespace fixture;
#define HOOK(n,v,T) if(!strcmp(s,n)){auto old=v;v=reinterpret_cast<T>(replacement);return reinterpret_cast<void *>(old);}
 HOOK("_ZN11CAutomobile14ProcessControlEv",control,Control);HOOK("_ZN11CAutomobile13VehicleDamageEft",damage,Damage);
 HOOK("_ZN11CAutomobile3FixEv",fix,Control);HOOK("_ZN7CEntity14DeleteRwObjectEv",deleteCar,Control);
 HOOK("_ZN9CPhysical14ApplyCollisionEPS_R9CColPointRfS3_",pair,Pair);
 HOOK("_ZN9CPhysical14ApplyCollisionER9CColPointRf",single,Single);
 HOOK("_ZN9CPhysical17ApplyCollisionAltEP7CEntityR9CColPointRfR7CVectorS6_",alt,Alt);
#undef HOOK
 return nullptr;
}}
static void *Lookup(void *,const char *s){if(fixture::extraLookup)if(auto symbol=fixture::extraLookup(s))return symbol;if(!strcmp(s,"_ZN6CPools15ms_pVehiclePoolE"))return &fixture::poolPointer;return nullptr;}
static int airHits=0;static Vec3 airNormal;static void AirCollision(uint8_t *,Vec3,Vec3 n,float){++airHits;airNormal=n;}
int main(int argc,char **argv){using namespace fixture;
 std::unique_ptr<nativejetshadowtest::Code> code;
 if(argc>1&&!strcmp(argv[1],"--native")){
  code=std::make_unique<nativejetshadowtest::Code>();code->Add(0x1001582c8,0x100158570);
  code->bindings={{0x100172e94,reinterpret_cast<void *>(+[](const CVector &a,const CVector &b)->CVector{return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};})},
                  {0x100172ec0,reinterpret_cast<void *>(+[](const float *m,const CVector &v)->CVector{return {m[0]*v.x+m[4]*v.y+m[8]*v.z,m[1]*v.x+m[5]*v.y+m[9]*v.z,m[2]*v.x+m[6]*v.y+m[10]*v.z};})}};
  code->Finish();actualSingle=reinterpret_cast<Single>(code->Address(0x1001582c8));
 }
 control=ControlNative;damage=DamageNative;fix=FixNative;deleteCar=DeleteNative;pair=PairNative;single=SingleNative;alt=AltNative;
 *reinterpret_cast<void **>(pool)=cars;*reinterpret_cast<void **>(pool+8)=flags;*reinterpret_cast<int *>(pool+16)=2;
 game::api.PoolsGetVehicle=Car;game::api.timeMs=&now;
 gConfig.carDeformation=gConfig.carMechanics=false;Check(carimpact::Install()&&pair==PairNative,"startup off installs no collision hooks");gConfig.carDeformation=gConfig.carMechanics=true;Check(carimpact::Install(),"native collision owner installs");
 for(int kind=0;kind<3;kind++){Reset();Hit(kind);control(cars[0]);Check(dents.size()==1&&dents[0].v==cars[0]&&dents[0].hit.point.y==2&&dents[0].hit.inward.y==1,"successful actual impulse dents local contact on correct car");Check(F(cars[0],0x2a0)==990,"native health remains unchanged by mesh tuning");}
 Reset();*reinterpret_cast<int16_t *>(cars[0]+0x6a)=180;*reinterpret_cast<int *>(cars[0]+0x348)=4;game::api.AirframeEffects=+[](const uint8_t *){airframemodel::Effects e;e.managed=true;return e;};game::api.AirframeCollision=AirCollision;
 Check(game::api.AirframeContactsManaged&&game::api.AirframeContactsManaged(cars[0]),"contact readiness certifies a live managed aircraft after hook installation");
 carimpact::gReady=false;Check(!game::api.AirframeContactsManaged(cars[0]),"material ownership cannot certify unavailable native collision hooks");carimpact::gReady=true;
 {auto p=Contact();float force=0;single(cars[0],p,force);}Check(airHits==1&&airNormal.y==1,"accepted actual plane contact reaches independent aircraft systems without an automobile damage manager");game::api.AirframeEffects=nullptr;game::api.AirframeCollision=nullptr;
 Reset();for(int i=0;i<100;i++){Hit();control(cars[0]);now+=20;}Check(dents.size()==1,"continuous contact across frames creates one dent episode");now+=350;Hit();control(cars[0]);Check(dents.size()==2,"separate collision can deepen existing dent");
 Reset();auto p=Contact();float a,b;pair(cars[0],cars[1],p,a,b);Record(cars[0],cars[1],p,a);Record(cars[1],cars[0],p,b,true);nested=true;control(cars[0]);Check(dents.size()==2&&dents[0].v==cars[1]&&dents[0].hit.point.y==-2&&dents[0].hit.inward.y==-1,"secondary car and nested controller retain opposite inward contact");
 Reset();p=Contact();pair(cars[0],wall,p,a,b);Record(cars[0],wall,p,a);
 p.pieceA=5;p.p.y+=.3f;pair(cars[0],wall,p,a,b); // native keeps the first record on equal force
 control(cars[0]);Check(dents.size()==1&&dents[0].hit.point.y==2,"equal-force later contact retains native first strongest record");
 Reset();Record(cars[0],wall,Contact(),200);control(cars[0]);Check(dents.empty(),"damage record without successful physical collision cannot dent");
 Reset();collision=false;Hit();control(cars[0]);Check(dents.empty(),"failed physical collision cannot dent");
 Reset();Hit();damage(cars[0],0,0);Check(dents.empty(),"unscoped native damage cannot claim collision");
 Reset();Hit();script=true;control(cars[0]);Check(dents.empty(),"nested script health damage cannot be claimed as physical dent");
 Reset();Hit();repair=true;control(cars[0]);Check(dents.empty(),"repair inside damage invalidates pending collision");
 Reset();Hit();fix(cars[0]);control(cars[0]);Check(dents.empty(),"repair before control drops old contact");
 Reset();Hit();accepted=false;control(cars[0]);Check(dents.empty(),"native damage veto prevents visual dent");
 Reset();Hit();now+=201;control(cars[0]);Check(dents.empty(),"stale collision cannot dent a later pose");
 Reset();Hit();++flags[0];control(cars[0]);Check(dents.empty(),"recycled pool slot cannot claim old contact");
 Reset();recycle=true;Hit();control(cars[0]);Check(dents.empty(),"recycling inside native impulse invalidates captured owner");
 for(int mismatch=0;mismatch<4;mismatch++){Reset();Hit();if(mismatch==0)F(cars[0],0x13c)=250;if(mismatch==1)*reinterpret_cast<void **>(cars[0]+0x140)=cars[1];if(mismatch==2)F(cars[0],0x148)=1;if(mismatch==3)*reinterpret_cast<uint16_t *>(cars[0]+0x154)=5;control(cars[0]);Check(dents.empty(),"only exact native strongest-impact record can claim contact");}
 for(int piece=1;piece<=4;piece++){Reset();p=Contact();p.pieceA=uint8_t(piece);pair(cars[0],wall,p,a,b);Record(cars[0],wall,p,a);control(cars[0]);Check(dents.size()==1,"native body pieces one through four can dent body");}
 Reset();p=Contact();p.pieceA=13;pair(cars[0],wall,p,a,b);Record(cars[0],wall,p,a);control(cars[0]);Check(dents.empty(),"native wheel piece thirteen leaves body rigid");
 Reset();p=Contact();p.n={0,0,1};pair(cars[0],wall,p,a,b);Record(cars[0],wall,p,a);control(cars[0]);Check(dents.empty(),"upward road support cannot dent body");
 Reset();Hit();gConfig.carDeformation=false;control(cars[0]);Check(dents.empty(),"runtime off passes through native health");gConfig.carDeformation=true;
 Reset();gConfig.carDeformation=false;p=Contact();p.pieceA=13;pair(cars[0],wall,p,a,b);Record(cars[0],wall,p,a);control(cars[0]);Check(dents.empty()&&mechanicalHits.size()==1&&mechanicalHits[0].piece==13,"mechanical wheel impacts remain certified with visual deformation disabled");gConfig.carDeformation=true;
 Reset();now=0;Hit();control(cars[0]);Check(dents.size()==1,"zero game clock is valid");
 Reset();Hit();*reinterpret_cast<int16_t *>(cars[0]+0x6a)=217;control(cars[0]);Check(dents.empty(),"aircraft keep native geometry");
 printf("cardeform: %s\n",failures?"FAILED":"all passed");return failures?1:0;
}

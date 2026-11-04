// Real native transmission and wheel consumers behind production mechanics.
#define main ExistingMechanicsChecks
#include "test_carmechanics.cpp"
#undef main
#include "nativejetshadowarm.h"
namespace {
CVector Cross(const CVector &a,const CVector &b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
CVector Multiply(const float *m,const CVector &v){return {m[0]*v.x+m[4]*v.y+m[8]*v.z,m[1]*v.x+m[5]*v.y+m[9]*v.z,m[2]*v.x+m[6]*v.y+m[10]*v.z};}
void Normalise(CVector *v){const float len=std::sqrt(v->x*v->x+v->y*v->y+v->z*v->z);if(len>1e-8f){v->x/=len;v->y/=len;v->z/=len;}}
int Rand(){return 0x5a5a;}
void NativeRecord(nativejetshadowtest::Code &code)
{
 using namespace fixture;F(handling,4)=1000;handling[0x7c]='R';handling[0x7d]='P';handling[0x7e]=5;
 F(handling,0x80)=.008f;F(handling,0x84)=2.4f;F(handling,0x88)=2;F(handling,0x8c)=-.3f;F(handling,0xa4)=.8f;
 reinterpret_cast<void (*)(uint8_t *)>(code.Address(0x100279918))(handling+0x34);
 for(auto &v:cars){F(v,0xcc)=F(v,0xd0)=1000;F(v,0xd4)=1;v[0x2a4]=1;}
 nativejetshadowtest::Store(code.Page(0x10058d674),1.0f);
}
struct Motion {float x,y,yaw;};
Motion OneWheel(nativejetshadowtest::Code &code,bool hurt,bool sideSlip)
{
 using namespace fixture;Reset();NativeRecord(code);carmechanics::Controller scope(cars[0]);
 if(hurt)carmechanics::Collision(cars[0],{{-1,1.4f,-.35f},{1,0,0},30,13});
 CVector f{0,1,0},r{1,0,0},speed{sideSlip?.3f:0,.25f,0},point{-1,1.4f,-.35f};int32_t state=0;
 fixture::wheel(cars[0],f,r,speed,point,4,0,0,.02f,0,&F(cars[0],0x5ec),&state,0);
 Check(std::isfinite(F(cars[0],0x5ec))&&f.x==0&&r.y==0,"real native wheel output and caller vectors preserved");
 return {F(cars[0],0x84),F(cars[0],0x88),F(cars[0],0x98)};
}
}
int main(){
 if(ExistingMechanicsChecks())return 1;
 using namespace fixture;using namespace nativejetshadowtest;
 Code code;code.Add(0x100279918,0x1002799d4);code.Add(0x100279a1c,0x100279da0);
 code.Add(0x10027ca40,0x10027cf68);code.Add(0x1001564c0,0x1001565ac);
 code.bindings={{0x100172e94,reinterpret_cast<void *>(Cross)},{0x100172ec0,reinterpret_cast<void *>(Multiply)},
  {0x100172e50,reinterpret_cast<void *>(Normalise)},{0x100143f28,reinterpret_cast<void *>(Rand)}};
 code.Finish();carmechanics::gCalculate=reinterpret_cast<Calc>(code.Address(0x100279a1c));
 Reset();NativeRecord(code);Run();const float intact=torque[0];Check(intact>0,"actual native transmission produces positive drive");
 hits[0]={Nose()};Run();Check(torque[0]>0&&torque[0]<intact*.9f,"localized engine injury reduces actual native drive acceleration");
 const auto retainedGear=Read<uint8_t *>(code.Page(0x1007f4598));Check(retainedGear>=handling+0x34&&retainedGear<handling+0x7c&&std::fabs(F(handling,0x90)-.2f)<1e-6f,"actual native transmission scratch and retained gear pointer use original allocation");
 carmechanics::gWheel=reinterpret_cast<WheelFn>(code.Address(0x10027ca40));
 const auto straight=OneWheel(code,false,false),bent=OneWheel(code,true,false);
 Check(std::fabs(bent.x)>std::fabs(straight.x)+1e-6f&&std::fabs(bent.yaw-straight.yaw)>1e-6f,"bent individual wheel produces real native lateral force and yaw");
 const auto grip=OneWheel(code,false,true),slip=OneWheel(code,true,true);
 Check(std::hypot(slip.x,slip.y)<std::hypot(grip.x,grip.y)*.9f,"damaged wheel has lower real native traction-circle force");
 printf("mechanical native: drive %.6f -> %.6f; bent lateral %.6f yaw %.6f; %s\n",intact,torque[0],bent.x,bent.yaw,fails?"FAILED":"all passed");return fails?1:0;
}

// Real fixed-wing adapter, native physical impulses, full world orientation.
#define main ExistingPlaneChecks
#include "test_planeflight.cpp"
#undef main
namespace {
bool OwnAircraft(const uint8_t *v){return v==gBoat&&game::EntityModel(v)!=190;}
void Prepare(int model,float side)
{
 Reset();planeflight::SetAirportOwner(OwnAircraft);planeflight::SetAirportWheelControl(nullptr);
 *reinterpret_cast<int16_t *>(gBoat+0x6a)=int16_t(model);*reinterpret_cast<int32_t *>(gBoat+0x348)=model==190?1:4;
 V(gBoat,0x38)={0,0,1500};V(gBoat,0x84)={side/50,60.0f/50,0};
 *reinterpret_cast<float *>(gBoat+0xd0)=model==248?20000:10000;
}
void Step(float dt,int mode)
{
 gStep=dt*50;V(gBoat,0x84).z-=.008f*gStep;
 const float resistance=std::pow(1/std::fabs(1+*reinterpret_cast<float *>(gBoat+0xd8)*.5f*planeflight::Dot(V(gBoat,0x84),V(gBoat,0x84))),gStep);
 V(gBoat,0x84)=Times(V(gBoat,0x84),resistance);V(gBoat,0x90)=Times(V(gBoat,0x90),.99f);
 CHECK(planeflight::Apply(gBoat,mode));
 V(gBoat,0x38)=Plus(V(gBoat,0x38),Times(V(gBoat,0x84),gStep));
 const CVector angular=Times(V(gBoat,0x90),gStep);
 V(gBoat,0x18)=Normal(Plus(V(gBoat,0x18),Cross(angular,V(gBoat,0x18))));
 V(gBoat,0x28)=Normal(Plus(V(gBoat,0x28),Cross(angular,V(gBoat,0x28))));
 V(gBoat,8)=Normal(Cross(V(gBoat,0x18),V(gBoat,0x28)));V(gBoat,0x28)=Normal(Cross(V(gBoat,8),V(gBoat,0x18)));
}
float Slip(){return std::atan2(planeflight::Dot(V(gBoat,0x84),V(gBoat,8)),planeflight::Dot(V(gBoat,0x84),V(gBoat,0x18)));}
struct Result {float slip,height,minForward,maxSlip,yawRate;};
Result Run(int model,float side,float dt,float yawRate=0)
{
 Prepare(model,side);V(gBoat,0x90).z=yawRate/50;float maxSlip=0,minForward=1000;
 for(int i=0;i<int(12/dt);++i){Step(dt,model==190?3:5);maxSlip=std::max(maxSlip,std::fabs(Slip()));minForward=std::min(minForward,50*planeflight::Dot(V(gBoat,0x84),V(gBoat,0x18)));}
 return {Slip(),V(gBoat,0x38).z,minForward,maxSlip,50*V(gBoat,0x90).z};
}
}
int main()
{
 Reset();planeflight::Install();
 for(int model:{190,181,248})for(float dt:{1/30.0f,1/120.0f}){
  const auto neutral=Run(model,0,dt);
  for(float side:{-6.0f,6.0f}){
   Prepare(model,side);Step(dt,model==190?3:5);
   CHECK(V(gBoat,0x90).z*side<0); // +X drift must turn +Y nose toward +X, hence -Z yaw
   const auto result=Run(model,side,dt);
   std::printf("model%d dt%.5f perturb%+.1f: beta %.3f max%.3f, forwardMin %.1f, height %.1f vsneutral %.1f\n",model,dt,side,result.slip,result.maxSlip,result.minForward,result.height,neutral.height);
   CHECK(std::fabs(result.slip)<.015f&&result.maxSlip<.13f);
   CHECK(result.minForward>neutral.minForward-.5f&&std::fabs(result.height-neutral.height)<15);
  }
  for(float rate:{-.2f,.2f}){
   const auto damped=Run(model,0,dt,rate);
   CHECK(std::fabs(damped.slip)<.015f&&std::fabs(damped.yawRate)<.02f&&damped.maxSlip<.13f);
  }
 }
 if(!failures)puts("plane stability native: all passed");return failures?1:0;
}

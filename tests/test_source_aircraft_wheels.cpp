#define VC_SOURCE_UUID_BYTES 1
#define main LegacyWheelChecks
#include "test_jetwheels.cpp"
#undef main

namespace sourcewheel {
bool owned=false;int reads=0;AircraftBodySnapshot material;
struct WheelCall {int index,count;float brake,grip;CVector forward;};std::vector<WheelCall> calls;
bool Read(const void *actor,AircraftBodySnapshot *out){if(!owned||actor!=v||!out)return false;++reads;*out=material;return true;}
void *Symbol(const char *name){return !strcmp(name,"VCSourceAircraftBodyRead")?reinterpret_cast<void *>(Read):nullptr;}
void Wheel(uint8_t *actor,CVector &forward,CVector &right,CVector &speed,CVector &point,int count,float thrust,float brakeForce,float adhesion,int8_t index,float *spin,int32_t *state,uint16_t status){
 calls.push_back({index,count,brakeForce,adhesion,forward});NativeWheel(actor,forward,right,speed,point,count,thrust,brakeForce,adhesion,index,spin,state,status);
}
void Reset(int model){
 ::Reset();owned=true;material={};reads=0;calls.clear();dodoOwner=model==181;airportModel=model;
 *reinterpret_cast<int16_t *>(v+0x6a)=int16_t(model);
 if(model==181){V(v,0x38).z=1.50155f;F(v,0xcc)=2400;F(v,0xd0)=8000;}
}
float Angle(CVector forward){return std::atan2(-forward.x,forward.y);}
}
int main(){gWheelExtraSymbol=sourcewheel::Symbol;if(LegacyWheelChecks())return 1;jetwheels::gWheel=sourcewheel::Wheel;
 for(int model:{180,181}){
  sourcewheel::Reset(model);brake=255;steer=64;Step();const auto healthy=sourcewheel::calls;
  Check(healthy.size()==3&&sourcewheel::reads==1,"source Learjet and Dodo use one committed material snapshot for three native tire consumers");
  sourcewheel::Reset(model);brake=255;steer=64;
  sourcewheel::material.wheels[0]={.25f,0,.5f,.1f,.5f};sourcewheel::material.wheels[1]={.6f,0,.7f,-.03f,1};sourcewheel::material.wheels[2]={0,0,0,0,0};sourcewheel::material.wheels[3]={.8f,0,.9f,.02f,1};
  Step();Check(sourcewheel::calls.size()==3&&sourcewheel::reads==1,"flat retained source tires keep physical support without additional snapshot reads");
  const float grips[]={.25f,.6f,.8f},brakes[]={.5f,.7f,.9f},toes[]={.1f,-.03f,.02f};
  for(int i=0;i<3&&sourcewheel::calls.size()==3;i++){
   const auto &actual=sourcewheel::calls[i];
   Check(actual.index==i&&actual.count==3&&std::fabs(actual.grip-healthy[i].grip*grips[i])<1e-7f&&std::fabs(actual.brake-healthy[i].brake*brakes[i])<1e-7f,"source wheel LF/LR/RR mapping scales native grip and brake exactly once");
   const float expected=sourcewheel::Angle(healthy[i].forward)*(i==0?.5f:1)+toes[i];
   Check(std::fabs(sourcewheel::Angle(actual.forward)-expected)<1e-6f,"source tire toe and steering authority apply once to current native runway tangents");
  }
  for(int missing=0;missing<3;missing++){
   sourcewheel::Reset(model);const int index=missing==0?0:missing==1?1:3;sourcewheel::material.wheelPresent[index]=false;Step();
   Check(queries==2&&sourcewheel::calls.size()==2&&sourcewheel::reads==1,"detached canonical tire contributes no runway query or physical wheel consumer");
   std::array<jetwheels::WheelPose,3> pose{};Check(jetwheels::Snapshot(v,pose)&&!pose[missing].grounded&&pose[missing].compression==-jetwheels::kTravel,"detached source tire renders an honest unsupported wheel pose");
   for(const auto &call:sourcewheel::calls)Check(call.index!=missing&&call.count==2,"remaining native wheel consumers retain their actual grounded count");
  }
  sourcewheel::Reset(model);Step();for(bool &present:sourcewheel::material.wheelPresent)present=false;queries=0;
  std::array<jetwheels::WheelPose,3> pose{};Check(!jetwheels::Grounded(v)&&queries==0,"loss of all retained tire material immediately disables ground queries");
  Check(jetwheels::Snapshot(v,pose)&&!pose[0].grounded&&!pose[1].grounded&&!pose[2].grounded,"source removal clears stale grounded poses before the next controller");
 }
#if __has_feature(address_sanitizer)
 __asan_unpoison_memory_region(v,sizeof v);
#endif
 printf("source aircraft wheels: %s\n",fails?"FAILED":"all passed");return fails?1:0;
}

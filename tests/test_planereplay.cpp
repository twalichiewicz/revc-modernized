// Replay a user-controlled native2B air segment through the actual adapter.
// Only health differs in the counterfactual. No runway/contact extrapolation.
#define main ExistingPlaneChecks
#include "test_planeflight.cpp"
#undef main
namespace {
struct TakeoffSample {uint32_t ms,frame;std::array<int16_t,21> pad;CVector position,velocity,forward,up;};
#include "fixtures/learjet_takeoff_2b.inc"
alignas(16) uint8_t replayPool[0x20]{};uint8_t replayFlags[1]{1},*replayPoolPtr=replayPool;
uint32_t replayClock=1000;
void *ReplaySymbol(const char *name){return std::strstr(name,"ms_pVehiclePool")?&replayPoolPtr:nullptr;}
uint8_t *ReplayVehicle(int ref){return ref==replayFlags[0]?gBoat:nullptr;}
bool ReplayOwner(const uint8_t *p){return p==gBoat;}
void Prepare(float health)
{
 Reset();++replayFlags[0];replayClock+=20000;game::api.timeMs=&replayClock;game::api.PoolsGetVehicle=ReplayVehicle;
 *reinterpret_cast<uint8_t **>(replayPool)=gBoat;*reinterpret_cast<uint8_t **>(replayPool+8)=replayFlags;*reinterpret_cast<int32_t *>(replayPool+16)=1;
 *reinterpret_cast<void **>(gBoat)=reinterpret_cast<void *>(uintptr_t(0x1000));
 *reinterpret_cast<int16_t *>(gBoat+0x6a)=248;*reinterpret_cast<int32_t *>(gBoat+0x348)=4;
 *reinterpret_cast<float *>(gBoat+0x2a0)=health;*reinterpret_cast<float *>(gBoat+0xd0)=20000;*reinterpret_cast<float *>(gBoat+0xd4)=1;
 V(gBoat,0x38)=kInitialPosition;V(gBoat,0x84)=Times(kInitialVelocity,1/50.0f);V(gBoat,0x90)=Times(kInitialAngular,1/50.0f);
 V(gBoat,8)=kInitialRight;V(gBoat,0x18)=kInitialForward;V(gBoat,0x28)=kInitialUp;
 planeflight::SetAirportOwner(ReplayOwner);planeflight::SetAirportWheelControl(nullptr);gNativePad=true;
}
void Input(const TakeoffSample &s)
{
 std::memcpy(gPad,s.pad.data(),sizeof s.pad);gRoll=s.pad[0];gThrottle=s.pad[16];gBrake=s.pad[14];gLeft=s.pad[5]!=0;gRight=s.pad[7]!=0;
}
void Step(float dt)
{
 gStep=dt*50;V(gBoat,0x84).z-=.008f*gStep;
 const float resistance=std::pow(1/std::fabs(1+*reinterpret_cast<float *>(gBoat+0xd8)*.5f*planeflight::Dot(V(gBoat,0x84),V(gBoat,0x84))),gStep);
 V(gBoat,0x84)=Times(V(gBoat,0x84),resistance);V(gBoat,0x90)=Times(V(gBoat,0x90),.99f);
 CHECK(planeflight::Apply(gBoat,5));V(gBoat,0x38)=Plus(V(gBoat,0x38),Times(V(gBoat,0x84),gStep));
 const CVector angular=Times(V(gBoat,0x90),gStep);
 V(gBoat,0x18)=Normal(Plus(V(gBoat,0x18),Cross(angular,V(gBoat,0x18))));V(gBoat,0x28)=Normal(Plus(V(gBoat,0x28),Cross(angular,V(gBoat,0x28))));
 V(gBoat,8)=Normal(Cross(V(gBoat,0x18),V(gBoat,0x28)));V(gBoat,0x28)=Normal(Cross(V(gBoat,8),V(gBoat,0x18)));
 replayClock+=uint32_t(std::lround(dt*1000));
}
float Alpha(){return std::atan2(-planeflight::Dot(V(gBoat,0x84),V(gBoat,0x28)),std::max(.002f,planeflight::Dot(V(gBoat,0x84),V(gBoat,0x18))));}
struct Result {float peakHeight,finalHeight,minForward,maxAlpha,finalVertical,positionRms,returnSeconds;};
Result Run(float health,int inputMode=0)
{
 Prepare(health);float peak=V(gBoat,0x38).z,minimum=1000,alpha=0,error=0,elapsed=0,returned=-1;int count=0;
 for(size_t i=0;i+1<std::size(kSamples);++i){
  const auto &s=kSamples[i],&next=kSamples[i+1];Input(inputMode==1?next:s);
  const int frames=int(next.frame-s.frame);CHECK(frames>0&&frames<30);
  const float dt=(next.ms-s.ms)/1000.0f/frames;
  for(int j=0;j<frames;++j){
   if(inputMode==2){TakeoffSample interpolated=s;for(size_t k=0;k<s.pad.size();++k)interpolated.pad[k]=int16_t(std::lround(s.pad[k]+(next.pad[k]-s.pad[k])*(j+1.0f)/frames));Input(interpolated);}
   Step(dt);elapsed+=dt;peak=std::max(peak,V(gBoat,0x38).z);minimum=std::min(minimum,50*planeflight::Dot(V(gBoat,0x84),V(gBoat,0x18)));alpha=std::max(alpha,Alpha());
   // The sampled native run returns to the runway. This air-only replay
   // stops at its original lift-off height rather than flying underground.
   if(V(gBoat,0x38).z<kInitialPosition.z&&V(gBoat,0x84).z<0){returned=elapsed;break;}
  }
  if(returned>=0)break;
  const CVector delta=planeflight::Sub(V(gBoat,0x38),next.position);error+=planeflight::Dot(delta,delta);++count;
 }
 return {peak,V(gBoat,0x38).z,minimum,alpha,50*V(gBoat,0x84).z,std::sqrt(error/std::max(1,count)),returned};
}
}
int main()
{
 gExtraSymbol=ReplaySymbol;Reset();planeflight::Install();
 for(int mode:{0,1,2}){
  const auto damaged=Run(kRecordedHealth,mode),full=Run(1000,mode);
  for(const auto &row:std::array<std::pair<const char *,Result>,2>{{{"capturedHP",damaged},{"fullHP",full}}}){
   const auto &r=row.second;std::printf("%s input%s: zpeak%.2f zend%.2f forwardMin%.2f alphaMax%.3f vzEnd%.2f positionRms%.2f returnSeconds%.2f\n",row.first,mode==0?"hold":mode==1?"next":"interpolated",r.peakHeight,r.finalHeight,r.minForward,r.maxAlpha,r.finalVertical,r.positionRms,r.returnSeconds);
  }
  CHECK(damaged.peakHeight>34&&damaged.peakHeight<54&&damaged.maxAlpha>.30f);
  CHECK(full.peakHeight>damaged.peakHeight+30);
 }
 if(!failures)puts("plane recorded replay: passed");return failures?1:0;
}

#define VC_SOURCE_UUID_BYTES 1
#define main LegacyMechanicalChecks
#include "test_carmechanics.cpp"
#undef main

namespace sourcefixture {
bool owned=false;int reads=0;float exposure=0;AircraftBodySnapshot snapshot;
bool Owns(const void *v){return owned&&v==fixture::cars[0];}
bool Read(const void *v,AircraftBodySnapshot *out){if(!Owns(v)||!out)return false;++reads;*out=snapshot;return true;}
void Burn(void *v,float dt){if(Owns(v))exposure+=dt;}
void *Symbol(const char *s){
 if(!strcmp(s,"VCSourceAircraftBodyOwned"))return reinterpret_cast<void *>(Owns);
 if(!strcmp(s,"VCSourceAircraftBodyRead"))return reinterpret_cast<void *>(Read);
 if(!strcmp(s,"VCSourceAircraftBodyBurn"))return reinterpret_cast<void *>(Burn);
 return nullptr;
}
}
int main(){using namespace fixture;extraLookup=sourcefixture::Symbol;
 if(LegacyMechanicalChecks())return 1;
 Reset();{carmechanics::Controller scope(cars[0]);scope.history->state.damage[mechanicalmodel::kEngine]=.9f;scope.history->state.damage[mechanicalmodel::kElectrical]=.8f;}
 const auto before=carmechanics::gHistory[0].state;
 sourcefixture::owned=true;sourcefixture::snapshot={};sourcefixture::snapshot.power=.5f;sourcefixture::snapshot.lighting=.3f;
 sourcefixture::snapshot.engine=.2f;sourcefixture::snapshot.electrical=.6f;sourcefixture::snapshot.heat=1.1f;sourcefixture::snapshot.fuel=.4f;sourcefixture::snapshot.breach=.5f;
 sourcefixture::snapshot.wheels[0]={.5f,.6f,.4f,.03f,1};sourcefixture::reads=0;
 hits[0]={Nose(35)};nested=true;Run();
 Check(sourcefixture::reads==1,"one committed body snapshot supplies each source automobile controller");
 Check(std::fabs(torque[0]-.02f)<1e-7f&&torque[1]==.04f,"source power applies once and preserves nested shared handling actors");
 Check(std::fabs(wheels[0][0].thrust-.012f)<1e-7f&&std::fabs(wheels[0][0].grip-.025f)<1e-7f&&std::fabs(wheels[0][0].brake-.004f)<1e-7f&&wheels[0][2].grip==.05f,"source wheel snapshot scales only its native wheel inputs once");
 Check(carmechanics::gHistory[0].state.damage==before.damage&&carmechanics::gHistory[0].state.heat==before.heat,"source control and contacts never tick or damage the old mechanical history");
 Check(!carmechanics::BeginBullet(cars[0]).revision,"canonical actors cannot acquire a legacy projectile lease");
 Check(carmechanics::LightingPower(cars[0])==.3f,"render lighting consumes canonical electrical supply");
 const auto fire=carmechanics::FireInput(cars[0]);Check(fire.engine==.2f&&fire.electrical==.6f&&fire.fuel==.4f&&fire.breach==.5f&&fire.heat==1.1f,"fire reads the same canonical material systems");
 carmechanics::Burn(cars[0],.2f);Check(sourcefixture::exposure==.2f&&carmechanics::gHistory[0].state.damage==before.damage,"fire exposure queues source work without competing old injuries");
 gConfig.carMechanics=false;Run();Check(std::fabs(torque[0]-.02f)<1e-7f&&carmechanics::LightingPower(cars[0])==.3f,"legacy mechanics switch cannot substitute a competing source state");
 gConfig.carMechanics=true;
 for(int model:{155,177,199,217,218,227,194}){
  Reset(model);sourcefixture::reads=0;Run();
  Check(sourcefixture::reads==1&&std::fabs(torque[0]-.04f)<1e-7f,"source automobile aircraft reads one wheel snapshot without applying flight power through transmission again");
  Check(std::fabs(wheels[0][0].thrust-.012f)<1e-7f&&std::fabs(wheels[0][0].grip-.025f)<1e-7f&&std::fabs(wheels[0][0].brake-.004f)<1e-7f&&wheels[0][2].grip==.05f,"source automobile aircraft consumes the same canonical localized tire effects once");
 }
 printf("source vehicle consumers: %s\n",fails?"FAILED":"all passed");return fails?1:0;
}

#define VC_SOURCE_UUID_BYTES 1
#define main LegacyContactChecks
#include "test_cardeform.cpp"
#undef main
namespace sourcefixture {
bool owned=false;bool Owns(const void *v){return owned&&v==fixture::cars[0];}
void *Symbol(const char *s){return !strcmp(s,"VCSourceAircraftBodyOwned")?reinterpret_cast<void *>(Owns):nullptr;}
}
int main(){using namespace fixture;extraLookup=sourcefixture::Symbol;if(LegacyContactChecks(0,nullptr))return 1;
 for(int kind=0;kind<3;kind++){
  Reset();sourcefixture::owned=true;const int before=nativeCalls;Hit(kind);control(cars[0]);
  Check(nativeCalls>before&&dents.empty()&&mechanicalHits.empty(),"source contact retains native impulses and bypasses duplicate plugin material work");
 }
 // Material ownership replaces hull/part writers, never the occupants' crash shock.
 Reset();sourcefixture::owned=true;int shocks=crashdamage::calls;Hit(0);
 Check(crashdamage::calls==shocks+1&&std::fabs(crashdamage::last-10)<.01f&&dents.empty()&&mechanicalHits.empty(),"source-owned car crash still delivers occupant shock without legacy material work");
 Reset();sourcefixture::owned=true;shocks=crashdamage::calls;{auto p=Contact();p.n={0,0,1};float a,b;pair(cars[0],wall,p,a,b);}
 Check(crashdamage::calls==shocks,"source-owned car road support is not an occupant crash");
 Reset();sourcefixture::owned=true;*reinterpret_cast<int16_t *>(cars[0]+0x6a)=227;shocks=crashdamage::calls;{auto p=Contact();p.n={0,0,1};float a,b;pair(cars[0],wall,p,a,b);}
 Check(crashdamage::calls==shocks+1,"source-owned helicopter hard landing delivers occupant shock");
 Reset();game::api.AirframeEffects=+[](const uint8_t *){airframemodel::Effects e;e.managed=true;return e;};game::api.AirframeCollision=AirCollision;
 *reinterpret_cast<int16_t *>(cars[0]+0x6a)=180;*reinterpret_cast<int *>(cars[0]+0x348)=4;const int before=airHits;Hit(1);
 Check(airHits==before,"source aircraft native contact is never queued again by legacy airframe routing");
 printf("source vehicle contact authority: %s\n",failures?"FAILED":"all passed");return failures?1:0;
}

#define VC_SOURCE_UUID_BYTES 1
#define main LegacyPartChecks
#include "test_carparts.cpp"
#undef main
namespace sourcefixture {
bool owned=false;bool Owns(const void *v){return owned&&v==fixture::vehicles[0];}
void *Symbol(const char *s){return !strcmp(s,"VCSourceAircraftBodyOwned")?reinterpret_cast<void *>(Owns):nullptr;}
}
int main(){using namespace fixture;extraLookup=sourcefixture::Symbol;if(LegacyPartChecks(0,nullptr))return 1;
 Reset();const auto old=carparts::BeginBullet(vehicles[0]);sourcefixture::owned=true;
 Check(!carparts::BeginBullet(vehicles[0]).revision&&!carparts::CurrentBullet(vehicles[0],old),"source actor cannot retain or acquire a legacy fastening lease");
 carparts::Projectile(vehicles[0],old,7,200);Check(calls.empty()&&State(vehicles[0],7)==0,"pending legacy projectile yields after canonical ownership transfer");
 Set(vehicles[0],7,1);Run();Check(calls.size()==1&&calls[0].force==60,"source actor leaves exact native scripted/control force unchanged");
 *reinterpret_cast<int16_t *>(vehicles[0]+0x6a)=199;Check(!carparts::SeverRotor(vehicles[0],true),"source rotor cannot detach through the legacy part owner");
 printf("source vehicle part authority: %s\n",failed?"FAILED":"all passed");return failed?1:0;
}

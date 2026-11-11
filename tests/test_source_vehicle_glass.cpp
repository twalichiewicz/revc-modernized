#define VC_SOURCE_UUID_BYTES 1
#define main LegacyGlassChecks
#include "test_carglass.cpp"
#undef main
namespace sourcefixture {
bool owned=false;bool Owns(const void *v){return owned&&v==fixture::cars.data();}
void *Symbol(const char *s){return !strcmp(s,"VCSourceAircraftBodyOwned")?reinterpret_cast<void *>(Owns):nullptr;}
}
int main(){using namespace fixture;extraLookup=sourcefixture::Symbol;if(LegacyGlassChecks())return 1;
 Reset();sourcefixture::owned=true;const auto original=atoms[0][0].geo;const int allocations=created;
 // The source kernel owns glazing: a penetrating round removes a tempered pane
 // whole (test_vehicle_glass_stopgap) and emits no legacy shards or meshes.
 Bullet();Check(atoms[0][0].geo==original&&created==allocations&&!shards,"source glass hit cannot allocate or break a legacy private mesh");
 dentmodel::Impact dent;dentmodel::Make({-.8f,.5f,-.1f},{1,0,0},15,dent);
 Check(!carglass::Dent(cars.data(),dent)&&!carglass::Perforate(cars.data(),&atoms[0][0],{-.8f,.5f,-.1f},{1,0,0},80),"source deformation/perforation bypass every legacy mesh writer");
 *reinterpret_cast<int16_t *>(cars.data()+0x6a)=180;*reinterpret_cast<int *>(cars.data()+0x348)=4;
 Check(!carglass::SeverSection(cars.data(),{1,0,0},0)&&atoms[0][0].geo==original,"source aircraft separation bypasses legacy section geometry");
 Check(created==allocations&&carglass::gCars.empty(),"canonical material writers create no competing legacy histories");
 printf("source vehicle glass authority: %s\n",failed?"FAILED":"all passed");return failed?1:0;
}

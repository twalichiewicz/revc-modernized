// Read-only original arm64 contracts for the boarding consumer.
#define main TowerNativeChecks
#include "test_towercollision_native.cpp"
#undef main
std::string NativeString(const Image &image,uint64_t address){std::string out;for(size_t i=0;i<64;++i){const char c=image.VA<char>(address+i);if(!c)break;out+=c;}return out;}
int main(){const char *path=std::getenv("MACVC_NATIVE");if(!path||!*path){std::fputs("MACVC_NATIVE must name the original arm64 macVC binary\n",stderr);return 2;}Image image;if(!image.Load(path))return 1;
 const auto definitions=image.symbols.at("__ZN12CAnimManager24ms_aAnimAssocDefinitionsE");
 for(const auto request:std::vector<std::pair<int,int>>{{30,0},{52,0},{30,3},{0,76},{0,80},{0,102}}){const auto group=definitions+uint64_t(request.first)*40;CHECK(NativeString(image,image.Pointer(group+8))=="ped");CHECK(image.VA<int>(group+20)>request.second);const auto names=image.Pointer(group+24),info=image.Pointer(group+32);
  const char *want=request.second==0?(request.first==52?"walk_back":"walk_player"):request.second==3?"IDLE_STANCE":request.second==76?"CAR_open_LHS":request.second==80?"CAR_getin_LHS":"CAR_sit";
  CHECK(NativeString(image,image.Pointer(names+uint64_t(request.second)*8))==want);CHECK(image.VA<int>(info+uint64_t(request.second)*8)==request.second);
  const int flags=image.VA<int>(info+uint64_t(request.second)*8+4);CHECK(flags==(request.second==0?0x162:request.second==102?4:request.second==3?2:0x18));
 }
 // A full-body sit starts at1 when only partial getin remains; a reused
 // sit preserves weight and receives (1-weight)*blend as its delta.
 CHECK(image.Word(0x100006fe4)==0x350000bb&&image.Word(0x100006fe8)==0x37200097);
 CHECK(image.Word(0x100006fec)==0x52a7f008&&image.Word(0x100006ff0)==0xb9002a68);
 CHECK(image.Word(0x100006ff8)==0xb9002a7f&&image.Word(0x100006ffc)==0xbd002e68);
 CHECK(image.Word(0x100006f38)==0xbd402a60&&image.Word(0x100006f40)==0x1e203820&&image.Word(0x100006f48)==0xbd002e60);
 CHECK(image.Word(0x1001a75f8)==0xb9419e68&&image.Word(0x1001a7604)==0x7100491f);
 CHECK(image.Branch(0x1001a7614)==image.symbols.at("__ZN8CVehicle9SetDriverEP4CPed"));
 CHECK(image.symbols.at("__ZN21CAnimBlendAssociation14SetCurrentTimeEf")==0x100004498);
 CHECK(image.Word(0x1000044d8)==0xbd003260); // current time at+30
 CHECK(image.Word(0x1000044b0)==0xbd402401); // hierarchy length at+24
 CHECK(image.Word(0x1000046d4)==0xf9401009&&image.Word(0x1000046d8)==0xbd402520); // same live hierarchy and clock
 CHECK(image.Word(0x100004480)==0xa9048801&&image.Word(0x100004490)==0xa9048801); // finish/delete callbacks occupy+48/+50
 CHECK(image.Word(0x10000a810)==0x3637ff08&&image.Word(0x10000a918)==0x3637fae8); // only bit6 extracts animation XY; car entry0x18 keeps authored XY
 CHECK(image.Branch(0x100138018)==0x10000c180&&image.Branch(0x1001381e0)==0x10000c180); // World updates anim before control independently
 CHECK(image.Branch(0x1001a73f8)==image.symbols.at("__ZN6CWorld6RemoveEP7CEntity")&&image.Branch(0x1001a7434,false)==image.symbols.at("__ZN6CWorld3AddEP7CEntity")); // Teleport refreshes sectors
 CHECK(image.Word(0x1001a73fc)==0x2d07266a&&image.Word(0x1001a7400)==0xbd004268); // exact by-value CVector HFA at+38/+40
 CHECK(image.Word(0x1001a7624)==0xb9434a88); // Warp's type guard is CVehicle+348
 CHECK(image.Word(0x1001a75d0)==0xb902fe68&&image.Word(0x1001a75e0)==0xf940c668); // driving state+2fc and ped flags+188
 CHECK(image.Branch(0x1001d5d30)==0x100192c88); // actual player control calls normal ped control
 CHECK(image.Word(0x10004e540)==0x90001b69&&image.Word(0x10004e544)==0x9119c129); // Initialise's three ped spheres at1003ba670
 CHECK(image.Word(0x10004e5a0)==0x7900514b&&image.Word(0x10004e5a4)==0xf9001d49); // Ped1 has3 spheres and that backing
 for(int i=0;i<3;++i){const uint64_t at=0x1003217c0+uint64_t(i)*16;CHECK(image.VA<float>(at)==0&&image.VA<float>(at+4)==0);CHECK(std::fabs(image.VA<float>(at+8)-(-.25f+.4f*i))<.000001f);CHECK(image.VA<float>(at+12)==.35f);}
 if(!failures)std::puts("Learjet boarding native: original IDs, clocks, root extraction, world animation and CPlane-safe seat/sector ABI passed");return failures?1:0;
}

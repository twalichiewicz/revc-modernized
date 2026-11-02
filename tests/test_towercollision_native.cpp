// Read-only exact-build evidence for the actual caller, solver geometry ABI,
// body contact consumer and the native COL triangle-plane cache lifetime.
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>
int failures;
#define CHECK(x) do{if(!(x)){++failures;std::printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
struct Image {
 std::vector<uint8_t> bytes;std::vector<segment_command_64> segments;std::map<std::string,uint64_t> symbols;
 template<class T>T At(size_t offset)const{T value{};CHECK(offset<=bytes.size()&&sizeof value<=bytes.size()-offset);if(offset<=bytes.size()&&sizeof value<=bytes.size()-offset)std::memcpy(&value,bytes.data()+offset,sizeof value);return value;}
 template<class T>T VA(uint64_t address)const{for(const auto &s:segments)if(address>=s.vmaddr&&address-s.vmaddr+sizeof(T)<=s.filesize)return At<T>(size_t(s.fileoff+address-s.vmaddr));CHECK(false);return {};}
 uint32_t Word(uint64_t address)const{return VA<uint32_t>(address);}
 uint64_t Branch(uint64_t address,bool link=true)const{const uint32_t instruction=Word(address);CHECK((instruction&0xfc000000)==(link?0x94000000u:0x14000000u));return uint64_t(int64_t(address)+(int64_t(int32_t(instruction<<6)>>6)*4));}
 uint64_t Pointer(uint64_t address)const{const uint64_t raw=VA<uint64_t>(address);return raw>=0x100000000&&raw<0x110000000?raw:0x100000000+(raw&0xfffffffffull);}
 bool Load(const char *path){std::ifstream file(path,std::ios::binary|std::ios::ate);CHECK(bool(file));if(!file)return false;const auto size=file.tellg();CHECK(size>0&&size<128*1024*1024);if(failures)return false;bytes.resize(size_t(size));file.seekg(0);file.read(reinterpret_cast<char *>(bytes.data()),size);const auto h=At<mach_header_64>(0);CHECK(h.magic==MH_MAGIC_64&&h.cputype==CPU_TYPE_ARM64);size_t at=sizeof h;bool uuid=false;
  for(uint32_t i=0;i<h.ncmds;++i){const auto c=At<load_command>(at);if(c.cmdsize<8||at+c.cmdsize>bytes.size())return false;if(c.cmd==LC_SEGMENT_64)segments.push_back(At<segment_command_64>(at));if(c.cmd==LC_UUID){constexpr uint8_t expected[]={0x02,0x98,0x3a,0x69,0x6b,0xd0,0x33,0xf7,0xb7,0x2b,0x2a,0xca,0x8b,0xcb,0x3c,0x33};uuid=!std::memcmp(At<uuid_command>(at).uuid,expected,16);}if(c.cmd==LC_SYMTAB){const auto t=At<symtab_command>(at);for(uint32_t n=0;n<t.nsyms;++n){const auto s=At<nlist_64>(t.symoff+size_t(n)*sizeof(nlist_64));const size_t name=t.stroff+s.n_un.n_strx;if(name<bytes.size()&&strnlen(reinterpret_cast<const char *>(bytes.data()+name),bytes.size()-name)<bytes.size()-name)symbols[reinterpret_cast<const char *>(bytes.data()+name)]=s.n_value;}}at+=c.cmdsize;
  }CHECK(uuid);return !failures;
 }
};
int main(){const char *path=std::getenv("MACVC_NATIVE");if(!path||!*path){std::fputs("MACVC_NATIVE must name the original arm64 macVC binary\n",stderr);return 2;}Image image;if(!image.Load(path))return 1;
 CHECK(image.Pointer(image.symbols["__ZTV6CPlane"]+16+0x90)==0x100155af0);
 CHECK(image.Pointer(image.symbols["__ZTV5CBoat"]+16+0x90)==0x100155af0);
 CHECK(image.Pointer(image.symbols["__ZTV11CAutomobile"]+16+0x90)==0x100258750);
 CHECK(image.Word(0x100258770)==0xaa0203e4&&image.Branch(0x100258844)==0x10004c40c); // original body output, direct automobile -> solver
 CHECK(image.Word(0x1002587b0)==0xf9401136&&image.Word(0x10025882c)==0xf9401103); // actual MI A/B COLs; alternate native A is refused
 CHECK(image.Word(0x100258830)==0x91002260&&image.Word(0x100258834)==0x91002282); // entity+8 matrices, no temporary copy
 CHECK(image.Word(0x100258838)==0x91138265&&image.Word(0x10025883c)==0x91160266); // wheel points+4e0/fractions+580
 CHECK(image.Word(0x1002481cc)==0x52800088&&image.Word(0x1002481d0)==0x3900baa8); // original automobile constructor creates four native lines
 CHECK(image.Word(0x10004c4c4)==0x39c0bb08&&image.Word(0x10004d074)==0xbc7b7a84&&image.Word(0x10004d09c)==0x52800509); // A.numLines and unguarded fractions/40-byte point output require real scratch
 CHECK(image.Branch(0x100155b3c)==0x10004c40c); // original CPhysical entity pair -> solver
 CHECK(image.Word(0x100155b1c)==0xf9401101&&image.Word(0x100155b28)==0xf9401103); // both MI+20 COLs
 CHECK(image.Word(0x10004cbb4)==0x7109651f); // native triangle candidate limit600
 CHECK(image.Word(0x100046f94)==0x52800197); // native vertex stride12
 CHECK(image.Word(0x100047008)==0x910022b5&&image.Word(0x10004700c)==0x91005294); // triangle8,plane20
 CHECK(image.Word(0x10004b718)==0x7ea0d428); // two-sided plane distance
 CHECK(image.Branch(0x10004cee4)==0x10004b6d8); // real ProcessSphereTriangle body consumer
 CHECK(image.Branch(0x10015b734)==0x100158570); // accepted native contacts -> native physical impulse
 CHECK(image.Word(0x10015b79c)==0xbd013e8a); // native accepted impulse publishes vehicle+13c
 CHECK(image.Word(0x10004a504)==0xa9002293); // cache LRU retains the COL identity
 CHECK(image.Branch(0x10004a5f4)==0x100046f48); // native cache owns allocated planes
 CHECK(image.Branch(0x10004d348)==0x1000470bc&&image.Branch(0x10004d37c,false)==0x100047030); // unlink then free
 const auto points=image.symbols["__ZZN9CPhysical26ProcessCollisionSectorListEP8CPtrListE10aColPoints"];CHECK(points==0x1007c6f28&&0x1007c7928-points==64*40);
 if(!failures)std::puts("tower collision native: Physical/Automobile callers, exact native body/wheel solver/float geometry/physical impulse and persistent COL-LRU contracts passed");return failures?1:0;
}

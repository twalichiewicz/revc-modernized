#pragma once
// Runs pristine arm64 shadow callers/consumers locally. Only external bindings
// and PC-relative addresses are relocated; the eligibility instructions remain
// unchanged. This never opens or modifies the running game.
#include "nativeweavearm.h"
#include <map>
#include <memory>
#include <unistd.h>

namespace nativejetshadowtest {
using nativeweavetest::Require;
using nativeweavetest::Signed;
template<class T> inline void Store(void *at,T value){std::memcpy(at,&value,sizeof value);}
template<class T> inline T Read(const void *at){T value;std::memcpy(&value,at,sizeof value);return value;}
struct Code {
 nativeweavetest::Image image;
 struct Range {uint64_t first,last;size_t offset;};
 std::vector<Range> ranges;
 std::map<uint64_t,void *> bindings;
 std::map<uint64_t,std::unique_ptr<uint8_t[]>> pages;
 uint8_t *code=nullptr;size_t cursor=0x8000,next=0;
 static void Unexpected(){Require(false,"unexpected native shadow dependency");}
 Code(){
  const auto h=image.At<mach_header_64>(0);size_t at=sizeof h;bool uuid=false;
  for(uint32_t i=0;i<h.ncmds;++i){const auto c=image.At<load_command>(at);if(c.cmd==LC_UUID){constexpr uint8_t expected[]={0x02,0x98,0x3a,0x69,0x6b,0xd0,0x33,0xf7,0xb7,0x2b,0x2a,0xca,0x8b,0xcb,0x3c,0x33};uuid=!std::memcmp(image.At<uuid_command>(at).uuid,expected,16);}at+=c.cmdsize;}
  Require(uuid,"shadow fixture requires exact pristine5217 UUID");
  code=static_cast<uint8_t *>(mmap(nullptr,0x10000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON|MAP_JIT,-1,0));
  Require(code!=MAP_FAILED,"allocate shadow consumer fixture");pthread_jit_write_protect_np(0);
 }
 ~Code(){if(code)munmap(code,0x10000);}
 uint8_t *Page(uint64_t address){
  const uint64_t base=address&~uint64_t(4095);auto &p=pages[base];if(!p){p=std::make_unique<uint8_t[]>(4096);const auto h=image.At<mach_header_64>(0);size_t at=sizeof h;
   for(uint32_t i=0;i<h.ncmds;++i){const auto c=image.At<load_command>(at);if(c.cmd==LC_SEGMENT_64){const auto s=image.At<segment_command_64>(at);for(size_t j=0;j<4096;++j)if(base+j>=s.vmaddr&&base+j-s.vmaddr<s.filesize)p[j]=image.bytes[size_t(s.fileoff+base+j-s.vmaddr)];}at+=c.cmdsize;}}
  return p.get()+size_t(address-base);
 }
 size_t Add(uint64_t first,uint64_t last){const size_t offset=next;Require(last>first&&last-first<0x4000,"bounded shadow range");next+=(size_t(last-first)+15)&~size_t(15);Require(next<0x8000,"native code fits fixture");ranges.push_back({first,last,offset});for(uint64_t p=first;p<last;p+=4)Store(code+offset+size_t(p-first),image.Word(p));return offset;}
 void *Address(uint64_t address){for(const auto &r:ranges)if(address>=r.first&&address<r.last)return code+r.offset+size_t(address-r.first);return nullptr;}
 size_t Literal(uint64_t value){cursor=(cursor+7)&~size_t(7);const auto at=cursor;cursor+=8;Require(cursor<0x10000,"shadow literals fit fixture");Store(code+at,value);return at;}
 uint32_t Load(size_t at,uint64_t value,unsigned reg){const auto dest=Literal(value);return 0x58000000|((uint32_t((int64_t(dest)-int64_t(at))/4)&0x7ffff)<<5)|reg;}
 size_t Thunk(void *destination){cursor=(cursor+7)&~size_t(7);const auto at=cursor;cursor+=16;Store(code+at,uint32_t(0x58000050));Store(code+at+4,uint32_t(0xd61f0200));Store(code+at+8,uint64_t(destination));return at;}
 void Finish(){
  for(const auto &r:ranges)for(uint64_t pc=r.first;pc<r.last;pc+=4){const auto w=image.Word(pc);const size_t at=r.offset+size_t(pc-r.first);
   if((w&0x9f000000)==0x90000000){const auto imm=((w>>29)&3)|(((w>>5)&0x7ffff)<<2);const uint64_t page=uint64_t(int64_t(pc&~uint64_t(4095))+Signed(imm,21)*4096);Store(code+at,Load(at,uint64_t(Page(page)),w&31));}
   else if((w&0x9f000000)==0x10000000){const auto imm=((w>>29)&3)|(((w>>5)&0x7ffff)<<2);const uint64_t dest=uint64_t(int64_t(pc)+Signed(imm,21));void *target=Address(dest);Store(code+at,Load(at,uint64_t(target?target:Page(dest)),w&31));}
   else if((w&0x7c000000)==0x14000000){const uint64_t dest=uint64_t(int64_t(pc)+Signed(w&0x3ffffff,26)*4);void *target=Address(dest);if(target){const auto delta=(reinterpret_cast<uint8_t *>(target)-(code+at))/4;Store(code+at,(w&0xfc000000)|(uint32_t(delta)&0x3ffffff));}else {const auto found=bindings.find(dest);const size_t thunk=Thunk(found==bindings.end()?reinterpret_cast<void *>(Unexpected):found->second);Store(code+at,(w&0xfc000000)|(uint32_t((int64_t(thunk)-int64_t(at))/4)&0x3ffffff));}}
  }
  sys_icache_invalidate(code,0x10000);pthread_jit_write_protect_np(1);
 }
};
struct GuardedPlane {
 uint8_t *allocation=nullptr,*entity=nullptr;size_t page=0;
 GuardedPlane(){page=size_t(getpagesize());allocation=static_cast<uint8_t *>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));Require(allocation!=MAP_FAILED,"guarded plane allocation");Require(mprotect(allocation+page,page,PROT_NONE)==0,"guard page after exact CPlane");entity=allocation+page-0x388;std::memset(entity,0,0x388);}
 ~GuardedPlane(){munmap(allocation,page*2);}
};
// The extracted native CRTShadows::Render vehicle-pool block expects its
// original stack camera and x24 pool binding. Preserve callee-saved registers
// around that exact block; its sole continuation branches to the saved label.
__attribute__((naked)) inline void VehiclePool(void (*)(),uint8_t **,const void *){
 __asm__ volatile(
  "stp x29, x30, [sp, #-0x60]!\n"
  "stp x19, x20, [sp, #0x10]\n"
  "stp x21, x22, [sp, #0x20]\n"
  "stp x23, x24, [sp, #0x30]\n"
  "stp d8, d9, [sp, #0x40]\n"
  "mov x29, sp\n"
  "sub sp, sp, #0xb0\n"
  "ldr x8, [x2]\n"
  "str x8, [sp, #0x98]\n"
  "ldr w8, [x2, #8]\n"
  "str w8, [sp, #0xa0]\n"
  "mov x24, x1\n"
  "adr x22, 1f\n"
  "blr x0\n"
  "1:\n"
  "add sp, sp, #0xb0\n"
  "ldp d8, d9, [sp, #0x40]\n"
  "ldp x23, x24, [sp, #0x30]\n"
  "ldp x21, x22, [sp, #0x20]\n"
  "ldp x19, x20, [sp, #0x10]\n"
  "ldp x29, x30, [sp], #0x60\n"
  "ret\n");
}
}

#pragma once
// Isolated execution of the pristine ApplyDamage leaf, matching the existing
// nativeweave fixture. It writes only our 24-byte damage records, calls no
// external functions, and reads one relocated seven-float component table.
#include "nativeweavearm.h"
namespace nativeparttest {
using Apply=bool (*)(uint8_t *,int,float,float);
struct Fixture {
 nativeweavetest::Image image;
 uint8_t *code=nullptr;
 Apply apply=nullptr;
 static constexpr uint64_t kStart=0x100270868,kEnd=0x100270a20,kTable=0x1003751f4;
 Fixture(){
  using nativeweavetest::Require;
  uint64_t hash=0xcbf29ce484222325ULL;
  for(uint64_t pc=kStart;pc<kEnd;pc+=4){uint32_t w=image.Word(pc);for(int i=0;i<4;i++){hash^=uint8_t(w>>(8*i));hash*=0x100000001b3ULL;}}
  Require(hash==0xc6aa7b03b0c7797bULL,"exact supported ApplyDamage leaf");
  Require(image.Word(0x1002708c0)==0xb000082a&&image.Word(0x1002708c4)==0x9107d14a,"only native table relocation");
  code=static_cast<uint8_t *>(mmap(nullptr,0x4000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON|MAP_JIT,-1,0));
  Require(code!=MAP_FAILED,"allocate isolated component fixture");pthread_jit_write_protect_np(0);
  for(uint64_t pc=kStart;pc<kEnd;pc+=4){uint32_t w=image.Word(pc);memcpy(code+pc-kStart,&w,4);}
  const int64_t pages=(int64_t(uintptr_t(code+0x1000)&~uintptr_t(0xfff))-int64_t(uintptr_t(code+0x58)&~uintptr_t(0xfff)))/4096;
  const uint32_t imm=uint32_t(pages)&0x1fffff;
  const uint32_t adrp=0x9000000a|((imm&3)<<29)|((imm>>2)<<5);
  memcpy(code+0x58,&adrp,4);
  for(int i=0;i<7;i++){uint32_t w=image.Word(kTable+4*i);memcpy(code+0x11f4+4*i,&w,4);}
  sys_icache_invalidate(code,0x4000);pthread_jit_write_protect_np(1);apply=reinterpret_cast<Apply>(code);
 }
 ~Fixture(){if(code&&code!=MAP_FAILED)munmap(code,0x4000);}
 Fixture(const Fixture &)=delete;Fixture &operator=(const Fixture &)=delete;
};
}

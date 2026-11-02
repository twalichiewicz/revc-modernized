// Production persistent anchors through the exact 5217 world destructor.
#define AIRPORTJETS_NO_MAIN
#include "test_airportjets.cpp"
#include "nativejetshadowarm.h"

namespace {
using nativejetshadowtest::Store;
struct WorldNode {uint8_t *entity;WorldNode *previous=nullptr,*next=nullptr;};
std::vector<uint8_t> worldBytes(0x358+80*80*80+4096);
WorldNode nodes[7];int rwDestroyed,entitiesDestroyed,sourceAdds[7],sourceRemoves[7];
void *sourceVtable[4]{};
void (*nativeWorldShutdown)();
WorldNode *&WorldHead(){return *reinterpret_cast<WorldNode **>(worldBytes.data()+0x358);}
int SourceIndex(uint8_t *v){for(int i=0;i<7;++i)if(v==buildings[i])return i;return -1;}
void UnlinkSource(uint8_t *v){
 const int index=SourceIndex(v);if(index<0)return;auto &node=nodes[index];
 if(node.previous)node.previous->next=node.next;else if(WorldHead()==&node)WorldHead()=node.next;
 if(node.next)node.next->previous=node.previous;
}
void NativeDeleteRw(uint8_t *){++rwDestroyed;}
void NativeDestroySource(uint8_t *v){++entitiesDestroyed;UnlinkSource(v);}
void NativeWorldAdd(uint8_t *v){
 const int index=SourceIndex(v);
 if(index>=0){CHECK(!sourceInWorld[index]);++sourceAdds[index];}
 AddWorld(v);if(index<0)return;auto &node=nodes[index];
 node.entity=v;node.previous=nullptr;node.next=WorldHead();
 if(node.next)node.next->previous=&node;WorldHead()=&node;
}
void NativeWorldRemove(uint8_t *v){
 const int index=SourceIndex(v);if(index>=0){CHECK(sourceInWorld[index]);++sourceRemoves[index];}
 RemoveWorld(v);UnlinkSource(v);
}
void NothingWorld(){}
void WorldDeleteNode(void *,size_t){}
void *WorldOriginal(const char *name,void *hook){
 void *previous=OriginalJet(name,hook);
 return !std::strcmp(name,"_ZN6CWorld8ShutDownEv")?reinterpret_cast<void *>(nativeWorldShutdown):previous;
}
void FreshWorld(){
 buildingPoolPointer=buildingPool;vehiclePoolPointer=vehiclePool;Setup();
 area=0;mission=cutsceneRunning=blocked=false;
 std::fill(worldBytes.begin(),worldBytes.end(),0);
 std::fill(sourceAdds,sourceAdds+7,0);std::fill(sourceRemoves,sourceRemoves+7,0);
 rwDestroyed=entitiesDestroyed=0;
 sourceVtable[1]=reinterpret_cast<void *>(NativeDestroySource);
 sourceVtable[3]=reinterpret_cast<void *>(NativeDeleteRw);
 for(int i=0;i<7;++i){Store(buildings[i],sourceVtable);nodes[i]={buildings[i],nullptr,WorldHead()};if(nodes[i].next)nodes[i].next->previous=nodes+i;WorldHead()=nodes+i;}
 airportjets::n.add=NativeWorldAdd;airportjets::n.remove=NativeWorldRemove;
 // Source cleanup is isolated from ordinary dynamic actors here. The actual
 // native routine owns their destruction; this fixture has only buildings.
 V(gPlayer,0x38)={10000,10000,20};
}
}
int main(){
 using namespace nativejetshadowtest;
 Code code;code.Add(0x100136014,0x100136cb0);
 code.bindings={{0x100033f74,reinterpret_cast<void *>(NothingWorld)},
  {0x100155738,reinterpret_cast<void *>(NothingWorld)},
  {0x1001020d8,reinterpret_cast<void *>(WorldDeleteNode)}};code.Finish();
 // Relocate the native world-sector ADRP pages to bounded fixture storage.
 pthread_jit_write_protect_np(0);
 for(auto range:code.ranges)for(uint64_t pc=range.first;pc<range.last;pc+=4){
  const auto word=code.image.Word(pc);if((word&0x9f000000)!=0x90000000)continue;
  const auto immediate=((word>>29)&3)|(((word>>5)&0x7ffff)<<2);
  const uint64_t page=uint64_t(int64_t(pc&~uint64_t(4095))+Signed(immediate,21)*4096);
  if(page==0x10058e000){const size_t at=range.offset+size_t(pc-range.first);Store(code.code+at,code.Load(at,uint64_t(worldBytes.data()),word&31));}
 }
 sys_icache_invalidate(code.code,0x10000);pthread_jit_write_protect_np(1);
 nativeWorldShutdown=reinterpret_cast<void (*)()>(code.Address(0x100136014));
 FreshWorld();nativeWorldShutdown();
 CHECK(rwDestroyed==7&&entitiesDestroyed==7);
 std::printf("Native visible-source control: %d RW deletions / %d destructors\n",rwDestroyed,entitiesDestroyed);

 FreshWorld();CHECK(planeflight::Install());gChainOriginal=WorldOriginal;CHECK(airportjets::Install());
 airportjets::n.add=NativeWorldAdd;airportjets::n.remove=NativeWorldRemove;
 auto world=JetHook<void (*)()>("CWorld8ShutDown");
 auto modelsShutdown=JetHook<void (*)()>("CModelInfo8ShutDown");CHECK(world&&modelsShutdown);
 for(int invalid=0;invalid<3;++invalid){
  if(invalid)FreshWorld();JetFrame(1000+invalid*1000);CHECK(WorldHead()==nullptr);
  for(int i=0;i<7;++i)CHECK(sourceRemoves[i]==1&&sourceAdds[i]==0);
  if(invalid==1)++buildingFlags[0];
  if(invalid==2)V(buildings[0],0x38).x+=1;
  // Missing hook still runs the real destructor, retaining a behavioral RED.
  if(world)world();else nativeWorldShutdown();
  const int expected=invalid?6:7;CHECK(rwDestroyed==expected&&entitiesDestroyed==expected);
  for(int i=0;i<7;++i)CHECK(sourceAdds[i]==int(i>0||!invalid));
  if(world)world();CHECK(rwDestroyed==expected&&entitiesDestroyed==expected);
  const int beforeAdds=added,beforeRemoves=removed;
  buildingPoolPointer=reinterpret_cast<uint8_t *>(uintptr_t(1));
  if(modelsShutdown)modelsShutdown();
  CHECK(added==beforeAdds&&removed==beforeRemoves);buildingPoolPointer=buildingPool;
  std::printf("Production anchors -> native world cleanup: %d RW deletions / %d destructors, invalid=%d\n",rwDestroyed,entitiesDestroyed,invalid);
 }
 std::printf("Native airport source cleanup: %d failures\n",failures);return failures?1:0;
}

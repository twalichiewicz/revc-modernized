// Actual arm64 CRect ABI + verified native BuildPedLists caller/sector math.
// The native-shaped sector backend records rejected addresses without reading
// the user's crashing out-of-grid list pointer. No game process is touched.
#include "../src/gridfix.cpp"
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mach-o/loader.h>
#include <string>
#include <vector>
static int failures;
namespace reacthit {
static bool expandedPose;
void ExpandBounds(uint8_t *,float &left,float &top,float &right,float &bottom)
{
 if(expandedPose){left=std::min(left,-3000.f);top=std::max(top,3000.f);right=std::max(right,3000.f);bottom=std::min(bottom,-3000.f);}
}
}
#define CHECK(c) do{if(!(c)){std::printf("FAIL %d: %s\n",__LINE__,#c);++failures;}}while(0)
using RectFn=uint8_t *(*)(uint8_t *,float,float,float,float);
struct TestBounds {float left,top,right,bottom;};
using BoundsFn=TestBounds (*)(uint8_t *);
static TestBounds NativeBounds(uint8_t *);
extern "C" {RectFn gGridRectTarget;BoundsFn gGridBoundsTarget;uint8_t *GridNativeRect(uint8_t *,float,float,float,float);uint8_t *GridPedRectCall(uint8_t *,float,float,float,float);void GridPedRectReturn();TestBounds GridPhysicalAddBounds(uint8_t *);void GridPhysicalAddReturn();TestBounds GridPhysicalReAddBounds(uint8_t *);void GridPhysicalReAddReturn();}
// Native ctor's exact three instructions (verified below against the executable).
// The caller preserves LR exactly as BuildPedLists does around its CRect call.
asm(".text\n.p2align 2\n.globl _GridNativeRect\n_GridNativeRect:\n"
    ".long 0x2d010402\n.long 0x2d000c00\n.long 0xd65f03c0\n"
    ".p2align 2\n.globl _GridPedRectCall\n_GridPedRectCall:\n"
    "stp x29,x30,[sp,#-16]!\nmov x29,sp\nadrp x8,_gGridRectTarget@PAGE\nldr x8,[x8,_gGridRectTarget@PAGEOFF]\nblr x8\n"
    ".globl _GridPedRectReturn\n_GridPedRectReturn:\nldp x29,x30,[sp],#16\nret\n"
    ".p2align 2\n.globl _GridPhysicalAddBounds\n_GridPhysicalAddBounds:\n"
    "stp x29,x30,[sp,#-16]!\nmov x29,sp\nldr x8,[x0]\nldr x8,[x8,#0x40]\nblr x8\n"
    ".globl _GridPhysicalAddReturn\n_GridPhysicalAddReturn:\nldp x29,x30,[sp],#16\nret\n"
    ".p2align 2\n.globl _GridPhysicalReAddBounds\n_GridPhysicalReAddBounds:\n"
    "stp x29,x30,[sp,#-64]!\nmov x29,sp\nldr x8,[x0]\nldr x8,[x8,#0x40]\nstr x0,[sp,#0x28]\nblr x8\n"
    ".globl _GridPhysicalReAddReturn\n_GridPhysicalReAddReturn:\nldp x29,x30,[sp],#64\nret\n");
static bool validNativeCall=true,validPhysicalCtor=true;
static uintptr_t imageBaseOverride;
static std::array<void *,16> physicalVtable{};
static int hooksInstalled,invalidReads,validReads;
namespace hooks {
void *Install(const char *name,void *fn){++hooksInstalled;if(!std::strcmp(name,"_ZN5CRectC1Effff")){gGridRectTarget=reinterpret_cast<RectFn>(fn);return reinterpret_cast<void *>(GridNativeRect);}if(!std::strcmp(name,"_ZN9CPhysical12GetBoundRectEv")){gGridBoundsTarget=reinterpret_cast<BoundsFn>(fn);physicalVtable[8]=fn;return reinterpret_cast<void *>(NativeBounds);}return reinterpret_cast<void *>(uintptr_t(1));}
const void *ImageBase(){return reinterpret_cast<void *>(imageBaseOverride?imageBaseOverride:reinterpret_cast<uintptr_t>(GridPedRectReturn)-0x18db50);}
bool IsCallTo(uint32_t ret,const char *symbol){CHECK((ret==0x18db50||ret==0x1556ac)&&!std::strcmp(symbol,"_ZN5CRectC1Effff"));return validNativeCall&&(ret!=0x1556ac||validPhysicalCtor);}
}
void Log(const char *,...){}
static uint32_t NativeWord(uint64_t address){
 const char *p=std::getenv("MACVC_NATIVE"),*home=std::getenv("HOME");
 // Verify original instructions even after the installed constructor is hooked.
 const std::string original=home?std::string(home)+"/Library/Application Support/macVC/vcmodernhud/macVC.vanilla":"";
 std::ifstream f(p?p:original.c_str(),std::ios::binary|std::ios::ate);CHECK(bool(f));if(!f)return 0;
 const size_t size=size_t(f.tellg());std::vector<uint8_t> bytes(size);f.seekg(0);f.read(reinterpret_cast<char *>(bytes.data()),size);
 if(size<sizeof(mach_header_64))return 0;mach_header_64 h{};std::memcpy(&h,bytes.data(),sizeof h);size_t at=sizeof h;
 for(uint32_t i=0;i<h.ncmds&&at+sizeof(load_command)<=size;++i){load_command c{};std::memcpy(&c,bytes.data()+at,sizeof c);if(c.cmdsize<8||c.cmdsize>size-at)break;
  if(c.cmd==LC_SEGMENT_64&&c.cmdsize>=sizeof(segment_command_64)){segment_command_64 s{};std::memcpy(&s,bytes.data()+at,sizeof s);if(address>=s.vmaddr&&address-s.vmaddr+4<=s.filesize&&s.fileoff+address-s.vmaddr+4<=size){uint32_t w;std::memcpy(&w,bytes.data()+s.fileoff+address-s.vmaddr,4);return w;}}
  at+=c.cmdsize;
 }return 0;
}
static int NativeIndex(float position,float offset){return int((position+offset)/50.0f);}
static void NativeLists(const std::array<float,4> &rect){
 for(float p:rect)if(!std::isfinite(p)){++invalidReads;return;}
 const int y0=NativeIndex(rect[3],2000),y1=NativeIndex(rect[1],2000),x0=NativeIndex(rect[0],2400),x1=NativeIndex(rect[2],2400);
 if(y0>y1||x0>x1)return;
 // Exact native upper-only bounds selection; a negative row/column survives.
 for(int row=y0;row<=y1&&row<y0+200;++row)for(int col=x0;col<=x1&&col<x0+200;++col){
  const intptr_t offset=row>79||col>79?0:intptr_t(row)*6400+intptr_t(col)*80;
  if(offset<0||offset>=80*80*80)++invalidReads;else ++validReads;
 }
}
static std::array<float,4> Query(float left,float bottom,float right,float top){std::array<float,4> r{};CHECK(GridPedRectCall(reinterpret_cast<uint8_t *>(r.data()),left,bottom,right,top)==reinterpret_cast<uint8_t *>(r.data()));NativeLists(r);return r;}
alignas(16) static uint8_t physical[0x388];
static TestBounds NativeBounds(uint8_t *actor){CHECK(actor==physical);TestBounds b{};std::memcpy(&b,actor+0x100,sizeof b);return b;}
struct Entry {void *list=nullptr,*node=nullptr;intptr_t sector=0;Entry *previous=nullptr,*next=nullptr;};
static_assert(sizeof(Entry)==40&&offsetof(Entry,sector)==16);
static std::vector<Entry> membership;
static int invalidWrites,collisionCalls,shiftCalls;
static void NativeMembership(TestBounds b,bool replace){
 if(replace)membership.clear();
 const std::array<float,4> r{b.left,b.top,b.right,b.bottom};for(float p:r)if(!std::isfinite(p)){++invalidWrites;return;}
 const int x0=NativeIndex(b.left,2400),x1=NativeIndex(b.right,2400),y0=NativeIndex(b.bottom,2000),y1=NativeIndex(b.top,2000);
 // Native re3_assert reports negative bounds, then continues into these loops.
 for(int row=y0;row<=y1&&row<y0+200;++row)for(int col=x0;col<=x1&&col<x0+200;++col){const intptr_t sector=row>79||col>79?0:intptr_t(row)*6400+intptr_t(col)*80;if(sector<0||sector>=512000)++invalidWrites;Entry entry{};entry.sector=sector;membership.push_back(entry);}
}
static void NativeCollisionAndShift(){
 // CheckCollision/CheckCollision_SimpleCar/ProcessShift consume entryInfo+16,
 // then inspect eight list heads. This is the exact failing indirection.
 for(const auto &entry:membership){if(entry.sector<0||entry.sector>=512000){++invalidReads;continue;}++collisionCalls;++shiftCalls;}
}
static void PhysicalCase(TestBounds b,BoundsFn call,bool readd){std::memset(physical,0,sizeof physical);void *vt=physicalVtable.data();std::memcpy(physical,&vt,sizeof vt);std::memcpy(physical+0x100,&b,sizeof b);const auto saved=std::vector<uint8_t>(physical,physical+sizeof physical);invalidReads=invalidWrites=collisionCalls=shiftCalls=0;if(!readd)membership.clear();NativeMembership(call(physical),readd);NativeCollisionAndShift();CHECK(invalidWrites==0&&invalidReads==0);CHECK(!std::memcmp(saved.data(),physical,sizeof physical));}
int main(){
 CHECK(NativeWord(0x100172e44)==0x2d010402&&NativeWord(0x100172e48)==0x2d000c00&&NativeWord(0x100172e4c)==0xd65f03c0);
 const uint32_t call=NativeWord(0x10018db4c);const int32_t imm=int32_t(call<<6)>>6;CHECK((call&0xfc000000)==0x94000000&&0x10018db4c+intptr_t(imm)*4==0x100172e44);
 CHECK(NativeWord(0x10018dc04)==0x52832008&&NativeWord(0x10018dc10)==0x9b082776&&NativeWord(0x10018dc3c)==0x9b085ae8&&NativeWord(0x10018dc48)==0x9a88c128&&NativeWord(0x10018dc84)==0xf94002bc);
 // VerticalLine already has native lower AND upper clamps; Sphere intersects
 // min/max ranges and skips empty intersections. Neither needs a new hook.
 CHECK(NativeWord(0x10012dce0)==0x0aac7d8c&&NativeWord(0x10012dcec)==0x1a8eb18c&&NativeWord(0x10012dcf0)==0x0aad7dad&&NativeWord(0x10012dcf8)==0x1a8eb1ad);
 CHECK(NativeWord(0x100131134)==0x0aa87d08&&NativeWord(0x100131150)==0x0aa97d33&&NativeWord(0x10013116c)==0x1a8ab129&&NativeWord(0x100131184)==0x1a8ab16a&&NativeWord(0x100131268)==0x5400018d);
 // Crash raw-register proof: x27=-1, x23=10, x22=sectors-0x1900.
 CHECK(intptr_t(0x104f8ca58)-intptr_t(0x104f8e358)==-6400);
 CHECK(NativeIndex(-2060,2000)==-1&&NativeIndex(-1880,2400)==10);
 gGridRectTarget=GridNativeRect;
 invalidReads=validReads=0;Query(-1880,-2060,-1840,-2020);CHECK(invalidReads>0); // characterized native failure
 CHECK(gridfix::Install());
 invalidReads=validReads=0;Query(-1880,-2060,-1840,-2020);CHECK(invalidReads==0&&validReads>0);
 for(auto bounds:{std::array<float,4>{-2455,-100,-2420,100},{1580,-100,1650,100},{-100,1980,100,2050},{-3000,-3000,3000,3000}}){invalidReads=validReads=0;Query(bounds[0],bounds[1],bounds[2],bounds[3]);CHECK(invalidReads==0&&validReads>0);}
 invalidReads=validReads=0;Query(NAN,-10,10,10);CHECK(invalidReads==0&&validReads==0);
 invalidReads=validReads=0;Query(-10,-10,INFINITY,10);CHECK(invalidReads==0&&validReads==0);
 const auto normal=Query(-100,-200,300,400);CHECK((normal==std::array<float,4>{-100,400,300,-200}));
 // Arbitrary CRect construction, including off-grid UI rectangles, is unchanged.
 std::array<float,4> other{};CHECK(gGridRectTarget(reinterpret_cast<uint8_t *>(other.data()),-9999,-8888,7777,6666)==reinterpret_cast<uint8_t *>(other.data()));CHECK((other==std::array<float,4>{-9999,6666,7777,-8888}));
 validNativeCall=false;const int previous=hooksInstalled;CHECK(!gridfix::Install()&&hooksInstalled==previous+2); // existing LOS hooks still install
 GridPedRectCall(reinterpret_cast<uint8_t *>(other.data()),-9999,-8888,7777,6666);CHECK((other==std::array<float,4>{-9999,6666,7777,-8888}));
 validNativeCall=true;CHECK(gridfix::Install());invalidReads=validReads=0;Query(-1880,-2060,-1840,-2020);CHECK(invalidReads==0&&validReads>0);
 // Second real crash: stored entryInfo sector=-5120 (row=-1,col16), consumed
 // list2 yields an invalid node. Protect the producer, not this dereference.
 CHECK(intptr_t(0x102dfcf58)-intptr_t(0x102dfe358)==-5120);
 membership.clear();invalidReads=invalidWrites=0;NativeMembership({-1600,-2020,-1550,-2060},false);NativeCollisionAndShift();CHECK(!membership.empty()&&membership.front().sector==-5120&&invalidWrites>0&&invalidReads>0);
 CHECK(NativeWord(0x100154d5c)==0xf9400008&&NativeWord(0x100154d60)==0xf9402108&&NativeWord(0x100154d64)==0xd63f0100);
 CHECK(NativeWord(0x100155254)==0xf9400008&&NativeWord(0x100155258)==0xf9402108&&NativeWord(0x10015525c)==0xf90017e0&&NativeWord(0x100155260)==0xd63f0100);
 CHECK(NativeWord(0x1001556ac)==0x2d4007e0&&NativeWord(0x1001556b0)==0x2d410fe2); // native HFA return s0..s3
 CHECK(NativeWord(0x100155518)==0xf9000a95&&NativeWord(0x10015cc98)==0xf9400a81&&NativeWord(0x10015b608)==0xf8797b53&&NativeWord(0x10015b620)==0xf9400277);
 CHECK(NativeWord(0x10034e278)==0x00155664&&(NativeWord(0x10034e27c)&15)==0); // CPlane slot; chained-fixup low36-bit image offset
 const uint32_t ctor=NativeWord(0x1001556a8);CHECK((ctor&0xfc000000)==0x94000000&&0x1001556a8+intptr_t(int32_t(ctor<<6)>>6)*4==0x100172e44);
 gGridBoundsTarget=NativeBounds;physicalVtable[8]=reinterpret_cast<void *>(NativeBounds);
 for(int producer:{0,1}){
  imageBaseOverride=reinterpret_cast<uintptr_t>(producer?GridPhysicalReAddReturn:GridPhysicalAddReturn)-(producer?0x155264:0x154d68);CHECK(gridfix::Install());
  auto call=producer?GridPhysicalReAddBounds:GridPhysicalAddBounds;membership.clear();
  for(TestBounds b: {TestBounds{-1600,-2020,-1550,-2060},TestBounds{-2480,20,-2420,-20},TestBounds{1580,20,1650,-20},TestBounds{-10,2060,10,1980},TestBounds{-2450,2050,1650,-2050}}){PhysicalCase(b,call,producer!=0);CHECK(collisionCalls>0&&shiftCalls>0);}
  const TestBounds normal{-100,400,300,-200};PhysicalCase(normal,call,true);CHECK(collisionCalls>0&&shiftCalls>0);TestBounds raw{};std::memcpy(&raw,physical+0x100,sizeof raw);CHECK(!std::memcmp(&raw,&normal,sizeof raw));
  reacthit::expandedPose=true;PhysicalCase(normal,call,true);const auto expanded=call(physical);
  CHECK(expanded.left==gridclip::kMinX+gridclip::kInside&&expanded.right==gridclip::kMaxX-gridclip::kInside);
  CHECK(expanded.bottom==gridclip::kMinY+gridclip::kInside&&expanded.top==gridclip::kMaxY-gridclip::kInside);
  reacthit::expandedPose=false;
  PhysicalCase({NAN,20,10,-20},call,true);CHECK(membership.empty()&&collisionCalls==0&&shiftCalls==0);
  PhysicalCase({-10,20,INFINITY,-20},call,true);CHECK(membership.empty());
  // Leaving/returning to the edge updates native stored membership safely.
  PhysicalCase(normal,call,true);PhysicalCase({-1600,-2020,-1550,-2060},call,true);PhysicalCase(normal,call,true);
 }
 // Ordinary GetBoundRect callers must still see the real off-world bounds.
 const TestBounds outside{-1600,-2020,-1550,-2060};std::memcpy(physical+0x100,&outside,sizeof outside);const auto unscoped=gGridBoundsTarget(physical);CHECK(!std::memcmp(&unscoped,&outside,sizeof outside));
 validPhysicalCtor=false;CHECK(!gridfix::Install());const auto unvalidated=GridPhysicalReAddBounds(physical);CHECK(!std::memcmp(&unvalidated,&outside,sizeof outside));
 if(!failures)std::puts("gridfix native query boundary: all passed");return failures?1:0;
}

// Real roster Frame -> native RequestModel dependency queue -> native Update
// pruning/conversion contract. This does not run or control the game.
#define main PlaneForceFixtures
#include "test_planeflight.cpp"
#undef main
#include <fstream>
#include <mach-o/loader.h>
#include <vector>

namespace {
struct NativeImage {
	std::vector<uint8_t> data;
	template<class T> T At(size_t offset) const {
		T value{};CHECK(offset<=data.size()&&sizeof value<=data.size()-offset);
		if(offset<=data.size()&&sizeof value<=data.size()-offset)std::memcpy(&value,data.data()+offset,sizeof value);
		return value;
	}
	uint32_t Instruction(uint64_t address) const {
		const auto header=At<mach_header_64>(0);CHECK(header.magic==MH_MAGIC_64);
		size_t cursor=sizeof header;
		for(uint32_t i=0;i<header.ncmds&&i<4096;++i){
			const auto command=At<load_command>(cursor);
			if(command.cmdsize<sizeof command||cursor>data.size()||command.cmdsize>data.size()-cursor)break;
			if(command.cmd==LC_SEGMENT_64){
				const auto segment=At<segment_command_64>(cursor);
				if(address>=segment.vmaddr&&address-segment.vmaddr+4<=segment.filesize)
					return At<uint32_t>(size_t(segment.fileoff+address-segment.vmaddr));
			}
			cursor+=command.cmdsize;
		}
		CHECK(false);return 0;
	}
};
int64_t Signed(uint32_t value,int bits){return int32_t(value<<(32-bits))>>(32-bits);}
uint64_t Call(const NativeImage &image,uint64_t address){
	const auto instruction=image.Instruction(address);CHECK((instruction&0xfc000000)==0x94000000);
	return uint64_t(int64_t(address)+4*Signed(instruction&0x3ffffff,26));
}
bool NativeContract()
{
	const char *path=std::getenv("MACVC_NATIVE");
	std::ifstream input(path?path:"/Applications/Grand Theft Auto Vice City.app/Contents/MacOS/macVC",std::ios::binary|std::ios::ate);
	CHECK(bool(input));if(!input)return false;
	const auto size=input.tellg();CHECK(size>0&&size<128*1024*1024);if(failures)return false;
	NativeImage image;image.data.resize(size_t(size));input.seekg(0);input.read(reinterpret_cast<char *>(image.data.data()),size);CHECK(bool(input));
	// RequestModel uses streamId*32, state+0x10 and TXD modelInfo+0x2e.
	CHECK(image.Instruction(0x100122988)==0x8b081535);
	CHECK(image.Instruction(0x10012298c)==0x394042aa);
	CHECK(image.Instruction(0x1001229c0)==0x79c05e88);
	CHECK(Call(image,0x1001229cc)==0x10012296c);
	CHECK(image.Instruction(0x100122a40)==0x52800048); // state REQUESTED2
	CHECK(image.Instruction(0x100122a44)==0x390042a8);
	CHECK(image.Instruction(0x100122a48)==0x390046a1); // exact caller flags
	// Update runs channels, then removes still-requested unflagged entries.
	CHECK(Call(image,0x10011df60)==0x1001263ac); // ProcessLoadingChannel
	CHECK(Call(image,0x10011df88)==0x100125b3c); // RequestModelStream
	CHECK(image.Instruction(0x10011e048)==0x39404509); // flags byte+0x11
	CHECK(image.Instruction(0x10011e04c)==0x72000d3f); // TST flags,#0xf
	CHECK(image.Instruction(0x10011e050)==0x54ffff41); // B.NE skip removal
	CHECK(Call(image,0x10011e05c)==0x100121440); // RemoveModel
	// Unflagged TXD conversion is discarded after its requesting DFF vanished.
	CHECK(image.Instruction(0x100121b50)==0x39404748);
	CHECK(image.Instruction(0x100121b54)==0x7200091f); // TST flags,#7
	CHECK(Call(image,0x100121b64)==0x100121ee0); // IsTxdUsedByRequestedModels
	CHECK(image.Instruction(0x100121b68)==0x36000d80); // unused -> RemoveModel
	CHECK(Call(image,0x100121d1c)==0x100121440);
	// One disk read follows only the physical next-ID chain, not an arbitrary
	// dependency elsewhere in the IMG. TXD and DFF are not adjacent below.
	CHECK(image.Instruction(0x100125e84)==0x79c02793); // next ID at info+0x12
	return failures==0;
}
bool SeparateAssets()
{
	const char *data=std::getenv("GAMEDATA");
	const std::string directory=std::string(data?data:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData")+"/models/gta3.dir";
	std::ifstream input(directory,std::ios::binary);CHECK(bool(input));if(!input)return false;
	uint32_t txd=0,txdSize=0,dff=0,dffSize=0;
	for(size_t count=0;count<100000;++count){
		std::array<uint8_t,32> record{};if(!input.read(reinterpret_cast<char *>(record.data()),record.size()))break;
		uint32_t offset=0,size=0;std::memcpy(&offset,record.data(),4);std::memcpy(&size,record.data()+4,4);
		const std::string name(reinterpret_cast<const char *>(record.data()+8),strnlen(reinterpret_cast<const char *>(record.data()+8),24));
		if(name=="deaddodo.txd"){txd=offset;txdSize=size;}
		if(name=="deaddodo.dff"){dff=offset;dffSize=size;}
	}
	CHECK(txdSize&&dffSize&&txd+txdSize!=dff&&dff+dffSize!=txd);
	std::printf("native IMG: TXD sectors%u..%u, DFF%u..%u (separate disk reads)\n",txd,txd+txdSize,dff,dff+dffSize);
	return failures==0;
}

constexpr int kModel=181,kTxd=6500+5;
struct Stream {uint8_t state=0,flags=0;} gModelStream,gTxdStream;
alignas(16) uint8_t gRosterVehicle[8][0x6f0],gRosterPool[0x20],gRosterModel[0x100],gRosterCol[0x80],gRosterSphere[20];
uint8_t gRosterFlags[8],*gRosterPoolPointer=gRosterPool,*gRosterModels[6500];
uint8_t gRosterStreaming[8000*0x20];
int gRosterArea,gConstructs,gRequestCalls,gPruned,gTxdDrops,gEmittedFlags=-1,gReleaseCalls,gTxdActorRefs;
bool gRosterCutscene,gRosterMission,gAllocated,gPinProposal,gFailAllocation;
int gInFlight=-1;

void Publish(){
	gRosterStreaming[kModel*0x20+0x10]=gModelStream.state;gRosterStreaming[kModel*0x20+0x11]=gModelStream.flags;
	gRosterStreaming[kTxd*0x20+0x10]=gTxdStream.state;gRosterStreaming[kTxd*0x20+0x11]=gTxdStream.flags;
}
void NativeRequest(int model,int flags)
{
	CHECK(model==kModel||model==kTxd);
	if(model==kModel){++gRequestCalls;gEmittedFlags=flags;}
	// --native-retain changes only the native call flags, as a source-only
	// proposal. Default consumes exactly the flags emitted by real Frame.
	if(gPinProposal)flags|=1;
	auto &stream=model==kModel?gModelStream:gTxdStream;
	if(!stream.state){
		if(model==kModel){
			const int txd=*reinterpret_cast<const int16_t *>(gRosterModels[model]+0x2e);
			NativeRequest(6500+txd,flags); // actual native dependency recursion
		}
		stream.state=2;stream.flags=uint8_t(flags);
	}else stream.flags|=uint8_t(stream.state==2?flags:flags&~8);
	Publish();
}
void NativeDeletable(int model)
{
	CHECK(model==kModel||model==kTxd);++gReleaseCalls;
	auto &stream=model==kModel?gModelStream:gTxdStream;
	stream.flags&=~uint8_t(1);
	// The native vehicle branch keeps its pending/loaded entry. The TXD
	// branch may cancel an unused unfinished request; actor refs stay native.
	if(model==kTxd&&stream.state!=1&&!(stream.flags&2))stream.state=0;
	Publish();
}
void NativeUpdate()
{
	// The other channel stays busy. This channel reads the actual separated
	// TXD before DFF, then reaches native Update's end-of-frame queued purge.
	if(gInFlight==kTxd){
		if(!(gTxdStream.flags&7)&&gModelStream.state!=2){gTxdStream.state=0;++gTxdDrops;}
		else gTxdStream.state=1;
		gInFlight=-1;
	}else if(gInFlight==kModel){gModelStream.state=1;gInFlight=-1;}
	if(gInFlight<0){
		if(gTxdStream.state==2){gTxdStream.state=3;gInFlight=kTxd;}
		else if(gModelStream.state==2&&gTxdStream.state==1){gModelStream.state=3;gInFlight=kModel;}
	}
	if(gModelStream.state==2&&!(gModelStream.flags&15)){gModelStream.state=0;++gPruned;}
	if(gTxdStream.state==2&&!(gTxdStream.flags&15))gTxdStream.state=0;
	Publish();
}
uint8_t *RosterLookup(int ref){return gAllocated&&ref==17?gRosterVehicle[0]:nullptr;}
bool NotMission(){return gRosterMission;}
bool NoExit(uint8_t *){return false;}
bool RosterGround(const CVector &,float,uint8_t *point,uint8_t *&entity,bool,bool,bool,bool,bool,bool,void *){
	entity=nullptr;V(point,0)={0,0,0};V(point,0x10)={0,0,1};return true;
}
uint8_t *RosterClear(CVector,float,uint8_t *,bool,bool,bool,bool,bool,bool){return nullptr;}
void *RosterAllocate(size_t size){CHECK(size==0x388);if(gFailAllocation)return nullptr;gAllocated=true;gRosterFlags[0]=17;return gRosterVehicle[0];}
void RosterConstruct(uint8_t *v,int model,uint8_t owner){
	CHECK(model==kModel&&owner==1);++gConstructs;
	*reinterpret_cast<void **>(v)=gRosterModel;
	*reinterpret_cast<void **>(v+0x58)=gRosterModel;
	*reinterpret_cast<int16_t *>(v+0x6a)=kModel;*reinterpret_cast<int32_t *>(v+0x348)=4;
	*reinterpret_cast<uint32_t *>(v+0x60)=2|(9<<3)|0x400000;
	v[0x294]=1;*reinterpret_cast<float *>(v+0x2a0)=1000;
	// Native CEntity::CreateRwObject -> CBaseModelInfo::AddRef: the actor
	// receives both references before the request's temporary pins are freed.
	++*reinterpret_cast<uint16_t *>(gRosterModel+0x2c);++gTxdActorRefs;
}
void RosterUpdate(void *){}
void RosterEntity(uint8_t *){}
void PrepareRoster()
{
	Reset();gConfig.planes=true;gModelStream={};gTxdStream={};gInFlight=-1;
	gAllocated=gRosterCutscene=gRosterMission=gFailAllocation=false;gRosterArea=0;
	gConstructs=gRequestCalls=gPruned=gTxdDrops=gReleaseCalls=gTxdActorRefs=0;gEmittedFlags=-1;
	std::memset(gRosterVehicle,0,sizeof gRosterVehicle);std::memset(gRosterStreaming,0,sizeof gRosterStreaming);
	std::fill(std::begin(gRosterFlags),std::end(gRosterFlags),0x80);
	*reinterpret_cast<uint8_t **>(gRosterPool)=gRosterVehicle[0];*reinterpret_cast<uint8_t **>(gRosterPool+8)=gRosterFlags;
	*reinterpret_cast<int *>(gRosterPool+0x10)=8;
	*reinterpret_cast<uint8_t **>(gRosterModel+0x20)=gRosterCol;V(gRosterCol,0x10)={-3,-7,-1.285f};
	gRosterModel[0x1d]=6;
	std::memset(gRosterModel+8,0,21);std::memcpy(gRosterModel+8,"deaddodo",8);*reinterpret_cast<uint16_t *>(gRosterModel+0x2c)=0;
	*reinterpret_cast<int16_t *>(gRosterCol+0x28)=1;*reinterpret_cast<uint8_t **>(gRosterCol+0x38)=gRosterSphere;
	V(gRosterSphere,0)={};*reinterpret_cast<float *>(gRosterSphere+12)=.5f;gRosterModels[kModel]=gRosterModel;
	*reinterpret_cast<int16_t *>(gRosterModel+0x2e)=5;
	gPlayer[0x4c0]=0;*reinterpret_cast<float *>(gPlayer+0x44c)=100;V(gPlayer,0x38)={-1545,-1130,1.04f};
	game::api.PoolsGetVehicle=RosterLookup;game::api.RequestModel=NativeRequest;game::api.modelInfoPtrs=gRosterModels;
	game::api.ProcessVerticalLine=RosterGround;game::api.TestSphereAgainstWorld=RosterClear;
	planeflight::gVehiclePool=&gRosterPoolPointer;planeflight::gStreaming=gRosterStreaming;
	planeflight::gArea=&gRosterArea;planeflight::gCutscene=&gRosterCutscene;
	planeflight::roster.mission=NotMission;planeflight::roster.exit=NoExit;
	planeflight::roster.allocate=RosterAllocate;planeflight::roster.construct=RosterConstruct;
	planeflight::roster.deletable=NativeDeletable;
	planeflight::roster.updateMatrix=RosterUpdate;planeflight::roster.updateFrame=RosterUpdate;
	planeflight::roster.add=RosterEntity;
	planeflight::gPlane={};planeflight::gPrevious=planeflight::gAttempt=0;planeflight::gRosterReady=true;
}
void CancelChecks()
{
	for(int gate=0;gate<9;++gate){
		gPinProposal=false;PrepareRoster();planeflight::Frame(1000);
		CHECK((gModelStream.flags&1)&&(gTxdStream.flags&1));
		switch(gate){
		case 0:gConfig.planes=false;break;
		case 1:gRosterArea=1;break;
		case 2:gRosterCutscene=true;break;
		case 3:gPlayer[0x4c0]=1;break;
		case 4:V(gPlayer,0x38).x=-2000;break;
		case 5:game::api.FindPlayerPed=nullptr;break;
		case 6:gRosterMission=true;break;
		case 7:*reinterpret_cast<float *>(gPlayer+0x44c)=0;break;
		case 8:V(gPlayer,0x38)={-1585,-1130,1.04f};break;
		}
		planeflight::Frame(1020); // cancellation must precede the2s throttle
		CHECK(!(gModelStream.flags&1)&&!(gTxdStream.flags&1)&&gReleaseCalls==2);
	}
}
void LeaseChecks()
{
	// A cancel followed by another eligible attempt acquires a fresh lease.
	PrepareRoster();planeflight::Frame(1000);gConfig.planes=false;planeflight::Frame(1020);
	gConfig.planes=true;planeflight::Frame(3000);CHECK(gRequestCalls==2&&(gModelStream.flags&1)&&(gTxdStream.flags&1));
	gConfig.planes=false;planeflight::Frame(3020);CHECK(gReleaseCalls==4&&!(gModelStream.flags&1)&&!(gTxdStream.flags&1));
	// Every mix of preexisting keep pins survives. Distinct other flag bits
	// also survive native unloaded RequestModel's overwrite branch.
	for(int own=0;own<4;++own){
		PrepareRoster();const int beforeModel=16|(own&1),beforeTxd=2|((own>>1)&1);
		gModelStream.flags=uint8_t(beforeModel);gTxdStream.flags=uint8_t(beforeTxd);Publish();
		planeflight::Frame(1000);CHECK((gModelStream.flags&beforeModel)==beforeModel&&(gTxdStream.flags&beforeTxd)==beforeTxd);
		gConfig.planes=false;planeflight::Frame(1020);
		CHECK((gModelStream.flags&1)==(beforeModel&1)&&(gTxdStream.flags&1)==(beforeTxd&1));
		CHECK((gModelStream.flags&beforeModel)==beforeModel&&(gTxdStream.flags&beforeTxd)==beforeTxd);
		CHECK(gReleaseCalls==2-(own&1)-((own>>1)&1));
	}
	// A native already-requested DFF does not recurse to its TXD. Our two
	// captures still release only newly acquired pins on each record.
	PrepareRoster();gModelStream={2,8};gTxdStream={0,16};Publish();planeflight::Frame(1000);
	gConfig.planes=false;planeflight::Frame(1020);CHECK(gModelStream.flags==8&&gTxdStream.flags==16&&gReleaseCalls==2);
	// Native ReInit preserves KEEP; timer rewind must release the same global
	// metadata even though the actor world/attempt epoch is being reset.
	PrepareRoster();planeflight::Frame(10000);gConfig.planes=false;planeflight::Frame(100);
	CHECK(gReleaseCalls==2&&!(gModelStream.flags&1)&&!(gTxdStream.flags&1));
	PrepareRoster();planeflight::Frame(10000);planeflight::Frame(100);
	CHECK(gReleaseCalls==2&&gRequestCalls==2&&(gModelStream.flags&1)&&(gTxdStream.flags&1));
	gConfig.planes=false;planeflight::Frame(120);CHECK(gReleaseCalls==4);
	// Replaced metadata is a foreign identity: no old lease may clear its
	// current flags. These negatives intentionally do not simulate ReInit,
	// which retains the same model metadata and is covered above.
	for(int change=0;change<3;++change){
		PrepareRoster();planeflight::Frame(1000);
		uint8_t replacement[0x100]{};std::memcpy(replacement,gRosterModel,sizeof replacement);
		if(change==0)gRosterModels[kModel]=replacement;
		if(change==1)*reinterpret_cast<int16_t *>(gRosterModel+0x2e)=6;
		if(change==2)gRosterModel[8]='x';
		gModelStream.flags=gTxdStream.flags=3;Publish();gConfig.planes=false;planeflight::Frame(1020);
		CHECK(gReleaseCalls==0&&gModelStream.flags==3&&gTxdStream.flags==3);
	}
	// The model is ready but native allocation fails: no unbounded residency
	// pin remains after this unsuccessful materialization attempt.
	PrepareRoster();planeflight::Frame(1000);NativeUpdate();NativeUpdate();NativeUpdate();
	gFailAllocation=true;planeflight::Frame(3000);CHECK(gConstructs==0&&gReleaseCalls==2&&!(gModelStream.flags&1)&&!(gTxdStream.flags&1));
}
}
int main(int argc,char **argv)
{
	if(!NativeContract()||!SeparateAssets())return 1;
	gPinProposal=argc==2&&!std::strcmp(argv[1],"--native-retain");PrepareRoster();
	for(uint32_t now:{1000u,3000u,5000u}){
		planeflight::Frame(now); // exact production request emission or spawn
		if(gConstructs)break;
		CHECK(gRequestCalls>0&&gModelStream.state==2); // no eligibility inference
		for(uint32_t delta:{20u,40u,60u}){
			NativeUpdate();planeflight::Frame(now+delta); // cooldown retains native ordering
		}
	}
	std::printf("plane stream: emittedFlags%d proposal%d requests%d pruned%d txdDrops%d state%d spawned%d\n",
	    gEmittedFlags,gPinProposal,gRequestCalls,gPruned,gTxdDrops,gModelStream.state,gConstructs);
	CHECK(gConstructs==1); // user-visible roster materialization, not just flag arithmetic
	CHECK(gReleaseCalls==2&&!(gModelStream.flags&1)&&!(gTxdStream.flags&1));
	CHECK(*reinterpret_cast<uint16_t *>(gRosterModel+0x2c)==1&&gTxdActorRefs==1);
	CancelChecks();
	LeaseChecks();
	if(!failures)std::puts("plane streaming residency: all passed");
	return failures?1:0;
}

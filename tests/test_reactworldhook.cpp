// Exercise the actual late-world hook, using the existing native ABI fixture.
#define main existing_reaction_tests
#include "test_reactionhook.cpp"
#undef main

namespace {
int failures,worldCalls,publications;
bool recycled,originalBeforeContact;
int SceneRef(uint8_t *p) {return p==gPed?17:p==gOther?18:p==gPlayer?31:-1;}
uint8_t *SceneLookup(int ref) {return ref==17?gPed:ref==18&&!recycled?gOther:ref==31?gPlayer:nullptr;}
void Publish(uint8_t *) {++publications;}
void RecycleDuringQuery() {recycled=true;}
void HitDuringQuery() {reactions::gPeople[0].physical.Impulse(reactphysics::Head,{.1f,0,0});}
void NativeWorld()
{
	++worldCalls;
	if(reactions::gPeople.size()==2)
		originalBeforeContact=std::fabs(reactions::gPeople[0].physical.Positions()[reactphysics::Pelvis].y-
			reactions::gPeople[1].physical.Positions()[reactphysics::Pelvis].y)<.3f;
}
void Check(bool good,const char *message) {if(!good){std::printf("FAIL: %s\n",message);++failures;}}
void Setup()
{
	gNow+=1000;reactions::gLastFrame=gNow;
	gWorldQueryEvent=nullptr;
	reactions::gPeople.clear();recycled=false;worldCalls=publications=0;originalBeforeContact=false;
	gConfig.npcReactions=true;reactions::gInstalled=true;reactions::gCutscene=nullptr;reactions::gReplay=nullptr;
	game::api.FindPlayerPed=Player;game::api.timeMs=&gNow;game::api.PoolsGetPed=SceneLookup;reactions::gRef=SceneRef;
	reactions::gUpdateMatrix=reactions::gUpdateFrame=Noop;reactions::gReindex=Publish;
	reactions::orig_WorldProcess=NativeWorld;
	Check(reactcontact::Install(),"late-world fixture binds native world queries");
	for(int i=0;i<2;i++) {
		uint8_t *ped=i?gOther:gPed;std::memset(ped,0,sizeof gPed);
		game::EntityFlags(ped)=3|game::kEntityUsesCollision;
		game::SetPedState(ped,game::kPedDead);
		*reinterpret_cast<uint8_t **>(ped+0x58)=gContactClump;
		const float y=i?.1f:-.1f;
		*reinterpret_cast<CVector *>(ped+0x38)={0,y,1.04f};
		reactions::Person person;person.handle=i?18:17;person.clump=gContactClump;
		auto pose=Standing();for(auto &p:pose) p.y+=y;
		person.physical.Reset(pose);person.physical.Impulse(reactphysics::Chest,{},true);
		// CWorld's individual actor pass precedes its late crowd phase. It
		// also establishes complete capsule clearance, including native skin.
		person.physical.Advance(1.f/60,pose,reactcontact::Sweep,ped);
		Check(person.physical.Active(),"individual actor pass sees a genuine static floor rather than its own ped as support");
		person.pelvisOffset={0,0,-.09f};person.stepped=gNow;
		reactions::gPeople.push_back(person);
	}
}
}

int main()
{
	Setup();reactions::Hook_WorldProcess();
	Check(worldCalls==1&&originalBeforeContact,"native world completes before crowd contact");
	Check(publications==2,"corrected native roots publish once per participant");
	Check(std::fabs(game::EntityPos(gPed).y-game::EntityPos(gOther).y)>.318f,"late world separates real native actor roots");
	for(const auto &person:reactions::gPeople) {
		const uint8_t *ped=SceneLookup(person.handle);
		const auto p=person.physical.Positions()[reactphysics::Pelvis];
		Check(std::fabs(game::EntityPos(ped).y-p.y)<1e-5f,"published root matches solved physical pelvis");
		Check(game::Health(ped)==0&&game::PedState(ped)==game::kPedDead,"crowd separation cannot revive or damage native actors");
	}
	const CVector first=game::EntityPos(gPed),second=game::EntityPos(gOther);
	reactions::Hook_WorldProcess();
	Check(worldCalls==2&&publications==2,"same-time native retry cannot apply crowd repair twice");
	Check(game::EntityPos(gPed).y==first.y&&game::EntityPos(gOther).y==second.y,"retry retains corrected roots");
	Setup();gPed[0x19a]=2;reactions::Hook_WorldProcess();
	Check(publications==0&&game::EntityPos(gPed).y==-.1f,"mission actor keeps native root ownership");
	Setup();recycled=true;reactions::Hook_WorldProcess();
	Check(publications==0&&game::EntityPos(gOther).y==.1f,"stale generation cannot receive or contribute crowd correction");
	Setup();gConfig.npcReactions=false;reactions::Hook_WorldProcess();
	Check(worldCalls==1&&publications==0,"disabled layer retains original native world processing");
	Setup();game::SetPedState(gPed,game::kPedFall);game::EntityFlags(gPed)&=~game::kEntityUsesCollision;reactions::Hook_WorldProcess();
	Check(publications==0,"living actor with native collision disabled retains its transition owner");
	// Native CPed::SetDead clears UsesCollision on every corpse; an articulated
	// corpse still separates from the other bodies around it.
	Setup();game::EntityFlags(gPed)&=~game::kEntityUsesCollision;reactions::Hook_WorldProcess();
	Check(publications==2,"articulated corpse whose collision native SetDead cleared still separates from other bodies");
	Setup();gWorldQueryEvent=RecycleDuringQuery;reactions::Hook_WorldProcess();
	Check(publications==0&&game::EntityPos(gPed).y==-.1f&&game::EntityPos(gOther).y==.1f,
		"query-time generation change discards the entire speculative crowd batch");
	Setup();const float velocity=reactions::gPeople[0].physical.Velocity(reactphysics::Head).x;
	gWorldQueryEvent=HitDuringQuery;reactions::Hook_WorldProcess();
	Check(publications==0&&reactions::gPeople[0].physical.Velocity(reactphysics::Head).x>velocity+.09f,
		"query-time impact survives instead of being overwritten by a solved snapshot");
	// Native control can finish an exit before the player's Frame cleanup
	// observes it. Never let a pre-exit ragdoll reclaim that new on-foot root.
	for (int transition : {0x3a, 0x3c}) {
		Setup();
		auto &prior=reactions::gPeople[0];
		prior.heldFall=true;prior.savedGetUpTimer=1234;
		*reinterpret_cast<uint32_t *>(gPed+0x68c)=UINT32_MAX;
		game::SetPedState(gPed,transition);gPed[0x4c0]=1;
		game::EntityFlags(gPed)&=~game::kEntityUsesCollision;
		reactions::orig_Control=[](uint8_t *ped) {
			Check(*reinterpret_cast<uint32_t *>(ped+0x68c)==1234,"fall timer released before native vehicle control");
			game::SetPedState(ped,game::kPedIdle);ped[0x4c0]=0;
			game::EntityFlags(ped)|=game::kEntityUsesCollision;
			*reinterpret_cast<CVector *>(ped+0x38)={3,0,1};
		};
		reactions::orig_WorldCollision=Native;gNativeCalls=0;
		reactions::Hook_Control(gPed);reactions::Hook_WorldCollision(gPed);
		Check(!reactions::Find(gPed),"vehicle handoff discards the stale physical controller");
		Check(gNativeCalls==1&&game::EntityPos(gPed).x==3,"completed exit retains native root and collision");
	}
	std::printf("reactworldhook: %d failures\n",failures);
	return failures?1:0;
}

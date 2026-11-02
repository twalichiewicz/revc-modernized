#include "playerreact.h"
#include "playerreactrig.h"
#include <cmath>
#include <cstdio>
#include <limits>

static int failures;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace playerreact;
static vm::Vec3 At(const Matrix &m) { return {m[12],m[13],m[14]}; }
static Matrices Standing()
{
	const vm::Vec3 p[kBones] = {{0,0,1},{0,0,1},{0,0,1.13f},{0,0,1.35f},{0,0,1.5f},{0,0,1.7f},
	 {-.12f,0,1.4f},{-.22f,0,1.4f},{-.36f,.03f,1.12f},{-.40f,.10f,.9f},{-.43f,.13f,.87f},
	 {.12f,0,1.4f},{.22f,0,1.4f},{.36f,.03f,1.12f},{.40f,.10f,.9f},{.43f,.13f,.87f},
	 {-.12f,0,.9f},{-.12f,0,.5f},{-.12f,0,.1f},{.12f,0,.9f},{.12f,0,.5f},{.12f,0,.1f}};
	Matrices out{};
	for(int i=0;i<kBones;++i) { auto f=vm::Identity();f.pos=p[i];vm::ToRw(f,out[size_t(i)].data()); }
	return out;
}
static vm::Vec3 TorsoForward(const Matrices &rig,vm::Vec3 heading)
{
	const auto up=vm::Normal(vm::Sub(At(rig[4]),At(rig[3])));
	auto front=vm::Normal(vm::Cross(up,vm::Sub(At(rig[12]),At(rig[7]))));
	return vm::Dot(front,heading)<0?vm::Mul(front,-1):front;
}
static void ProtectiveAnatomy()
{
	const auto captured=playerreacttest::NativeRig();
	float worstElbow=std::numeric_limits<float>::infinity(),worstWrist=worstElbow,worstReach=0;
	int scenario=0,badCases=0;
	for(float yaw:{0.0f,.9f,2.8f})for(float pitch:{-.32f,0.0f,.32f})for(float roll:{-.20f,.20f})
	for(float contactAngle:{0.0f,1.5707963f,-1.5707963f,3.1415926f})for(float turnAfter:{0.0f,1.9f})for(float retreatAfter:{0.0f,.35f}) {
		auto placement=vm::Compose(vm::TurnAbout({0,0,1},yaw,{}),vm::Compose(vm::TurnAbout({1,0,0},pitch,{}),vm::TurnAbout({0,1,0},roll,{})));
		auto currentPlacement=vm::Compose(vm::TurnAbout(vm::Rotate(placement,{0,0,1}),turnAfter,{}),placement);
		placement.pos={17,-25,8};
		currentPlacement.pos=vm::Sub(placement.pos,vm::Rotate(placement,{0,retreatAfter,0}));
		Matrices current=captured;
		for(auto &m:current)vm::ToRw(vm::Compose(currentPlacement,vm::FromRw(m.data())),m.data());
		const auto toward=vm::Rotate(placement,{std::sin(contactAngle),std::cos(contactAngle),0});
		const auto point=vm::Add(vm::Apply(placement,At(captured[3])),vm::Add(vm::Mul(toward,.40f),vm::Rotate(placement,{0,0,.12f})));
		const auto heading=vm::Rotate(currentPlacement,{0,1,0});
		for(float weight:{.4f,.8f,.95f}) {
			const Response response{weight,{point.x,point.y,point.z},{-toward.x,-toward.y,-toward.z}};
			const auto posed=Pose(current,response,{heading.x,heading.y,heading.z},false);
			const auto front=TorsoForward(posed,heading);
			bool bad=false;
			for(int upper:{7,12}) {
				const auto shoulder=At(posed[size_t(upper)]),elbow=At(posed[size_t(upper+1)]),wrist=At(posed[size_t(upper+2)]);
				const float a=vm::Length(vm::Sub(elbow,shoulder)),b=vm::Length(vm::Sub(wrist,elbow));
				const float elbowForward=vm::Dot(vm::Sub(elbow,shoulder),front),wristForward=vm::Dot(vm::Sub(wrist,shoulder),front);
				const float extension=vm::Length(vm::Sub(wrist,shoulder))/(a+b);
				worstElbow=std::min(worstElbow,elbowForward);worstWrist=std::min(worstWrist,wristForward);worstReach=std::max(worstReach,extension);
				bad|=elbowForward<-.002f||wristForward<.01f||extension>.97f;
				CHECK(std::fabs(a-vm::Length(vm::Sub(At(current[size_t(upper+1)]),At(current[size_t(upper)]))))<.0002f);
				CHECK(std::fabs(b-vm::Length(vm::Sub(At(current[size_t(upper+2)]),At(current[size_t(upper+1)]))))<.0002f);
			}
			for(int bone:{0,1,16,17,18,19,20,21})CHECK(posed[size_t(bone)]==current[size_t(bone)]);
			const auto aimed=Pose(current,response,{heading.x,heading.y,heading.z},true);
			for(int bone:{7,8,9,10,12,13,14,15})CHECK(aimed[size_t(bone)]==current[size_t(bone)]);
			badCases+=bad;++scenario;
		}
	}
	if(badCases){std::printf("FAIL: %d/%d native-rig guard cases extend behind shoulder/lock elbow (elbow %.4fm wrist %.4fm reach %.4f)\n",badCases,scenario,worstElbow,worstWrist,worstReach);++failures;}
	else std::printf("native-rig guard anatomy: %d cases, min elbow %.4fm / wrist %.4fm ahead, max reach %.4f\n",scenario,worstElbow,worstWrist,worstReach);
}
int main()
{
	ProtectiveAnatomy();
	Guard guard;
	const auto native=Standing();
	CHECK(guard.Contact({0,.38f,1.4f},{0,-1,0},{0,3,0}));
	auto response=guard.Current();
	CHECK(response.weight>.3f && response.weight<=1);
	auto posed=Pose(native,response,{0,1,0},false);
	CHECK(vm::Length(vm::Sub(At(posed[5]),At(native[5])))>.025f);
	CHECK(At(posed[9]).y>At(native[9]).y+.10f);
	CHECK(At(posed[14]).y>At(native[14]).y+.10f);
	for(int b:{0,1,16,17,18,19,20,21}) CHECK(posed[size_t(b)]==native[size_t(b)]);
	const int parent[kBones]={-1,0,1,2,3,4,3,6,7,8,9,3,11,12,13,14,1,16,17,1,19,20};
	for(int b=1;b<kBones;++b) {
		CHECK(std::fabs(vm::Length(vm::Sub(At(posed[size_t(b)]),At(posed[size_t(parent[b])]))) -
		                vm::Length(vm::Sub(At(native[size_t(b)]),At(native[size_t(parent[b])]))))<.0002f);
		const auto f=vm::FromRw(posed[size_t(b)].data());
		CHECK(std::fabs(vm::Length(f.x)-1)<.0001f && std::fabs(vm::Dot(f.x,f.y))<.0001f);
	}
	const auto aimed=Pose(native,response,{0,1,0},true);
	for(int b:{7,8,9,10,12,13,14,15}) CHECK(aimed[size_t(b)]==native[size_t(b)]);
	CHECK(aimed[5]!=native[5]);
	const auto moved=ViewTransform(response,{1,0,0},{0,1,0},false);
	CHECK(vm::Length(moved.pos)>.015f);
	CHECK(vm::Length(vm::Sub(GuardHand(response,{1,0,0},{0,1,0},true).pos,{-0.15f,.35f,-.2f}))>.015f);
	CHECK(ViewTransform(response,{1,0,0},{0,1,0},true).x.x==1); // retain aimed weapon direction
	CHECK(Pose(native,response,{0,0,0},false)==native);
	CHECK(Pose(native,response,{0,std::numeric_limits<float>::quiet_NaN(),0},false)==native);
	const auto once=posed;
	CHECK(Pose(native,response,{0,1,0},false)==once); // repeated render starts from native input
	for(int i=0;i<120;++i) guard.Advance(1.0f/60);
	CHECK(guard.Current().weight==0);
	CHECK(Pose(native,guard.Current(),{0,1,0},false)==native);
	CHECK(!guard.Contact({0,0,.1f},{0,0,1},{0,0,-3}));
	CHECK(!guard.Contact({0,.38f,1.4f},{0,-1,0},{0,-3,0}));
	CHECK(!guard.Contact({0,.38f,1.4f},{0,0,0},{0,3,0}));
	CHECK(!guard.Contact({0,.38f,1.4f},{0,-1,0},{0,std::numeric_limits<float>::quiet_NaN(),0}));
	CHECK(guard.Current().weight==0);
	for(int i=0;i<1000;++i) { guard.Contact({.2f,.4f,1.5f},{0,-1,0},{0,8,0});guard.Advance(.001f); }
	CHECK(guard.Current().weight<=1);
	guard.Advance(std::numeric_limits<float>::infinity());
	CHECK(std::isfinite(guard.Current().weight));
	guard.Reset();
	CHECK(guard.Current().weight==0);
	if(failures) return 1;
	std::puts("all player contact pose tests passed");
}

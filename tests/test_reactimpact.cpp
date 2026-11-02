#include "reactimpact.h"
#include <cmath>
#include <cstdio>
#include <limits>
using namespace reactphysics;
int main()
{
	Pose pose{{{0,0,1},{0,0,1.3f},{0,0,1.7f},
		{-.2f,0,1.5f},{-.45f,0,1.25f},{-.6f,0,1},
		{.2f,0,1.5f},{.45f,0,1.25f},{.6f,0,1},
		{-.12f,0,1},{-.12f,0,.5f},{-.12f,.1f,.08f},
		{.12f,0,1},{.12f,0,.5f},{.12f,.1f,.08f}}};
	int fail=0;
	auto check=[&](bool ok,const char *message){if(!ok){printf("FAIL: %s\n",message);++fail;}};
	for(int piece:{0,1,4}) {
		const auto foot=reactimpact::Resolve(pose,pose[FootL],piece);
		check(foot.valid && foot.b==FootL && foot.along>.99f,"actual left foot point must select distal foot despite coarse torso piece");
	}
	const auto hand=reactimpact::Resolve(pose,pose[HandR],3);
	check(hand.valid && hand.b==HandR && hand.along>.99f,"actual hand point must select distal hand");
	const Vec3 shin{-.12f,.05f,.29f};const auto middle=reactimpact::Resolve(pose,shin,4);
	check(middle.valid && middle.a==KneeL && middle.b==FootL && std::fabs(middle.along-.5f)<.001f,"mid-shin must resolve both local segment endpoints");
	Pose shifted=pose;for(auto &p:shifted){p={p.x+17,p.y-25,p.z+4};}
	const auto moved=reactimpact::Resolve(shifted,shifted[FootL],0);
	check(moved.valid && moved.b==FootL && moved.along>.99f,"world translation must not alter local impact selection");
	check(!reactimpact::Resolve(pose,{100,100,100},4).valid,"remote/stale point must not select a limb");
	check(!reactimpact::Resolve(pose,{std::numeric_limits<float>::quiet_NaN(),0,0},4).valid,"nonfinite point must be rejected");
	if(fail)return 1;puts("reactimpact: all passed");
}

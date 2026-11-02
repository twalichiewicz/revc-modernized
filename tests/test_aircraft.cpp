#include "aircraft.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
using namespace aircraft;
static int failures;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static bool Near(float a,float b) { return std::fabs(a-b)<0.01f; }
// Solid floor z=0 and wall x=10. Optional ceiling z=12.
static Contact Room(Vec3 from,Vec3 to,float r,void *context)
{
	Contact c;
	auto take=[&](float a,float b,Vec3 n) {
		if (b>=0) return;
		const float t=a<=0?0:std::clamp(a/(a-b),0.0f,1.0f);
		if (t<c.fraction) c={true,t,n};
	};
	take(from.z-r,to.z-r,{0,0,1});
	take(10-from.x-r,10-to.x-r,{-1,0,0});
	if (context) take(12-from.z-r,12-to.z-r,{0,0,-1});
	return c;
}
int main()
{
	Pose p; p.position={0,0,6};
	const Motion hit=Move(p,{20,4,0},{20,4,0},Room,nullptr);
	CHECK(hit.contact && hit.position.x<4 && hit.position.y>3.8f);
	CHECK(hit.velocity.x<=0 && hit.velocity.y>3.8f);
	const Motion floor=Move(p,{0,0,-20},{2,0,-20},Room,nullptr);
	CHECK(floor.contact && floor.position.z>=1.34f); // cabin centre is 0.3 m above the root
	// An opposing approach cannot cross, even if the end points are clear again.
	const Motion pair=Separate({-20,0,20},{20,0,20},{40,0,0},{0,0,20},{-20,0,0},1);
	CHECK(pair.contact && pair.position.x<0 && pair.velocity.x<=0);
	const Motion overlap=Separate({0,0,20},{0,0,20},{1,0,0},{0,0,20},{},0.02f);
	CHECK(overlap.contact && std::fabs(overlap.position.x)>10);
	p.position={3,0,8};
	const Vec3 escape=Escape(p,{10,0,0},1,Room,reinterpret_cast<void *>(1));
	CHECK(std::fabs(escape.y)>0.1f || escape.x<0);
	CHECK(Spotlight(1,1)==0 && Spotlight(0,1)>0.99f);
	CHECK(Spotlight(0.5f,1)>0 && Spotlight(0.5f,1)<1);
	CHECK(Spotlight(0,0)==0);
	Crash crash;
	crash.Disable(Failure::Pilot,{8,0,0});
	crash.Disable(Failure::Hull,{}); // repeated hit cannot restart or erase momentum
	CHECK(crash.cause==Failure::Pilot && Near(crash.velocity.x,8));
	p.position={-50,0,30};
	Motion step=crash.Step(p,0.1f,Room,nullptr);
	CHECK(step.position.z<p.position.z && step.position.x>p.position.x);
	CHECK(crash.phase==Phase::Descending);
	for (int i=0;i<500 && crash.phase==Phase::Descending;i++) {
		p.position=step.position; step=crash.Step(p,0.02f,Room,nullptr);
	}
	CHECK(crash.phase==Phase::Impacted && crash.fallen>20);
	CHECK(step.position.z<1.0f); // actual cabin contact, not the navigation clearance margin
	if (!failures) std::puts("aircraft: all passed");
	return failures?1:0;
}

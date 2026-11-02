#include "populationrules.h"
#include <cstdio>
using namespace populationrules;
int main()
{
	int failures=0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n",__LINE__,#c); failures++; } } while (0)
	Spawn s;
	CHECK(Eligible(s));
	s.visible=true; CHECK(!Eligible(s)); s.visible=false;
	s.ground=false; CHECK(!Eligible(s)); s.ground=true;
	s.clear=false; CHECK(!Eligible(s)); s.clear=true;
	s.path=false; CHECK(!Eligible(s)); s.path=true;
	s.local=s.limit; CHECK(!Eligible(s)); s.local=0;
	s.nearby=4; CHECK(!Eligible(s)); s.nearby=0;
	s.freeSlots=8; CHECK(!Eligible(s)); s.freeSlots=20;
	s.distance=10; CHECK(!Eligible(s));
	s.distance=160; CHECK(!Eligible(s)); s.distance=70;
	s.cop=true; s.distance=35; CHECK(!Eligible(s)); s.distance=70;
	s.loaded=false; CHECK(!Eligible(s)); s.loaded=true;
	s.outdoors=false; CHECK(!Eligible(s));
	Release r;
	CHECK(CanVirtualize(r));
	r.visible=true; CHECK(!CanVirtualize(r)); r.visible=false;
	r.random=false; CHECK(!CanVirtualize(r)); r.random=true;
	r.attached=true; CHECK(!CanVirtualize(r)); r.attached=false;
	r.inVehicle=true; CHECK(!CanVirtualize(r)); r.inVehicle=false;
	r.deletable=false; CHECK(!CanVirtualize(r));
	CHECK(SameOwner(769,769,false,false));
	CHECK(!SameOwner(769,770,false,false));
	CHECK(!SameOwner(-1,-1,false,false));
	CHECK(!SameOwner(769,769,true,false));
	CHECK(!SameOwner(769,769,false,true));
	const PathStep before{1,2,0.99f},proposed{2,3,0.1f};
	PathStep blocked=Travel(before,proposed,true,false);
	CHECK(blocked.node==1 && blocked.next==2 && blocked.along==0.99f);
	blocked=Travel(before,proposed,false,true);
	CHECK(blocked.node==1 && blocked.next==2 && blocked.along==0.99f);
	const PathStep opened=Travel(before,proposed,true,true);
	CHECK(opened.node==2 && opened.next==3 && opened.along==0.1f);
	if (!failures) std::puts("populationstream: all passed");
	return failures?1:0;
}

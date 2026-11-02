#include "flightpath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
static int failures;
#define CHECK(c) do{if(!(c)){std::printf("FAIL %d: %s\n",__LINE__,#c);++failures;}}while(0)
int main()
{
	const char *env=std::getenv("GAMEDATA");const std::string data=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";
	std::ifstream path(data+"/data/paths/flight.dat");int count=0;path>>count;CHECK(count>=3&&count<10000);if(count<3||count>=10000)return 1;
	std::vector<Vec3> before(static_cast<size_t>(count));
	for(Vec3 &p:before){path>>p.x>>p.y>>p.z;p.z=std::max(p.z,flightpath::kGround);}CHECK(bool(path));
	auto after=before;flightpath::Raise(after);int first=-1,last=-1;float peak=0,maxSlope=0,lastSlope=0,lastBefore=0,lastRun=0;
	for(int i=0;i<count;i++){
		CHECK(after[size_t(i)].x==before[size_t(i)].x&&after[size_t(i)].y==before[size_t(i)].y);
		CHECK(after[size_t(i)].z>=before[size_t(i)].z);
		if(before[size_t(i)].z<=flightpath::kGround)CHECK(after[size_t(i)].z==before[size_t(i)].z);
		else{if(first<0)first=i;last=i;}
		peak=std::max(peak,after[size_t(i)].z);
		if(i){
			const float run=std::hypot(before[size_t(i)].x-before[size_t(i-1)].x,before[size_t(i)].y-before[size_t(i-1)].y);
			if(run<.01f)continue;
			const float slope=(after[size_t(i)].z-after[size_t(i-1)].z)/run,old=(before[size_t(i)].z-before[size_t(i-1)].z)/run;
			maxSlope=std::max(maxSlope,std::fabs(slope));
			if(lastRun>0)CHECK(std::fabs(slope-lastSlope)<=std::fabs(old-lastBefore)+.0015f*(run+lastRun)*.5f);
			lastSlope=slope;lastBefore=old;lastRun=run;
		}
	}
	CHECK(first>=0&&last>first);CHECK(peak>3000&&peak<=flightpath::kPeak+.01f);CHECK(maxSlope<.65f);
	if(first>=0&&last>first){CHECK(after[size_t(first)].z==before[size_t(first)].z);CHECK(after[size_t(last)].z==before[size_t(last)].z);}
	std::printf("actual flight.dat: %d nodes, peak %.1fm, max vertical slope %.3f; runway/XY/departure/arrival retained\n",count,peak,maxSlope);
	if(!failures)std::puts("flightpath assets: all passed");return failures?1:0;
}

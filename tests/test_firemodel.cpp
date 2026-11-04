#if __has_include("firemodel.h")
#include "firemodel.h"
#include <cstdio>
#include <cmath>
using namespace firemodel;
int main(){int fails=0;auto check=[&](bool b,const char *s){if(!b){printf("FAIL: %s\n",s);++fails;}};
 Input i;State s;i.health=200;for(int n=0;n<24;n++)Tick(s,i,.1f);check(!s.lit,"low health requires ignition dwell");Tick(s,i,.1f);check(s.lit&&!s.spent,"ignition after continuous2.5s exposure");
 State healthy;for(int n=0;n<300;n++)Tick(healthy,Input{},.1f);check(!healthy.lit,"healthy stationary or rolled car has no invented ignition source");
 Input hot;hot.running=true;hot.heat=1.2f;State heatOnly;for(int n=0;n<30;n++)Tick(heatOnly,hot,.1f);check(!heatOnly.lit,"heat without leaking fuel does not ignite");hot.breach=.2f;State hotLeak;for(int n=0;n<30;n++)Tick(hotLeak,hot,.1f);check(hotLeak.lit,"hot running engine and fuel breach interact");
 hot.fuel=0;State dry;for(int n=0;n<30;n++)Tick(dry,hot,.1f);check(!dry.lit,"empty reservoir cannot ignite from module heat");
 Input electric;electric.running=true;electric.electrical=.9f;electric.breach=.4f;State shorted;for(int n=0;n<30;n++)Tick(shorted,electric,.1f);check(shorted.lit,"failed electrical supply and leaking fuel interact");
 Extinguish(s);for(int n=0;n<100;n++)Tick(s,i,.1f);check(!s.lit&&s.suppressed,"water prevents immediate relight from unchanged damage");Rearm(s);for(int n=0;n<26;n++)Tick(s,i,.1f);check(s.lit,"new accepted damage rearms ignition");
 State a,b;Ignite(a);Ignite(b);for(int n=0;n<3000;n++)Tick(a,Input{},.02f);for(int n=0;n<200;n++)Tick(b,Input{},.3f);check(a.spent&&b.spent&&!a.lit&&!b.lit,"sixty-second burnout agrees across frame rates");
 State grow;Ignite(grow);check(std::fabs(Intensity(grow)-.3f)<1e-6f,"small initial native fire");for(int n=0;n<15;n++)Tick(grow,Input{},1);check(std::fabs(Intensity(grow)-1)<1e-5f,"fire grows over15s");
 const auto age=grow.age;Tick(grow,Input{},5);Tick(grow,Input{},NAN);Tick(grow,Input{},-1);check(grow.age==age,"discontinuities do not fast-forward burning");
 State intermittent;for(int n=0;n<20;n++)Tick(intermittent,i,.1f);Tick(intermittent,Input{},.1f);for(int n=0;n<10;n++)Tick(intermittent,i,.1f);check(!intermittent.lit,"ignition requires continuous exposure");
 printf("firemodel: %s\n",fails?"FAILED":"all passed");return fails?1:0;
}
#else
#include <cstdio>
int main(){puts("FAIL: progressive fire model is missing");return 1;}
#endif

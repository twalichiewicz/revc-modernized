#include "partsmodel.h"
#include <cmath>
#include <cstdio>
using namespace partsmodel;
int main()
{
 int failed=0;auto check=[&](bool ok,const char *s){if(!ok){std::printf("FAIL: %s\n",s);++failed;}};
 for(int c=0;c<30;c++)check(Slot(c)==(c>=5&&c<=10?c-5:c==16?6:c==17?7:-1),"only six doors and two bumpers owned");
 check(Slot(-1)==-1,"unknown negative component");
 check(Describe(0).index==0&&!Describe(0).panel&&Describe(5).index==5,"native door ids");
 check(Describe(6).index==5&&Describe(6).panel&&Describe(7).index==6,"native bumper panel ids");
 check(Scale(0)==1&&std::fabs(Scale(1)-.85f)<1e-6f&&std::fabs(Scale(2)-.55f)<1e-6f&&Scale(3)==0,"increasing resistance to progression");
 Episode e{};check(Contact(e,0),"first contact at time zero allowed");
 check(Contact(e,10),"failed small contact leaves progression available");Commit(e);
 for(uint32_t t=20;t<=2000;t+=20)check(!Contact(e,t),"sustained scrape cannot consume another state");
 check(!Contact(e,2349),"349 ms quiet is the same contact episode");
 check(Contact(e,2699),"350 ms quiet starts another collision");Commit(e);
 check(Contact(e,100),"backward clock resets the episode");Commit(e);
 check(!Contact(e,100),"same frame cannot progress twice");
 Episode other{};check(Contact(other,100),"different component independent");
 std::printf("partsmodel: %s\n",failed?"FAILED":"all passed");return failed?1:0;
}

#include "ControllerButtons.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
int main(){size_t page=getpagesize();auto mem=(unsigned char*)mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);mprotect(mem+page,page,PROT_NONE);auto source=mem+page-19;for(int i=0;i<19;++i)source[i]=i%2;unsigned char out[33];memset(out,255,33);int n=CopyControllerButtons(out,sizeof out,source,19);bool okay=n==19;for(int i=0;i<33;++i)okay&=out[i]==(i<19?source[i]:0);okay&=CopyControllerButtons(out,sizeof out,nullptr,19)==0;okay&=CopyControllerButtons(out,sizeof out,source,-1)==0;munmap(mem,page*2);printf("controller bounded copy: %s\n",okay?"PASS":"FAIL");return okay?0:1;}

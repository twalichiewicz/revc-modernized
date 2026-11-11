#include "CompressedTexture.h"
#include <cstdio>
int main(){bool good=CompressedTextureLevelSize(610,610,0,16)==374544&&CompressedTextureLevelSize(610,610,0,8)==187272&&CompressedTextureLevelSize(610,610,1,16)==94864&&CompressedTextureLevelSize(1,2,0,16)==16&&CompressedTextureLevelSize(1,1,8,8)==8&&CompressedTextureLevelSize(0,3,0,16)==0;printf("compressed texture block sizing: %s\n",good?"PASS":"FAIL");return good?0:1;}

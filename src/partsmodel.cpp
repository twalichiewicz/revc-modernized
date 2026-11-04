#include "partsmodel.h"
namespace partsmodel {
int Slot(int c){return c>=5&&c<=10?c-5:c==16?6:c==17?7:-1;}
const Part &Describe(int slot)
{
 static const Part parts[]={{"bonnet",false,0},{"boot",false,1},{"door_lf",false,2},
  {"door_rf",false,3},{"door_lr",false,4},{"door_rr",false,5},
  {"bumper_front",true,5},{"bumper_rear",true,6}};
 return parts[slot];
}
float Scale(int state){return state==0?1.0f:state==1?.85f:state==2?.55f:0;}
bool Contact(Episode &e,uint32_t now)
{
 if(!e.seen||now<e.last||now-e.last>=kQuietMs)e.advanced=false;
 e.seen=true;e.last=now;return !e.advanced;
}
void Commit(Episode &e){e.advanced=true;}
}

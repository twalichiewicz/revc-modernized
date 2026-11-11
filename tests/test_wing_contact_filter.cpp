// Actual IPC/CCD integration: connected render overlap is opt-in; separated
// structural fragments still contact. Existing research tests are unchanged.
#define main UnusedResearchMain
#include "../docs/research/world-physics-probe.cpp"
#undef main
#include "../docs/research/world-physics-structure.h"
#include "../docs/research/world-physics-persistent.h"
#include <cstdio>
int main(){int failures=0;auto check=[&](bool value,const char *name){printf("contact-filter %s %s\n",name,value?"PASS":"FAIL");failures+=!value;};
 for(bool culling:{false,true})for(bool connected:{true,false}){
  Settings settings;settings.explicitForces=true;World world(settings);structure::Force force;structure::Asset skin(world,force);
  const structure::Material material{1e5,1e12,0,1,1e12,.2};
  for(int triangle=0;triangle<2;++triangle){double z=triangle?(connected?.000001:.5):0;for(auto p:{btVector3(-.5,-.5,z),btVector3(.5,-.5,z),btVector3(0,.5,z)})skin.Node(p,1,triangle&&!connected?btVector3(0,0,-1):btVector3(0,0,0));int n=triangle*3;for(int i=0;i<3;++i)skin.Member(n+i,n+(i+1)%3,.001,material,"skin");skin.Face(n,n+1,n+2);}
  if(connected)skin.Member(0,3,.001,material,"control-surface connection");skin.Build();persistent::Options options;options.excludeConnectedSelfContact=true;options.linearAabbCulling=culling;
  std::vector<persistent::Pin> pins;if(!connected)for(int i=0;i<3;++i)pins.push_back({&skin,i});persistent::Stepper stepper(world,{&skin},pins,options);Eigen::MatrixXd loads=Eigen::MatrixXd::Zero(6,3);bool accepted=true;int contacts=0;
  try{for(int i=0;i<(connected?1:160);++i){auto result=stepper.Advance(.005,loads);contacts=std::max(contacts,result.peakContacts);}}catch(const std::exception &e){printf("rejected: %s\n",e.what());accepted=false;}
  if(connected)check(accepted&&contacts==0,"overlapping_connected_skin_accepts_motion");else{double bottom=1e9;for(int i=3;i<6;++i)bottom=std::min(bottom,double(skin.nodes[i].x.z()));check(accepted&&contacts>0&&bottom>=options.gap-1e-7,"disconnected_fragments_keep_surface_contact");}
 }
 return failures?1:0;
}

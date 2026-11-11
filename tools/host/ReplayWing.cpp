// Replays force/query receipts from an ordinary saved-game encounter. It never
// substitutes a scene for gameplay; this is a diagnostic for a rejected step.
#include "WingPhysics.h"
#include <fstream>
#include <cstdio>
int main(int argc,char **argv){if(argc!=3)return 2;std::ifstream file(argv[2],std::ios::binary);uint64_t id=0;WpInitial initial{};double dt=0;char tag,error[2048]{};int steps=0;
 while(file.read(&tag,1)){
  if(tag=='I'){file.read((char*)&initial,sizeof initial);}
  else if(tag=='W'){WpStaticMesh world{};file.read((char*)&world,sizeof world);if(!id){if(wp_create_world(argv[1],&initial,&world,&id,error,sizeof error)){std::puts(error);return 1;}}else if(wp_replace_world(id,&world,error,sizeof error)){std::puts(error);return 1;}}
  else if(tag=='T'){file.read((char*)&dt,sizeof dt);}
  else if(tag=='F'){WpLoad load{};file.read((char*)&load,sizeof load);if(!file||!id)return 2;if(wp_advance_forces(id,dt,&load,error,sizeof error)){std::printf("replay rejected step=%d dt=%.17g %s\n",steps,dt,error);wp_destroy(id);return 1;}++steps;}
  else return 2;
 }
 WpState state{};wp_snapshot(id,&state);std::printf("replay steps=%d t=%.9f fragments=%d mass=%.9f\n",steps,state.time,state.fragments,state.mass);wp_destroy(id);return 0;
}

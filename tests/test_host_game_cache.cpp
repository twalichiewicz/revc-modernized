#include "../tools/host/GameCache.h"
#include <cassert>
#include <iostream>
int main(){
 const char *root="/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData/";
 const char *profile="/Users/player/Library/Application Support/macVC-source";
 for(const char *file:{"models/txd.img","models/txd.dir","audio/sound.cache","data/caps.dat"}){
  auto expected=std::string(profile)+"/host-cache/"+file;
  assert(HostGameCachePath(file,root,profile)==expected);
  assert(HostGameCachePath((std::string(root)+file).c_str(),root,profile)==expected);
 }
 assert(HostGameCachePath("MODELS\\TXD.IMG",root,profile)==std::string(profile)+"/host-cache/models/txd.img");
 assert(HostGameCachePath("./DATA\\CAPS.DAT",root,profile)==std::string(profile)+"/host-cache/data/caps.dat");
 for(const char *file:{"models/gta3.img","data/gta_vc.dat","audio/sfx.raw","GTAVCsf1.b","../models/txd.img","/another/GameData/models/txd.img","models/txd.img.extra"})
  assert(HostGameCachePath(file,root,profile).empty());
 std::cout<<"host game cache: all passed\n";
}

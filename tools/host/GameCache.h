#pragma once
#include <string>
// These files are renderer/audio-generated caches, never original game data.
// Route both reads and writes, including streaming and RenderWare file paths.
inline std::string HostGameCachePath(const char *path,const char *gameRoot,const char *profile){
 auto normalize=[](const char *value){std::string out=value?value:"";for(auto &c:out){if(c=='\\')c='/';if(c>='A'&&c<='Z')c+=('a'-'A');}return out;};
 auto relative=normalize(path),root=normalize(gameRoot);
 while(!root.empty()&&root.back()=='/')root.pop_back();
 if(!relative.empty()&&relative.front()=='/'){
  if(root.empty()||relative.compare(0,root.size()+1,root+"/"))return {};
  relative.erase(0,root.size()+1);
 }
 while(relative.compare(0,2,"./")==0)relative.erase(0,2);
 if(relative!="models/txd.img"&&relative!="models/txd.dir"&&relative!="audio/sound.cache"&&relative!="data/caps.dat")return {};
 return std::string(profile)+"/host-cache/"+relative;
}

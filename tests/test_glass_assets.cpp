// Inventory cabin panes on the installed cars, preserving native DFF frame names.
#include "glassmodel.h"
#include "game.h"
#include "../tools/bake/visualmodel.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <set>
struct Chunk {unsigned type;size_t a,b;};
template<class T>T Get(const std::vector<uint8_t> &b,size_t p){T v{};if(p+sizeof v<=b.size())memcpy(&v,b.data()+p,sizeof v);return v;}
std::vector<Chunk> Children(const std::vector<uint8_t> &b,size_t a,size_t end){std::vector<Chunk> out;while(a+12<=end){auto n=Get<unsigned>(b,a+4);if(n>end-a-12)break;out.push_back({Get<unsigned>(b,a),a+12,a+12+n});a+=12+n;}return out;}
Vec3 Sub(Vec3f a,Vec3f b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
int main()
{
 const char *env=getenv("GAMEDATA");std::string base=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";
 ImgArchive img;if(!img.Open(base+"/models/gta3.img")){puts("FAIL: installed car archive unavailable");return 1;}
 std::ifstream ide(base+"/data/default.ide");if(!ide){puts("FAIL: vehicle roster unavailable");return 1;}
 int fail=0,cars=0,windows=0,unclassified=0;std::set<std::string> standardMissing;
 for(std::string line;std::getline(ide,line);){
  std::replace(line.begin(),line.end(),',',' ');std::istringstream in(line);int id;std::string name,txd,kind;
  if(!(in>>id>>name>>txd>>kind)||kind!="car"||id==171)continue;
  uint8_t vehicle[0x350]{};memcpy(vehicle+0x6a,&id,2);if(game::IsAircraft(vehicle))continue;
  ++cars;std::vector<uint8_t> b;if(!img.Read(name+".dff",b)){printf("FAIL: %s missing DFF\n",name.c_str());++fail;continue;}
  auto top=Children(b,12,12+Get<unsigned>(b,4));std::vector<std::string> names;
  for(auto c:top)if(c.type==26)for(auto g:Children(b,c.a,c.b))if(g.type==15){auto ch=Children(b,g.a,g.b);if(Get<unsigned>(b,ch.at(0).a+12)!=1){printf("FAIL: %s has unsupported multiple morph targets\n",name.c_str());++fail;}}
  struct Part{size_t flags;std::string name;};std::vector<Part> parts;
  for(auto c:top)if(c.type==14){auto ch=Children(b,c.a,c.b);unsigned n=Get<unsigned>(b,ch.at(0).a);for(unsigned i=0;i<n;i++){
   std::string s;for(auto e:Children(b,ch.at(i+1).a,ch.at(i+1).b))if(e.type==0x253f2fe)s=std::string(reinterpret_cast<const char *>(b.data()+e.a),strnlen(reinterpret_cast<const char *>(b.data()+e.a),e.b-e.a));names.push_back(s);
  }}
  for(auto c:top)if(c.type==20){auto ch=Children(b,c.a,c.b);auto frame=Get<unsigned>(b,ch.at(0).a);parts.push_back({ch.at(0).a+8,names.at(frame)});unsigned zero=0;memcpy(b.data()+parts.back().flags,&zero,4);}
  unsigned panes=0;int total=0,glass=0;bool layout=true;
  for(const auto &p:parts){
   if(p.name.find("_lo")!=std::string::npos||p.name.find("_vlo")!=std::string::npos||p.name.find("_dam")!=std::string::npos)continue;
   unsigned flags=4;memcpy(b.data()+p.flags,&flags,4);visualmodel::Mesh m;std::string why;bool ok=visualmodel::Read(b,m,why);flags=0;memcpy(b.data()+p.flags,&flags,4);
   if(!ok){printf("FAIL: %s %s %s\n",name.c_str(),p.name.c_str(),why.c_str());++fail;continue;}
   layout&=m.vertices.size()<=8192&&m.faces.size()<=16384;
   for(auto f:m.faces){auto a=m.vertices[f.a],c=m.vertices[f.b],d=m.vertices[f.c];auto alpha=m.materials[f.material].alpha;++total;
    if(alpha&&alpha<255){++glass;const auto &texture=m.materials[f.material].texture;auto pane=glassmodel::Classify(p.name.c_str(),{(a.x+c.x+d.x)/3,(a.y+c.y+d.y)/3,(a.z+c.z+d.z)/3},Cross(Sub(c,a),Sub(d,a)),alpha,texture.c_str());
     const bool decal=texture.find("decal")!=std::string::npos||texture.find("badge")!=std::string::npos||texture.find("sign")!=std::string::npos||texture=="vcfd8bit128a"||texture=="cabbie8bit128";
     const bool lamp=name=="infernus"&&p.name=="chassis_hi"&&alpha==152&&(a.y+c.y+d.y)/3>2;
     if((decal||lamp)&&pane!=glassmodel::Pane::None){printf("FAIL: %s %s decorative/lamp face classified as glass\n",name.c_str(),texture.c_str());++fail;}
     panes|=glassmodel::Bit(pane);if(pane==glassmodel::Pane::None)++unclassified;
    }
   }
  }
  if(!layout){printf("FAIL: %s exceeds geometry copy bound\n",name.c_str());++fail;}
  int count=0;for(int i=0;i<6;i++)count+=(panes>>i)&1;windows+=count;
  if((name=="admiral"||name=="police"||name=="infernus"||name=="voodoo"||name=="stretch")&&!(panes&glassmodel::Bit(glassmodel::Pane::Rear)))standardMissing.insert(name);
  printf("glass assets: %-10s panes %02x (%d), translucent %d/%d faces\n",name.c_str(),panes,count,glass,total);
 }
 for(auto &name:standardMissing){printf("FAIL: %s rear pane not classified\n",name.c_str());++fail;}
 if(cars<70||windows<150){puts("FAIL: insufficient actual-car pane coverage");++fail;}
 printf("glass assets: %d cars, %d cabin panes, %d conservative exclusions, %d failures\n",cars,windows,unclassified,fail);return fail?1:0;
}

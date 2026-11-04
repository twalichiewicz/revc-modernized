#include "mechanicalmodel.h"
#include "game.h"
#include "../tools/bake/gamefiles.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
struct Chunk {uint32_t type;size_t a,b;};
template<class T>T Get(const std::vector<uint8_t> &b,size_t p){T out{};if(p<=b.size()&&sizeof out<=b.size()-p)memcpy(&out,b.data()+p,sizeof out);return out;}
std::vector<Chunk> Children(const std::vector<uint8_t> &b,size_t a,size_t end){std::vector<Chunk> out;while(a+12<=end){auto n=Get<uint32_t>(b,a+4);if(n>end-a-12)break;out.push_back({Get<uint32_t>(b,a),a+12,a+12+n});a+=12+n;}return out;}
int main(){
 const char *env=getenv("GAMEDATA");std::string base=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";
 ImgArchive archive;if(!archive.Open(base+"/models/gta3.img"))return 2;
 std::vector<uint8_t> col;if(!ReadGameFile(base+"/models/coll/vehicles.col",col))return 2;
 std::map<std::string,std::pair<Vec3,Vec3>> bounds;
 for(size_t at=0;at+80<=col.size()&&!memcmp(col.data()+at,"COLL",4);){size_t end=at+8+Get<uint32_t>(col,at+4);if(end>col.size()||end<=at)break;
  std::string name(reinterpret_cast<const char *>(col.data()+at+8),strnlen(reinterpret_cast<const char *>(col.data()+at+8),22));
  bounds[Lower(name)]={Get<Vec3>(col,at+48),Get<Vec3>(col,at+60)};at=end;
 }
 int fails=0,count=0;std::ifstream ide(base+"/data/default.ide");
 for(std::string line;std::getline(ide,line);){std::replace(line.begin(),line.end(),',',' ');std::istringstream in(line);int id;std::string name,txd,kind;
  if(!(in>>id>>name>>txd>>kind)||kind!="car"||id==162||id==171)continue;
  uint8_t v[0x350]{};memcpy(v+0x6a,&id,2);if(game::IsAircraft(v))continue;
  std::vector<uint8_t> bytes;if(!archive.Read(name+".dff",bytes)){++fails;continue;}
  std::array<Vec3,4> wheels{};unsigned seen=0;
  for(auto c:Children(bytes,12,12+Get<uint32_t>(bytes,4)))if(c.type==14){auto children=Children(bytes,c.a,c.b);if(children.empty())continue;const auto st=children[0].a;auto n=Get<uint32_t>(bytes,st);
   for(unsigned i=0;i<n&&i+1<children.size();i++){
    std::string frame;for(auto e:Children(bytes,children[i+1].a,children[i+1].b))if(e.type==0x253f2fe)frame=std::string(reinterpret_cast<const char *>(bytes.data()+e.a),strnlen(reinterpret_cast<const char *>(bytes.data()+e.a),e.b-e.a));
    const char *names[]={"wheel_lf_dummy","wheel_lb_dummy","wheel_rf_dummy","wheel_rb_dummy"};
    for(int w=0;w<4;w++)if(frame==names[w]){wheels[w]=Get<Vec3>(bytes,st+4+size_t(i)*56+36);seen|=1u<<w;}
   }
  }
  mechanicalmodel::Layout layout;const auto box=bounds.find(name);
  if(seen!=15||box==bounds.end()||!mechanicalmodel::Build(id,box->second.first,box->second.second,wheels,layout)){printf("FAIL: native mechanical layout %s, wheel mask %x, COL %d\n",name.c_str(),seen,box!=bounds.end());++fails;continue;}
  if(id==211&&layout.volumes[0].hi.y>=0){puts("FAIL: actual Deluxo engine volume belongs at rear");++fails;}
  ++count;
 }
 if(count<70){puts("FAIL: insufficient actual mechanical layouts");++fails;}
 printf("mechanical assets: %d original car layouts, %d failures\n",count,fails);return fails?1:0;
}

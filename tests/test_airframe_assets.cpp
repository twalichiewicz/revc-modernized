#define main CarAssetChecks
#include "test_mechanical_assets.cpp"
#undef main
#include "airframemodel.h"
int main(){const char *env=getenv("GAMEDATA");std::string base=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";std::vector<uint8_t> col;if(!ReadGameFile(base+"/models/coll/vehicles.col",col))return 2;
 std::map<std::string,std::pair<Vec3,Vec3>> bounds;for(size_t at=0;at+80<=col.size()&&!memcmp(col.data()+at,"COLL",4);){size_t end=at+8+Get<uint32_t>(col,at+4);if(end>col.size()||end<=at)break;std::string name(reinterpret_cast<const char *>(col.data()+at+8),strnlen(reinterpret_cast<const char *>(col.data()+at+8),22));bounds[Lower(name)]={Get<Vec3>(col,at+48),Get<Vec3>(col,at+60)};at=end;}
 int count=0,bad=0;std::ifstream ide(base+"/data/default.ide");for(std::string line;std::getline(ide,line);){std::replace(line.begin(),line.end(),',',' ');std::istringstream in(line);int id;std::string name,txd,kind;if(!(in>>id>>name>>txd>>kind))continue;uint8_t v[0x350]{};memcpy(v+0x6a,&id,2);int nativeKind=kind=="plane"?4:kind=="heli"?3:kind=="boat"?1:0;memcpy(v+0x348,&nativeKind,4);if(!game::IsAircraft(v))continue;
  auto b=bounds.find(name);airframemodel::Layout layout;if(b==bounds.end()||!airframemodel::Build(id,nativeKind,b->second.first,b->second.second,layout)){printf("FAIL: original airframe %s bounds\n",name.c_str());++bad;continue;}
  for(const auto &m:layout.volumes)if(!std::isfinite(m.lo.x)||m.lo.x>=m.hi.x||m.lo.y>=m.hi.y||m.lo.z>=m.hi.z){printf("FAIL: invalid component in %s\n",name.c_str());++bad;}
  if(id==194&&layout.helicopter){puts("FAIL: RC Baron fixed-wing skin uses fixed-wing internals");++bad;}++count;
 }
 if(count<12){puts("FAIL: insufficient original aircraft coverage");++bad;}printf("airframe assets: %d original layouts, %d failures\n",count,bad);return bad?1:0;
}

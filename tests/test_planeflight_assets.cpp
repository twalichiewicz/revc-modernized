// Installed native roster assets: keep CPlane allocation separate from car
// wheel/seat assumptions, and verify the exact original landing COL input.
#include "../tools/bake/gamefiles.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>
static int failures;
#define CHECK(c) do{if(!(c)){std::printf("FAIL %d: %s\n",__LINE__,#c);++failures;}}while(0)
template<class T> static T Get(const std::vector<uint8_t> &b,size_t at){T value{};if(at+sizeof value<=b.size())std::memcpy(&value,b.data()+at,sizeof value);return value;}
struct Chunk{uint32_t type;size_t a,z;};
static std::vector<Chunk> Chunks(const std::vector<uint8_t> &b,size_t at,size_t end)
{
	std::vector<Chunk> out;end=std::min(end,b.size());
	while(at+12<=end){const uint32_t size=Get<uint32_t>(b,at+4);if(size>end-at-12)break;out.push_back({Get<uint32_t>(b,at),at+12,at+12+size});at+=12+size;}
	return out;
}
static std::set<std::string> Frames(const std::vector<uint8_t> &bytes)
{
	std::set<std::string> names;
	for(const Chunk &c:Chunks(bytes,12,bytes.size()))if(c.type==14)
	for(const Chunk &extension:Chunks(bytes,c.a,c.z))if(extension.type==3)
	for(const Chunk &name:Chunks(bytes,extension.a,extension.z))if(name.type==0x253f2fe)
		names.emplace(reinterpret_cast<const char *>(bytes.data()+name.a),strnlen(reinterpret_cast<const char *>(bytes.data()+name.a),name.z-name.a));
	return names;
}
int main()
{
	const char *env=std::getenv("GAMEDATA");const std::string data=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";
	ImgArchive archive;CHECK(archive.Open(data+"/models/gta3.img"));
	std::vector<uint8_t> dff;CHECK(archive.Read("deaddodo.dff",dff));const auto names=Frames(dff);
	CHECK(names.count("wheel_front_dummy")&&names.count("wheel_rear_dummy"));
	CHECK(!names.count("wheel_lf_dummy")&&!names.count("wheel_rf_dummy")&&!names.count("ped_frontseat"));
	std::vector<uint8_t> skimmer;CHECK(archive.Read("skimmer.dff",skimmer));CHECK(Frames(skimmer).count("ped_frontseat"));
	std::vector<uint8_t> ide;CHECK(ReadGameFile(data+"/data/default.ide",ide));
	std::istringstream rows(std::string(ide.begin(),ide.end()));std::map<int,std::string> kinds;
	for(std::string row;std::getline(rows,row);){
		std::replace(row.begin(),row.end(),',',' ');std::istringstream fields(row);int id=0;std::string name,txd,kind;
		if(fields>>id>>name>>txd>>kind)kinds[id]=kind;
	}
	CHECK(kinds[180]=="plane"&&kinds[181]=="plane"&&kinds[190]=="boat"&&kinds[194]=="car");
	std::vector<uint8_t> col;CHECK(ReadGameFile(data+"/models/coll/vehicles.col",col));bool found=false;
	for(size_t at=0;at+80<=col.size();){
		const size_t end=at+8+Get<uint32_t>(col,at+4);if(end>col.size()||end<=at)break;
		const std::string name(reinterpret_cast<const char *>(col.data()+at+8),strnlen(reinterpret_cast<const char *>(col.data()+at+8),22));
		if(name=="deaddodo"){
			found=true;CHECK(!std::memcmp(col.data()+at,"COLL",4));
			const float minimumZ=Get<float>(col,at+56);const int spheres=Get<uint16_t>(col,at+72);
			CHECK(spheres==27&&minimumZ<-.5f&&minimumZ>-3);
			for(int i=0;i<spheres;i++){
				const size_t sphere=at+76+size_t(i)*20;CHECK(sphere+20<=end);
				const float radius=Get<float>(col,sphere),z=Get<float>(col,sphere+12);
				CHECK(radius>0&&radius<5&&-minimumZ+.08f+z-radius>0);
			}
			std::printf("Dodo181: native plane wheel frames, no car/seat dummies; %d original COL spheres, base %.3fm\n",spheres,-minimumZ);
		}
		at=end;
	}
	CHECK(found);
	if(!failures)std::puts("planeflight assets: all passed");return failures?1:0;
}

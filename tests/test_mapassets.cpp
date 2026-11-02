// Exercise the actual bake pipeline against the user's installed world assets.
#define main vcmm_bake_entry
#include "../tools/bake/bake.cpp"
#undef main
#include <cstdlib>

int main()
{
	const char *env=std::getenv("GAMEDATA");const std::string data=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";
	ImgArchive archive;int failures=0;
	auto check=[&](bool yes,const char *what){if(!yes){std::printf("FAIL: %s\n",what);++failures;}};
	check(archive.Open(data+"/models/gta3.img"),"installed native archive is available");if(failures)return 1;
	const auto all=LoadInstances(data);std::map<std::string,ColModel> cols;
	for(const std::string &name:archive.Names())if(name.size()>4&&name.substr(name.size()-4)==".col") {
		std::vector<uint8_t> bytes;if(archive.Read(name,bytes))ParseColArchive(bytes,cols);
	}
	auto instance=[&](const char *name) {for(const Instance &in:all)if(in.model==name)return in;return Instance{};};
	auto mesh=[&](const char *name) {std::vector<uint8_t> bytes;visualmodel::Mesh out;std::string why;check(archive.Read(std::string(name)+".dff",bytes)&&visualmodel::Read(bytes,out,why),"native visual model parses");return out;};
	const float water=SeaLevel(data);std::vector<float> ground(size_t(kGrid)*kGrid,8.765f);
	const std::vector<Instance> pair{instance("starbridge2"),instance("starbridge2b")};
	const auto bridge=VisualBuildings(pair,cols,ground,{},archive,water);
	check(bridge.size()==2,"both native Star Island bridge pieces are baked");
	if(bridge.size()==2) {
		float reference[2]{};
		for(int i=0;i<2;i++) {
			const auto source=mesh(pair[size_t(i)].model.c_str());std::map<int,int> votes;
			// Recover the common translation from unchanged native vertices. Most
			// XY positions have only one source height; modal voting handles piers.
			for(const Vec3 &v:bridge[size_t(i)].verts)for(Vec3f local:source.vertices) {
				const Vec3f p=visualmodel::Place(local,pair[size_t(i)]);
				if(std::fabs(p.x-v.x)<.001f&&std::fabs(p.y-v.y)<.001f)++votes[int(std::lround((p.z-v.z)*1000))];
			}
			int most=0;for(auto [value,count]:votes)if(count>most){most=count;reference[i]=float(value)/1000;}
			check(most>100,"bridge triangles retain native source positions");
			for(const Vec3 &v:bridge[size_t(i)].verts)check(v.z+reference[i]>=water-.002f,"bridge underwater geometry is clipped at actual sea level");
		}
		check(std::fabs(reference[0]-reference[1])<.002f,"native adjoining bridge pieces retain a common vertical placement");
		std::printf("native bridge references: %.3f / %.3f\n",reference[0],reference[1]);
	}
	const std::vector<Instance> timed{instance("roosbridge_dt"),instance("roosbridge_nt")};
	check(timed[0].timeOn==5&&timed[1].timeOn==23,"native IDE visibility periods are loaded");
	check(VisualBuildings(timed,cols,ground,{},archive,water).size()==1,"mutually exclusive native bridge variants are not overlaid");
	for(const char *name:{"manmrgbuild2","mansion3_c"}) {
		const std::vector<Instance> house{instance(name)};const auto old=ExtractBuildings(house,cols,ground);
		const auto replacement=VisualBuildings(house,cols,ground,old,archive,water);
		size_t oldTriangles=0;for(const auto &b:old.buildings)oldTriangles+=b.tris.size()/3;
		check(replacement.size()==1&&(replacement[0].flags&mapdata::kVisualMesh)&&replacement[0].tris.size()/3>oldTriangles,
		      "neighboring mansion uses its pitched visual structure rather than collision boxes");
		std::printf("native mansion %s: %zu collision / %zu visual triangles\n",name,oldTriangles,replacement.empty()?0:replacement[0].tris.size()/3);
	}
	auto awning=mesh("pierentrance02");
	awning.faces.erase(std::remove_if(awning.faces.begin(),awning.faces.end(),[&](const visualmodel::Face &f){return awning.materials[f.material].texture!="blueshade2_64";}),awning.faces.end());
	check(!awning.faces.empty(),"native shaded awning material exists");mapdata::Building baked;Instance origin{};origin.qw=1;
	check(visualmodel::Build(awning,origin,-1000,baked)&&baked.tris.size()==awning.faces.size()*3,"native shaded awning faces are not mistaken for shadows");
	if(!failures)std::puts("mapassets: all passed");return failures?1:0;
}

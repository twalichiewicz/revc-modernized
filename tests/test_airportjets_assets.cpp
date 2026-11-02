// Verify the shipped Learjet inputs used by the scenery-to-aircraft adapter.
// Native construction/lifetime is exercised separately; this is no flight test.
#include "../tools/bake/visualmodel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

static int failures;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)
template<class T> static T Get(const std::vector<uint8_t> &b, size_t at)
{
	T value{};
	CHECK(at <= b.size() && sizeof value <= b.size() - at);
	if (at <= b.size() && sizeof value <= b.size() - at) std::memcpy(&value, b.data() + at, sizeof value);
	return value;
}
int main()
{
	const char *env = std::getenv("GAMEDATA");
	const std::string data = env ? env : "/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";
	ImgArchive archive;
	CHECK(archive.Open(data + "/models/gta3.img"));
	std::vector<uint8_t> dff, txd;
	CHECK(archive.Read("ap_learjet1_01.dff", dff));
	CHECK(archive.Read("ap_learjets.txd", txd));
	visualmodel::Mesh mesh; std::string why;
	CHECK(visualmodel::Read(dff, mesh, why));
	CHECK(mesh.vertices.size() == 1303 && mesh.faces.size() == 1212);
	Vec3f lo{1e6f,1e6f,1e6f}, hi{-1e6f,-1e6f,-1e6f};
	for (const auto &v : mesh.vertices) {
		lo = {std::min(lo.x,v.x),std::min(lo.y,v.y),std::min(lo.z,v.z)};
		hi = {std::max(hi.x,v.x),std::max(hi.y,v.y),std::max(hi.z,v.z)};
	}
	CHECK(hi.x-lo.x > 20 && hi.y-lo.y > 17 && hi.z-lo.z > 4);
	bool hasLivery = false;
	for (const auto &m : mesh.materials) hasLivery |= m.texture == "ap_learjet";
	CHECK(hasLivery);
	int width=0,height=0;std::vector<uint8_t> rgba;
	CHECK(DecodeTxdTexture(txd,"ap_learjet",width,height,rgba));
	CHECK(width > 0 && height > 0 && rgba.size() == size_t(width)*size_t(height)*4);

	// The visual spans20m; retaining a Dodo COL would omit most of its wings.
	std::vector<uint8_t> col;
	CHECK(ReadGameFile(data + "/models/coll/generic.col",col));
	bool foundCol=false;
	for (size_t at=0;at+80<=col.size();) {
		const size_t end=at+8+Get<uint32_t>(col,at+4);
		if(end>col.size()||end<=at) { CHECK(false);break; }
		const std::string name(reinterpret_cast<const char *>(col.data()+at+8),strnlen(reinterpret_cast<const char *>(col.data()+at+8),22));
		if(name=="ap_learjet1_01") {
			foundCol=true;CHECK(std::memcmp(col.data()+at,"COLL",4)==0);
			const auto minimum=Get<Vec3f>(col,at+48),maximum=Get<Vec3f>(col,at+60);
			CHECK(minimum.x<=lo.x+.01f&&maximum.x>=hi.x-.01f);
			CHECK(minimum.y<=lo.y+.01f&&maximum.y>=hi.y-.01f);
			CHECK(minimum.z<=lo.z+.01f&&maximum.z>=hi.z-.01f);
			CHECK(minimum.z < -2.9f && minimum.z > -3.1f);
			CHECK(Get<uint32_t>(col,at+72)==2);
			float gearBottom=0;
			for(int i=0;i<2;++i) {
				const size_t sphere=at+76+size_t(i)*20;
				const float radius=Get<float>(col,sphere),z=Get<float>(col,sphere+12);
				gearBottom=std::min(gearBottom,z-radius);
			}
			CHECK(gearBottom < -2.32f && gearBottom > -2.34f);
			CHECK(gearBottom-minimum.z > .6f); // oversized static wing box is not landing-gear height
		}
		at=end;
	}
	CHECK(foundCol);
	std::map<std::string,ColModel> collisions;ParseColArchive(col,collisions);
	CHECK(collisions["ap_learjet1_01"].boxes.size()==2);

	const auto instances=LoadInstances(data);int count=0,north=0;
	for(const auto &in:instances) if(in.model=="ap_learjet1_01") {
		++count;north+=in.pos.y>-500;
		CHECK(in.interior==0&&in.pos.x>-1900&&in.pos.x<-1300&&in.pos.y>-1400&&in.pos.y<-300);
		CHECK(std::fabs(in.pos.z-15.9968605f)<.001f);
		CHECK(std::fabs(in.qx)<.001f&&std::fabs(in.qy)<.001f);
		CHECK(std::fabs(in.qz*in.qz+in.qw*in.qw-1)<.001f);
	}
	CHECK(count==7&&north==1);

	// Data-level reservation is only a prerequisite: production must also
	// validate the live model table, native store capacity and stream slot.
	int vehicles=0;bool declared=false;
	for(const auto &entry:std::filesystem::recursive_directory_iterator(data+"/data")) {
		if(!entry.is_regular_file()||Lower(entry.path().extension().string())!=".ide")continue;
		std::ifstream file(entry.path());std::string line,section;
		while(std::getline(file,line)) {
			std::replace(line.begin(),line.end(),',',' ');std::istringstream fields(line);
			std::string first;if(!(fields>>first)||first[0]=='#')continue;
			if(first=="end") { section.clear();continue; }
			if(first=="cars"||first=="objs"||first=="tobj"||first=="hier"||first=="peds"||first=="weap") {section=first;continue;}
			if(section.empty()||section=="2dfx")continue;
			char *tail=nullptr;const long id=std::strtol(first.c_str(),&tail,10);
			if(!tail||*tail)continue;
			CHECK(id<240||id>249);
			if(section=="cars")++vehicles;
			if(section=="objs"&&id==636) {
				std::string model,texture;int atomics=0;fields>>model>>texture>>atomics;
				CHECK(model=="ap_learjet1_01"&&texture=="ap_learjets"&&atomics==1);declared=true;
			}
		}
	}
	CHECK(declared&&vehicles==107&&vehicles<110);
	std::printf("Learjet636: %d placements (%d northern), %d native vehicle definitions, visual %.2fx%.2fx%.2fm; real texture/COL verified\n",count,north,vehicles,hi.x-lo.x,hi.y-lo.y,hi.z-lo.z);
	if(!failures)std::puts("airport jet assets: all passed");
	return failures?1:0;
}

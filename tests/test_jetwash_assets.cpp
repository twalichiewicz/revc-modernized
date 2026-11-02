// Read-only native ABI and original source-geometry evidence. Never starts,
// attaches to, or changes the installed game.
#define main ExistingAirworldNativeChecks
#include "test_airworld_native.cpp"
#undef main
#include "../tools/bake/gamefiles.h"
#include <cmath>
#include <set>
namespace {
struct WashChunk {uint32_t id,version;size_t a,z;};
template<class T>T WashAt(const std::vector<uint8_t> &b,size_t p){T out{};CHECK(p<=b.size()&&sizeof out<=b.size()-p);if(p<=b.size()&&sizeof out<=b.size()-p)std::memcpy(&out,b.data()+p,sizeof out);return out;}
std::vector<WashChunk> WashChunks(const std::vector<uint8_t> &b,size_t p,size_t end){std::vector<WashChunk> out;while(p+12<=end&&p+12<=b.size()){const size_t n=WashAt<uint32_t>(b,p+4);if(n>end-p-12||n>b.size()-p-12)break;out.push_back({WashAt<uint32_t>(b,p),WashAt<uint32_t>(b,p+8),p+12,p+12+n});p+=12+n;}return out;}
void NozzleAsset(const std::string &data){
 ImgArchive archive;CHECK(archive.Open(data+"/models/gta3.img"));std::vector<uint8_t> b;CHECK(archive.Read("ap_learjet1_01.dff",b));WashChunk geometry{};
 for(auto list:WashChunks(b,12,b.size()))if(list.id==26)for(auto c:WashChunks(b,list.a,list.z))if(c.id==15){geometry=c;break;}
 const auto children=WashChunks(b,geometry.a,geometry.z);CHECK(!children.empty());if(children.empty())return;
 const auto st=children[0];size_t p=st.a;const uint32_t flags=WashAt<uint32_t>(b,p),nt=WashAt<uint32_t>(b,p+4),nv=WashAt<uint32_t>(b,p+8);p+=16;
 const uint32_t lib=st.version&0xffff0000u?(((st.version>>14)&0x3ff00u)+0x30000u)|((st.version>>16)&0x3fu):st.version<<8;
 if(lib<0x34000)p+=12;if(flags&8)p+=nv*4;p+=nv*8;const size_t triangles=p;p+=nt*8+24;
 CHECK(nt==1212&&nv==1303);
 for(int side=0;side<2;++side){std::set<uint16_t> ring;const int first=side?1187:1163;
  for(int face=first;face<first+24;++face)for(int j:{0,1,3}){const uint16_t vertex=WashAt<uint16_t>(b,triangles+face*8+j*2);const Vec3f q=WashAt<Vec3f>(b,p+vertex*12);if(q.y< -5.7f)ring.insert(vertex);}
  CHECK(ring.size()>=10);Vec3f center{};for(auto vertex:ring){const auto q=WashAt<Vec3f>(b,p+vertex*12);center.x+=q.x;center.y+=q.y;center.z+=q.z;}if(ring.empty())continue;center.x/=ring.size();center.y/=ring.size();center.z/=ring.size();
  CHECK(std::fabs(center.x-(side?-1.2424f:1.2424f))<.03f&&std::fabs(center.y+5.7319f)<.001f&&std::fabs(center.z-.2924f)<.03f);
  // Every original rim vertex lies around the measured outlet. The emitted
  // point y-5.80 is behind the original rear face; its axis is local-y.
  for(auto vertex:ring){const auto q=WashAt<Vec3f>(b,p+vertex*12);const float radius=std::hypot(q.x-(side?-1.2424f:1.2424f),q.z-.2924f);CHECK(radius>.26f&&radius<.29f&&q.y> -5.80f);}
  std::printf("source636 %s rear rim %.4f %.4f %.4f (%zu native vertices)\n",side?"left":"right",center.x,center.y,center.z,ring.size());
 }
 std::ifstream cfg(data+"/data/particle.cfg");CHECK(bool(cfg));std::vector<std::string> names;
 for(std::string line;std::getline(cfg,line);){std::istringstream fields(line);std::string name;int red=0;if(fields>>name>>red)if(!name.empty()&&name[0]!=';'&&name[0]!='#')names.push_back(name);}
 CHECK(names.size()>82&&names[67]=="EXHAUST_FUMES"&&names[81]=="HEATHAZE"&&names[82]=="HEATHAZE_IN_DIST");
 std::ifstream exhaustCfg(data+"/data/particle.cfg");bool exhaust=false;
 for(std::string line;std::getline(exhaustCfg,line);){std::istringstream fields(line);std::vector<std::string> columns;for(std::string v;fields>>v;)columns.push_back(v);if(!columns.empty()&&columns[0]=="EXHAUST_FUMES"){exhaust=true;CHECK(columns.size()==41);if(columns.size()==41){CHECK(std::stof(columns[39])==25&&std::stoi(columns[40])==4);CHECK(std::stoi(columns[29])==1000&&std::stoi(columns[14])==96);}}}CHECK(exhaust);
}
void WashABI(const Image &image){
 // Uncoloured particle wrapper retains world position/direction references,
 // passes zero colour and forwards to the coloured native implementation.
 CHECK(image.Instruction(0x100204474)==0xb81fc3bf);CHECK(image.Branch(0x100204484)==0x1002045b8);
 // Exact native create-range ABI: manager[83],160-byte entries; ID+0,
 // name+4, CR-squared+24. Column39 parses metres then squares into+24.
 CHECK(image.symbols.at("_mod_ParticleSystemManager")==0x10076a198);
 CHECK(image.Instruction(0x100208124)==0x52801408);CHECK(image.Instruction(0x100208128)==0x9b284efc);CHECK(image.Instruction(0x100208134)==0xb8004417);
 CHECK((image.Instruction(0x10032b380)&255)==155); // column39 jump→208300
 CHECK(image.Instruction(0x100208304)==0x1e600800);CHECK(image.Instruction(0x10020830c)==0xbd001b80);
 CHECK(image.Instruction(0x1002046e4)==0x91066108);CHECK(image.Instruction(0x1002046e8)==0x52801409);CHECK(image.Instruction(0x1002046ec)==0x9ba92015);
 CHECK(image.Instruction(0x1002046f0)==0xbd401aa1);CHECK(image.Instruction(0x1002046f8)==0x540001e0); // zero bypasses range
 CHECK(image.Instruction(0x100204704)==0x2d460d02);CHECK(image.Instruction(0x100204714)==0xbd403904); // camera worldXYZ
 CHECK(image.Instruction(0x100204728)==0x1f040882);CHECK(image.Instruction(0x10020472c)==0x1e222020);CHECK(image.Instruction(0x100204730)==0x54fff8c4); // squared distance rejection
 // No callback/reentrant particle creation on supported type67(flags4):
 // non-heat ID skips CalcScreenCoors; flags4 skip all Z/world queries. The
 // remaining callees are pure record-buffer writes and the native RNG.
 CHECK(image.Instruction(0x1002047d0)==0x7101441f);CHECK(image.Instruction(0x1002047d4)==0x540003c1);
 CHECK(image.Instruction(0x100204c20)==0x3941b2a8);CHECK(image.Instruction(0x100204c24)==0x370000e8);CHECK(image.Instruction(0x100204c2c)==0x36600548);
 for(uint64_t at=0x1002045b8;at<0x100204f28;at+=4)CHECK((image.Instruction(at)&0xfffffc1f)!=0xd63f0000); // no BLR
 for(uint64_t at=0x10007bc60;at<0x10007c1bc;at+=4){const auto word=image.Instruction(at);CHECK((word&0xfc000000)!=0x94000000&&(word&0xfffffc1f)!=0xd63f0000);} // RecordParticle has no calls
 // Creation takes a real unused particle, sets expiry from config+80,
 // records world position/velocity, and links system+152. Restoring CR²
 // cannot erase this native particle: Update/Render never reload that field.
 CHECK(image.Instruction(0x1002046d8)==0xf940c293);CHECK(image.Instruction(0x100204758)==0xb94052a8);CHECK(image.Instruction(0x100204760)==0xb9001a68);
 CHECK(image.Instruction(0x100204858)==0xf9000268);CHECK(image.Instruction(0x100204868)==0xf800c268);CHECK(image.Instruction(0x100204e74)==0xf9004eb3);
 CHECK(image.Instruction(0x10020594c)==0xb9401a89);CHECK(image.Instruction(0x100205954)==0x54004068); // native lifespan consumer
 for(uint64_t at=0x100204f28;at<0x10020704c;at+=4){const auto word=image.Instruction(at);CHECK((word&0xffffffe0)!=0xbd401ae0);CHECK((word&0xffffffe0)!=0xbd401ac0);} // no CR² load from Update(x23)/Render(x22)
 // ApplyMoveForce is impulse/mass into native velocity (no hidden timestep).
 CHECK(image.Instruction(0x1001564c0)==0xbd40cc03);CHECK(image.Instruction(0x1001564c8)==0x1e231883);CHECK(image.Instruction(0x1001564e0)==0xfc084000);
 // Native blast clears static before AddToMovingList; later explicitly
 // promotes SIMPLE to PHYSICS before the autopilot initializer. Calling
 // SwitchVehicleToRealPhysics alone never changes the status byte.
 CHECK(image.Instruction(0x100138bc0)==0xb90062c8);CHECK(image.Branch(0x100138bc8)==0x1001556c8);
 CHECK(image.Instruction(0x100138e04)==0x12186908);CHECK(image.Instruction(0x100138e08)==0x321d0508);CHECK(image.Instruction(0x100138e0c)==0xb90062c8);CHECK(image.Branch(0x100138e14)==0x100057010);
 CHECK(image.Instruction(0x100057014)==0x79033c08);CHECK(image.Instruction(0x10005702c)==0xd65f03c0);
 // A braked native wheel caps slip with the traction circle. Native ped
 // control applies gravity/damping, then standing UpdatePosition owns XY.
 CHECK(image.Instruction(0x10027cd68)==0x1e202020);CHECK(image.Instruction(0x10027cdbc)==0x1e201820);CHECK(image.Instruction(0x10027cdc4)==0x1e200a31);
 CHECK(image.Branch(0x1001d5d30)==0x100192c88); // player reaches the same existing CPed control consumer
 CHECK(image.Branch(0x100194c2c)==0x100155d50);CHECK(image.Branch(0x100194cf8)==0x100191854);CHECK(image.Branch(0x100194d44)==0x1001920b0);
 CHECK(image.Branch(0x100155e70)==0x100155fd4); // physical control applies gravity
 for(uint64_t at=0x100155d50;at<0x100155fd4;at+=4){const auto word=image.Instruction(at);if((word&0xfc000000)==0x94000000)CHECK(image.Branch(at)!=0x1001563a0);} // never ApplyMoveSpeed
 CHECK(image.symbols.at("__ZN7CReplay4ModeE")==0x10041d5a8);
 CHECK(image.Instruction(0x1001920c4)==0x39462008);CHECK(image.Instruction(0x1001920cc)==0xf9439008); // standing, unattached
 CHECK(image.Instruction(0x100192104)==0xf9424280);CHECK(image.Instruction(0x100192108)==0xb4000d80); // no moving support entity
 CHECK(image.Instruction(0x1001922b8)==0x39456109);CHECK(image.Instruction(0x1001922c4)==0xfd40a502); // exceptional surface26 normal
 CHECK(image.Instruction(0x1001922dc)==0xfc484101);CHECK(image.Instruction(0x1001922e4)==0x0ea1d400);CHECK(image.Instruction(0x1001922ec)==0xfc084100);
}
}
int main(){const char *env=std::getenv("MACVC_NATIVE"),*home=std::getenv("HOME"),*data=std::getenv("GAMEDATA");Image image;if(!image.Load(env?env:std::string(home?home:"")+"/Library/Application Support/macVC/vcmodernhud/macVC.vanilla"))return 1;WashABI(image);NozzleAsset(data?data:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData");if(!failures)std::puts("jetwash native ABI/source assets: all passed");return failures?1:0;}

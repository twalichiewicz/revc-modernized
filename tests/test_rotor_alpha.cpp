// Native CAutomobile::Render rotor presentation for material helicopters: the
// stock blade/blur crossfade survives at speed, and the blur disc fades with the
// surviving blade material. Also checks the installed engine patch uses it.
#include "../src/aircraftbodybridge.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
static int fails=0;
static void Check(bool ok,const char *what){if(!ok){std::printf("FAIL: %s\n",what);++fails;}}
// Exactly CAutomobile::Render's stock values (Min/Max, float->int truncation).
static void Stock(float speed,int &blade,int &blur){
 blade=int((1.5f-std::min(1.7f*std::max(speed,0.0f)/.22f,1.5f))*255.0f);blade=std::min(blade,255);
 blur=int(std::max(1.5f*speed/.22f-.4f,0.0f)*150.0f);blur=std::min(blur,150);
}
int main(int argc,char **argv){
 AircraftBodySnapshot intact;int blade,blur,rearBlade,rearBlur;
 Stock(.22f,blade,blur);rearBlade=blade;rearBlur=blur;AircraftBodyRotorAlpha(intact,blade,blur,rearBlade,rearBlur);
 Check(blade==0&&blur==150&&rearBlade==0&&rearBlur==150,"owned intact Maverick at full rotor speed keeps stock alpha: solid blades 0, blur 150");
 Stock(0,blade,blur);rearBlade=blade;rearBlur=blur;AircraftBodyRotorAlpha(intact,blade,blur,rearBlade,rearBlur);
 Check(blade==255&&blur==0&&rearBlade==255&&rearBlur==0,"stopped material rotor draws its solid blades and no disc");
 Stock(.1f,blade,blur);const int midBlade=blade,midBlur=blur;rearBlade=blade;rearBlur=blur;AircraftBodyRotorAlpha(intact,blade,blur,rearBlade,rearBlur);
 Check(blade==midBlade&&blur==midBlur,"spool-up crossfade is unchanged for an intact rotor");
 AircraftBodySnapshot damaged;damaged.rotor=.5f;damaged.antiTorque=0;Stock(.22f,blade,blur);rearBlade=blade;rearBlur=blur;AircraftBodyRotorAlpha(damaged,blade,blur,rearBlade,rearBlur);
 Check(blur==75&&blade==128,"half the main-rotor material: half the blur disc, remaining blades exposed");
 Check(rearBlur==0&&rearBlade==255,"tail rotor with no material draws no disc");
 const std::string path=argc>1?argv[1]:"tools/host/regular-engine.patch";std::ifstream in(path);std::stringstream text;text<<in.rdbuf();const std::string patch=text.str();
 const auto render=patch.find("CAutomobile::Render(void)");Check(in.good()&&render!=std::string::npos,"engine patch has the Automobile render hunk");
 const std::string hunk=render==std::string::npos?"":patch.substr(render,patch.find("\n@@",render)-render);
 Check(hunk.find("rotorAlpha=255;blurAlpha=0")==std::string::npos,"material ownership no longer forces solid blades and a hidden blur disc");
 Check(hunk.find("AircraftBodyRotorAlpha(material,rotorAlpha,blurAlpha,rearRotorAlpha,rearBlurAlpha)")!=std::string::npos,"render reads the material rotor to fade the stock crossfade");
 Check(hunk.find("SetComponentAtomicAlpha(atomic, rearRotorAlpha)")!=std::string::npos&&hunk.find("SetComponentAtomicAlpha(atomic, rearBlurAlpha)")!=std::string::npos,"tail rotor blade/blur use the tail material");
 if(!fails)std::puts("rotor alpha: all passed");return fails?1:0;
}

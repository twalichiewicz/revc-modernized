// Real fixed-wing force adapter must leave rolling brakes/steer to the wheels.
#define main PlaneForceFixtures
#include "test_planeflight.cpp"
#undef main
namespace {
alignas(16) uint8_t info[0x258]{},collision[0x68]{};
uint8_t *models[6500]{};bool wheelOwner=true;
bool OwnPlane(const uint8_t *v){return v==gBoat;}
bool OwnWheels(const uint8_t *v){return v==gBoat&&wheelOwner&&game::EntityPos(v).z<2.494784f;}
bool Floor(const CVector &,float,uint8_t *point,uint8_t *&entity,bool,bool,bool,bool,bool,bool,void *)
{entity=nullptr;V(point,0)={0,0,0};V(point,0x10)={0,0,1};return true;}
void Prepare(bool wheels)
{
	Reset();wheelOwner=wheels;planeflight::SetAirportOwner(OwnPlane);planeflight::SetAirportWheelControl(OwnWheels);
	models[248]=info;*reinterpret_cast<uint8_t **>(info+0x20)=collision;V(collision,0x10)={-10,-9,-2.194784f};
	game::api.modelInfoPtrs=models;game::api.ProcessVerticalLine=Floor;
	*reinterpret_cast<int16_t *>(gBoat+0x6a)=248;*reinterpret_cast<int32_t *>(gBoat+0x348)=4;
	V(gBoat,0x38)={0,0,2.194784f};V(gBoat,0x84)={0,.4f,0};gBrake=255;gRight=1;gRoll=128;
}
}
int main()
{
	Reset();planeflight::Install();Prepare(true);CHECK(planeflight::Apply(gBoat,5));
	const float wheelBrake=V(gBoat,0x84).y;CHECK(std::fabs(V(gBoat,0x90).z)<1e-7f);
	// Native ProcessWheel has already built a real yaw rate at the contact.
	// A zero target still cancels that steering if the air controller applies
	// its rate servo while the rolling controller owns this axis.
	Prepare(true);V(gBoat,0x90).z=.004f;CHECK(planeflight::Apply(gBoat,5));CHECK(std::fabs(V(gBoat,0x90).z-.004f)<1e-7f);
	// A retracted/unloaded physical wheel controller reports no contacts.
	// The COL still spans the extended tire plane; that broad box must not
	// activate the legacy wheel brake while the real gear is up.
	Prepare(false);CHECK(planeflight::Apply(gBoat,5));CHECK(std::fabs(V(gBoat,0x84).y-wheelBrake)<1e-7f);
	Prepare(false);planeflight::SetAirportWheelControl(nullptr);CHECK(planeflight::Apply(gBoat,5));const float legacyBrake=V(gBoat,0x84).y;
	CHECK(wheelBrake-legacyBrake>.0027f&&wheelBrake-legacyBrake<.0029f);CHECK(std::fabs(V(gBoat,0x90).z)>1e-5f);
	Prepare(true);V(gBoat,0x38).z=100;CHECK(planeflight::Apply(gBoat,5));CHECK(std::fabs(V(gBoat,0x90).z)>1e-5f);
	// The callback cannot consume native Skimmer water/ground handling.
	Reset();planeflight::SetAirportOwner(nullptr);planeflight::SetAirportWheelControl(OwnWheels);CHECK(planeflight::Apply(gBoat,3));
	if(!failures)std::puts("jet wheel flight handoff: all passed");return failures?1:0;
}

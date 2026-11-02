#pragma once
#include <cstdint>
struct CVector;
namespace jetboarding {struct CabinPose;}

namespace airportjets {
bool Install();
void Frame(uint32_t now);
bool Owns(const uint8_t *vehicle);
// Current owned generation only; 1 is fully deployed, 0 is fully stowed.
float GearDown(const uint8_t *vehicle);
// Also covers a registered-model actor after foreign control takeover, so
// native audio never indexes its fixed vehicle profile table with this ID.
bool UsesRegisteredModel(const uint8_t *vehicle);
// Private render adapter: current owned reach target in the ped's frame.
bool ReachDoor(uint8_t *ped,CVector &target,float &weight,int &pedRef,uint64_t &serial);
bool CabinPose(uint8_t *ped,jetboarding::CabinPose &pose);
void RejectBoardingPose(uint8_t *ped,int ref,uint64_t serial);
}

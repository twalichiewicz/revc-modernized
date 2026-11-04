#pragma once
#include "types.h"
#include <array>
#include <cstdint>
namespace mechanicalmodel {
// Wheels and brakes use native LF, LR, RF, RR order.
enum Module {kEngine,kCooling,kWheelLF,kWheelLR,kWheelRF,kWheelRR,
 kBrakeLF,kBrakeLR,kBrakeRF,kBrakeRR,kSteering,kDrivetrain,kFuel,kElectrical,kModules};
struct Box {Vec3 lo{},hi{};};
struct Layout {std::array<Vec3,4> wheels{};std::array<Box,kModules> volumes{};};
struct Hit {Vec3 point{},inward{};float severity=0;uint8_t piece=0;};
struct State {std::array<float,kModules> damage{};std::array<float,4> toe{};float coolant=1,heat=.2f,fuel=1,steeringBias=0;};
struct WheelEffect {float grip=1,drive=1,brake=1,toe=0,steering=1;};
bool Build(int model,Vec3 lo,Vec3 hi,const std::array<Vec3,4> &wheels,Layout &);
std::array<float,kModules> Doses(const Layout &,const Hit &);
void Apply(State &,const std::array<float,kModules> &,Vec3 inward);
void Tick(State &,float seconds,bool engineOn,float load,float speed);
float Power(const State &);
float Lighting(const State &);
WheelEffect Wheel(const State &,int index);
const char *Name(int module);
}

#pragma once
#include "types.h"
#include <array>
#include <cstdint>
namespace mechanicalmodel {
constexpr int kModules=6; // engine, cooling, LF, LR, RF, RR
struct Box {Vec3 lo{},hi{};};
struct Layout {std::array<Vec3,4> wheels{};std::array<Box,kModules> volumes{};};
struct Hit {Vec3 point{},inward{};float severity=0;uint8_t piece=0;};
struct State {std::array<float,kModules> damage{};std::array<float,4> toe{};float coolant=1,heat=.2f;};
struct WheelEffect {float grip=1,drive=1,brake=1,toe=0;};
bool Build(int model,Vec3 lo,Vec3 hi,const std::array<Vec3,4> &wheels,Layout &);
std::array<float,kModules> Doses(const Layout &,const Hit &);
void Apply(State &,const std::array<float,kModules> &,Vec3 inward);
void Tick(State &,float seconds,bool engineOn,float load,float speed);
float Power(const State &);
WheelEffect Wheel(const State &,int index);
const char *Name(int module);
}

#pragma once
#include "projectilemodel.h"
#include "firemodel.h"
namespace airframemodel {
enum Module {EngineL,EngineR,FuelL,FuelR,WingL,WingR,Elevator,Rudder,AileronL,AileronR,Hydraulics,Electrical,MainRotor,TailRotor,Count};
using Box=mechanicalmodel::Box;
struct Layout {Box body;std::array<Box,Count> volumes{};bool helicopter=false,single=false;};
struct State {std::array<float,Count> damage{};float fuel=1,heat=.2f,holes=0;};
struct Effects {float power=1,leftLift=1,rightLift=1,pitch=1,roll=1,yaw=1,rotor=1,antiTorque=1,drag=0;bool managed=false;};
bool Build(int model,int kind,Vec3 lo,Vec3 hi,Layout &);
projectilemodel::Result Projectile(State &,const Layout &,const projectilemodel::Ray &);
void Impact(State &,const Layout &,Vec3 point,float severity);
void Tick(State &,float dt,bool running);
Effects Read(const State &,const Layout &);
firemodel::Input Fire(const State &,bool running);
}

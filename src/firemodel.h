#pragma once
namespace firemodel {
struct Input {float health=1000,heat=.2f,fuel=1,engine=0,electrical=0,breach=0;bool running=false;};
struct State {double exposure=0,age=0;bool lit=false,spent=false,suppressed=false;};
void Tick(State &,const Input &,float seconds);
void Ignite(State &);
void Extinguish(State &);
void Rearm(State &);
float Intensity(const State &);
}

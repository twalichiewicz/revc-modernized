#include "SourceGates.h"
#include <cstring>
extern "C" volatile uint64_t *VCSourceGate(const char *name){
 static const char *names[]={"skip:hud_health","skip:hud_armour","skip:rifle_first_person","skip:driveby_pistol_car","skip:driveby_pistol_bike","skip:driveby_pistol_boat","skip:car_fire_countdown","skip:car_rollover_drain","skip:car_ordinary_lethal","skip:aircraft_boat_fire"};
 static volatile uint64_t values[10]={};
 for(unsigned i=0;i<10;++i)if(name&&!std::strcmp(names[i],name))return &values[i];return nullptr;
}

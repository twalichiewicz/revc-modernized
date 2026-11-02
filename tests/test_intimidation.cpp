#include "intimidation.h"
#include <cstdio>
using namespace intimidation;
int main()
{
	int failures = 0;
	Threat t{true, true, true, false, false, false, 12.0f, 0.0f, 20};
	Driver timid;
	Response r = Response::None;
	for (int i = 0; i < 10; i++)
		r = timid.Update(t, 0.05f);
	if (r != Response::Surrender) {
		puts("FAIL: stopped timid driver never surrenders");
		failures++;
	}
	t.temper = 90;
	Driver angry;
	for (int i = 0; i < 10; i++)
		r = angry.Update(t, 0.05f);
	if (r != Response::Flee) {
		puts("FAIL: aggressive driver never flees");
		failures++;
	}
	t.speed = 14;
	Driver moving;
	for (int i = 0; i < 10; i++)
		r = moving.Update(t, 0.05f);
	if (r != Response::Brake) {
		puts("FAIL: moving driver must brake before choosing");
		failures++;
	}
	for (int gate = 0; gate < 5; gate++) {
		Threat safe = t;
		if (gate == 0)
			safe.visible = false;
		if (gate == 1)
			safe.targeted = false;
		if (gate == 2)
			safe.mission = true;
		if (gate == 3)
			safe.sameCar = true;
		if (gate == 4)
			safe.distance = 80;
		Driver protectedDriver;
		for (int i = 0; i < 30; i++)
			if (protectedDriver.Update(safe, 0.05f) != Response::None)
				failures++;
	}
	// A momentary occluder does not alternate the decision every frame.
	t.speed = 0;
	if (angry.Update(t, 0.05f) != Response::Flee)
		failures++;
	// Police confront threats; they are no longer excluded as civilians.
	Threat officer{true,true,true,false,true,false,12,0,30};
	Driver cop;
	for (int i=0;i<10;i++) r=cop.Update(officer,.05f);
	if (r!=Response::Fight) failures++;
	officer.targetInCar=true;
	Driver ram;
	for (int i=0;i<10;i++) r=ram.Update(officer,.05f);
	if (r!=Response::Ram) failures++;
	Threat scared{true,true,true,false,false,false,8,0,20};
	scared.fear=90;
	Driver panic;
	for (int i=0;i<10;i++) r=panic.Update(scared,.05f);
	if (r!=Response::Flee) failures++;
	if (failures) {
		printf("intimidation: %d failures\n", failures);
		return 1;
	}
	puts("intimidation: all passed");
}
